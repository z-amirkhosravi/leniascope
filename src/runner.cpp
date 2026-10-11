#include "runner.h"
#include "gpu_solver.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>
namespace lenia {
Runner::Runner(bool prefer_gpu) {
    use_gpu = prefer_gpu;
    worker = std::thread([this] { loop(); });
}
Runner::~Runner() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake.notify_one();
    worker.join();
}
std::shared_ptr<const Frame> Runner::frame() const {
    std::lock_guard<std::mutex> lock(mutex);
    return latest;
}
std::string Runner::message() const {
    std::lock_guard<std::mutex> lock(mutex);
    return status;
}
void Runner::set_message(std::string s) {
    std::lock_guard<std::mutex> lock(mutex);
    status = std::move(s);
}
void Runner::enqueue(std::function<void()> f) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (commands.size() >= 64)
            throw std::runtime_error("Simulation command queue is full");
        commands.push_back(std::move(f));
    }
    wake.notify_one();
}
std::unique_ptr<Solver> Runner::create(State s) {
    if (use_gpu)
        return std::make_unique<GpuSolver>(std::move(s));
    return std::make_unique<CpuSolver>(std::move(s));
}
void Runner::play(bool b) {
    enqueue([this, b] {
        running = b;
        single_steps = 0;
    });
}
void Runner::step() {
    enqueue([this] {
        running = false;
        ++single_steps;
    });
}
void Runner::reset(Config c, uint64_t seed) {
    enqueue([this, c, seed] {
        auto next = create(random_state(c, seed));
        volume = c.depth > 1;
        sessions[volume] = std::move(next);
        ++generation;
        set_message("Reset from reproducible seed " + std::to_string(seed));
    });
}
void Runner::parameters(Config c) {
    enqueue([this, c] {
        sessions[volume]->parameters(c);
        set_message("Parameters applied at a step boundary");
    });
}
void Runner::mode(bool v) {
    enqueue([this, v] {
        if (v == volume)
            return;
        if (!sessions[v]) {
            Config c;
            if (!v)
                c = {1024, 1024, 1, 104};
            sessions[v] = create(random_state(c, 1));
        }
        volume = v;
        ++generation;
        set_message("Restored independent simulation session");
    });
}
void Runner::backend(bool gpu) {
    enqueue([this, gpu] {
        if (use_gpu == gpu)
            return;
        std::unique_ptr<Solver> next[2];
        for (int i = 0; i < 2; ++i)
            if (sessions[i]) {
                auto s = sessions[i]->snapshot();
                if (gpu)
                    next[i] = std::make_unique<GpuSolver>(std::move(s));
                else
                    next[i] = std::make_unique<CpuSolver>(std::move(s));
            }
        for (int i = 0; i < 2; ++i)
            sessions[i] = std::move(next[i]);
        use_gpu = gpu;
        set_message(gpu ? "GPU float solver active" : "CPU double reference active");
    });
}
void Runner::speed(double rate) {
    enqueue([this, rate] { target_rate = std::max(1., std::min(240., rate)); });
}
void Runner::save(std::string path) {
    enqueue([this, path] {
        save_state(sessions[volume]->snapshot(), path);
        set_message("Saved " + path);
    });
}
void Runner::load(std::string path) {
    enqueue([this, path] {
        auto s = load_state(path);
        bool v = s.config.depth > 1;
        auto next = create(std::move(s));
        sessions[v] = std::move(next);
        volume = v;
        ++generation;
        running = false;
        single_steps = 0;
        set_message("Loaded " + path);
    });
}
void Runner::publish() {
    auto f = std::make_shared<Frame>();
    f->state = sessions[volume]->snapshot();
    f->timings = sessions[volume]->timings();
    f->measures = measure(f->state.values);
    f->serial = ++serial;
    f->generation = generation;
    f->running = running;
    f->gpu = use_gpu;
    f->steps_per_second = running ? measured_rate : 0;
    std::lock_guard<std::mutex> lock(mutex);
    latest = std::move(f);
}
void Runner::loop() {
    using Clock = std::chrono::steady_clock;
    try {
        auto initial = random_state(Config{}, 1);
        std::string message = "Ready. Press Play or Space.";
        try {
            sessions[1] = create(initial);
        } catch (const std::exception &e) {
            if (!use_gpu)
                throw;
            use_gpu = false;
            sessions[1] = create(std::move(initial));
            message = std::string("GPU unavailable; using CPU: ") + e.what();
        }
        publish();
        set_message(message);
    } catch (const std::exception &e) {
        set_message(e.what());
        return;
    }
    auto deadline = Clock::now(), last_publish = deadline, rate_start = deadline;
    uint64_t interval_steps = 0;
    for (;;) {
        std::deque<std::function<void()>> work;
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!running && !single_steps)
                wake.wait(lock, [this] { return stopping || !commands.empty(); });
            else
                wake.wait_until(lock, deadline, [this] { return stopping || !commands.empty(); });
            if (stopping)
                break;
            work.swap(commands);
        }
        try {
            for (auto &f : work)
                f();
            auto now = Clock::now();
            bool advanced = false;
            if ((running || single_steps) && now >= deadline) {
                sessions[volume]->step();
                ++interval_steps;
                advanced = true;
                if (single_steps)
                    --single_steps;
                deadline = std::max(Clock::now(), now + std::chrono::duration_cast<Clock::duration>(
                                                            std::chrono::duration<double>(1. / target_rate)));
            }
            now = Clock::now();
            double elapsed = std::chrono::duration<double>(now - rate_start).count();
            if (elapsed >= .5) {
                measured_rate = interval_steps / elapsed;
                interval_steps = 0;
                rate_start = now;
            }
            if (!work.empty() || (advanced && (single_steps == 0 && !running ||
                                               now - last_publish >= std::chrono::milliseconds(33)))) {
                publish();
                last_publish = now;
            }
        } catch (const std::exception &e) {
            running = false;
            single_steps = 0;
            set_message(e.what());
            try {
                publish();
            } catch (...) {
            }
        }
    }
}
} // namespace lenia
