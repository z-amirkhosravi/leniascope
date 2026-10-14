#include "cuda_solver.h"
#include <algorithm>
#include <cmath>
#include <cuda_d3d11_interop.h>
#include <cuda_runtime.h>
#include <cufft.h>
#include <d3d11_4.h>
#include <stdexcept>
#include <vector>

namespace lenia {
namespace {
void cucheck(cudaError_t error, const char *operation) {
    if (error != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(error));
}
void fftcheck(cufftResult error, const char *operation) {
    if (error != CUFFT_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": cuFFT error " + std::to_string(int(error)));
}
__global__ void multiply(cufftComplex *data, const cufftComplex *kernel, size_t count) {
    size_t i = size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i < count) {
        auto a = data[i], b = kernel[i];
        data[i] = make_cuFloatComplex(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
    }
}
__device__ float softplus(float x) {
    return fmaxf(x, 0.f) + log1pf(expf(-fabsf(x)));
}
__global__ void update(float *field, const float *neighborhood, size_t count, float mu, float sigma, float dt,
                       int rule, float sharpness) {
    size_t i = size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= count)
        return;
    float a = field[i], q = (neighborhood[i] / float(count) - mu) / sigma;
    float g = 2.f * expf(-.5f * q * q) - 1.f;
    if (rule == 0)
        a = fminf(1.f, fmaxf(0.f, a + dt * g));
    else if (rule == 2) {
        float x = a + dt * g, y = fminf(x, 1.f - x);
        float lower = (softplus(sharpness * y) - softplus(sharpness * (y - 1.f))) / sharpness;
        a = x <= .5f ? lower : 1.f - lower;
    } else {
        float b = softplus(sharpness * g) / sharpness, d = softplus(-sharpness * g) / sharpness;
        float rate = b + d;
        a += (b / rate - a) * (-expm1f(-rate * dt));
    }
    field[i] = a;
}
__device__ float4 combine(float4 a, float4 b) {
    return make_float4(a.x + b.x, fminf(a.y, b.y), fmaxf(a.z, b.z), 0);
}
__global__ void reduce(const float *field, float4 *partials, size_t count) {
    __shared__ float4 values[256];
    auto v = make_float4(0, 1, 0, 0);
    for (size_t i = size_t(blockIdx.x) * 256 + threadIdx.x; i < count; i += 256 * 1024) {
        float a = field[i];
        v = combine(v, make_float4(a, a, a, 0));
    }
    values[threadIdx.x] = v;
    __syncthreads();
    for (int stride = 128; stride; stride /= 2) {
        if (threadIdx.x < stride)
            values[threadIdx.x] = combine(values[threadIdx.x], values[threadIdx.x + stride]);
        __syncthreads();
    }
    if (threadIdx.x == 0)
        partials[blockIdx.x] = values[0];
}
__global__ void reduce_final(float4 *partials, size_t count) {
    __shared__ float4 values[256];
    auto v = make_float4(0, 1, 0, 0);
    for (int i = threadIdx.x; i < 1024; i += 256)
        v = combine(v, partials[i]);
    values[threadIdx.x] = v;
    __syncthreads();
    for (int stride = 128; stride; stride /= 2) {
        if (threadIdx.x < stride)
            values[threadIdx.x] = combine(values[threadIdx.x], values[threadIdx.x + stride]);
        __syncthreads();
    }
    if (threadIdx.x == 0) {
        auto result = values[0];
        result.x /= float(count);
        partials[0] = result;
    }
}
} // namespace

struct CudaSolver::Impl {
    State state;
    Timings timing;
    int device = -1;
    size_t count = 0, frequencies = 0, workspace_bytes = 0;
    std::shared_ptr<GpuContext> gpu;
    cudaStream_t stream = nullptr;
    cufftHandle forward = 0, inverse = 0;
    void *workspace = nullptr;
    float *field = nullptr, *neighborhood = nullptr;
    cufftComplex *frequency = nullptr, *kernel = nullptr;
    float4 *statistics = nullptr, *host_statistics = nullptr;
    cudaEvent_t events[5]{};
    struct Image {
        std::shared_ptr<GpuImage> image;
        cudaGraphicsResource_t resource = nullptr;
    };
    std::vector<Image> images;

