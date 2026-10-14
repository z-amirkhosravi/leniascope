#include "study_panel.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace lenia {
namespace {
template <class T> bool ready(std::future<T> &f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
} // namespace
StudyPanel::StudyPanel(std::filesystem::path path, bool can_write, std::shared_ptr<GpuContext> context)
    : archive_root(std::move(path)), writable(can_write), gpu(std::move(context)) {}
StudyPanel::~StudyPanel() {
    study_stop = true;
    if (work.valid())
        work.wait();
}
bool StudyPanel::busy() const {
    return work.valid() || capture.valid() || restore.valid();
}
bool StudyPanel::can_change_source() const {
    return !busy() && !pending_study && !captured;
}
bool StudyPanel::showing_components(const Frame *frame) const {
    return shape && frame && !frame->running && shape->revision == frame->field_revision;
}
Renderer &StudyPanel::field_renderer(Renderer &live, const Frame *frame) const {
    return showing_components(frame) ? *shape->renderer : live;
}
bool StudyPanel::take_activation() {
    return std::exchange(activate, false);
}
void StudyPanel::request(Job operation, Runner &runner, const std::array<ViewSettings, 2> &views) {
    if (!can_change_source() || (operation != Job::Shape && (!writable || archive_root.empty())))
        return;
    if (operation == Job::Shape &&
        (!std::isfinite(shape_threshold) || shape_threshold < 0 || shape_threshold > 1))
        throw std::invalid_argument("Shape threshold must be in [0,1]");
    capture_views = views;
    capture = runner.capture_field();
    requested = operation;
    study_stop = false;
    message.clear();
}
void StudyPanel::start_requested() {
    auto operation = std::exchange(requested, Job::None);
    if (study_stop)
        return;
    if (operation == Job::Study)
        begin_study();
    else if (operation == Job::Batch)
        begin_batch();
}
void StudyPanel::save_capture() {
    if (!captured || busy())
        return;
    write_error.clear();
    job = Job::Capture;
    work = std::async(std::launch::async, [this] {
        Result r;
        const auto &s = captured->state;
        auto view = capture_views[s.config.depth > 1];
        Renderer preview(gpu);
        preview.upload(s);
        preview.draw(view, 384, 288);
        GalleryNotes notes;
        notes.title =
            std::string(s.config.depth > 1 ? "3D" : "2D") + " field at step " + std::to_string(s.steps);
        // Retain exact experiment inputs with their reports, without exposing a second gallery.
        r.entry = save_gallery_entry(archive_root, s, view, notes, preview.capture_bmp());
        r.source_root = archive_root;
        return r;
    });
}
void StudyPanel::open_source(std::filesystem::path path, std::string id) {
    if (!can_change_source())
        return;
    message.clear();
    job = Job::Select;
    work = std::async(std::launch::async, [path, id] {
        Result r;
        r.entry = read_gallery_entry(path, id);
        r.state = load_gallery_state(path, id);
        r.source_root = path;
        r.studies_source = id;
        try {
            r.batches = read_batches(path / id / "batches", r.catalog.errors);
        } catch (const std::exception &e) {
            r.catalog.errors.push_back(e.what());
        }
        try {
            r.studies = list_gallery_studies(path, id);
        } catch (const std::exception &e) {
            r.studies.emplace().errors.push_back(e.what());
        }
        return r;
    });
}
void StudyPanel::reset_source() {
    studies = {};
    batches.clear();
    study_index = batch_index = batch_run = 0;
    batch_label[0] = 0;
    requested_presentation = 0;
}
void StudyPanel::restore_source() {
    restore_view = selected->view;
    restore_mode = selected->state.config.depth > 1;
    attach_restore = true;
    job = Job::Load;
    work = std::async(std::launch::async, [path = root, id = selected->id] {
        Result r;
        r.state = load_gallery_state(path, id);
        return r;
    });
}
void StudyPanel::retry_save() {
    if (busy())
        return;
    if (pending_study)
        save_study();
    else if (captured)
        save_capture();
}
void StudyPanel::tick(Runner &runner, std::array<ViewSettings, 2> &views) {
    try {
        if (ready(capture)) {
            auto input = capture.get();
            if (requested == Job::Shape) {
                requested = Job::None;
                begin_shape(std::move(input));
            } else if (selected && source_revision == input.revision) {
                start_requested();
            } else {
                captured = std::move(input);
                save_capture();
            }
        }
        if (ready(work)) {
            auto result = work.get();
            auto completed = job;
            job = Job::None;
            if (result.entry) {
                selected = std::move(result.entry);
                root = std::move(result.source_root);
                source_revision.reset();
                reset_source();
                message.clear();
            }
            if (completed == Job::Capture) {
                source_revision = captured->revision;
                captured.reset();
                write_error.clear();
            }
            if (result.studies && selected && selected->id == result.studies_source) {
                studies = std::move(*result.studies);
                study_index = 0;
            }
            if (result.batches && selected && selected->id == result.studies_source) {
                batches = std::move(*result.batches);
                batch_index = batch_run = 0;
                std::snprintf(batch_label, sizeof batch_label, "%s",
                              batches.empty() || batches.front().result.runs.empty()
                                  ? ""
                                  : batches.front().result.runs.front().label.c_str());
            }
            if (result.batch) {
                if (completed == Job::BatchLabel)
                    batches.at(batch_index) = std::move(*result.batch);
                else {
                    batches.insert(batches.begin(), std::move(*result.batch));
                    batch_index = batch_run = 0;
                    batch_label[0] = 0;
                }
                const auto &b = batches.at(batch_index).result;
                message = "Batch " + b.status + ": " + std::to_string(b.runs.size()) + " saved runs.";
                if (!b.error.empty())
                    message += " " + b.error;
            }
            if (result.shape) {
                shape = std::move(result.shape);
                shape_component = 0;
                shape_scalar = false;
                message.clear();
            }
            if (result.state) {
                if (completed == Job::Select) {
                    activate_on_restore = true;
                    restore_view = selected->view;
                    restore_mode = selected->state.config.depth > 1;
                    attach_restore = true;
                }
                restore = runner.restore_field(std::move(*result.state));
            }
            if (result.report) {
                pending_study = std::move(result.report);
                save_study();
            }
            if (completed == Job::SaveStudy) {
                pending_study.reset();
                message = "Behavior report saved.";
            }
            if (completed == Job::Select && !result.catalog.errors.empty())
                message = result.catalog.errors.front();
            if (completed == Job::Capture)
                start_requested();
        }
        if (ready(restore)) {
            auto revision = restore.get();
            views[restore_mode] = restore_view;
            if (attach_restore)
                source_revision = revision;
            else
                source_revision.reset();
            attach_restore = false;
            requested_presentation = 0;
            activate |= std::exchange(activate_on_restore, false);
        }
        auto current = runner.frame();
        if (shape && current && (current->running || shape->revision != current->field_revision))
            shape.reset();
    } catch (const std::exception &e) {
        message = e.what();
        if (job == Job::Capture || job == Job::SaveStudy)
            write_error = "Study save failed: " + message;
        job = Job::None;
        if (!captured)
            requested = Job::None;
        if (smoke_phase)
            throw;
    }
}
void StudyPanel::draw_controls(Runner &runner, const std::array<ViewSettings, 2> &views, float dpi,
                               bool closing) {
    auto frame = runner.frame();
    if (!controls_initialized && frame) {
        study_backend = frame->cuda ? 2 : frame->gpu ? 1 : 0;
        batch_plan.first_seed = frame->state.seed;
        const auto mu = frame->state.config.mu;
        std::snprintf(batch_value_text, sizeof batch_value_text, "%.6g, %.6g, %.6g", mu - .01, mu, mu + .01);
        controls_initialized = true;
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##Study tool", &tool, "Shape\0Behavior\0Batches\0");
    ImGui::Separator();
    // Tool controls are always present; neither a source selector nor a capture step gates them.
    ImGui::BeginDisabled(!frame);
    State empty;
    const auto &state = frame ? frame->state : empty;
    if (tool == 0)
        draw_shape(runner, views, dpi, closing);
    else if (tool == 1)
        draw_study(runner, views, state, dpi, closing);
    else
        draw_batch(runner, views, state, dpi, closing);
    ImGui::EndDisabled();
    if (busy()) {
        const char *action = capture.valid()       ? "Pausing for measurement..."
                             : job == Job::Capture ? "Saving experiment input..."
                             : job == Job::Select || job == Job::Load || restore.valid() ? "Loading field..."
                             : job == Job::Shape ? "Measuring shape..."
                             : job == Job::Study ? "Running behavior study..."
                             : job == Job::Batch ? "Running batch..."
                                                 : "Saving results...";
        ImGui::TextColored({.38f, .85f, .77f, 1}, "%s", action);
    }
    if (!message.empty())
        ImGui::TextWrapped("%s", message.c_str());
    if ((!write_error.empty() || pending_study || captured) && !busy()) {
        if (ImGui::Button("Retry save"))
            retry_save();
        if (ImGui::Button("Discard unsaved result")) {
            pending_study.reset();
            captured.reset();
            requested = Job::None;
            write_error.clear();
        }
    }
    if ((job == Job::Study && tool != 1) || (job == Job::Batch && tool != 2)) {
        if (ImGui::Button("Stop and keep partial results"))
            stop_study();
    }
}
double StudyPanel::draw_scene(Renderer &live, const Frame *frame, const Config &config, ViewSettings &view,
                              ViewerUi &viewer, float dpi, bool closing) {
    ImGui::TextUnformatted("Current field");
    if (frame) {
        ImGui::Text("%s   %d x %d x %d", config.depth > 1 ? "3D LENIA" : "2D LENIA", config.width,
                    config.height, config.depth);
        ImGui::SameLine();
        ImGui::TextDisabled("  t = %.3f   step %llu", frame->state.time,
                            static_cast<unsigned long long>(frame->state.steps));
    } else
        ImGui::TextUnformatted("Preparing simulation...");
    double submit = 0;
    auto field = [&] {
        auto size = ImGui::GetContentRegionAvail();
        size.x = std::max(1.f, size.x);
        size.y = std::max(1.f, size.y - 118 * dpi);
        ViewerOptions options;
        if (showing_components(frame)) {
            options.components = !shape_scalar;
            options.isolate = shape_component;
            options.measured_threshold = float(shape->threshold);
        }
        submit =
            draw_viewer("Simulation", field_renderer(live, frame), config, view, viewer, size, dpi, options);
        if (frame) {
            ImGui::Text("%.1f steps/s    %.2f ms compute    %.0f display fps", frame->steps_per_second,
                        frame->timings.total_ms, ImGui::GetIO().Framerate);
            ImGui::TextDisabled("%.2f ms display preparation    %s", frame->publication_ms,
                                frame->gpu_image ? "GPU resident" : "CPU transfer");
            ImGui::TextDisabled("Mean %.5f    range [%.4f, %.4f]    %s", frame->measures.mean,
                                frame->measures.minimum, frame->measures.maximum,
                                frame->gpu ? "GPU float" : "CPU double");
        }
        ImGui::TextDisabled(config.depth > 1 && view.view != 0
                                ? "Drag to orbit  |  Wheel to zoom  |  Space to play/pause"
                                : "Wheel to zoom  |  Space to play/pause");
    };
    if (studies.entries.empty() && batches.empty()) {
        presentation = 0;
        field();
    } else if (ImGui::BeginTabBar("Workspace presentation")) {
        if (ImGui::BeginTabItem("Field", nullptr,
                                requested_presentation == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            presentation = 0;
            field();
            ImGui::EndTabItem();
        }
        auto results = [&](const char *label, int index, auto draw) {
            if (ImGui::BeginTabItem(label, nullptr,
                                    requested_presentation == index ? ImGuiTabItemFlags_SetSelected : 0)) {
                presentation = index;
                ImGui::BeginChild(label);
                ImGui::Text("Source: %s, step %llu, seed %llu",
                            selected->state.config.depth > 1 ? "3D" : "2D",
                            static_cast<unsigned long long>(selected->state.steps),
                            static_cast<unsigned long long>(selected->state.seed));
                draw();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        };
        if (!studies.entries.empty())
            results("Behavior results", 1, [&] { draw_study_results(dpi); });
        if (!batches.empty())
            results("Batch results", 2, [&] { draw_batch_results(dpi, closing); });
        ImGui::EndTabBar();
    }
    requested_presentation = -1;
    return submit;
}
} // namespace lenia
