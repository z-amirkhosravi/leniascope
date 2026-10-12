#include "src/cuda_solver.h"
#include "src/renderer.h"
#include "src/runner.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <thread>
using namespace lenia;
using Clock = std::chrono::steady_clock;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
double difference(const State &a, const State &b) {
    require(a.values.size() == b.values.size(), "Comparison dimensions differ");
    double error = 0;
    for (size_t i = 0; i < a.values.size(); ++i) {
        require(std::isfinite(b.values[i]) && b.values[i] >= 0 && b.values[i] <= 1,
                "CUDA state is not bounded");
        error = std::max(error, std::abs(a.values[i] - b.values[i]));
    }
    return error;
}
std::vector<double> read_image(const std::shared_ptr<GpuContext> &gpu, const GpuImage &image) {
    auto lock = gpu->lock();
    D3D11_TEXTURE3D_DESC desc{};
    image.texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture3D> staging;
    check(gpu->device->CreateTexture3D(&desc, nullptr, &staging), "Create test readback");
    gpu->context->CopyResource(staging.Get(), image.texture.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    check(gpu->context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "Read test image");
    std::vector<double> values;
    for (UINT z = 0; z < desc.Depth; ++z)
        for (UINT y = 0; y < desc.Height; ++y) {
            auto row = reinterpret_cast<const float *>(static_cast<const char *>(map.pData) +
                                                       z * map.DepthPitch + y * map.RowPitch);
            values.insert(values.end(), row, row + desc.Width);
        }
    gpu->context->Unmap(staging.Get(), 0);
    return values;
}
void validate_worker() {
    auto gpu = std::make_shared<GpuContext>();
    SessionStates initial;
    initial[0] = std::make_shared<State>(random_state({32, 16, 1, 6}, 123));
    initial[1] = std::make_shared<State>(random_state({32, 32, 32, 6}, 456));
    Runner runner(true, initial, true, {}, gpu, {}, true);
    auto baseline = runner.pause_and_snapshot().get();
    auto retained = runner.frame();
    require(retained && retained->cuda && retained->gpu_image, "Worker did not select CUDA");
    // Retain a CUDA texture through destruction of its original solver and across all backends.
    for (int backend : {1, 0, 2}) {
        runner.backend(backend != 0, backend == 2);
        auto next = runner.pause_and_snapshot().get();
        require(runner.frame()->cuda == (backend == 2) && runner.frame()->gpu == (backend != 0),
                "Worker backend flags are stale");
        for (int i = 0; i < 2; ++i)
            require(next.states[i]->values == baseline.states[i]->values &&
                        next.states[i]->seed == baseline.states[i]->seed,
                    "Backend transition changed a paused field");
        require(read_image(gpu, *retained->gpu_image) == baseline.states[1]->values,
                "CUDA texture lifetime ended with its solver");
    }
    Renderer renderer(gpu);
    runner.speed(120);
    runner.play(true);
    auto began = Clock::now();
    while (ms(began, Clock::now()) < 1200) {
        auto frame = runner.frame();
        if (frame && frame->gpu_image) {
            renderer.use_gpu_image(frame->gpu_image);
            renderer.draw(ViewSettings{}, 192, 160);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    auto paused = runner.pause_and_snapshot().get();
    require(paused.states[1]->steps >= 80, "CUDA worker cannot sustain responsive playback");
    runner.mode(false);
    runner.step();
    auto other = runner.pause_and_snapshot().get();
    require(!other.volume && other.states[0]->steps == 1 && other.states[1]->steps == paused.states[1]->steps,
            "CUDA mode switch advanced or lost an inactive session");
    require(runner.frame()->gpu_image &&
                read_image(gpu, *runner.frame()->gpu_image) == other.states[0]->values,
            "Concurrent CUDA display and capture disagree");
    std::cerr << "PASS CUDA concurrent worker/rendering, backend transitions, texture lifetime, independent "
                 "sessions and pacing\n";
}
void validate() {
    for (auto config : {Config{16, 8, 1, 3}, Config{8, 4, 16, 2}, Config{32, 32, 32, 6}})
        for (auto rule : {Rule::Classic, Rule::SmoothReaction}) {
            config.rule = rule;
            auto initial = random_state(config, 987, {23, .8, 1.5, .2, .7});
            CpuSolver cpu(initial);
            GpuSolver direct(initial);
            CudaSolver cuda(initial);
            double cpu_error = 0, direct_error = 0;
            for (int i = 0; i < 5; ++i) {
                cpu.step();
                direct.step();
                cuda.step();
                auto got = cuda.snapshot();
                cpu_error = std::max(cpu_error, difference(cpu.snapshot(), got));
                direct_error = std::max(direct_error, difference(direct.snapshot(), got));
                require(got.steps == unsigned(i + 1) && got.initialization &&
                            got.initialization->patches == 23,
                        "CUDA lost state metadata");
            }
            require(cpu_error < 1e-5 && direct_error < 1e-5,
                    "CUDA convolution/update differs from references");
            std::cerr << "PASS CUDA " << config.width << 'x' << config.height << 'x' << config.depth
                      << " rule=" << int(rule) << " max CPU/DirectCompute error=" << cpu_error << '/'
                      << direct_error << '\n';
            config.radius = 1;
            cpu.parameters(config);
            cuda.parameters(config);
            cpu.step();
            cuda.step();
            require(difference(cpu.snapshot(), cuda.snapshot()) < 2e-5, "CUDA kernel rebuild differs");
            auto saved = cuda.snapshot();
            CudaSolver resumed(saved);
            require(resumed.snapshot().values == saved.values, "CUDA checkpoint restart changed field");
            for (int i = 0; i < 5; ++i) {
                cuda.step();
                resumed.step();
            }
            require(resumed.snapshot().values == cuda.snapshot().values, "CUDA restart changed continuation");
        }
    auto gpu = std::make_shared<GpuContext>();
    for (auto config : {Config{32, 16, 1, 6}, Config{32, 16, 8, 3}}) {
        CudaSolver cuda(random_state(config, 1), gpu);
        auto first = cuda.display_snapshot();
        auto saved = cuda.snapshot();
        require(first && first->metadata.values.empty() && read_image(gpu, *first) == saved.values,
                "CUDA display texture differs from checkpoint");
        auto measures = measure(saved.values);
        require(std::abs(first->measures.mean - measures.mean) < 1e-6 &&
                    first->measures.minimum == measures.minimum &&
                    first->measures.maximum == measures.maximum,
                "CUDA display statistics differ");
        cuda.step();
        auto second = cuda.display_snapshot();
        cuda.step();
        auto third = cuda.display_snapshot();
        cuda.step();
        require(!cuda.display_snapshot(), "CUDA display pool is unbounded");
        require(read_image(gpu, *first) == saved.values, "CUDA overwrote a retained display image");
        second.reset();
        auto reused = cuda.display_snapshot();
        require(reused && reused->metadata.steps == 3, "CUDA display pool did not recover");
        require(read_image(gpu, *reused) == cuda.snapshot().values, "CUDA reused texture is stale");
    }
    for (auto rule : {Rule::Classic, Rule::SmoothReaction}) {
        Config config{32, 32, 32, 6};
        config.rule = rule;
        auto initial = random_state(config, 1);
        CudaSolver cuda(initial);
        CpuSolver cpu(initial);
        for (int i = 0; i < 500; ++i) {
            cuda.step();
            cpu.step();
            if ((i + 1) % 100 == 0) {
                double error = difference(cpu.snapshot(), cuda.snapshot());
                std::cerr << "CUDA/CPU trajectory rule=" << int(rule) << " step=" << i + 1
                          << " max_error=" << error << '\n';
            }
        }
    }
    validate_worker();
    std::cerr << "PASS CUDA bounds, rectangular/non-cubic convolution, both rules, kernel edits, exact "
                 "restart, GPU display copies/statistics and bounded immutable leases\n";
}
struct Samples {
    std::vector<double> values;
    void print() {
        std::sort(values.begin(), values.end());
        std::cout << ',' << values[values.size() / 2] << ',' << values[size_t((values.size() - 1) * .95)];
    }
};
void benchmark() {
    std::cout << "backend,grid,path,step_wall_p50_ms,step_wall_p95_ms,compute_p50_ms,compute_p95_ms,display_"
                 "p50_ms,display_p95_ms,render_p50_ms,render_p95_ms,total_p50_ms,total_p95_ms,cuda_explicit_"
                 "device_bytes\n";
    for (auto c : {Config{1024, 1024, 1, 104}, Config{128, 128, 128, 26}, Config{256, 256, 256, 26}})
        for (bool display : {false, true})
            for (bool use_cuda : {false, true}) {
                auto gpu = std::make_shared<GpuContext>();
                auto initial = random_state(c, 42);
                std::unique_ptr<CudaSolver> cuda;
                std::unique_ptr<GpuSolver> direct;
                auto started = Clock::now();
                if (use_cuda)
                    cuda = std::make_unique<CudaSolver>(initial, display ? gpu : nullptr);
                else
                    direct = std::make_unique<GpuSolver>(initial, display ? gpu : nullptr);
                Solver &solver = use_cuda ? static_cast<Solver &>(*cuda) : static_cast<Solver &>(*direct);
                std::cerr << "Initialized " << (use_cuda ? "CUDA" : "DirectCompute") << ' ' << c.width << "x"
                          << c.depth << " in " << ms(started, Clock::now()) << " ms\n";
                Renderer renderer(gpu);
                ComPtr<ID3D11Query> done;
                D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
                check(gpu->device->CreateQuery(&desc, &done), "Create benchmark event");
                Samples step, compute, publication, render, total;
                for (int i = 0; i < 110; ++i) {
                    auto a = Clock::now();
                    solver.step();
                    auto b = Clock::now();
                    if (display) {
                        auto image = use_cuda ? cuda->display_snapshot() : direct->display_snapshot();
                        require(bool(image), "Benchmark exhausted image pool");
                        renderer.use_gpu_image(image);
                    }
                    auto d = Clock::now();
                    if (display) {
                        ViewSettings view;
                        if (c.depth == 1)
                            view.view = 0;
                        renderer.draw(view, 960, 720);
                        gpu->context->End(done.Get());
                        gpu->context->Flush();
                        HRESULT ready;
                        while ((ready = gpu->context->GetData(done.Get(), nullptr, 0, 0)) == S_FALSE) {
                            if (ms(d, Clock::now()) > 10000)
                                throw std::runtime_error("Render timeout");
                            SwitchToThread();
                        }
                        check(ready, "Complete benchmark render");
                    }
                    auto e = Clock::now();
                    if (i >= 10) {
                        step.values.push_back(ms(a, b));
                        compute.values.push_back(solver.timings().total_ms);
                        publication.values.push_back(ms(b, d));
                        render.values.push_back(ms(d, e));
                        total.values.push_back(ms(a, e));
                    }
                }
                std::cout << (use_cuda ? "cuda," : "directcompute,") << c.width << 'x' << c.height << 'x'
                          << c.depth << ',' << (display ? "display" : "headless") << std::fixed
                          << std::setprecision(4);
                for (auto sample : {step, compute, publication, render, total})
                    sample.print();
                std::cout << ',';
                if (use_cuda)
                    std::cout << cuda->device_bytes();
                std::cout << std::endl;
            }
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 1 || std::string(argv[1]) == "--tests-only")
            validate();
        if (argc == 1 || std::string(argv[1]) == "--benchmark-only")
            benchmark();
        if (argc > 2 || (argc == 2 && std::string(argv[1]) != "--tests-only" &&
                         std::string(argv[1]) != "--benchmark-only"))
            throw std::invalid_argument("Usage: CudaBench [--tests-only|--benchmark-only]");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