    void activate() {
        cucheck(cudaSetDevice(device), "Select CUDA device");
    }
    ~Impl() {
        if (device < 0)
            return;
        cudaSetDevice(device);
        if (stream)
            cudaStreamSynchronize(stream);
        if (gpu) {
            auto lock = gpu->lock();
            for (auto &slot : images)
                if (slot.resource)
                    cudaGraphicsUnregisterResource(slot.resource);
        }
        if (forward)
            cufftDestroy(forward);
        if (inverse)
            cufftDestroy(inverse);
        for (auto event : events)
            if (event)
                cudaEventDestroy(event);
        cudaFree(field);
        cudaFree(neighborhood);
        cudaFree(frequency);
        cudaFree(kernel);
        cudaFree(workspace);
        cudaFree(statistics);
        if (host_statistics)
            cudaFreeHost(host_statistics);
        if (stream)
            cudaStreamDestroy(stream);
    }
    void initialize(State initial, std::shared_ptr<GpuContext> shared) {
        state = std::move(initial);
        gpu = std::move(shared);
        const auto &c = state.config;
        c.validate();
        if (state.values.size() != c.count())
            throw std::invalid_argument("CUDA field dimensions differ");
        for (double a : state.values)
            if (!std::isfinite(a) || a < 0 || a > 1)
                throw std::invalid_argument("Invalid CUDA field value");
        if (gpu) {
            auto lock = gpu->lock();
            // CUDA interop also uses the immediate context internally. Protect those calls,
            // including driver work outside our own submission mutex, from UI rendering.
            ComPtr<ID3D11Multithread> multithread;
            check(gpu->context.As(&multithread), "Access graphics thread protection");
            multithread->SetMultithreadProtected(TRUE);
            ComPtr<IDXGIDevice> dxgi;
            ComPtr<IDXGIAdapter> adapter;
            check(gpu->device.As(&dxgi), "Find graphics device");
            check(dxgi->GetAdapter(&adapter), "Find graphics adapter");
            cucheck(cudaD3D11GetDevice(&device, adapter.Get()), "Match CUDA and graphics adapters");
        } else {
            int devices = 0;
            cucheck(cudaGetDeviceCount(&devices), "Enumerate CUDA devices");
            if (!devices)
                throw std::runtime_error("No CUDA device available");
            device = 0;
        }
        activate();
        cudaDeviceProp properties{};
        cucheck(cudaGetDeviceProperties(&properties, device), "Read CUDA device");
        int version = 0;
        fftcheck(cufftGetVersion(&version), "Read cuFFT version");
        state.backend = "GPU float / cuFFT " + std::to_string(version) + " / " + properties.name;
        count = c.count();
        frequencies = size_t(c.depth) * c.height * (c.width / 2 + 1);
        cucheck(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "Create CUDA stream");
        for (auto &event : events)
            cucheck(cudaEventCreateWithFlags(&event, cudaEventBlockingSync), "Create CUDA timestamp");
        // One shared work area: forward/inverse transforms execute serially on this stream.
        fftcheck(cufftCreate(&forward), "Create forward FFT");
        fftcheck(cufftCreate(&inverse), "Create inverse FFT");
        fftcheck(cufftSetAutoAllocation(forward, 0), "Configure forward workspace");
        fftcheck(cufftSetAutoAllocation(inverse, 0), "Configure inverse workspace");
        int dimensions[]{c.depth, c.height, c.width};
        int rank = c.depth > 1 ? 3 : 2;
        int *shape = dimensions + (rank == 2 ? 1 : 0);
        size_t fw = 0, bw = 0;
        fftcheck(cufftMakePlanMany(forward, rank, shape, nullptr, 1, 0, nullptr, 1, 0, CUFFT_R2C, 1, &fw),
                 "Plan real forward FFT");
        fftcheck(cufftMakePlanMany(inverse, rank, shape, nullptr, 1, 0, nullptr, 1, 0, CUFFT_C2R, 1, &bw),
                 "Plan real inverse FFT");
        workspace_bytes = std::max(fw, bw);
        if (workspace_bytes)
            cucheck(cudaMalloc(&workspace, workspace_bytes), "Allocate FFT workspace");
        fftcheck(cufftSetWorkArea(forward, workspace), "Set forward workspace");
        fftcheck(cufftSetWorkArea(inverse, workspace), "Set inverse workspace");
        fftcheck(cufftSetStream(forward, stream), "Set forward stream");
        fftcheck(cufftSetStream(inverse, stream), "Set inverse stream");
        cucheck(cudaMalloc(&field, count * sizeof(float)), "Allocate CUDA field");
        cucheck(cudaMalloc(&neighborhood, count * sizeof(float)), "Allocate CUDA neighborhood");
        cucheck(cudaMalloc(&frequency, frequencies * sizeof(cufftComplex)), "Allocate CUDA spectrum");
        cucheck(cudaMalloc(&kernel, frequencies * sizeof(cufftComplex)), "Allocate CUDA kernel");
        std::vector<float> data(state.values.size());
        std::transform(state.values.begin(), state.values.end(), data.begin(),
                       [](double a) { return float(a); });
        cucheck(cudaMemcpyAsync(field, data.data(), count * sizeof(float), cudaMemcpyHostToDevice, stream),
                "Upload CUDA field");
        rebuild(c);
        state.values.clear();
        state.values.shrink_to_fit();
    }
    void rebuild(const Config &c) {
        auto k = make_kernel(c);
        std::vector<float> data(k.size());
        std::transform(k.begin(), k.end(), data.begin(), [](double a) { return float(a); });
        // Kernel replacement commits only after its construction succeeds.
        cucheck(
            cudaMemcpyAsync(neighborhood, data.data(), count * sizeof(float), cudaMemcpyHostToDevice, stream),
            "Upload CUDA kernel");
        fftcheck(cufftExecR2C(forward, neighborhood, frequency), "Transform CUDA kernel");
        cucheck(cudaStreamSynchronize(stream), "Complete CUDA kernel");
        std::swap(frequency, kernel);
    }
};

