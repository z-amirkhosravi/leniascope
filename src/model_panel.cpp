#include "model_panel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lenia {
bool same_model_parameters(const Config &a, const Config &b) {
    return a.width == b.width && a.height == b.height && a.depth == b.depth && a.radius == b.radius &&
           a.rule == b.rule && float(a.mu) == float(b.mu) && float(a.sigma) == float(b.sigma) &&
           float(a.dt) == float(b.dt) && float(a.smoothness) == float(b.smoothness);
}
State kernel_preview(const Config &c, double half_extent) {
    const bool volume = c.depth > 1;
    const int n = volume ? 64 : 128;
    State result;
    result.config = {n, n, volume ? n : 1, n / 4};
    result.values.resize(result.config.count());
    double peak = 0;
    for (int z = 0; z < result.config.depth; ++z)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                // Sample the same lattice weights used by make_kernel, centered for display.
                double dx = std::round((x - n / 2) * (2 * half_extent / n));
                double dy = std::round((y - n / 2) * (2 * half_extent / n));
                double dz = volume ? std::round((z - n / 2) * (2 * half_extent / n)) : 0;
                double weight = kernel_profile(std::sqrt(dx * dx + dy * dy + dz * dz) / c.radius);
                result.values[(size_t(z) * n + y) * n + x] = weight;
                peak = std::max(peak, weight);
            }
    // Show shape without brightness depending on the number of grid cells. The
    // simulation's actual convolution kernel is normalized by its sum, not its peak.
    if (peak > 0)
        for (double &value : result.values)
            value /= peak;
    return result;
}
namespace {
void growth_plot(const Config &draft, const Config &applied, float &cell, float dpi) {
    ImGui::TextUnformatted("Change per step");
    ImGui::SetNextItemWidth(145 * dpi);
    ImGui::SliderFloat("Preview cell value", &cell, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    auto response = [cell](const Config &c) {
        std::vector<double> samples;
        for (int i = 0; i <= 256; ++i)
            samples.push_back(i / 256.);
        // Retain the peak and narrow growth windows, even at the minimum Width.
        for (int i = -40; i <= 40; ++i)
            samples.push_back(std::clamp(c.mu + c.sigma * i / 8., 0., 1.));
        std::sort(samples.begin(), samples.end());
        std::vector<ImVec2> values;
        for (double u : samples)
            values.push_back({float(u), float(advance_value(cell, u, c) - cell)});
        return values;
    };
    const auto current = response(draft), previous = response(applied);
    const float base = float(std::max(draft.dt, applied.dt));
    float limit = base;
    // Smooth birth/death rates near empty/full cells can exceed one unit per time.
    // Include both curves in the scale instead of drawing them outside the graph.
    for (const auto *values : {&current, &previous})
        for (const auto &value : *values)
            limit = std::max(limit, std::ceil(std::abs(value.y) / base * 4) * base / 4);
    const auto start = ImGui::GetCursorScreenPos();
    const ImVec2 size{ImGui::GetContentRegionAvail().x, 112 * dpi};
    ImGui::InvisibleButton("Growth response", size);
    char positive[32], negative[32];
    std::snprintf(positive, sizeof positive, "+%.3g", limit);
    std::snprintf(negative, sizeof negative, "-%.3g", limit);
    const float label_width = std::max(ImGui::CalcTextSize(positive).x, ImGui::CalcTextSize(negative).x);
    const ImVec2 lo{start.x + label_width + 6 * dpi, start.y + 8 * dpi};
    const ImVec2 hi{start.x + size.x - 8 * dpi, start.y + size.y - 22 * dpi};
    const float middle = (lo.y + hi.y) * .5f;
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, {start.x + size.x, start.y + size.y}, IM_COL32(9, 12, 16, 255), 4 * dpi);
    draw->AddLine({lo.x, middle}, {hi.x, middle}, IM_COL32(75, 90, 100, 255));
    draw->AddLine(lo, {lo.x, hi.y}, IM_COL32(75, 90, 100, 255));
    const auto text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    draw->AddText({start.x + 2 * dpi, lo.y}, text, positive);
    draw->AddText({start.x + 2 * dpi, hi.y - ImGui::GetFontSize()}, text, negative);
    draw->AddText({lo.x, hi.y + 3 * dpi}, text, "0");
    draw->AddText({hi.x - ImGui::CalcTextSize("1").x, hi.y + 3 * dpi}, text, "1");
    const char *axis = "Neighborhood value";
    draw->AddText({(lo.x + hi.x - ImGui::CalcTextSize(axis).x) * .5f, hi.y + 3 * dpi}, text, axis);
    auto curve = [&](const std::vector<ImVec2> &values, ImU32 color, float thickness) {
        std::vector<ImVec2> points;
        for (const auto &value : values)
            points.push_back(
                {lo.x + value.x * (hi.x - lo.x), middle - value.y / limit * (hi.y - lo.y) * .5f});
        draw->AddPolyline(points.data(), int(points.size()), color, 0, thickness * dpi);
    };
    if (!same_model_parameters(draft, applied))
        curve(previous, IM_COL32(145, 155, 165, 255), 1);
    curve(current, IM_COL32(97, 217, 196, 255), 2);
    if (ImGui::IsItemHovered()) {
        const double u = std::clamp(double((ImGui::GetIO().MousePos.x - lo.x) / (hi.x - lo.x)), 0., 1.);
        ImGui::SetTooltip("Neighborhood %.4f\nPreview: %+.6f\nApplied: %+.6f", u,
                          advance_value(cell, u, draft) - cell, advance_value(cell, u, applied) - cell);
    }
}
} // namespace
void ModelPanel::toggle() {
    visible = !visible;
    focus = visible;
}
void ModelPanel::draw_kernel(const Config &draft, const Config &applied, float dpi) {
    const bool volume = draft.depth > 1;
    const double extent = std::ceil(std::max(draft.radius, applied.radius) * 1.15);
    if (!renderer) {
        renderer = std::make_unique<Renderer>(gpu);
        camera.distance = .85f;
        camera.yaw = 1.05f;
        camera.pitch = .35f;
        camera.opacity = .6f;
        camera.threshold = 0;
        camera.clip = .5f;
        camera.quality = .5f;
    }
    if (preview_radius != draft.radius || preview_dimension != draft.depth || preview_extent != extent) {
        renderer->upload(kernel_preview(draft, extent));
        preview_radius = draft.radius;
        preview_dimension = draft.depth;
        preview_extent = extent;
    }
    ImGui::TextUnformatted(volume ? "Kernel (3D cutaway)" : "Kernel (2D)");
    ImGui::SameLine();
    ImGui::TextDisabled("  R = %d cells", draft.radius);
    const ImVec2 size{ImGui::GetContentRegionAvail().x, 190 * dpi};
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("Kernel preview", size);
    if (volume && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        camera.yaw += ImGui::GetIO().MouseDelta.x * .008f;
        camera.pitch = std::clamp(camera.pitch + ImGui::GetIO().MouseDelta.y * .008f, -1.5f, 1.5f);
    }
    if (volume && ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY))
        camera.distance = std::clamp(camera.distance * std::exp(-ImGui::GetIO().MouseWheel * .12f), .7f, 5.f);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip(volume ? "Relative kernel weights. Drag to rotate; scroll to zoom."
                                 : "Relative kernel weights. The simulation normalizes their sum to 1.");
    auto view = camera;
    view.view = volume ? 1 : 0;
    view.axis = 2;
    auto image = renderer->draw(view, int(size.x), int(size.y));
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)image, origin, {origin.x + size.x, origin.y + size.y});
    ImGui::TextDisabled("+/-%.0f cells    Relative weight: 0-1", extent);
}
bool ModelPanel::draw(Config &draft, const Config &applied, float dpi, bool disabled) {
    if (!visible)
        return false;
    const auto *viewport = ImGui::GetMainViewport();
    const float margin = 8 * dpi;
    const ImVec2 extent{std::max(1.f, viewport->WorkSize.x - 2 * margin),
                        std::max(1.f, viewport->WorkSize.y - 2 * margin)};
    const float width = std::min(430 * dpi, extent.x);
    ImGui::SetNextWindowPos({viewport->WorkPos.x + 350 * dpi, viewport->WorkPos.y + 65 * dpi},
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({width, 0}, {width, extent.y});
    if (focus) {
        ImGui::SetNextWindowFocus();
        focus = false;
    }
    bool apply = false;
    if (ImGui::Begin("Model parameters", &visible,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::BeginDisabled(disabled);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .65f);
        float mu = float(draft.mu), sigma = float(draft.sigma), dt = float(draft.dt);
        if (ImGui::SliderFloat("Mean", &mu, .01f, .5f, "%.4f", ImGuiSliderFlags_AlwaysClamp))
            draft.mu = mu;
        if (ImGui::SliderFloat("Width", &sigma, .001f, .1f, "%.4f", ImGuiSliderFlags_AlwaysClamp))
            draft.sigma = sigma;
        if (ImGui::SliderFloat("Time step", &dt, .001f, .5f, "%.4f", ImGuiSliderFlags_AlwaysClamp))
            draft.dt = dt;
        int max_radius = std::min(draft.width, draft.height) / 2;
        if (draft.depth > 1)
            max_radius = std::min(max_radius, draft.depth / 2);
        ImGui::SliderInt("Radius", &draft.radius, 1, max_radius, "%d", ImGuiSliderFlags_AlwaysClamp);
        int rule = int(draft.rule);
        if (ImGui::Combo("Rule", &rule, "Classic Lenia\0Smooth reaction (experiment)\0"))
            draft.rule = Rule(rule);
        if (draft.rule == Rule::SmoothReaction) {
            float sharpness = float(draft.smoothness);
            if (ImGui::SliderFloat("Sharpness", &sharpness, 1, 40, "%.1f", ImGuiSliderFlags_AlwaysClamp))
                draft.smoothness = sharpness;
        }
        ImGui::BeginDisabled(same_model_parameters(draft, applied));
        apply = ImGui::Button("Apply", {-1, 0});
        ImGui::EndDisabled();
        draw_kernel(draft, applied, dpi);
        growth_plot(draft, applied, preview_cell, dpi);
        ImGui::PopItemWidth();
        ImGui::EndDisabled();
    }
    const auto position = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    const ImVec2 lower{viewport->WorkPos.x + margin, viewport->WorkPos.y + margin};
    const ImVec2 bounded{std::clamp(position.x, lower.x, lower.x + std::max(0.f, extent.x - size.x)),
                         std::clamp(position.y, lower.y, lower.y + std::max(0.f, extent.y - size.y))};
    if (position.x != bounded.x || position.y != bounded.y)
        ImGui::SetWindowPos(bounded);
    ImGui::End();
    return apply;
}
} // namespace lenia
