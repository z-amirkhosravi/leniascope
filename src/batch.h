#pragma once
#include "behavior.h"
#include <filesystem>

namespace lenia {
enum class BatchAxis {
    Mu,
    Sigma,
    Radius,
    Spread,
    Angle
};
const char *batch_axis_name(BatchAxis);
BatchAxis batch_axis(const std::string &);
std::vector<double> batch_values(const std::string &);
struct BatchPlan {
    BatchAxis axis = BatchAxis::Mu;
    std::vector<double> values{.14, .15, .16};
    bool reseed = false;
    int repetitions = 1;
    uint64_t first_seed = 1;
    std::string backend = "cpu";
    BehaviorOptions observation{1000, 100, .01};
    int count() const;
    void validate() const;
    void validate(const State &) const;
};
struct BatchRun {
    int index = 0;
    uint64_t initial_hash = 0, final_hash = 0;
    BehaviorReport report;
    std::string label; // User assessment at this observation horizon; empty means unclassified.
};
struct BatchResult {
    BatchPlan plan;
    std::string source_id, status = "incomplete", error;
    uint64_t source_hash = 0;
    std::vector<BatchRun> runs;
};
using BatchSolver = std::function<std::unique_ptr<Solver>(const State &, const std::string &)>;
// Each run commits independently. Stop preserves completed runs and the current partial endpoint.
// Destination must be new. Source and live simulation are never modified. No backend fallback.
BatchResult run_batch(const std::filesystem::path &, const State &, const BatchPlan &,
                      const std::string &source_id, const BatchSolver &,
                      const std::function<bool(int run, int step)> &progress = {});
BatchResult read_batch(const std::filesystem::path &);
std::string batch_csv(const BatchResult &);
State load_batch_field(const std::filesystem::path &, int run, bool final);
void label_batch_run(const std::filesystem::path &, int run, const std::string &label);
} // namespace lenia
