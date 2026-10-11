#pragma once
#include "simulation.h"
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace lenia {
struct Frame {
    State state;
    Timings timings;
    Measures measures;
    uint64_t serial = 0, generation = 0;
    bool running = false, gpu = false;
    double steps_per_second = 0;
};
class Runner {
  public:
    explicit Runner(bool prefer_gpu = true);
    ~Runner();
    Runner(const Runner &) = delete;
    Runner &operator=(const Runner &) = delete;
    std::shared_ptr<const Frame> frame() const;
    std::string message() const;
    void play(bool);
    void step();
    void reset(Config, uint64_t);
    void parameters(Config);
    void mode(bool volume);
    void backend(bool gpu);
    void speed(double steps_per_second);
    void save(std::string);
    void load(std::string);

  private:
    void enqueue(std::function<void()>);
    void loop();
    void publish();
    void set_message(std::string);
    std::unique_ptr<Solver> create(State);
    mutable std::mutex mutex;
    struct Wake;
    std::unique_ptr<Wake> wake;
    std::deque<std::function<void()>> commands;
    bool stopping = false;
    std::thread worker;
    std::shared_ptr<const Frame> latest;
    std::string status = "Starting simulation...";
    // Fields below are accessed only by the worker.
    std::unique_ptr<Solver> sessions[2];
    bool volume = true, use_gpu = false, running = false;
    int single_steps = 0;
    double target_rate = 60, measured_rate = 0;
    uint64_t serial = 0, generation = 0;
};
} // namespace lenia
