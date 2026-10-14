#pragma once
#include "imgui.h"
#include "renderer.h"

namespace lenia {
struct ViewerOptions {
    bool components = false;
    uint32_t isolate = 0;
    // Gallery comparison uses the measured threshold; changing appearance must not change the mask.
    std::optional<float> measured_threshold;
};
// Shared image, gestures and compact View menu. State belongs to the caller, independently per viewer.
// Size includes the footer; exports remain the renderer's clean image. Returns render submission ms.
double draw_viewer(const char *id, Renderer &, const Config &, ViewSettings &, ImVec2 size, float dpi,
                   const ViewerOptions & = {});
} // namespace lenia
