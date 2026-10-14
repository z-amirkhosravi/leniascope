#include "study_panel.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace lenia {
namespace {
template <class T> bool ready(std::future<T> &f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
std::string signature(const State &s) {
    // Fast in-app check; the external smoke harness also compares full checkpoint bytes.
    uint64_t hash = 14695981039346656037ULL;
    auto p = reinterpret_cast<const unsigned char *>(s.values.data());
    for (size_t i = 0; i < s.values.size() * sizeof(double); ++i)
        hash = (hash ^ p[i]) * 1099511628211ULL;
    std::ostringstream out;
    out << hash << ' ' << s.steps << ' ' << std::setprecision(17) << s.time << ' ' << s.seed << ' '
        << s.config.width << ' ' << s.config.depth;
    return out.str();
}
} // namespace
const char *StudyPanel::smoke_capture_name() const {
    if (smoke_wait != 4)
        return nullptr;
    return smoke_phase == 6    ? "study-behavior-ui.bmp"
           : smoke_phase == 9  ? "study-batch-ui.bmp"
           : smoke_phase == 19 ? "study-current-ui.bmp"
                               : nullptr;
}
bool StudyPanel::smoke(Runner &runner, const std::array<ViewSettings, 2> &views) {
    if (busy())
        return false;
    if (!selected)
        throw std::runtime_error("Study smoke has no source: " + message);
    if (!smoke_phase) {
        smoke_signature = signature(load_gallery_state(root, selected->id));
        smoke_phase = 5;
    }
    if ((smoke_phase == 8 || smoke_phase == 12 || smoke_phase == 14 || smoke_phase == 16) &&
        !ready(smoke_check))
        return false;
    std::ofstream("gallery-smoke.log", std::ios::app)
        << GetTickCount64() << " study phase " << smoke_phase << " direct=" << smoke_current << '\n';
    // Let the actual Results tab settle before capturing its layout, for both source types.
    if (smoke_phase == 6 || smoke_phase == 9 || smoke_phase == 19) {
        if (smoke_wait++ < 5)
            return false;
        smoke_wait = 0;
    }
    switch (smoke_phase) {
    case 5:
        study_options = {7, 3, .01};
        tool = 1;
        begin_study();
        ++smoke_phase;
        break;
    case 6:
        if (studies.entries.size() != 1 || studies.entries[0].report.completed_steps != 7 ||
            studies.entries[0].report.samples.size() != 4)
            throw std::runtime_error("Gallery smoke study failed: " + message);
        open_source(root, selected->id); // Re-read both the entry and its persisted report.
        ++smoke_phase;
        break;
    case 7:
        if (studies.entries.size() != 1)
            throw std::runtime_error("Saved study did not reload");
        smoke_check = runner.capture_current();
        ++smoke_phase;
        break;
    case 8:
        if (signature(smoke_check.get()) != smoke_signature)
            throw std::runtime_error("Study changed the live field");
        // Edited fields have no recipe; exercise an exact-field batch for those sources.
        batch_plan.reseed =
            selected->state.initialization && selected->state.initialization->layout != SeedLayout::Uniform;
        batch_plan.axis = batch_plan.reseed ? BatchAxis::Spread : BatchAxis::Mu;
        batch_plan.repetitions = 1;
        batch_plan.first_seed = 216;
        batch_plan.observation = {5, 2, .01};
        std::snprintf(batch_value_text, sizeof batch_value_text, "%s",
                      batch_plan.reseed ? "0.8, 1.5" : "0.14, 0.16");
        tool = 2;
        begin_batch();
        ++smoke_phase;
        break;
    case 9:
        if (batches.size() != 1 || batches[0].result.status != "complete" ||
            batches[0].result.runs.size() != 2)
            throw std::runtime_error("Gallery batch failed: " + message);
        std::snprintf(batch_label, sizeof batch_label, "smoke assessment");
        batch_annotation();
        ++smoke_phase;
        break;
    case 10:
        open_source(root, selected->id);
        ++smoke_phase;
        break;
    case 11:
        if (batches.size() != 1 || batches[0].result.runs[0].label != "smoke assessment")
            throw std::runtime_error("Gallery batch did not reload");
        smoke_check = runner.capture_current();
        ++smoke_phase;
        break;
    case 12:
        if (signature(smoke_check.get()) != smoke_signature)
            throw std::runtime_error("Batch changed the live field");
        batch_field(true);
        ++smoke_phase;
        break;
    case 13:
        smoke_check = runner.capture_current();
        ++smoke_phase;
        break;
    case 14:
        if (signature(smoke_check.get()) !=
            signature(load_batch_field(root / selected->id / "batches" / batches[0].id, 0, true)))
            throw std::runtime_error("Gallery batch endpoint did not load exactly");
        restore_source();
        ++smoke_phase;
        break;
    case 15:
        smoke_check = runner.capture_current();
        ++smoke_phase;
        break;
    case 16:
        if (signature(smoke_check.get()) != smoke_signature)
            throw std::runtime_error("Could not return to gallery source after batch inspection");
        tool = 0;
        viewing_source = true;
        activate = true;
        begin_shape();
        ++smoke_phase;
        break;
    case 17:
        if (!shape || !studies.entries[0].report.samples.front().morphology ||
            morphology_csv(shape->shape) !=
                morphology_csv(studies.entries[0].report.samples.front().morphology))
            throw std::runtime_error("Saved shape measurements disagree with the initial study sample");
        if (shape->components.size() != shape->shape.components || !shape->renderer)
            throw std::runtime_error("Shape component preview is incomplete");
        shape->renderer->draw_components(shape_view, 320, 240);
        shape->renderer->export_bmp((root.parent_path() / "shape-components.bmp").u8string());
        if (!shape->order.empty()) {
            shape->renderer->draw_components(shape_view, 320, 240, shape->order.front());
            shape->renderer->export_bmp((root.parent_path() / "shape-largest.bmp").u8string());
        }
        if (!smoke_current) {
            smoke_current = true;
            use_current(runner, views);
            smoke_phase = 18;
            break;
        }
        viewer_ui.controls_open = true; // Include the floating inspector in the final UI capture.
        show_current();
        smoke_phase = 19;
        break;
    case 19:
        viewing_source = true;
        ++smoke_phase;
        return true;
    case 18:
        if (root != archive_root || !selected ||
            signature(load_gallery_state(root, selected->id)) != smoke_signature)
            throw std::runtime_error("Direct study capture changed the source");
        smoke_phase = 5;
        break;
    default:
        return true;
    }
    return false;
}
} // namespace lenia
