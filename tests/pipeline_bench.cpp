#include "src/gpu_solver.h"
#include "src/renderer.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>
using namespace lenia;
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
struct Samples {
    std::vector<double> v;
    void print() {
        std::sort(v.begin(), v.end());
        std::cout << ',' << v[v.size() / 2] << ',' << v[size_t((v.size() - 1) * .95)];
    }
};
int main() {
    try {
        std::cout << "path,size,step_p50_ms,step_p95_ms,compute_p50_ms,compute_p95_ms,prepare_p50_ms,prepare_"
                     "p95_ms,measure_p50_ms,measure_p95_ms,upload_p50_ms,upload_p95_ms,render_p50_ms,render_"
                     "p95_ms,total_p50_ms,total_p95_ms\n";
        for (int n : {128, 256})
            for (bool resident : {false, true}) {
                Config c{n, n, n, n / 5};
                auto shared = std::make_shared<GpuContext>();
                auto device = shared->device;
                auto ctx = shared->context;
                GpuSolver solver(random_state(c, 42), resident ? shared : nullptr);
                Renderer renderer(shared);
                ComPtr<ID3D11Query> done;
                D3D11_QUERY_DESC q{D3D11_QUERY_EVENT, 0};
                check(device->CreateQuery(&q, &done), "Create benchmark event");
                Samples step, compute, readback, measureTime, upload, render, total;
                for (int i = 0; i < 65; ++i) {
                    auto a = Clock::now();
                    solver.step();
                    auto b = Clock::now();
                    State state;
                    std::shared_ptr<const GpuImage> image;
                    if (resident)
                        image = solver.display_snapshot();
                    else
                        state = solver.snapshot();
                    auto d = Clock::now();
                    auto measures = resident ? image->measures : measure(state.values);
                    auto e = Clock::now();
                    if (resident)
                        renderer.use_gpu_image(image);
                    else
                        renderer.upload(state);
                    auto f = Clock::now();
                    renderer.draw(ViewSettings{}, 960, 720);
                    ctx->End(done.Get());
                    ctx->Flush();
                    HRESULT ready;
                    while ((ready = ctx->GetData(done.Get(), nullptr, 0, 0)) == S_FALSE) {
                        if (ms(f, Clock::now()) > 10000)
                            throw std::runtime_error("Benchmark timeout");
                        SwitchToThread();
                    }
                    check(ready, "Read render completion");
                    auto g = Clock::now();
                    if (i >= 5) {
                        step.v.push_back(ms(a, b));
                        compute.v.push_back(solver.timings().total_ms);
                        readback.v.push_back(ms(b, d));
                        measureTime.v.push_back(ms(d, e));
                        upload.v.push_back(ms(e, f));
                        render.v.push_back(ms(f, g));
                        total.v.push_back(ms(a, g));
                    }
                    if (measures.mean < 0)
                        throw std::runtime_error("Invalid mean");
                }
                std::cout << (resident ? "gpu-resident," : "cpu-transfer,") << n << std::fixed
                          << std::setprecision(3);
                for (auto s : {step, compute, readback, measureTime, upload, render, total})
                    s.print();
                std::cout << std::endl;
            }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
