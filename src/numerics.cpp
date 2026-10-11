#include "numerics.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace lenia {
std::vector<TimestepSample> timestep_study(const State &initial, int base_steps, int levels) {
    if (base_steps < 1 || base_steps > 1000000 || levels < 2 || levels > 6)
        throw std::invalid_argument("Use 1..1000000 base steps and 2..6 refinement levels");
    auto evolve = [&](int level) {
        State state = initial;
        state.config.dt /= (1 << level);
        CpuSolver solver(std::move(state));
        for (int i = 0; i < base_steps * (1 << level); ++i)
            solver.step();
        return solver.snapshot();
    };
    auto finest = evolve(levels - 1);
    std::vector<TimestepSample> result;
    for (int level = 0; level < levels; ++level) {
        State candidate;
        const State *state = &finest;
        if (level != levels - 1) {
            candidate = evolve(level);
            state = &candidate;
        }
        double square = 0, maximum = 0;
        for (size_t i = 0; i < state->values.size(); ++i) {
            double error = state->values[i] - finest.values[i];
            square += error * error;
            maximum = std::max(maximum, std::abs(error));
        }
        result.push_back({state->config.dt, initial.config.dt * base_steps,
                          std::sqrt(square / state->values.size()), maximum, measure(state->values).mean,
                          uint64_t(base_steps) * (uint64_t(1) << level)});
    }
    return result;
}
} // namespace lenia
