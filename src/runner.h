#pragma once
#include "simulation.h"
#include "workspace.h"
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
namespace lenia {
struct GpuContext;
struct GpuImage;
struct Frame {
    State state; // Values are empty when gpu_image supplies a GPU-resident display.
    std::shared_ptr<const GpuImage> gpu_image;
    double publication_ms = 0;
    Timings timings;
    Measures measures;
    uint64_t serial = 0, generation = 0, initialization_revision = 0;
    bool running = false, gpu = false, cuda = false, has_initial_field = false;
    double steps_per_second = 0;
};
class Runner {
  public:
    explicit Runner(bool prefer_gpu = true, SessionStates initial = {}, bool initial_volume = true,
                    std::function<void()> notify_frame = {}, std::shared_ptr<GpuContext> display_context = {},
                    std::array<SeedSettings, 2> seeds = {}, bool prefer_cuda = false,
                    SessionStates reset_states = {});
    ~Runner();
    Runner(const Runner &) = delete;
    Runner &operator=(const Runner &) = delete;
    std::shared_ptr<const Frame> frame() const;
    std::string message() const;
    void play(bool);
    void step();
    // Reset uses the loaded field, if any, and the currently applied model parameters.
    // Otherwise it regenerates a random field with these seed controls.
    void reset(uint64_t, SeedSettings = {});
    void randomize(Config, uint64_t, SeedSettings = {});
    void parameters(Config);
    void mode(bool volume);
    void backend(bool gpu, bool cuda = false);
    void speed(double steps_per_second);
    void save(std::string);
    void load(std::string);
    std::future<State> capture_current();
    std::future<void> restore(State);
    std::future<SessionSnapshot> snapshot_sessions();
    std::future<SessionSnapshot> pause_and_snapshot();

  private:
    void enqueue(std::function<void()>);
    void loop(SessionStates);
    void publish(int index = -1);
    SessionSnapshot snapshot_sessions_now();
    std::future<SessionSnapshot> capture(bool pause);
    void set_message(std::string);
    std::unique_ptr<Solver> create(State);
    void randomize_now(Config, uint64_t, SeedSettings);
    bool install(State, bool remember_initial);
    mutable std::mutex mutex;
    struct Wake;
    std::unique_ptr<Wake> wake;
    std::deque<std::function<void()>> commands;
    bool stopping = false, finished = false;
    std::thread worker;
    std::shared_ptr<const Frame> latest;
    std::shared_ptr<GpuContext> display_context;
    std::function<void()> notify_frame;
    std::string status = "Starting simulation...";
    // Fields below are accessed only by the worker.
    std::unique_ptr<Solver> sessions[2];
    SessionStates reset_states; // Immutable starting fields; absent for random initializations.
    std::array<SeedSettings, 2> seed_defaults;
    uint64_t initialization_revision[2]{};
    bool volume = true, use_gpu = false, use_cuda = false, running = false;
    double target_rate = 60, measured_rate = 0;
    uint64_t serial = 0, generation = 0;
};
} // namespace lenia
