#pragma once
#include <cstdint>
#include <memory>
#include <optional>
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
enum class SeedLayout : uint32_t {
    Uniform,
    Cluster,
    Pair
};
const char *seed_layout_name(SeedLayout);
struct SeedSettings {
    int patches = 10;
    double size_min = .6, size_max = 1.2; // Independent box side lengths, in kernel radii.
    double value_min = 0, value_max = 1;
    SeedLayout layout = SeedLayout::Uniform;
    double spread = 1.5;             // Cluster centre radius or pair centre separation, in kernel radii.
    double angle = 0, elevation = 0; // Pair direction in degrees; elevation applies only in 3D.
    void validate() const;
};
// Expected geometric coverage for uniformly located, overlapping boxes; not a growth prediction.
double seed_coverage(const Config &, const SeedSettings &);
SeedSettings resize_seed_settings(const Config &from, const Config &to, SeedSettings);
struct State {
    Config config;
    std::vector<double> values;
    uint64_t seed = 1, steps = 0;
    double time = 0;
    std::string backend = "CPU double / FFTW";
    // Unknown for older/imported or externally constructed fields.
    std::optional<SeedSettings> initialization;
    int initialization_radius = 0;
    // Bounded construction history for manually edited fields, retained through evolution.
    std::string edits;
};
struct Timings {
    double forward_ms = 0, multiply_ms = 0, inverse_ms = 0, update_ms = 0, total_ms = 0;
};
struct Measures {
    double mean = 0, minimum = 0, maximum = 0, rms_change = 0;
};
std::vector<double> make_kernel(const Config &);
State random_state(Config, uint64_t seed, SeedSettings = {});
double advance_value(double a, double neighborhood, const Config &);
Measures measure(const std::vector<double> &, const std::vector<double> *previous = nullptr);
// RMS mismatch after an integer translation on the periodic domain.
double recurrence_error(const State &, const State &, int dx, int dy, int dz);
struct Translation {
    int dx = 0, dy = 0, dz = 0;
    double rms = 0;
    bool unique = true; // False when correlation has tied peaks within numerical tolerance.
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
