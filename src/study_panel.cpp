#include "study_panel.h"
#include "imgui.h"
#include "viewer.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <utility>

namespace lenia {
namespace {
template <class T> bool ready(std::future<T> &f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
std::string snapshot_label(const GalleryEntry &entry) {
    const auto created = static_cast<time_t>(entry.created);
    tm local{};
    localtime_s(&local, &created);
    char date[40];
    std::strftime(date, sizeof date, "%Y-%m-%d %H:%M:%S", &local);
    return std::string(date) + " / " + (entry.state.config.depth > 1 ? "3D" : "2D");
}
} // namespace
StudyPanel::StudyPanel(std::filesystem::path path, bool can_write, std::shared_ptr<GpuContext> context)
    : archive_root(std::move(path)), writable(can_write), gpu(std::move(context)) {
    if (!archive_root.empty()) {
        job = Job::Scan;
        work = std::async(std::launch::async, [path = archive_root] {
            Result r;
            r.catalog = list_gallery(path);
            return r;
        });
    }
}
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
bool StudyPanel::take_activation() {
    return std::exchange(activate, false);
}
bool StudyPanel::take_simulation_request() {
    return std::exchange(simulation_requested, false);
}
void StudyPanel::use_current(Runner &runner, const std::array<ViewSettings, 2> &views) {
    if (!can_change_source() || !writable || archive_root.empty())
        return;
    capture_views = views;
    capture = runner.capture_current();
    message.clear();
    activate = true;
}
void StudyPanel::save_capture() {
    if (!captured || busy())
        return;
    write_error.clear();
    job = Job::Capture;
    work = std::async(std::launch::async, [this] {
        Result r;
        const auto &s = *captured;
        auto view = capture_views[s.config.depth > 1];
        r.renderer = std::make_unique<Renderer>(gpu);
        r.renderer->upload(s);
        r.renderer->draw(view, 384, 288);
        GalleryNotes notes;
        notes.title = std::string(s.config.depth > 1 ? "3D" : "2D") + " current field at step " +
                      std::to_string(s.steps);
        // Reuse the atomic checkpoint/metadata format, in the separate study archive.
        r.entry = save_gallery_entry(archive_root, s, view, notes, r.renderer->capture_bmp());
        r.source_root = archive_root;
        try {
            r.catalog = list_gallery(archive_root);
        } catch (const std::exception &e) {
            r.catalog.entries.push_back(*r.entry);
            r.catalog.errors.push_back(e.what());
        }
        return r;
    });
}
void StudyPanel::open_source(std::filesystem::path path, std::string id) {
    if (!can_change_source())
        return;
    activate = true;
    message.clear();
    job = Job::Select;
    work = std::async(std::launch::async, [path, id, context = gpu] {
        Result r;
        r.entry = read_gallery_entry(path, id);
        auto source = load_gallery_state(path, id);
        r.renderer = std::make_unique<Renderer>(context);
        r.renderer->upload(source);
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
    shape.reset();
    shape_component = 0;
    shape_scalar = false;
    shape_view = selected->view;
    studies = {};
    batches.clear();
    study_index = batch_index = batch_run = 0;
    batch_label[0] = 0;
    study_settings_open = batch_settings_open = true;
    results_requested = false;
    study_backend = selected->state.backend.find("GPU") != std::string::npos ? 1 : 0;
#ifdef LENIA_ENABLE_CUDA
    if (selected->state.backend.find("cuFFT") != std::string::npos)
        study_backend = 2;
#endif
    batch_plan.first_seed = selected->state.seed;
    batch_plan.reseed = false;
    batch_plan.repetitions = 1;
    batch_plan.axis = BatchAxis::Mu;
    std::snprintf(batch_value_text, sizeof batch_value_text, "%.6g, %.6g, %.6g",
                  selected->state.config.mu - .01, selected->state.config.mu,
                  selected->state.config.mu + .01);
}
void StudyPanel::restore_source() {
    restore_view = shape_view;
    restore_mode = selected->state.config.depth > 1;
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
            captured = capture.get();
            save_capture();
        }
        if (ready(work)) {
            auto result = work.get();
            auto completed = job;
            job = Job::None;
            if (completed == Job::Scan || completed == Job::Capture)
                archive = std::move(result.catalog);
            if (result.entry) {
                selected = std::move(result.entry);
                root = std::move(result.source_root);
                source_renderer = std::move(result.renderer);
                reset_source();
                viewing_source = true;
                runner.play(false); // Explicit snapshot selection pauses the now-hidden simulation.
                message.clear();
            }
            if (completed == Job::Capture) {
                captured.reset();
                write_error.clear();
            }
            if (result.studies && selected && selected->id == result.studies_source) {
                studies = std::move(*result.studies);
                study_index = 0;
                study_settings_open = studies.entries.empty();
            }
            if (result.batches && selected && selected->id == result.studies_source) {
                batches = std::move(*result.batches);
                batch_settings_open = batches.empty();
                batch_index = batch_run = 0;
                std::snprintf(batch_label, sizeof batch_label, "%s",
                              batches.empty() || batches.front().result.runs.empty()
                                  ? ""
                                  : batches.front().result.runs.front().label.c_str());
            }
            if (result.batch) {
                batch_settings_open = false;
                if (completed == Job::BatchLabel)
                    batches.at(batch_index) = std::move(*result.batch);
                else {
                    batches.insert(batches.begin(), std::move(*result.batch));
                    batch_index = batch_run = 0;
                    batch_label[0] = 0;
                    results_requested = tool == 2;
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
            if (result.state)
                restore = runner.restore(std::move(*result.state));
            if (result.report) {
                pending_study = std::move(result.report);
                save_study();
            }
            if (completed == Job::SaveStudy) {
                pending_study.reset();
                results_requested = tool == 1;
                message = "Behavior report saved.";
            }
            if (completed == Job::Select && !result.catalog.errors.empty())
                message = result.catalog.errors.front();
        }
        if (ready(restore)) {
            restore.get();
            views[restore_mode] = restore_view;
            viewing_source = false;
            simulation_requested = true;
            message = "Loaded field into Simulation, paused. Reset restores that field.";
        }
    } catch (const std::exception &e) {
        message = e.what();
        if (job == Job::Capture || job == Job::SaveStudy)
            write_error = "Study save failed: " + message;
        job = Job::None;
        if (smoke_phase)
            throw;
    }
}
void StudyPanel::draw_controls(Runner &runner, const std::array<ViewSettings, 2> &views, float dpi,
                               bool closing) {
    // Selecting a panel never selects a different field. Only these explicit source actions do.
    const std::string label =
        showing_source()
            ? (root == archive_root ? snapshot_label(*selected) : "Gallery: " + selected->annotation.title)
            : "Current simulation";
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##Study field", label.c_str())) {
        if (ImGui::Selectable("Current simulation", !showing_source()))
            show_current();
        if (selected && root != archive_root &&
            ImGui::Selectable(("Gallery: " + selected->annotation.title).c_str(), showing_source())) {
            runner.play(false);
            viewing_source = true;
        }
        if (!archive.entries.empty()) {
            ImGui::SeparatorText("Saved snapshots");
            for (const auto &entry : archive.entries) {
                ImGui::PushID(entry.id.c_str());
                const bool retained = selected && root == archive_root && selected->id == entry.id;
                ImGui::BeginDisabled(closing || (!retained && !can_change_source()));
                if (ImGui::Selectable(snapshot_label(entry).c_str(), showing_source() && retained)) {
                    if (retained) {
                        runner.play(false);
                        viewing_source = true;
                    } else
                        open_source(archive_root, entry.id);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    }
    if (showing_source()) {
        ImGui::BeginDisabled(!can_change_source() || closing);
        if (ImGui::Button("Load into Simulation", {-1, 0}))
            restore_source();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Replace the simulation with this snapshot, paused. Reset will return to it.");
    } else {
        ImGui::BeginDisabled(!can_change_source() || closing || !writable || archive_root.empty());
        if (ImGui::Button("Take snapshot", {-1, 0}))
            use_current(runner, views);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Pause and save this field for measurement and experiments.");
        if (!writable || archive_root.empty())
            ImGui::TextWrapped("Snapshot saving is unavailable until workspace access is restored.");
    }
    if (busy()) {
        const char *action = capture.valid()                       ? "Pausing and capturing..."
                             : job == Job::Scan                    ? "Reading saved snapshots..."
                             : job == Job::Capture                 ? "Saving snapshot..."
                             : job == Job::Select                  ? "Opening snapshot..."
                             : job == Job::Load || restore.valid() ? "Loading simulation..."
                             : job == Job::Shape                   ? "Measuring shape..."
                             : job == Job::Study                   ? "Running behavior study..."
                             : job == Job::Batch                   ? "Running batch..."
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
            write_error.clear();
        }
    }
    for (const auto &error : archive.errors)
        ImGui::TextWrapped("Snapshot skipped: %s", error.c_str());
    if (!showing_source()) {
        if ((job == Job::Study || job == Job::Batch) && ImGui::Button("Stop and keep partial results"))
            stop_study();
        return;
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##Study tool", &tool, "Shape\0Behavior\0Batches\0");
    ImGui::Separator();
    if (tool == 0)
        draw_shape(dpi, closing);
    else if (tool == 1)
        draw_study(dpi, closing);
    else
        draw_batch(dpi, closing);
    if ((job == Job::Study && tool != 1) || (job == Job::Batch && tool != 2)) {
        ImGui::TextWrapped("A study is running in the background.");
        if (ImGui::Button("Stop and keep partial results"))
            stop_study();
    }
}
void StudyPanel::draw_simulation_actions(bool closing) {
    ImGui::BeginDisabled(!can_change_source() || closing);
    if (ImGui::Button("Load into Simulation", {-1, 0}))
        restore_source();
    ImGui::EndDisabled();
    if (ImGui::Button("Show current simulation", {-1, 0}))
        show_current();
    if (job == Job::Study || job == Job::Batch) {
        if (ImGui::Button("Stop and keep partial results"))
            stop_study();
    } else if (busy())
        ImGui::TextDisabled("Finishing study operation...");
    else if (!can_change_source())
        ImGui::TextWrapped("Save or discard the unsaved result in Study first.");
}
double StudyPanel::draw_scene(float dpi, bool closing) {
    if (!showing_source())
        return 0;
    const auto &state = selected->state;
    const auto title = root == archive_root ? snapshot_label(*selected) : selected->annotation.title;
    ImGui::Text("Snapshot: %s", title.c_str());
    ImGui::Text("%s   %d x %d x %d", state.config.depth > 1 ? "3D LENIA" : "2D LENIA", state.config.width,
                state.config.height, state.config.depth);
    ImGui::SameLine();
    ImGui::TextDisabled("  t = %.3f   step %llu", state.time, static_cast<unsigned long long>(state.steps));
    double submit = 0;
    auto field = [&] {
        auto size = ImGui::GetContentRegionAvail();
        size.x = std::max(1.f, size.x);
        size.y = std::max(1.f, size.y - 118 * dpi); // Match the simulation viewport across capture/load.
        ViewerOptions options;
        Renderer *renderer = source_renderer.get();
        if (tool == 0 && shape) {
            renderer = shape->renderer.get();
            options.components = !shape_scalar;
            options.isolate = shape_component;
            options.measured_threshold = float(shape->threshold);
        }
        submit = draw_viewer("Study", *renderer, selected->state.config, shape_view, viewer_ui, size, dpi,
                             options);
    };
    if (tool == 0)
        field();
    else {
        ImGui::PushID(tool);
        if (ImGui::BeginTabBar("Study presentation")) {
            if (ImGui::BeginTabItem("Field")) {
                field();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Results", nullptr,
                                    results_requested ? ImGuiTabItemFlags_SetSelected : 0)) {
                ImGui::BeginChild("Study results");
                if (tool == 1) {
                    if (studies.entries.empty())
                        ImGui::TextWrapped("Run a behavior study to measure change from this source.");
                    draw_study_results(dpi);
                } else {
                    if (batches.empty())
                        ImGui::TextWrapped("Run a batch to compare outcomes from this source.");
                    draw_batch_results(dpi, closing);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            results_requested = false;
        }
        ImGui::PopID();
    }
    return submit;
}
} // namespace lenia
