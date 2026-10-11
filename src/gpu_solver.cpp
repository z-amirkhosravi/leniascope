#include "gpu_solver.h"
#include "d3d_support.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <d3d11_4.h>
namespace lenia {
namespace {
const char *compute_source = R"(
cbuffer Params : register(b0) { uint W,H,D,axis; uint inverse,rule,count,pad; float mu,sigma,dt,sharp; };
RWStructuredBuffer<float2> data : register(u0);
RWTexture3D<float> field : register(u1);
StructuredBuffer<float2> kernel : register(t0);
groupshared float2 fftLine[2048];
float2 mul(float2 a,float2 b){return float2(a.x*b.x-a.y*b.y,a.x*b.y+a.y*b.x);}
uint address(uint row,uint i){if(axis==0)return row*W+i;if(axis==1)return (row/W)*W*H+i*W+row%W;return i*W*H+row;}
[numthreads(256,1,1)] void fft(uint3 group:SV_GroupID,uint tid:SV_GroupIndex){
 uint n=axis==0?W:(axis==1?H:D),row=group.x+group.y*65535;
 uint rows=count/n;if(row>=rows)return;
 uint bits=firstbithigh(n);
 for(uint i=tid;i<n;i+=256)fftLine[reversebits(i)>>(32-bits)]=data[address(row,i)];
 GroupMemoryBarrierWithGroupSync();
 for(uint span=2;span<=n;span<<=1){uint halfspan=span>>1;
   for(uint j=tid;j<n/2;j+=256){uint k=(j/halfspan)*span+j%halfspan;float angle=(inverse?6.28318530718:-6.28318530718)*(j%halfspan)/span;float sn,cs;sincos(angle,sn,cs);float2 a=fftLine[k],b=mul(fftLine[k+halfspan],float2(cs,sn));fftLine[k]=a+b;fftLine[k+halfspan]=a-b;}
   GroupMemoryBarrierWithGroupSync();
 }
 for(uint i=tid;i<n;i+=256)data[address(row,i)]=fftLine[i];
}
[numthreads(256,1,1)] void multiply(uint3 gid:SV_GroupID,uint tid:SV_GroupIndex){uint i=(gid.x+gid.y*65535)*256+tid;if(i<count)data[i]=mul(data[i],kernel[i]);}
float sp(float x){return max(x,0)+log(1+exp(-abs(x)));}
[numthreads(256,1,1)] void update(uint3 gid:SV_GroupID,uint tid:SV_GroupIndex){uint i=(gid.x+gid.y*65535)*256+tid;if(i>=count)return;uint3 p=uint3(i%W,(i/W)%H,i/(W*H));float a=field[p],q=(data[i].x/count-mu)/sigma,g=2*exp(-.5*q*q)-1;
 if(rule==0)a=saturate(a+dt*g);else {float b=sp(sharp*g)/sharp,d=sp(-sharp*g)/sharp,rate=b+d;float x=rate*dt;float decay=x<.001?x*(1-.5*x+x*x/6):1-exp(-x);a+=(b/rate-a)*decay;}field[p]=a;}
[numthreads(256,1,1)] void pack(uint3 gid:SV_GroupID,uint tid:SV_GroupIndex){uint i=(gid.x+gid.y*65535)*256+tid;if(i<count)data[i]=float2(field[uint3(i%W,(i/W)%H,i/(W*H))],0);}
)";
struct Params {
    uint32_t w, h, d, axis, inverse, rule, count, pad;
    float mu, sigma, dt, sharp;
};
} // namespace
struct GpuSolver::Impl {
    State state;
    Timings timing;
    std::string adapter;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11Buffer> data, kernel, constants;
    ComPtr<ID3D11UnorderedAccessView> data_uav, field_uav;
    ComPtr<ID3D11ShaderResourceView> kernel_srv;
    ComPtr<ID3D11Texture3D> field, staging;
    ComPtr<ID3D11ComputeShader> fft_shader, multiply_shader, update_shader, pack_shader;
    ComPtr<ID3D11Query> timestamp_begin, timestamp_end, disjoint;
    Params params{};
    ComPtr<ID3D11Device5> device5;
    ComPtr<ID3D11DeviceContext4> context4;
    ComPtr<ID3D11Fence> fence;
    HANDLE completion = nullptr;
    UINT64 fence_value = 0;
    ~Impl() {
        if (completion)
            CloseHandle(completion);
    }
    explicit Impl(State s) : state(std::move(s)) {
        state.config.validate();
        if (state.values.size() != state.config.count())
            throw std::invalid_argument("GPU state size mismatch");
        for (double v : state.values)
            if (!std::isfinite(v) || v < 0 || v > 1)
                throw std::invalid_argument("Invalid GPU state");
        make_device(device, ctx, &adapter);
        state.backend = "GPU float / DirectCompute FFT-1 / " + adapter;
        auto shader = [&](const char *entry, ComPtr<ID3D11ComputeShader> &target) {
            auto code = compile_shader(compute_source, entry, "cs_5_0");
            check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                              &target),
                  "Create compute shader");
        };
        shader("fft", fft_shader);
        shader("multiply", multiply_shader);
        shader("update", update_shader);
        shader("pack", pack_shader);
        const auto &c = state.config;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = UINT(c.count() * 8);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = 8;
        check(device->CreateBuffer(&bd, nullptr, &data), "Create FFT buffer");
        check(device->CreateBuffer(&bd, nullptr, &kernel), "Create kernel buffer");
        check(device->CreateUnorderedAccessView(data.Get(), nullptr, &data_uav), "Create FFT UAV");
        check(device->CreateShaderResourceView(kernel.Get(), nullptr, &kernel_srv), "Create kernel SRV");
        bd = {};
        bd.ByteWidth = sizeof(Params);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&bd, nullptr, &constants), "Create solver constants");
        D3D11_TEXTURE3D_DESC td{};
        td.Width = c.width;
        td.Height = c.height;
        td.Depth = c.depth;
        td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R32_FLOAT;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        std::vector<float> v(state.values.size());
        std::transform(state.values.begin(), state.values.end(), v.begin(),
                       [](double a) { return float(a); });
        D3D11_SUBRESOURCE_DATA initial{v.data(), UINT(c.width * 4), UINT(c.width * c.height * 4)};
        check(device->CreateTexture3D(&td, &initial, &field), "Create simulation field");
        check(device->CreateUnorderedAccessView(field.Get(), nullptr, &field_uav), "Create field UAV");
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture3D(&td, nullptr, &staging), "Create readback texture");
        D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP, 0};
        check(device->CreateQuery(&q, &timestamp_begin), "Create timestamp");
        check(device->CreateQuery(&q, &timestamp_end), "Create timestamp");
        q.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        check(device->CreateQuery(&q, &disjoint), "Create GPU clock query");
        update_params();
        rebuild_kernel();
        state.values.clear();
        state.values.shrink_to_fit();
        if (SUCCEEDED(device.As(&device5)) && SUCCEEDED(ctx.As(&context4)) &&
            SUCCEEDED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            completion = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!completion)
                throw std::runtime_error("Create GPU completion event failed");
        }
    }
    void update_params() {
        const auto &c = state.config;
        params = {uint32_t(c.width),
                  uint32_t(c.height),
                  uint32_t(c.depth),
                  0,
                  0,
                  uint32_t(c.rule),
                  uint32_t(c.count()),
                  0,
                  float(c.mu),
                  float(c.sigma),
                  float(c.dt),
                  float(c.smoothness)};
    }
    void bind() {
        ID3D11UnorderedAccessView *views[] = {data_uav.Get(), field_uav.Get()};
        ctx->CSSetUnorderedAccessViews(0, 2, views, nullptr);
        ID3D11Buffer *b = constants.Get();
        ctx->CSSetConstantBuffers(0, 1, &b);
    }
    void dispatch(ID3D11ComputeShader *shader, UINT groups) {
        ctx->UpdateSubresource(constants.Get(), 0, nullptr, &params, 0, 0);
        ctx->CSSetShader(shader, nullptr, 0);
        ctx->Dispatch(std::min(groups, 65535u), (groups + 65534) / 65535, 1);
    }
    void transform(bool inverse) {
        params.inverse = inverse;
        for (uint32_t a = 0; a < 3; ++a) {
            params.axis = a;
            uint32_t n = a == 0 ? params.w : (a == 1 ? params.h : params.d);
            if (n > 1)
                dispatch(fft_shader.Get(), params.count / n);
        }
    }
    void unbind() {
        ID3D11UnorderedAccessView *uavs[2] = {};
        ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        ID3D11ShaderResourceView *srv = nullptr;
        ctx->CSSetShaderResources(0, 1, &srv);
    }
    void rebuild_kernel() {
        auto k = make_kernel(state.config);
        std::vector<float> pairs(k.size() * 2);
        for (size_t i = 0; i < k.size(); ++i)
            pairs[2 * i] = float(k[i]);
        ctx->UpdateSubresource(data.Get(), 0, nullptr, pairs.data(), 0, 0);
        bind();
        transform(false);
        unbind();
        ctx->CopyResource(kernel.Get(), data.Get());
    }
};
GpuSolver::GpuSolver(State s) : impl(std::make_unique<Impl>(std::move(s))) {}
GpuSolver::~GpuSolver() = default;
void GpuSolver::step() {
    auto &p = *impl;
    p.ctx->Begin(p.disjoint.Get());
    p.ctx->End(p.timestamp_begin.Get());
    p.bind();
    p.dispatch(p.pack_shader.Get(), (p.params.count + 255) / 256);
    p.transform(false);
    ID3D11ShaderResourceView *kernel = p.kernel_srv.Get();
    p.ctx->CSSetShaderResources(0, 1, &kernel);
    p.dispatch(p.multiply_shader.Get(), (p.params.count + 255) / 256);
    p.transform(true);
    p.dispatch(p.update_shader.Get(), (p.params.count + 255) / 256);
    p.unbind();
    p.ctx->End(p.timestamp_end.Get());
    p.ctx->End(p.disjoint.Get());
    // Bound queued GPU work; the worker waits, leaving the UI device independent.
    if (p.fence) {
        ++p.fence_value;
        check(p.context4->Signal(p.fence.Get(), p.fence_value), "Signal GPU completion");
        check(p.fence->SetEventOnCompletion(p.fence_value, p.completion), "Arm GPU completion event");
        p.ctx->Flush();
        if (WaitForSingleObject(p.completion, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("GPU completion timed out");
    } else
        p.ctx->Flush();
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
    auto wait_start = std::chrono::steady_clock::now();
    HRESULT ready;
    while ((ready = p.ctx->GetData(p.disjoint.Get(), &clock, sizeof clock, D3D11_ASYNC_GETDATA_DONOTFLUSH)) ==
           S_FALSE) {
        if (std::chrono::steady_clock::now() - wait_start > std::chrono::seconds(10))
            throw std::runtime_error("GPU query timed out");
        SwitchToThread();
    }
    check(ready, "Read GPU clock");
    UINT64 a = 0, b = 0;
    check(p.ctx->GetData(p.timestamp_begin.Get(), &a, sizeof a, 0), "GPU timestamp");
    check(p.ctx->GetData(p.timestamp_end.Get(), &b, sizeof b, 0), "GPU timestamp");
    if (!clock.Disjoint && clock.Frequency)
        p.timing.total_ms = 1000. * double(b - a) / clock.Frequency;
    ++p.state.steps;
    p.state.time += p.state.config.dt;
}
State GpuSolver::snapshot() {
    auto &p = *impl;
    State s = p.state;
    s.values.resize(s.config.count());
    p.ctx->CopyResource(p.staging.Get(), p.field.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    check(p.ctx->Map(p.staging.Get(), 0, D3D11_MAP_READ, 0, &m), "Read GPU state");
    const auto &c = s.config;
    for (int z = 0; z < c.depth; ++z)
        for (int y = 0; y < c.height; ++y) {
            auto row = reinterpret_cast<const float *>(static_cast<const char *>(m.pData) +
                                                       size_t(z) * m.DepthPitch + size_t(y) * m.RowPitch);
            for (int x = 0; x < c.width; ++x)
                s.values[(size_t(z) * c.height + y) * c.width + x] = row[x];
        }
    p.ctx->Unmap(p.staging.Get(), 0);
    return s;
}
void GpuSolver::parameters(const Config &c) {
    c.validate();
    auto &p = *impl;
    const auto &old = p.state.config;
    if (c.width != old.width || c.height != old.height || c.depth != old.depth)
        throw std::invalid_argument("Dimensions require reset");
    bool rebuild = c.radius != old.radius;
    p.state.config = c;
    p.update_params();
    if (rebuild)
        p.rebuild_kernel();
}
const Timings &GpuSolver::timings() const {
    return impl->timing;
}
} // namespace lenia
