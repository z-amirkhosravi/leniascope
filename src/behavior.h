#pragma once
#include "morphology.h"
#include "simulation.h"
#include <functional>

namespace lenia {
struct BehaviorOptions {
    int steps = 1000, sample_every = 20;
    double activity_threshold = .01;
    void validate() const;
};
struct BehaviorSample {
    int step = 0;
    double elapsed = 0, mean = 0, active_fraction = 0;
    double raw_rms = 0, aligned_rms = 0, relative_rms = 0;
    int dx = 0, dy = 0, dz = 0;
    bool shift_valid = false;
    std::optional<Morphology> morphology; // Missing in schema-1 reports; never treat missing as zero.
};
struct BehaviorReport {
    Config config;
    BehaviorOptions options;
    std::string source_id, source_backend, backend;
    uint64_t source_hash = 0, initial_step = 0, seed = 0;
    double initial_time = 0, reference_rms = 0, initial_rounding_rms = 0, wall_seconds = 0;
    int completed_steps = 0;
    bool stopped = false;
    std::vector<BehaviorSample> samples;
};
// The solver must be newly constructed from source. No input fields are modified.
// Return false from progress to stop; the partial report includes the final completed step.
BehaviorReport behavior_study(const State &source, Solver &, const BehaviorOptions &,
                              const std::function<bool(int)> &progress = {});
std::string encode_behavior(const BehaviorReport &);
BehaviorReport decode_behavior(const std::string &);
std::string behavior_csv(const BehaviorReport &);
} // namespace lenia
