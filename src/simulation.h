#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lenia {
enum class Rule : uint32_t {
    Classic,
    SmoothReaction
};
struct Config {
    int width = 128, height = 128, depth = 128, radius = 26;
    double mu = .15, sigma = .017, dt = .1, smoothness = 12;
    Rule rule = Rule::Classic;
    size_t count() const;
    void validate() const;
    bool operator==(const Config &c) const;
};
struct State {
    Config config;
    std::vector<double> values;
    uint64_t seed = 1, steps = 0;
    double time = 0;
    std::string backend = "CPU double / FFTW";
};
struct Timings {
    double forward_ms = 0, multiply_ms = 0, inverse_ms = 0, update_ms = 0, total_ms = 0;
};
struct Measures {
    double mean = 0, minimum = 0, maximum = 0, rms_change = 0;
};
std::vector<double> make_kernel(const Config &);
State random_state(Config, uint64_t seed);
double advance_value(double a, double neighborhood, const Config &);
Measures measure(const std::vector<double> &, const std::vector<double> *previous = nullptr);
// RMS mismatch after an integer translation on the periodic domain.
double recurrence_error(const State &, const State &, int dx, int dy, int dz);
struct Translation {
    int dx = 0, dy = 0, dz = 0;
    double rms = 0;
};
Translation find_translation(const State &, const State &);
void save_state(const State &, const std::string &filename);
State load_state(const std::string &filename);
class Solver {
  public:
    virtual ~Solver() = default;
    virtual void step() = 0;
    virtual State snapshot() = 0;
    virtual void parameters(const Config &) = 0;
    virtual const Timings &timings() const = 0;
};
class CpuSolver final : public Solver {
  public:
    explicit CpuSolver(State);
    ~CpuSolver();
    void step() override;
    State snapshot() override;
    void parameters(const Config &) override;
    const Timings &timings() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace lenia
