#pragma once
#include "imgui.h"

namespace lenia {
// Panel navigation has no side effects on the simulation, selected field, or camera.
inline void draw_inspector_tabs(int &active, int requested = -1) {
    if (ImGui::BeginTabBar("Inspector tabs")) {
        const char *labels[] = {"Simulation", "Study"};
        for (int i = 0; i < 2; ++i)
            if (ImGui::BeginTabItem(labels[i], nullptr, requested == i ? ImGuiTabItemFlags_SetSelected : 0)) {
                active = i;
                ImGui::EndTabItem();
            }
        ImGui::EndTabBar();
    }
}
} // namespace lenia
