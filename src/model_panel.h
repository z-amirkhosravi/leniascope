#pragma once
#include "imgui.h"
#include "renderer.h"

namespace lenia {
// Compare at the precision of the editable controls, so returning a slider to its
// original value also returns Apply to its disabled state.
bool same_model_parameters(const Config &, const Config &);
State kernel_preview(const Config &, double half_extent);

class ModelPanel {
  public:
    explicit ModelPanel(std::shared_ptr<GpuContext> context) : gpu(std::move(context)) {}
    void toggle();
    bool draw(Config &draft, const Config &applied, float dpi, bool disabled);

  private:
    void draw_kernel(const Config &, const Config &, float dpi);
    std::shared_ptr<GpuContext> gpu;
    std::unique_ptr<Renderer> renderer;
    ViewSettings camera;
    bool visible = false, focus = false;
    int preview_radius = 0, preview_dimension = 0;
    double preview_extent = 0;
    float preview_cell = .25f;
};
} // namespace lenia
