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
void workspace_tests();
void gallery_tests(bool cpu_only);
void behavior_tests();
void morphology_tests();
void batch_tests(bool cpu_only);
void field_edit_tests();
void numerical_tests(bool cpu_only);
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
void seed_tests();
void smooth_clip_tests() {
    Config smooth{16, 16, 1, 3};
    smooth.rule = Rule::SmoothClip;
    // Check the actual update, including both clipping corners and overshoots.
    for (double dt : {1., .1, .001}) {
        smooth.dt = dt;
        for (double u : {0., .15, 1.})
            for (int i = 0; i <= 100; ++i) {
                const double a = i / 100.;
                Config classic = smooth;
                classic.rule = Rule::Classic;
                const double reference = advance_value(a, u, classic);
                double previous_error = 1;
                for (double k : {1., 5., 12., 40., 100., 1000., 10000.}) {
                    smooth.smoothness = k;
                    smooth.validate();
                    const double value = advance_value(a, u, smooth);
                    const double error = std::abs(value - reference);
                    require(std::isfinite(value) && value >= 0 && value <= 1,
                            "Smooth clipping left finite [0,1] bounds");
                    require(error <= std::log(2.) / k + 1e-14 && error <= previous_error + 1e-14,
                            "Smooth clipping does not converge uniformly to classic clipping");
                    previous_error = error;
                }
            }
    }
    smooth.dt = .1;
    // At a=dt and g=-1, the unclipped proposal is exactly zero.
    for (double k : {1., 5., 40., 1000., 10000.}) {
        smooth.smoothness = k;
        const double corner = (std::log(2.) - std::log1p(std::exp(-k))) / k;
        require(std::abs(advance_value(.1, 1, smooth) - corner) < 1e-14 &&
                    std::abs(advance_value(.9, smooth.mu, smooth) - (1 - corner)) < 1e-14,
                "Smooth clip corners differ from the analytic approximation");
        const double h = 1e-7;
        const double left = (advance_value(.1, 1, smooth) - advance_value(.1 - h, 1, smooth)) / h;
        const double right = (advance_value(.1 + h, 1, smooth) - advance_value(.1, 1, smooth)) / h;
        require(std::abs(left - right) < k * h, "Smooth clip retained the classic derivative jump at zero");
    }
    smooth.smoothness = 1000;
    require(std::abs(advance_value(.25, 1, smooth) - .15) < 1e-14 &&
                std::abs(advance_value(.25, smooth.mu, smooth) - .35) < 1e-14,
            "Smooth clipping rescaled interior birth or death");
    // Model IDs must not reinterpret an existing birth/death checkpoint.
    for (auto rule : {Rule::SmoothReaction, Rule::SmoothClip}) {
        auto c = smooth;
        c.rule = rule;
        c.smoothness = default_sharpness(rule);
        const auto source = random_state(c, 123);
        save_state(source, "test-rule.lenia");
        const auto loaded = load_state("test-rule.lenia");
        std::remove("test-rule.lenia");
        require(loaded.config == source.config && loaded.values == source.values,
                "Saved rule or sharpness changed on load");
        CpuSolver first(source), resumed(loaded);
        first.step();
        resumed.step();
        require(first.snapshot().values == resumed.snapshot().values, "Saved rule changed continuation");
    }
    std::cout << "PASS smooth clip bounds, analytic corners, interior growth, convergence and saved rules\n";
}
int main(int argc, char **argv) {
    try {
        bool cpu_only = argc > 1 && std::string(argv[1]) == "--cpu-only";
        smooth_clip_tests();
        seed_tests();
        workspace_tests();
        gallery_tests(cpu_only);
        behavior_tests();
        morphology_tests();
        batch_tests(cpu_only);
        field_edit_tests();
        numerical_tests(cpu_only);
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
                    after.steps == loaded.steps && after.time == loaded.time && loaded.seed == 999 &&
                    loaded.initialization && loaded.initialization->patches == 10,
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
            runner.randomize({16, 16, 16, 3}, 123);
            auto original =
                await_frame([](const Frame &f) { return f.state.config.width == 16 && f.state.seed == 123; });
            runner.step();
            auto advanced = await_frame([](const Frame &f) { return f.state.steps == 1; });
            require(original->state.steps == 0 && original->state.values != advanced->state.values,
                    "Published snapshot was mutated");
            runner.randomize({16, 16, 1, 3}, 456);
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
        {
            // Queue the whole batch during startup: a failure must not discard later actions,
            // and Step must apply to the session selected at that point in the command sequence.
            Runner runner(false);
            runner.load("missing-reliability-test-checkpoint.lenia");
            Config cfg{16, 16, 1, 3};
            SeedSettings recipe{23, .8, 1.5, .2, .7};
            runner.randomize(cfg, 987, recipe);
            runner.speed(1);
            runner.step();
            cfg.mu = .21;
            runner.parameters(cfg);
            runner.mode(true);
            auto future = runner.pause_and_snapshot();
            require(future.wait_for(std::chrono::seconds(15)) == std::future_status::ready,
                    "Failed command discarded following commands/barrier");
            auto states = future.get();
            require(states.volume && states.states[0] && states.states[1] && states.states[0]->seed == 987 &&
                        states.states[0]->steps == 1 && states.states[0]->config.mu == .21 &&
                        states.states[1]->steps == 0 && states.states[0]->initialization &&
                        states.states[0]->initialization->patches == 23 &&
                        states.states[0]->initialization_radius == 3,
                    "Command order or inactive session snapshot was lost");
            Runner resumed(false, states.states, false);
            auto restored = resumed.pause_and_snapshot().get();
            require(!restored.volume && restored.states[0]->values == states.states[0]->values &&
                        restored.states[1]->values == states.states[1]->values,
                    "Restored runner changed a paused session");
        }
        std::cout << "PASS worker pause, step, immutable snapshots, publication cadence, speed changes, "
                     "mode preservation, backend transition, command failures/order, restored sessions, "
                     "interruptible shutdown\n";
#endif
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
