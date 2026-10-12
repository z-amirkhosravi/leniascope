#pragma once
#include "gpu_solver.h"
#include <stdexcept>

namespace lenia {
// Explicit choice: no fallback that could silently change a research run's arithmetic.
inline std::unique_ptr<Solver> study_solver(const State &source, const std::string &backend) {
    if (backend == "cpu")
        return std::make_unique<CpuSolver>(source);
#ifdef _WIN32
    if (backend == "gpu" || backend == "cuda")
        return std::make_unique<GpuSolver>(source, nullptr, backend == "cuda");
#endif
    throw std::invalid_argument("Study backend must be an available cpu, gpu, or cuda solver");
}
} // namespace lenia
