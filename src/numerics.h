#pragma once
#include "simulation.h"
namespace lenia {
struct TimestepSample {
    double dt = 0, duration = 0, rms_to_finest = 0, max_to_finest = 0, mean = 0;
    uint64_t steps = 0;
};
// Evolve the exact same saved field for the same physical duration at dt, dt/2, ... .
// All runs use CPU double. Errors are relative to the finest run, not an exact solution.
std::vector<TimestepSample> timestep_study(const State &initial, int base_steps, int levels);
} // namespace lenia
