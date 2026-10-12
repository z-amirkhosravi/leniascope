#pragma once
#include "simulation.h"
#ifdef _WIN32
#include "d3d_support.h"
#endif
namespace lenia {
struct GpuContext;
struct GpuImage;
#ifdef _WIN32
// A leased, immutable display texture. Metadata has no CPU field values; use snapshot()
// for a complete, exact checkpoint. A bounded pool prevents a slow viewer growing VRAM use.
struct GpuImage {
    State metadata;
    Measures measures;
    ComPtr<ID3D11Texture3D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
};
#endif
// Float32 DirectCompute backend; optionally shares the renderer's device/context.
class GpuSolver final : public Solver {
  public:
    explicit GpuSolver(State, std::shared_ptr<GpuContext> = {});
    ~GpuSolver();
    void step() override;
    State snapshot() override;
    void parameters(const Config &) override;
    const Timings &timings() const override;
    // Returns null if all three display leases are still held. Simulation can continue.
    std::shared_ptr<const GpuImage> display_snapshot();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace lenia
