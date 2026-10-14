#include "imgui.h"
#include "study_panel.h"
#include "viewer.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace lenia {
void StudyPanel::begin_shape() {
    if (!selected || busy() || pending_study)
        return;
    if (!std::isfinite(shape_threshold) || shape_threshold < 0 || shape_threshold > 1)
        throw std::invalid_argument("Shape threshold must be in [0,1]");
    message.clear();
    job = Job::Shape;
    work = std::async(std::launch::async,
                      [path = root, id = selected->id, threshold = shape_threshold, context = gpu] {
                          auto source = load_gallery_state(path, id);
                          auto map = label_components(source, threshold);
                          Result r;
                          auto &m = r.shape.emplace();
                          m.shape = map.summary;
                          m.threshold = threshold;
                          m.components = std::move(map.components);
                          m.order.resize(m.components.size());
                          std::iota(m.order.begin(), m.order.end(), 1u);
                          std::sort(m.order.begin(), m.order.end(), [&](auto a, auto b) {
                              auto ca = m.components[a - 1].cells, cb = m.components[b - 1].cells;
                              return ca != cb ? ca > cb : a < b;
                          });
                          m.renderer = std::make_unique<Renderer>(context);
                          m.renderer->upload_components(source.config, map.labels);
                          m.renderer->upload(source);
                          return r;
                      });
}
void StudyPanel::draw_shape_summary(const std::optional<Morphology> &shape, const Config &config,
                                    double threshold) {
    if (!shape) {
        ImGui::TextWrapped("Shape measurements are unavailable in this older report. Run a new study "
                           "or inspect the saved pattern in the Shape tab.");
        return;
    }
    const auto &m = *shape;
    ImGui::Text("Cells > %.6g: %llu (%.4g%% of grid)", threshold,
                static_cast<unsigned long long>(m.active_cells), 100. * m.active_cells / config.count());
    ImGui::Text("Connected components: %llu", static_cast<unsigned long long>(m.components));
    if (m.active_cells)
        ImGui::Text("Largest: %llu cells (%.4g%% of active cells)",
                    static_cast<unsigned long long>(m.largest_cells),
                    100. * m.largest_cells / m.active_cells);
    ImGui::Text("Components with periodic loops: %llu",
                static_cast<unsigned long long>(m.winding_components));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip(
            "Closed paths winding around the periodic domain, not a count of handles or genus.");
    ImGui::Text("Loop directions: %s", winding_axes_text(m.winding_axes).c_str());
    ImGui::Text("Connectivity: %d face neighbors per cell", config.depth > 1 ? 6 : 4);
}
void StudyPanel::draw_components(float dpi) {
    auto &m = *shape;
    const auto &c = selected->state.config;
    auto &v = shape_view;
    ImGui::TextWrapped("Components: %llu   Measured threshold: %.6g",
                       static_cast<unsigned long long>(m.shape.components), m.threshold);
    if (shape_threshold != m.threshold)
        ImGui::TextWrapped("Threshold changed. Measure again to update the colors and counts.");
    if (ImGui::Button("Show all components")) {
        shape_component = 0;
        shape_scalar = false;
    }
    ImGui::Checkbox("Scalar field", &shape_scalar);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Show all saved values with interpolated color, without component isolation.");
    if (shape_scalar) {
        if (c.depth > 1 && v.view != 0)
            ImGui::Text("Display threshold: %.6g", m.threshold);
    } else if (shape_component) {
        const auto &part = m.components[shape_component - 1];
        ImGui::TextWrapped("Only ID %u: %u cells (%.4g%%), loops %s", shape_component, part.cells,
                           100. * part.cells / m.shape.active_cells,
                           winding_axes_text(part.winding_axes).c_str());
    }

    if (ImGui::BeginTable(
            "Components", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            {0, std::min(230 * dpi, (m.order.size() + 1) * ImGui::GetFrameHeightWithSpacing())})) {
        ImGui::TableSetupColumn("ID / color");
        ImGui::TableSetupColumn("Cells");
        ImGui::TableSetupColumn("% active");
        ImGui::TableSetupColumn("Loops");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(int(m.order.size()));
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                uint32_t id = m.order[row];
                const auto &part = m.components[id - 1];
                auto color = Renderer::component_color(id);
                ImGui::PushID(int(id));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::ColorButton("Color", {color[0], color[1], color[2], 1},
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                   {ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()});
                ImGui::SameLine();
                auto label = std::to_string(id);
                if (ImGui::Selectable(label.c_str(), shape_component == id,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    shape_component = id;
                    shape_scalar = false;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("Isolate component %u. IDs and colors apply to this measurement only.",
                                      id);
                ImGui::TableNextColumn();
                ImGui::Text("%u", part.cells);
                ImGui::TableNextColumn();
                ImGui::Text("%.4g", 100. * part.cells / m.shape.active_cells);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(winding_axes_text(part.winding_axes).c_str());
                ImGui::PopID();
            }
        ImGui::EndTable();
    }
}
void StudyPanel::draw_shape(float dpi, bool closing) {
    ImGui::BeginDisabled(busy() || closing || bool(pending_study));
    ImGui::SetNextItemWidth(110 * dpi);
    ImGui::InputDouble("Shape threshold", &shape_threshold, 0, 0, "%.6g");
    const bool valid = std::isfinite(shape_threshold) && shape_threshold >= 0 && shape_threshold <= 1;
    if (!valid)
        ImGui::TextWrapped("Use a threshold between 0 and 1.");
    ImGui::BeginDisabled(!valid);
    if (valid && ImGui::GetContentRegionAvail().x > 460 * dpi)
        ImGui::SameLine();
    if (ImGui::Button("Measure shape")) {
        try {
            begin_shape();
        } catch (const std::exception &e) {
            message = e.what();
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (job == Job::Shape)
        ImGui::TextDisabled("Measuring source...");
    if (shape) {
        ImGui::Separator();
        draw_components(dpi);
        if (ImGui::CollapsingHeader("Measurement details"))
            draw_shape_summary(shape->shape, selected->state.config, shape->threshold);
    }
}
} // namespace lenia
