#include "gallery_panel.h"
#include "imgui.h"
#include <cmath>

namespace lenia {
void GalleryPanel::begin_shape() {
    if (!selected || busy() || pending_study)
        return;
    if (!std::isfinite(shape_threshold) || shape_threshold < 0 || shape_threshold > 1)
        throw std::invalid_argument("Shape threshold must be in [0,1]");
    message.clear();
    job = Job::Shape;
    work = std::async(std::launch::async, [path = root, id = selected->id, threshold = shape_threshold] {
        auto source = load_gallery_state(path, id);
        Result r;
        r.shape = ShapeMeasurement{measure_morphology(source, threshold), threshold};
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
void GalleryPanel::draw_shape(float dpi, bool closing) {
    ImGui::TextWrapped("Measure the saved field without evolving it. This joins cells across the periodic "
                       "edges and counts connected regions above your chosen threshold.");
    ImGui::BeginDisabled(busy() || closing || bool(pending_study));
    ImGui::SetNextItemWidth(180 * dpi);
    ImGui::InputDouble("Shape threshold", &shape_threshold, 0, 0, "%.6g");
    const bool valid = std::isfinite(shape_threshold) && shape_threshold >= 0 && shape_threshold <= 1;
    if (!valid)
        ImGui::TextWrapped("Use a threshold between 0 and 1.");
    ImGui::BeginDisabled(!valid);
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
        draw_shape_summary(shape->shape, selected->state.config, shape->threshold);
    }
    ImGui::Separator();
    ImGui::TextWrapped("The threshold changes what is connected: a faint bridge may join two regions. "
                       "Try several thresholds when comparing shapes. Corner-only contacts are separate.");
    ImGui::TextWrapped("These are measurements of a thresholded field, not an organism count or a "
                       "sphere/tube classifier. A hollow sphere or a local ring can have no periodic loops.");
    ImGui::TextWrapped("New behavior studies and batches record these measurements at every sampled time "
                       "using their activity threshold, and include them in CSV exports.");
}
} // namespace lenia
