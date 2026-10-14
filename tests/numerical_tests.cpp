#include "src/gpu_solver.h"
#include "src/numerics.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace lenia;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
State periodic_field(int size, bool volume, Rule rule) {
    Config c{size, size, volume ? size : 1, size / 4};
    c.sigma = .03;
    c.dt = .01;
    c.rule = rule;
    c.smoothness = default_sharpness(rule);
    State state{c, std::vector<double>(c.count()), 0};
    const double tau = 2 * std::acos(-1.);
    for (int z = 0; z < c.depth; ++z)
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                state.values[(size_t(z) * size + y) * size + x] =
                    .14 + .025 * std::cos(tau * x / size) * std::cos(tau * y / size) *
                              (volume ? std::cos(tau * z / size) : 1);
    return state;
}
State evolved(State state, int steps) {
    CpuSolver solver(std::move(state));
    for (int i = 0; i < steps; ++i)
        solver.step();
    return solver.snapshot();
}
double aligned_error(const State &coarse, const State &fine) {
    int ratio = fine.config.width / coarse.config.width;
    double sum = 0;
    for (int z = 0; z < coarse.config.depth; ++z)
        for (int y = 0; y < coarse.config.height; ++y)
            for (int x = 0; x < coarse.config.width; ++x) {
                double delta =
                    coarse.values[(size_t(z) * coarse.config.height + y) * coarse.config.width + x] -
                    fine.values[(size_t(z * ratio) * fine.config.height + y * ratio) * fine.config.width +
                                x * ratio];
                sum += delta * delta;
            }
    return std::sqrt(sum / coarse.values.size());
}
void bounded(const State &s) {
    for (double v : s.values)
        check(std::isfinite(v) && v >= 0 && v <= 1, "Long trajectory left finite [0,1] bounds");
}
} // namespace
void numerical_tests(bool cpu_only) {
    for (bool volume : {false, true}) {
        for (Rule rule : {Rule::Classic, Rule::SmoothReaction, Rule::SmoothClip}) {
            auto initial = periodic_field(16, volume, rule);
            // Fixed-sharpness smooth clipping is not an ODE time discretization.
            if (rule != Rule::SmoothClip) {
                auto report = timestep_study(initial, 20, 4);
                check(report[0].rms_to_finest > report[1].rms_to_finest * 1.4 &&
                          report[1].rms_to_finest > report[2].rms_to_finest * 1.4 &&
                          report[2].rms_to_finest > 0 && report[3].rms_to_finest == 0,
                      "Time refinement failed on a smooth periodic field");
                auto coarse = evolved(periodic_field(16, volume, rule), 4);
                auto medium = evolved(periodic_field(32, volume, rule), 4);
                auto fine = evolved(periodic_field(64, volume, rule), 4);
                double coarse_error = aligned_error(coarse, fine), medium_error = aligned_error(medium, fine);
                check(medium_error < coarse_error * .75 && medium_error > 0,
                      "Spatial refinement failed with physical kernel radius held fixed");
                std::cout << "PASS refinement " << (volume ? "3D" : "2D") << " rule=" << unsigned(rule)
                          << " dt errors=" << report[0].rms_to_finest << '/' << report[1].rms_to_finest << '/'
                          << report[2].rms_to_finest << " grid errors=" << coarse_error << '/' << medium_error
                          << '\n';
            }
            auto cfg = initial.config;
            cfg.dt = .025;
            auto seed = random_state(cfg, 42);
            CpuSolver cpu(seed);
#ifdef _WIN32
            std::unique_ptr<GpuSolver> gpu;
            if (!cpu_only)
                gpu = std::make_unique<GpuSolver>(seed);
#endif
            for (int step = 1; step <= 500; ++step) {
                cpu.step();
#ifdef _WIN32
                if (gpu)
                    gpu->step();
#endif
                if (step % 100 != 0)
                    continue;
                auto reference = cpu.snapshot();
                bounded(reference);
#ifdef _WIN32
                if (gpu) {
                    auto actual = gpu->snapshot();
                    bounded(actual);
                    std::cout << "CPU/GPU drift " << (volume ? "3D" : "2D") << " rule=" << unsigned(rule)
                              << " step=" << step << " rms=" << aligned_error(reference, actual) << '\n';
                }
#endif
            }
        }
    }
    std::cout
        << "PASS 500-step trajectories; drift is measured without imposing long-term CPU/GPU identity\n";
}