CudaSolver::CudaSolver(State s, std::shared_ptr<GpuContext> gpu) : impl(std::make_unique<Impl>()) {
    impl->initialize(std::move(s), std::move(gpu));
}
CudaSolver::~CudaSolver() = default;
void CudaSolver::step() {
    auto &p = *impl;
    p.activate();
    const auto &c = p.state.config;
    cucheck(cudaEventRecord(p.events[0], p.stream), "Start CUDA step");
    fftcheck(cufftExecR2C(p.forward, p.field, p.frequency), "Forward CUDA FFT");
    cucheck(cudaEventRecord(p.events[1], p.stream), "Time forward FFT");
    multiply<<<unsigned((p.frequencies + 255) / 256), 256, 0, p.stream>>>(p.frequency, p.kernel,
                                                                          p.frequencies);
    cucheck(cudaGetLastError(), "CUDA spectrum multiply");
    cucheck(cudaEventRecord(p.events[2], p.stream), "Time multiply");
    fftcheck(cufftExecC2R(p.inverse, p.frequency, p.neighborhood), "Inverse CUDA FFT");
    cucheck(cudaEventRecord(p.events[3], p.stream), "Time inverse FFT");
    update<<<unsigned((p.count + 255) / 256), 256, 0, p.stream>>>(p.field, p.neighborhood, p.count,
                                                                  float(c.mu), float(c.sigma), float(c.dt),
                                                                  int(c.rule), float(c.smoothness));
    cucheck(cudaGetLastError(), "CUDA reaction update");
    cucheck(cudaEventRecord(p.events[4], p.stream), "End CUDA step");
    cucheck(cudaEventSynchronize(p.events[4]), "Complete CUDA step");
    auto elapsed = [&](int begin, int end) {
        float ms = 0;
        cucheck(cudaEventElapsedTime(&ms, p.events[begin], p.events[end]), "Read CUDA timing");
        return double(ms);
    };
    p.timing = {elapsed(0, 1), elapsed(1, 2), elapsed(2, 3), elapsed(3, 4), elapsed(0, 4)};
    ++p.state.steps;
    p.state.time += c.dt;
}
State CudaSolver::snapshot() {
    auto &p = *impl;
    p.activate();
    State s = p.state;
    std::vector<float> values(p.count);
    cucheck(
        cudaMemcpyAsync(values.data(), p.field, p.count * sizeof(float), cudaMemcpyDeviceToHost, p.stream),
        "Read CUDA field");
    cucheck(cudaStreamSynchronize(p.stream), "Complete CUDA readback");
    s.values.assign(values.begin(), values.end());
    return s;
}
void CudaSolver::parameters(const Config &c) {
    c.validate();
    auto &p = *impl;
    p.activate();
    const auto &old = p.state.config;
    if (c.width != old.width || c.height != old.height || c.depth != old.depth)
        throw std::invalid_argument("Dimensions require reset");
    if (c.radius != old.radius)
        p.rebuild(c);
    p.state.config = c;
}
const Timings &CudaSolver::timings() const {
    return impl->timing;
}
size_t CudaSolver::device_bytes() const {
    const auto &p = *impl;
    return 2 * p.count * sizeof(float) + 2 * p.frequencies * sizeof(cufftComplex) + p.workspace_bytes +
           (p.statistics ? 1024 * sizeof(float4) : 0) + p.images.size() * p.count * sizeof(float);
}
std::shared_ptr<const GpuImage> CudaSolver::display_snapshot() {
    auto &p = *impl;
    p.activate();
    if (!p.gpu)
        throw std::runtime_error("CUDA display requires a shared graphics device");
    Impl::Image *slot = nullptr;
    for (auto &candidate : p.images)
        if (candidate.image.use_count() == 1) {
            slot = &candidate;
            break;
        }
    if (!slot) {
        if (p.images.size() == 3)
            return {};
        auto image = std::make_shared<GpuImage>();
        D3D11_TEXTURE3D_DESC d{};
        const auto &c = p.state.config;
        d.Width = c.width;
        d.Height = c.height;
        d.Depth = c.depth;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R32_FLOAT;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        check(p.gpu->device->CreateTexture3D(&d, nullptr, &image->texture), "Create CUDA display texture");
        check(p.gpu->device->CreateShaderResourceView(image->texture.Get(), nullptr, &image->view),
              "Create CUDA display view");
        p.images.push_back({image, nullptr});
        slot = &p.images.back();
        auto lock = p.gpu->lock();
        cucheck(cudaGraphicsD3D11RegisterResource(&slot->resource, image->texture.Get(),
                                                  cudaGraphicsRegisterFlagsNone),
                "Register CUDA display texture");
        cucheck(cudaGraphicsResourceSetMapFlags(slot->resource, cudaGraphicsMapFlagsWriteDiscard),
                "Configure CUDA display texture");
    }
    if (!slot->resource)
        throw std::runtime_error("CUDA display registration failed; select another backend");
    if (!p.statistics)
        cucheck(cudaMalloc(&p.statistics, 1024 * sizeof(float4)), "Allocate CUDA statistics");
    if (!p.host_statistics)
        cucheck(cudaMallocHost(&p.host_statistics, sizeof(float4)), "Allocate statistics readback");
    reduce<<<1024, 256, 0, p.stream>>>(p.field, p.statistics, p.count);
    reduce_final<<<1, 256, 0, p.stream>>>(p.statistics, p.count);
    cucheck(cudaGetLastError(), "Reduce CUDA field");
    cucheck(
        cudaMemcpyAsync(p.host_statistics, p.statistics, sizeof(float4), cudaMemcpyDeviceToHost, p.stream),
        "Read CUDA statistics");
    {
        auto lock = p.gpu->lock();
        cucheck(cudaGraphicsMapResources(1, &slot->resource, p.stream), "Map CUDA display texture");
        try {
            cudaArray_t array;
            cucheck(cudaGraphicsSubResourceGetMappedArray(&array, slot->resource, 0, 0),
                    "Access CUDA display texture");
            const auto &c = p.state.config;
            cudaMemcpy3DParms copy{};
            copy.srcPtr =
                make_cudaPitchedPtr(p.field, c.width * sizeof(float), c.width * sizeof(float), c.height);
            copy.dstArray = array;
            copy.extent = make_cudaExtent(c.width, c.height, c.depth);
            copy.kind = cudaMemcpyDeviceToDevice;
            cucheck(cudaMemcpy3DAsync(&copy, p.stream), "Copy CUDA display snapshot");
        } catch (...) {
            cudaGraphicsUnmapResources(1, &slot->resource, p.stream);
            throw;
        }
        cucheck(cudaGraphicsUnmapResources(1, &slot->resource, p.stream), "Unmap CUDA display texture");
    }
    cucheck(cudaEventRecord(p.events[4], p.stream), "Signal CUDA display ready");
    cucheck(cudaEventSynchronize(p.events[4]), "Complete CUDA display snapshot");
    auto v = *p.host_statistics;
    slot->image->metadata = p.state;
    slot->image->measures = {v.x, v.y, v.z, 0};
    return slot->image;
}
} // namespace lenia
