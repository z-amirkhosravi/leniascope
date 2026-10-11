#include "src/gpu_solver.h"
#include "src/runner.h"
#include "src/simulation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <thread>
using namespace lenia;
void require(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
void compare_convolution(Config c) {
    State s{c, std::vector<double>(c.count()), 42};
    for (size_t i = 0; i < s.values.size(); ++i)
        s.values[i] = .08 + .12 * ((i * 31) % 101) / 100.;
    // Build the radial kernel independently, including its periodic coordinates.
    std::vector<double> kernel(c.count());
    double sum = 0;
    for (int z = 0; z < c.depth; ++z)
        for (int y = 0; y < c.height; ++y)
            for (int x = 0; x < c.width; ++x) {
                int xx = std::min(x, c.width - x), yy = std::min(y, c.height - y),
                    zz = std::min(z, c.depth - z);
                double r = std::sqrt(double(xx * xx + yy * yy + zz * zz)) / c.radius;
                if (r < 1) {
                    double v = std::exp(-.5 * std::pow((r - .5) / .15, 2));
                    kernel[(z * c.height + y) * c.width + x] = v;
                    sum += v;
                }
            }
    CpuSolver solver(s);
    solver.step();
    auto actual = solver.snapshot();
    double error = 0;
    for (int z = 0; z < c.depth; ++z)
        for (int y = 0; y < c.height; ++y)
            for (int x = 0; x < c.width; ++x) {
                double u = 0;
                for (int k = 0; k < c.depth; ++k)
                    for (int j = 0; j < c.height; ++j)
                        for (int i = 0; i < c.width; ++i)
                            u += kernel[(k * c.height + j) * c.width + i] / sum *
                                 s.values[(((z - k + c.depth) % c.depth) * c.height +
                                           (y - j + c.height) % c.height) *
                                              c.width +
                                          (x - i + c.width) % c.width];
                double g = 2 * std::exp(-.5 * std::pow((u - c.mu) / c.sigma, 2)) - 1;
                double expected = std::clamp(s.values[(z * c.height + y) * c.width + x] + c.dt * g, 0., 1.);
                error = std::max(error, std::abs(expected - actual.values[(z * c.height + y) * c.width + x]));
            }
    require(error < 1e-12, "FFT differs from independent direct convolution");
    std::cout << "PASS convolution " << c.width << 'x' << c.height << 'x' << c.depth << " error=" << error
              << '\n';
}
int main(int argc, char **argv) {
    try {
        bool cpu_only = argc > 1 && std::string(argv[1]) == "--cpu-only";
        compare_convolution({8, 8, 1, 2});
        compare_convolution({16, 8, 1, 2});
        compare_convolution({8, 4, 16, 2});
        Config c{16, 8, 4, 2};
        auto s = random_state(c, 123), same = random_state(c, 123), other = random_state(c, 124);
        require(s.values == same.values && s.values != other.values, "Seed reproducibility");
        auto k = make_kernel(c);
        require(std::abs(std::accumulate(k.begin(), k.end(), 0.) - 1) < 1e-14, "Kernel normalization");
        CpuSolver a(s), b(s);
        for (int i = 0; i < 5; ++i) {
            a.step();
            b.step();
        }
        auto after = a.snapshot();
        require(after.values == b.snapshot().values, "Repeated CPU trajectories differ");
        after.seed = 999;
        save_state(after, "test-roundtrip.lenia");
        save_state(after, "test-roundtrip.lenia");
        auto loaded = load_state("test-roundtrip.lenia");
        std::remove("test-roundtrip.lenia");
        require(after.values == loaded.values && after.config == loaded.config &&
                    after.steps == loaded.steps && after.time == loaded.time && loaded.seed == 999,
                "Checkpoint round trip");
        std::ofstream bad("test-invalid.lenia", std::ios::binary);
        bad.write("LeniaS2", 7);
        bad.close();
        bool rejected = false;
        try {
            load_state("test-invalid.lenia");
        } catch (...) {
            rejected = true;
        }
        std::remove("test-invalid.lenia");
        require(rejected, "Truncated file accepted");
        auto translated = s;
        for (int z = 0; z < c.depth; ++z)
            for (int y = 0; y < c.height; ++y)
                for (int x = 0; x < c.width; ++x)
                    translated.values[(z * c.height + y) * c.width + (x + 3) % c.width] =
                        s.values[(z * c.height + y) * c.width + x];
        require(recurrence_error(s, translated, 3, 0, 0) < 1e-15, "Torus translation mismatch");
        auto shift = find_translation(s, translated);
        require(shift.dx == 3 && shift.dy == 0 && shift.dz == 0 && shift.rms < 1e-14,
                "FFT shift recovery failed");
        for (int axis = 0; axis < 3; ++axis) {
            Config invalid = c;
            if (axis == 0)
                invalid.width = 3;
            if (axis == 1)
                invalid.radius = 100;
            if (axis == 2)
                invalid.sigma = std::numeric_limits<double>::quiet_NaN();
            bool failed = false;
            try {
                invalid.validate();
            } catch (...) {
                failed = true;
            }
            require(failed, "Invalid configuration accepted");
        }
        c.rule = Rule::SmoothReaction;
        for (double dt : {1., .1, .001})
            for (double u : {0., .15, 1.})
                for (double v : {0., .2, 1.}) {
                    c.dt = dt;
                    double n = advance_value(v, u, c);
                    require(std::isfinite(n) && n >= 0 && n <= 1, "Smooth reaction left bounds");
                }
        c.dt = 1e-8;
        double v = .3, u = .15, g = 1, sharp = c.smoothness, birth = std::log1p(std::exp(sharp * g)) / sharp,
               death = std::log1p(std::exp(-sharp * g)) / sharp;
        require(std::abs((advance_value(v, u, c) - v) / c.dt - (birth * (1 - v) - death * v)) < 1e-7,
                "Smooth small-step consistency");
        std::cout << "PASS seeded initialization, normalized kernel, repeatability, checkpoints, periodic "
                     "recurrence, smooth bounded reaction\n";
#ifdef _WIN32
        if (!cpu_only)
            for (Config cfg : {Config{16, 8, 1, 2}, Config{8, 4, 16, 2}, Config{32, 32, 32, 6}}) {
                auto initial = random_state(cfg, 100);
                CpuSolver cpu(initial);
                GpuSolver gpu(initial);
                for (int step = 0; step < 5; ++step) {
                    cpu.step();
                    gpu.step();
                }
                auto expected = cpu.snapshot(), actual = gpu.snapshot();
                double error = 0;
                for (size_t i = 0; i < expected.values.size(); ++i)
                    error = std::max(error, std::abs(expected.values[i] - actual.values[i]));
                require(error < 2e-5, "GPU differs from CPU reference");
                std::cout << "PASS GPU " << cfg.width << 'x' << cfg.height << 'x' << cfg.depth
                          << " max_error=" << error << " step_ms=" << gpu.timings().total_ms << '\n';
                cfg.rule = Rule::SmoothReaction;
                cpu.parameters(cfg);
                gpu.parameters(cfg);
                cpu.step();
                gpu.step();
                expected = cpu.snapshot();
                actual = gpu.snapshot();
                error = 0;
                for (size_t i = 0; i < expected.values.size(); ++i)
                    error = std::max(error, std::abs(expected.values[i] - actual.values[i]));
                require(error < 3e-5, "GPU smooth reaction differs from CPU");
            }
        auto shutdown_start = std::chrono::steady_clock::now();
        {
            Runner runner(false);
            auto await_frame = [&](std::function<bool(const Frame &)> predicate) {
                auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                while (std::chrono::steady_clock::now() < end) {
                    auto f = runner.frame();
                    if (f && predicate(*f))
                        return f;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                throw std::runtime_error("Runner timeout: " + runner.message());
            };
            await_frame([](const Frame &) { return true; });
            runner.reset({16, 16, 16, 3}, 123);
            auto original =
                await_frame([](const Frame &f) { return f.state.config.width == 16 && f.state.seed == 123; });
            runner.step();
            auto advanced = await_frame([](const Frame &f) { return f.state.steps == 1; });
            require(original->state.steps == 0 && original->state.values != advanced->state.values,
                    "Published snapshot was mutated");
            runner.reset({16, 16, 1, 3}, 456);
            await_frame([](const Frame &f) { return f.state.config.depth == 1 && f.state.seed == 456; });
            runner.mode(true);
            auto restored = await_frame([](const Frame &f) { return f.state.config.depth == 16; });
            require(restored->state.values == advanced->state.values, "Mode switch lost state");
            runner.play(true);
            await_frame([](const Frame &f) { return f.state.steps > 1; });
            runner.play(false);
            auto paused = await_frame([](const Frame &f) { return !f.running && f.state.steps > 1; });
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            require(runner.frame()->state.steps == paused->state.steps, "Paused worker advanced");
            // Count publications by serial, so this check does not depend on how often the test
            // thread is scheduled. Every completed step must be available to the display even
            // when no controls are being touched (dragging a slider used to bypass a 30 Hz cap).
            for (double rate : {60., 120.}) {
                runner.speed(rate);
                runner.play(true);
                auto start = await_frame(
                    [&](const Frame &f) { return f.running && f.state.steps > paused->state.steps; });
                auto begin = std::chrono::steady_clock::now();
                auto finish =
                    await_frame([&](const Frame &f) { return f.state.steps >= start->state.steps + 48; });
                double seconds =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
                auto steps = finish->state.steps - start->state.steps;
                auto frames = finish->serial - start->serial;
                runner.play(false);
                paused = await_frame([](const Frame &f) { return !f.running; });
                std::cout << "Worker target=" << rate << " steps/s: " << steps / seconds << " steps/s, "
                          << frames / seconds << " snapshots/s (" << frames << " snapshots / " << steps
                          << " steps)\n";
                require(frames == steps, "Running steps were hidden from the display between UI commands");
            }
            runner.speed(1);
            runner.play(true);
            auto slow =
                await_frame([&](const Frame &f) { return f.running && f.state.steps > paused->state.steps; });
            auto command_start = std::chrono::steady_clock::now();
            runner.speed(120);
            await_frame([&](const Frame &f) { return f.state.steps >= slow->state.steps + 4; });
            require(std::chrono::steady_clock::now() - command_start < std::chrono::milliseconds(750),
                    "Speed change waited for the previous one-second deadline");
            runner.play(false);
            await_frame([](const Frame &f) { return !f.running; });
            if (!cpu_only) {
                runner.backend(true);
                await_frame([](const Frame &f) { return f.gpu; });
                runner.mode(false);
                auto restored2 =
                    await_frame([](const Frame &f) { return f.gpu && f.state.config.depth == 1; });
                require(restored2->state.seed == 456 && restored2->state.steps == 0,
                        "Backend switch lost inactive mode");
            }
            auto before_shutdown = runner.frame();
            runner.speed(1);
            runner.play(true);
            await_frame(
                [&](const Frame &f) { return f.running && f.state.steps > before_shutdown->state.steps; });
            shutdown_start = std::chrono::steady_clock::now();
        }
        require(std::chrono::steady_clock::now() - shutdown_start < std::chrono::milliseconds(750),
                "Shutdown waited for the pending one-second deadline");
        std::cout << "PASS worker pause, step, immutable snapshots, publication cadence, speed changes, "
                     "mode preservation, backend transition, interruptible shutdown\n";
#endif
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
