#pragma once
#include "gpu_solver.h"

namespace lenia {
// Optional float32 cuFFT real-transform backend. No CUDA types escape this interface.
class CudaSolver final : public Solver {
  public:
    explicit CudaSolver(State, std::shared_ptr<GpuContext> = {});
    ~CudaSolver();
    void step() override;
    State snapshot() override;
    void parameters(const Config &) override;
    const Timings &timings() const override;
    std::shared_ptr<const GpuImage> display_snapshot();
    size_t device_bytes() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace lenia
