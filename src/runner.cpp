#include "runner.h"
#include "gpu_solver.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <system_error>
#include <utility>
namespace lenia {
struct Runner::Wake {
    using Clock = std::chrono::steady_clock;
    using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
    Handle command{nullptr, CloseHandle}, timer{nullptr, CloseHandle};
    Wake() {
        command.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!command)
            throw std::system_error(GetLastError(), std::system_category(), "Create worker command event");
        timer.reset(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                           TIMER_MODIFY_STATE | SYNCHRONIZE));
        // Older Windows versions may not support the high-resolution flag.
        if (!timer && GetLastError() == ERROR_INVALID_PARAMETER)
            timer.reset(CreateWaitableTimerW(nullptr, FALSE, nullptr));
        if (!timer)
            throw std::system_error(GetLastError(), std::system_category(), "Create worker timer");
    }
    void notify() {
        SetEvent(command.get());
    }
    void wait(std::unique_lock<std::mutex> &lock, bool active, Clock::time_point deadline) {
        if (active) {
            auto remaining = deadline - Clock::now();
            if (remaining <= Clock::duration::zero())
                return;
            using Ticks = std::chrono::duration<LONGLONG, std::ratio<1, 10000000>>;
            LARGE_INTEGER due;
            due.QuadPart = -std::max<LONGLONG>(1, std::chrono::duration_cast<Ticks>(remaining).count());
            if (!SetWaitableTimerEx(timer.get(), &due, 0, nullptr, nullptr, nullptr, 0))
                throw std::system_error(GetLastError(), std::system_category(), "Set worker deadline");
        } else {
            CancelWaitableTimer(timer.get());
        }
        HANDLE handles[]{command.get(), timer.get()};
        // The auto-reset event remembers commands arriving between unlock and wait. Both
        // paused and timed waits can be interrupted immediately, including during shutdown.
        lock.unlock();
        auto result = WaitForMultipleObjects(active ? 2 : 1, handles, FALSE, INFINITE);
        auto error = GetLastError();
        lock.lock();
        if (result == WAIT_FAILED)
            throw std::system_error(error, std::system_category(), "Wait for worker deadline");
    }
};
Runner::Runner(bool prefer_gpu, SessionStates initial, bool initial_volume, std::function<void()> notify,
               std::shared_ptr<GpuContext> display, std::array<SeedSettings, 2> seeds, bool prefer_cuda)
    : wake(std::make_unique<Wake>()), display_context(std::move(display)), notify_frame(std::move(notify)) {
    seed_defaults = seeds;
    use_gpu = prefer_gpu;
    use_cuda = prefer_gpu && prefer_cuda;
    volume = initial_volume;
    worker = std::thread([this, initial = std::move(initial)]() mutable {
        loop(std::move(initial));
        std::lock_guard<std::mutex> lock(mutex);
        finished = true;
        commands.clear(); // Resolve pending promises even if initialization failed.
    });
}
Runner::~Runner() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake->notify();
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
        if (stopping || finished)
            throw std::runtime_error("Simulation worker is unavailable: " + status);
        if (commands.size() >= 64)
            throw std::runtime_error("Simulation command queue is full");
        commands.push_back(std::move(f));
    }
    wake->notify();
}
std::unique_ptr<Solver> Runner::create(State s) {
    if (use_gpu)
        return std::make_unique<GpuSolver>(std::move(s), display_context, use_cuda);
    return std::make_unique<CpuSolver>(std::move(s));
}
void Runner::play(bool b) {
    enqueue([this, b] { running = b; });
}
void Runner::step() {
    enqueue([this] {
        running = false;
        // Execute in command order, before a following mode switch, reset, or save.
        sessions[volume]->step();
        publish();
    });
}
void Runner::reset(Config c, uint64_t seed, SeedSettings settings) {
    enqueue([this, c, seed, settings] {
        auto next = create(random_state(c, seed, settings));
        if (volume != (c.depth > 1))
            publish();
        volume = c.depth > 1;
        sessions[volume] = std::move(next);
        ++initialization_revision[volume];
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
            sessions[v] = create(random_state(c, 1, seed_defaults[v]));
        }
        publish(); // Preserve preceding parameter/step commands before leaving this session.
        volume = v;
        ++generation;
        set_message("Restored independent simulation session");
    });
}
void Runner::backend(bool gpu, bool cuda) {
    cuda = gpu && cuda;
    enqueue([this, gpu, cuda] {
        if (use_gpu == gpu && use_cuda == cuda)
            return;
        std::unique_ptr<Solver> next[2];
        for (int i = 0; i < 2; ++i)
            if (sessions[i]) {
                auto s = sessions[i]->snapshot();
                if (gpu)
                    next[i] = std::make_unique<GpuSolver>(std::move(s), display_context, cuda);
                else
                    next[i] = std::make_unique<CpuSolver>(std::move(s));
            }
        for (int i = 0; i < 2; ++i)
            sessions[i] = std::move(next[i]);
        use_gpu = gpu;
        use_cuda = cuda;
        if (sessions[!volume])
            publish(!volume);
        set_message(cuda ? "CUDA / cuFFT float solver active"
                         : (gpu ? "DirectCompute float solver active" : "CPU double reference active"));
    });
}
void Runner::speed(double rate) {
    if (!std::isfinite(rate))
        throw std::invalid_argument("Playback rate must be finite");
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
        if (volume != v)
            publish();
        sessions[v] = std::move(next);
        ++initialization_revision[v];
        volume = v;
        ++generation;
        running = false;
        set_message("Loaded " + path);
    });
}
std::future<State> Runner::capture_current() {
    auto promise = std::make_shared<std::promise<State>>();
    auto future = promise->get_future();
    enqueue([this, promise] {
        running = false;
        try {
            auto state = sessions[volume]->snapshot();
            publish();
            promise->set_value(std::move(state));
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });
    return future;
}
std::future<void> Runner::restore(State state) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    enqueue([this, promise, state = std::move(state)]() mutable {
        running = false;
        try {
            bool v = state.config.depth > 1;
            bool needs_double = use_gpu && std::any_of(state.values.begin(), state.values.end(),
                                                       [](double a) { return double(float(a)) != a; });
            std::unique_ptr<Solver> next =
                needs_double ? std::make_unique<CpuSolver>(std::move(state)) : create(std::move(state));
            std::unique_ptr<Solver> other;
            if (needs_double && sessions[!v])
                other = std::make_unique<CpuSolver>(sessions[!v]->snapshot());
            if (volume != v)
                publish();
            if (needs_double) {
                sessions[!v] = std::move(other);
                use_gpu = use_cuda = false;
            }
            sessions[v] = std::move(next);
            ++initialization_revision[v];
            volume = v;
            ++generation;
            publish();
            set_message(needs_double ? "Loaded gallery field exactly; CPU selected to preserve float64 values"
                                     : "Loaded gallery field; paused");
            promise->set_value();
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });
    return future;
}
SessionSnapshot Runner::snapshot_sessions_now() {
    // Worker only: exact, ordered capture on demand, independent of display leases.
    SessionSnapshot result;
    result.volume = volume;
    for (int i = 0; i < 2; ++i)
        if (sessions[i])
            result.states[i] = std::make_shared<State>(sessions[i]->snapshot());
    return result;
}
std::future<SessionSnapshot> Runner::snapshot_sessions() {
    return capture(false);
}
std::future<SessionSnapshot> Runner::pause_and_snapshot() {
    return capture(true);
}
std::future<SessionSnapshot> Runner::capture(bool pause) {
    auto promise = std::make_shared<std::promise<SessionSnapshot>>();
    auto future = promise->get_future();
    enqueue([this, promise, pause] {
        if (pause)
            running = false;
        try {
            auto states = snapshot_sessions_now();
            if (pause)
                publish();
            promise->set_value(std::move(states));
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });
    return future;
}
void Runner::publish(int index) {
    if (index < 0)
        index = volume;
    auto started = std::chrono::steady_clock::now();
    auto f = std::make_shared<Frame>();
    if (use_gpu && display_context) {
        f->gpu_image = static_cast<GpuSolver *>(sessions[index].get())->display_snapshot();
        if (!f->gpu_image)
            return; // Bounded latest-frame delivery; never skip a numerical step.
        f->state = f->gpu_image->metadata;
        f->measures = f->gpu_image->measures;
    } else {
        f->state = sessions[index]->snapshot();
        f->measures = measure(f->state.values);
    }
    f->timings = sessions[index]->timings();
    f->publication_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    f->serial = ++serial;
    f->generation = generation;
    f->initialization_revision = initialization_revision[index];
    f->running = running && index == int(volume);
    f->gpu = use_gpu;
    f->cuda = use_cuda;
    f->steps_per_second = running ? measured_rate : 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (index == int(volume))
            latest = std::move(f);
    }
    if (index == int(volume) && notify_frame)
        notify_frame();
}
void Runner::loop(SessionStates initial) {
    using Clock = std::chrono::steady_clock;
    try {
        if (!initial[volume])
            initial[volume] = std::make_shared<State>(
                random_state(volume ? Config{} : Config{1024, 1024, 1, 104}, 1, seed_defaults[volume]));
        auto initialize = [&] {
            std::unique_ptr<Solver> next[2];
            for (int i = 0; i < 2; ++i)
                if (initial[i]) {
                    if ((initial[i]->config.depth > 1) != bool(i))
                        throw std::invalid_argument("Restored session dimension mismatch");
                    next[i] = create(*initial[i]);
                }
            for (int i = 0; i < 2; ++i)
                sessions[i] = std::move(next[i]);
        };
        std::string message = "Ready. Press Play or Space.";
        try {
            try {
                initialize();
            } catch (const std::exception &e) {
                if (!use_cuda)
                    throw;
                use_cuda = false;
                message = std::string("CUDA unavailable; using DirectCompute: ") + e.what();
                initialize();
            }
        } catch (const std::exception &e) {
            if (!use_gpu)
                throw;
            use_gpu = false;
            initialize();
            message = std::string("GPU unavailable; using CPU: ") + e.what();
        }
        initial = {};
        if (sessions[!volume])
            publish(!volume);
        publish();
        set_message(message);
    } catch (const std::exception &e) {
        set_message(e.what());
        return;
    }
    auto deadline = Clock::now(), last_step_start = deadline, rate_start = deadline;
    uint64_t interval_steps = 0;
    for (;;) {
        std::deque<std::function<void()>> work;
        bool finish = false;
        try {
            std::unique_lock<std::mutex> lock(mutex);
            while (!stopping && commands.empty() && (!running || Clock::now() < deadline))
                wake->wait(lock, running, deadline);
            if (stopping && commands.empty())
                break;
            finish = stopping;
            work.swap(commands);
        } catch (const std::exception &e) {
            running = false;
            set_message(e.what());
            try {
                publish();
            } catch (...) {
            }
            return;
        }
        try {
            bool was_active = running;
            double previous_rate = target_rate;
            std::string command_error;
            for (auto &f : work) {
                try {
                    f();
                } catch (const std::exception &e) {
                    running = false;
                    command_error = e.what();
                }
            }
            if (!command_error.empty())
                set_message(command_error);
            if (finish)
                running = false;
            auto now = Clock::now();
            auto period =
                std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1. / target_rate));
            // Resume and speed changes must not inherit an old (possibly far away) deadline.
            if (running && !was_active) {
                deadline = now;
                rate_start = now;
                interval_steps = 0;
                measured_rate = 0;
            } else if (target_rate != previous_rate)
                deadline = last_step_start + period;
            bool advanced = false;
            if (running && now >= deadline) {
                last_step_start = now;
                sessions[volume]->step();
                ++interval_steps;
                advanced = true;
                // Anchor pacing to the schedule, rather than accumulating the wait's overshoot.
                // If computation falls behind, continue at capacity without a catch-up backlog.
                deadline = std::max(Clock::now(), deadline + period);
            }
            now = Clock::now();
            double elapsed = std::chrono::duration<double>(now - rate_start).count();
            if (elapsed >= .5) {
                measured_rate = interval_steps / elapsed;
                interval_steps = 0;
                rate_start = now;
            }
            // Retain only the newest immutable state, but offer every completed step. A separate
            // 30 Hz cap here repeated old images even on a fast solver and high-refresh display.
            if (!work.empty() || advanced)
                publish();
        } catch (const std::exception &e) {
            running = false;
            set_message(e.what());
            try {
                publish();
            } catch (...) {
            }
        }
        if (finish)
            break;
    }
}
} // namespace lenia
