#include "viewer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace lenia {
namespace {
void draw_box(const ViewSettings &v, const Config &c, ImVec2 origin, ImVec2 size) {
    float cp = std::cos(v.pitch), sp = std::sin(v.pitch), cy = std::cos(v.yaw), sy = std::sin(v.yaw),
          maximum = float(std::max({c.width, c.height, c.depth}));
    ImVec2 points[8];
    for (int i = 0; i < 8; ++i) {
        float x = ((i & 1) ? .5f : -.5f) * c.width / maximum - sy * cp * v.distance,
              y = ((i & 2) ? .5f : -.5f) * c.height / maximum - sp * v.distance,
              z = ((i & 4) ? .5f : -.5f) * c.depth / maximum - cy * cp * v.distance;
        float depth = -sy * cp * x - sp * y - cy * cp * z;
        if (depth <= 0)
            return;
        float px = cy * x - sy * z, py = -sy * sp * x + cp * y - cy * sp * z;
        points[i] = {origin.x + (.5f + px / depth / (size.x / size.y)) * size.x,
                     origin.y + (.5f - py / depth) * size.y};
    }
    auto *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    for (int i = 0; i < 8; ++i)
        for (int axis : {1, 2, 4})
            if (!(i & axis))
                draw->AddLine(points[i], points[i | axis], IM_COL32(100, 150, 164, 95));
    draw->PopClipRect();
}
void reset_camera(ViewSettings &v) {
    const ViewSettings defaults;
    v.yaw = defaults.yaw;
    v.pitch = defaults.pitch;
    v.distance = defaults.distance;
    v.zoom = defaults.zoom;
}
void view_controls(ViewSettings &v, bool volume, const ViewerOptions &options) {
    const bool spatial = volume && v.view != 0;
    if (spatial) {
        ImGui::SliderFloat("Cutaway Z", &v.clip, 0, 1, "%.3f");
    } else {
        if (volume) {
            ImGui::Combo("Slice axis", &v.axis, "X\0Y\0Z\0");
            ImGui::SliderFloat("Position", &v.slice, 0, .999999f, "%.3f");
        }
    }
    if (ImGui::Button("Reset camera"))
        reset_camera(v);
    ImGui::SeparatorText("Periodic origin");
    ImGui::SliderFloat("X", &v.offset[0], 0, 1, "%.3f");
    ImGui::SliderFloat("Y", &v.offset[1], 0, 1, "%.3f");
    if (volume)
        ImGui::SliderFloat("Z", &v.offset[2], 0, 1, "%.3f");
    if (ImGui::Button("Reset origin"))
        std::fill(std::begin(v.offset), std::end(v.offset), 0.f);
    if (!options.components) {
        ImGui::SeparatorText("Appearance");
        const auto &palettes = Renderer::palette_names();
        if (ImGui::BeginCombo("Palette", palettes.at(v.palette).c_str())) {
            for (size_t i = 0; i < palettes.size(); ++i)
                if (ImGui::Selectable(palettes[i].c_str(), v.palette == int(i)))
                    v.palette = int(i);
            ImGui::EndCombo();
        }
        if (spatial) {
            if (v.view == 1)
                ImGui::SliderFloat("Opacity", &v.opacity, .1f, 10, "%.2f");
            if (options.measured_threshold)
                ImGui::Text("Measured threshold: %.6g", *options.measured_threshold);
            else
                ImGui::SliderFloat(v.view == 2 ? "Surface value" : "Opacity floor", &v.threshold, 0, 1,
                                   "%.3f");
        }
    }
    if (!options.components && spatial) {
        ImGui::SeparatorText("Rendering quality");
        ImGui::SliderFloat("Sample spacing", &v.quality, .3f, 2, "%.2f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Smaller spacing gives finer rendering detail.");
    }
}
} // namespace
double draw_viewer(const char *id, Renderer &renderer, const Config &config, ViewSettings &v, ViewerUi &ui,
                   ImVec2 size, float dpi, const ViewerOptions &options) {
    ImGui::PushID(id);
    ImGui::BeginGroup();
    const bool volume = config.depth > 1, spatial = volume && v.view != 0;
    // Reserve a quiet, stable footer outside the image. It never covers data or moves on hover.
    const float footer = ImGui::GetFontSize() + 10 * dpi;
    size.x = std::max(1.f, size.x);
    size.y = std::max(1.f, size.y - footer);
    ImGui::InvisibleButton("Canvas", size);
    const auto origin = ImGui::GetItemRectMin();
    if (ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY)) {
        const auto &io = ImGui::GetIO();
        if (spatial) {
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                v.yaw += io.MouseDelta.x * .008f;
                v.pitch = std::clamp(v.pitch + io.MouseDelta.y * .008f, -1.5f, 1.5f);
            }
            v.distance = std::clamp(v.distance * std::exp(-io.MouseWheel * .12f), .7f, 5.f);
        } else
            v.zoom = std::clamp(v.zoom * std::exp(io.MouseWheel * .12f), .25f, 4.f);
    }
    // Rendering may need a 2D axis or a locked comparison threshold. Neither overrides saved settings.
    auto display = v;
    if (!volume) {
        display.view = 0;
        display.axis = 2;
    }
    if (options.measured_threshold)
        display.threshold = *options.measured_threshold;
    auto start = std::chrono::steady_clock::now();
    auto image = options.components
                     ? renderer.draw_components(display, int(size.x), int(size.y), options.isolate)
                     : renderer.draw(display, int(size.x), int(size.y));
    double submit =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddImage((ImTextureID)image, origin, {origin.x + size.x, origin.y + size.y});
    if (spatial)
        draw_box(display, config, origin, size);

    // Match both placements, with transparent buttons until hovered and a subtle dividing line.
    ImGui::SetCursorScreenPos({origin.x, origin.y + size.y + 2 * dpi});
    draw->AddLine({origin.x, origin.y + size.y}, {origin.x + size.x, origin.y + size.y},
                  ImGui::GetColorU32(ImGuiCol_Border, .4f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6 * dpi, 2 * dpi});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {6 * dpi, 0});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
    const auto normal_text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (volume) {
        const char *mode = v.view == 0          ? "Slice"
                           : options.components ? "Solid"
                           : v.view == 1        ? "Volume"
                                                : "Surface";
        ImGui::SetNextItemWidth(std::min(size.x * .55f, 110 * dpi));
        if (ImGui::BeginCombo("##Mode", mode, ImGuiComboFlags_HeightSmall)) {
            ImGui::PushStyleColor(ImGuiCol_Text, normal_text);
            for (int i = 0; i < (options.components ? 2 : 3); ++i) {
                const char *label = i == 0   ? "Slice"
                                    : i == 1 ? (options.components ? "Solid" : "Volume")
                                             : "Surface";
                const bool selected = v.view == i || (options.components && i == 1 && v.view == 2);
                if (ImGui::Selectable(label, selected))
                    v.view = i;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::PopStyleColor();
            ImGui::EndCombo();
        }
    } else
        ImGui::TextDisabled("2D");
    ImGui::SameLine();
    const float button_width = ImGui::CalcTextSize("View").x + 12 * dpi;
    ImGui::SetCursorScreenPos({origin.x + std::max(0.f, size.x - button_width), origin.y + size.y + 2 * dpi});
    if (ui.controls_open)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    const bool toggle = ImGui::Button("View", {button_width, 0});
    if (ui.controls_open)
        ImGui::PopStyleColor();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Show or hide view controls");
    if (toggle)
        ui.controls_open = !ui.controls_open;
    if (ui.controls_open) {
        const auto *viewport = ImGui::GetMainViewport();
        const float margin = 8 * dpi;
        const ImVec2 extent{std::max(1.f, viewport->WorkSize.x - 2 * margin),
                            std::max(1.f, viewport->WorkSize.y - 2 * margin)};
        const float width = std::min(340 * dpi, extent.x);
        ImGui::SetNextWindowPos({origin.x + size.x - width - margin, origin.y + margin},
                                ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints({width, 0}, {width, extent.y});
        if (toggle)
            ImGui::SetNextWindowFocus();
        // A normal window keeps the image interactive and stays tied to this viewer's lifetime.
        // The parent scene must not raise over tool windows when its canvas receives focus.
        const auto name =
            std::string(id) + " view###view-controls-" + std::to_string(ImGui::GetID("Controls"));
        const bool visible =
            ImGui::Begin(name.c_str(), &ui.controls_open,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
        const auto position = ImGui::GetWindowPos(), window_size = ImGui::GetWindowSize();
        const ImVec2 lower{viewport->WorkPos.x + margin, viewport->WorkPos.y + margin};
        const ImVec2 bounded{
            std::clamp(position.x, lower.x, lower.x + std::max(0.f, extent.x - window_size.x)),
            std::clamp(position.y, lower.y, lower.y + std::max(0.f, extent.y - window_size.y))};
        if (position.x != bounded.x || position.y != bounded.y)
            ImGui::SetWindowPos(bounded); // Keep controls reachable after a resize or DPI change.
        if (visible) {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .57f);
            view_controls(v, volume, options);
            ImGui::PopItemWidth();
        }
        ImGui::End();
    }
    ImGui::SetCursorScreenPos({origin.x, origin.y + size.y + footer});
    ImGui::Dummy({size.x, 0});
    ImGui::EndGroup();
    ImGui::PopID();
    return submit;
}
} // namespace lenia
