#pragma once
#include "imgui.h"
#include "renderer.h"

namespace lenia {
// Transient UI state stays separate from persisted camera settings and numerical state.
struct ViewerUi {
    bool controls_open = false;
};
struct ViewerOptions {
    bool components = false;
    uint32_t isolate = 0;
    // Study comparison uses the measured threshold; changing appearance must not change the mask.
    std::optional<float> measured_threshold;
};
// Shared image, gestures and non-modal View inspector. State belongs to the caller, independently per viewer.
// Size includes the footer; exports remain the renderer's clean image. Returns render submission ms.
double draw_viewer(const char *id, Renderer &, const Config &, ViewSettings &, ViewerUi &, ImVec2 size,
                   float dpi, const ViewerOptions & = {});
} // namespace lenia
