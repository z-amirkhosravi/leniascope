#include "gallery_panel.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace lenia {
void GalleryPanel::begin_shape() {
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
void GalleryPanel::draw_shape_summary(const std::optional<Morphology> &shape, const Config &config,
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
    ImGui::Text("Loop directions: %s", winding_axes_text(m.winding_axes).c_str());
    ImGui::TextWrapped("Connectivity: %d face neighbors per cell. A periodic loop returns to the same "
                       "cell after going around the domain. An isolated blob crossing an edge does not "
                       "count as a periodic loop.",
                       config.depth > 1 ? 6 : 4);
}
void GalleryPanel::draw_components(float dpi) {
    auto &m = *shape;
    const auto &c = selected->state.config;
    auto &v = shape_view;
    ImGui::Text("Components: %llu   Measured threshold: %.6g",
                static_cast<unsigned long long>(m.shape.components), m.threshold);
    if (shape_threshold != m.threshold)
        ImGui::TextWrapped("Threshold changed. Measure again to update the colors and counts.");
    if (ImGui::Button("Show all components")) {
        shape_component = 0;
        shape_scalar = false;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Scalar field", &shape_scalar);
    if (shape_scalar) {
        ImGui::TextWrapped("All saved values with interpolated color; no component isolation.");
        if (c.depth > 1 && v.view != 0)
            ImGui::Text("Volume opacity threshold: %.6g", m.threshold);
    } else if (shape_component) {
        const auto &part = m.components[shape_component - 1];
        ImGui::Text("Only ID %u: %u cells (%.4g%%), loops %s", shape_component, part.cells,
                    100. * part.cells / m.shape.active_cells, winding_axes_text(part.winding_axes).c_str());
    } else
        ImGui::TextWrapped("Select a component row to show only that region.");

    v.threshold = float(m.threshold);
    const bool beside =
        ImGui::GetContentRegionAvail().x >= 500 * dpi && ImGui::BeginTable("Shape inspection", 2);
    if (beside) {
        ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch, .55f);
        ImGui::TableSetupColumn("Regions", ImGuiTableColumnFlags_WidthStretch, .45f);
        ImGui::TableNextColumn();
    }
    const float w = std::max(1.f, ImGui::GetContentRegionAvail().x);
    const float h = std::min(w, (beside ? 230 : 170) * dpi);
    auto image = shape_scalar ? m.renderer->draw(v, int(w), int(h))
                              : m.renderer->draw_components(v, int(w), int(h), shape_component);
    ImGui::InvisibleButton("##component-canvas", {w, h});
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)image, ImGui::GetItemRectMin(),
                                         ImGui::GetItemRectMax());
    if (ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY)) {
        const auto &io = ImGui::GetIO();
        if (c.depth > 1 && v.view != 0) {
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                v.yaw += io.MouseDelta.x * .008f;
                v.pitch = std::clamp(v.pitch + io.MouseDelta.y * .008f, -1.5f, 1.5f);
            }
            v.distance = std::clamp(v.distance * std::exp(-io.MouseWheel * .12f), .7f, 5.f);
        } else
            v.zoom = std::clamp(v.zoom * std::exp(io.MouseWheel * .12f), .25f, 16.f);
    }
    if (c.depth > 1) {
        ImGui::RadioButton("3D", &v.view, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Slice", &v.view, 0);
        if (!beside)
            ImGui::SameLine();
        ImGui::TextDisabled("Drag to rotate; wheel to zoom");
        if (v.view == 0) {
            ImGui::SetNextItemWidth(110 * dpi);
            ImGui::Combo("Axis", &v.axis, "X\0Y\0Z\0");
            ImGui::SliderFloat("Slice position", &v.slice, 0, .999999f, "%.3f");
        }
    } else
        ImGui::TextDisabled("Wheel to zoom; move the origin in View controls");
    if (beside)
        ImGui::TableNextColumn();
    if (ImGui::BeginTable("Components", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          {0, std::min((beside ? 270 : 120) * dpi,
                                       (m.order.size() + 1) * ImGui::GetFrameHeightWithSpacing())})) {
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
    if (beside)
        ImGui::EndTable();
    ImGui::TextWrapped("Largest first. IDs/colors belong to this measurement; they can change when you "
                       "remeasure. One color on opposite sides can be one region across the periodic seam.");
    if (ImGui::CollapsingHeader("View controls")) {
        if (c.depth > 1 && v.view != 0)
            ImGui::SliderFloat("Cutaway", &v.clip, 0, 1, "%.3f");
        ImGui::SliderFloat("Origin X", &v.offset[0], 0, 1, "%.3f");
        ImGui::SliderFloat("Origin Y", &v.offset[1], 0, 1, "%.3f");
        if (c.depth > 1)
            ImGui::SliderFloat("Origin Z", &v.offset[2], 0, 1, "%.3f");
        if (shape_scalar && c.depth > 1)
            ImGui::SliderFloat("Opacity", &v.opacity, .05f, 8.f, "%.2f");
        if (ImGui::Button("Reset inspection view")) {
            v = selected->view;
            v.view = c.depth > 1 ? 1 : 0;
            if (c.depth == 1)
                v.axis = 2;
            v.clip = 1;
        }
        ImGui::TextWrapped("Camera, cutaway and origin affect only this preview. Hidden cells still belong "
                           "to the measured components. A slice of one component may look disconnected.");
    }
}
void GalleryPanel::draw_shape(float dpi, bool closing) {
    ImGui::TextWrapped("Colors identify connected regions of the saved field, including periodic edges.");
    ImGui::BeginDisabled(busy() || closing || bool(pending_study));
    ImGui::SetNextItemWidth(110 * dpi);
    ImGui::InputDouble("Shape threshold", &shape_threshold, 0, 0, "%.6g");
    const bool valid = std::isfinite(shape_threshold) && shape_threshold >= 0 && shape_threshold <= 1;
    if (!valid)
        ImGui::TextWrapped("Use a threshold between 0 and 1.");
    ImGui::BeginDisabled(!valid);
    if (valid && ImGui::GetContentRegionAvail().x > 460 * dpi)
        ImGui::SameLine();
    if (ImGui::Button("Measure saved field")) {
        try {
            begin_shape();
        } catch (const std::exception &e) {
            message = e.what();
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (job == Job::Shape)
        ImGui::TextDisabled("Measuring saved field...");
    if (shape) {
        ImGui::Separator();
        draw_components(dpi);
        if (ImGui::CollapsingHeader("Measurement details"))
            draw_shape_summary(shape->shape, selected->state.config, shape->threshold);
    }
    ImGui::Separator();
    ImGui::TextWrapped("The threshold changes what is connected: a faint bridge may join two regions. "
                       "Try several thresholds when comparing shapes. Corner-only contacts are separate.");
    ImGui::TextWrapped("Components count regions of active material, not their boundary surfaces. A hollow "
                       "shell can be one region with inner and outer surfaces. Periodic loops describe "
                       "winding around the domain, not genus or knotting. Those require further analysis.");
    ImGui::TextWrapped("New behavior studies and batches record these measurements at every sampled time "
                       "using their activity threshold, and include them in CSV exports.");
}
} // namespace lenia
