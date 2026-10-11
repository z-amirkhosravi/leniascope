#pragma once
#include "simulation.h"
namespace lenia {
// Float32 DirectCompute backend. Its device/context is owned by the worker.
class GpuSolver final : public Solver {
  public:
    explicit GpuSolver(State);
    ~GpuSolver();
    void step() override;
    State snapshot() override;
    void parameters(const Config &) override;
    const Timings &timings() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace lenia
