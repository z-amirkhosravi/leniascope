#include "gallery_panel.h"
#include "imgui.h"
#include "study_backend.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <shellapi.h>

namespace lenia {
std::vector<GalleryPanel::SavedBatch> GalleryPanel::read_batches(const std::filesystem::path &path,
                                                                 std::vector<std::string> &errors) {
    std::vector<SavedBatch> result;
    if (!std::filesystem::exists(path))
        return result;
    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        if (!entry.is_directory())
            continue;
        try {
            result.push_back({entry.path().filename().u8string(), read_batch(entry.path())});
        } catch (const std::exception &e) {
            errors.push_back("Batch " + entry.path().filename().u8string() + ": " + e.what());
        }
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.id > b.id; });
    return result;
}
void GalleryPanel::begin_batch() {
    if (!selected || !writable || busy() || pending_study)
        return;
    batch_plan.values = batch_values(batch_value_text);
    batch_plan.backend = study_backend == 2 ? "cuda" : study_backend == 1 ? "gpu" : "cpu";
    batch_plan.validate(selected->state);
    study_stop = false;
    study_progress = batch_progress = 0;
    message.clear();
    job = Job::Batch;
    work = std::async(std::launch::async, [this, id = selected->id, plan = batch_plan] {
        auto source = load_gallery_state(root, id);
        auto name = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());
        auto path = root / id / "batches" / name;
        Result r;
        r.batch = SavedBatch{name, run_batch(path, source, plan, id, study_solver, [this](int run, int step) {
                                 batch_progress = run;
                                 study_progress = step;
                                 return !study_stop.load();
                             })};
        return r;
    });
}
void GalleryPanel::batch_field(bool final) {
    auto path = root / selected->id / "batches" / batches.at(batch_index).id;
    restore_view = selected->view;
    restore_mode = selected->state.config.depth > 1;
    job = Job::Load;
    work = std::async(std::launch::async, [path, index = batch_run, final] {
        Result r;
        r.state = load_batch_field(path, index, final);
        return r;
    });
}
void GalleryPanel::batch_annotation() {
    auto saved = batches.at(batch_index);
    auto path = root / selected->id / "batches" / saved.id;
    job = Job::BatchLabel;
    work = std::async(std::launch::async,
                      [path, id = saved.id, index = batch_run, label = std::string(batch_label)] {
                          label_batch_run(path, index, label);
                          Result r;
                          r.batch = SavedBatch{id, read_batch(path)};
                          return r;
                      });
}
void GalleryPanel::draw_batch(float dpi, bool closing) {
    ImGui::TextWrapped("Compare several parameter values at a fixed step horizon. Every run starts afresh; "
                       "the live simulation and saved gallery field are unchanged.");
    ImGui::SetNextItemOpen(batch_settings_open);
    batch_settings_open = ImGui::CollapsingHeader("Batch settings");
    if (batch_settings_open) {
        ImGui::BeginDisabled(busy() || closing || !writable || bool(pending_study));
        ImGui::BeginDisabled(!selected->state.initialization);
        if (ImGui::Checkbox("Generate new fields from saved seed recipe", &batch_plan.reseed))
            batch_plan.repetitions = batch_plan.reseed ? 3 : 1;
        ImGui::EndDisabled();
        if (batch_plan.reseed) {
            ImGui::SetNextItemWidth(150 * dpi);
            ImGui::InputInt("Seeds per value", &batch_plan.repetitions);
            ImGui::SetNextItemWidth(180 * dpi);
            ImGui::InputScalar("First seed", ImGuiDataType_U64, &batch_plan.first_seed);
            ImGui::TextWrapped(
                "Uses consecutive seeds, paired across parameter values. Patch dimensions "
                "use each run's kernel radius. This regenerates the recipe, not the saved organism.");
        } else
            ImGui::TextWrapped("Each run uses the exact saved field. Changing R changes the kernel, "
                               "without resizing the field or organism.");
        int axis = int(batch_plan.axis);
        ImGui::SetNextItemWidth(240 * dpi);
        if (ImGui::Combo("Vary", &axis,
                         "Growth mean (mu)\0Growth width (sigma)\0Kernel radius\0"
                         "Pair separation / cluster radius (R)\0Pair angle (degrees)\0")) {
            batch_plan.axis = BatchAxis(axis);
            const auto &c = selected->state.config;
            if (axis == 2)
                std::snprintf(batch_value_text, sizeof batch_value_text, "%d, %d, %d", c.radius - 1, c.radius,
                              c.radius + 1);
            else if (axis == 3)
                std::snprintf(batch_value_text, sizeof batch_value_text, "1, 1.5, 2");
            else if (axis == 4)
                std::snprintf(batch_value_text, sizeof batch_value_text, "0, 30, 60");
            else {
                double v = axis == 0 ? c.mu : c.sigma, delta = axis == 0 ? .01 : .002;
                std::snprintf(batch_value_text, sizeof batch_value_text, "%.6g, %.6g, %.6g", v - delta, v,
                              v + delta);
            }
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##batch-values", "Values separated by commas", batch_value_text,
                                 sizeof batch_value_text);
        ImGui::SetNextItemWidth(150 * dpi);
        ImGui::InputInt("Steps per run", &batch_plan.observation.steps);
        ImGui::SetNextItemWidth(150 * dpi);
        ImGui::InputInt("Measure every", &batch_plan.observation.sample_every);
        ImGui::SetNextItemWidth(150 * dpi);
        ImGui::InputDouble("Active above", &batch_plan.observation.activity_threshold, 0, 0, "%.4f");
        ImGui::SetNextItemWidth(220 * dpi);
        ImGui::Combo("Batch backend", &study_backend,
#ifdef LENIA_ENABLE_CUDA
                     "CPU reference\0DirectCompute\0CUDA / cuFFT\0"
#else
                     "CPU reference\0DirectCompute\0"
#endif
        );
        std::string error;
        try {
            batch_plan.values = batch_values(batch_value_text);
            batch_plan.validate(selected->state);
        } catch (const std::exception &e) {
            error = e.what();
        }
        if (error.empty()) {
            double mib = double(selected->state.config.count()) * 8 * (2 * batch_plan.count() + 1) / 1048576;
            ImGui::TextWrapped("%d runs; duration %.6g each; approximately %.1f MiB for saved fields.",
                               batch_plan.count(), batch_plan.observation.steps * selected->state.config.dt,
                               mib);
        } else
            ImGui::TextWrapped("%s", error.c_str());
        ImGui::BeginDisabled(!error.empty());
        if (ImGui::Button("Run batch")) {
            try {
                begin_batch();
            } catch (const std::exception &e) {
                message = e.what();
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }
    if (job == Job::Batch) {
        ImGui::Text("Run %d / %d; step %d / %d", batch_progress.load() + 1, batch_plan.count(),
                    study_progress.load(), batch_plan.observation.steps);
        ImGui::ProgressBar(
            (batch_progress.load() + float(study_progress.load()) / batch_plan.observation.steps) /
                batch_plan.count(),
            {-1, 0});
        if (ImGui::Button("Stop batch and keep results"))
            stop_study();
        ImGui::TextWrapped("Stopping waits for the current step, measurement or checkpoint write.");
    }
    if (batches.empty())
        return;
    ImGui::Separator();
    ImGui::BeginDisabled(busy() || closing || bool(pending_study));
    batch_index = std::clamp(batch_index, 0, int(batches.size()) - 1);
    auto label = [&](int i) {
        const auto &b = batches[i];
        return std::to_string(i + 1) + ": " + batch_axis_name(b.result.plan.axis) + " / " +
               std::to_string(b.result.plan.count()) + " runs / " + b.result.status;
    };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##batch-results", label(batch_index).c_str())) {
        for (int i = 0; i < int(batches.size()); ++i)
            if (ImGui::Selectable(label(i).c_str(), i == batch_index)) {
                batch_index = i;
                batch_run = 0;
                std::snprintf(batch_label, sizeof batch_label, "%s",
                              batches[i].result.runs.empty() ? ""
                                                             : batches[i].result.runs.front().label.c_str());
            }
        ImGui::EndCombo();
    }
    const auto &saved = batches[batch_index];
    const auto &r = saved.result;
    ImGui::Text("%zu / %d saved runs; %s; dt from source", r.runs.size(), r.plan.count(),
                r.plan.backend.c_str());
    if (!r.error.empty())
        ImGui::TextWrapped("%s", r.error.c_str());
    if (ImGui::Button("Open batch folder / CSV")) {
        auto path = root / selected->id / "batches" / saved.id;
        if (reinterpret_cast<INT_PTR>(
                ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            message = "Could not open batch folder: " + path.u8string();
    }
    if (ImGui::BeginTable("Batch results", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          {0, 175 * dpi})) {
        for (const char *title : {"Run", "Value", "Seed", "Parts", "Active %", "Status / label"})
            ImGui::TableSetupColumn(title);
        ImGui::TableHeadersRow();
        for (const auto &run : r.runs) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::to_string(run.index + 1).c_str(), batch_run == run.index,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                batch_run = run.index;
                std::snprintf(batch_label, sizeof batch_label, "%s", run.label.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::Text("%.5g", r.plan.values[run.index / r.plan.repetitions]);
            ImGui::TableNextColumn();
            ImGui::Text("%llu", static_cast<unsigned long long>(run.report.seed));
            ImGui::TableNextColumn();
            if (run.report.samples.back().morphology)
                ImGui::Text("%llu", static_cast<unsigned long long>(
                                        run.report.samples.back().morphology->components));
            else
                ImGui::TextDisabled("--");
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", 100 * run.report.samples.back().active_fraction);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s%s%s", run.report.stopped ? "Partial / " : "",
                               run.label.empty() ? "Unclassified" : "", run.label.c_str());
        }
        ImGui::EndTable();
    }
    if (!r.runs.empty()) {
        batch_run = std::clamp(batch_run, 0, int(r.runs.size()) - 1);
        const auto &report = r.runs[batch_run].report;
        ImGui::Text("Run %d: %d steps, elapsed %.6g; rounding RMS %.6g", batch_run + 1,
                    report.completed_steps, report.samples.back().elapsed, report.initial_rounding_rms);
        ImGui::Text("Mean activity: %.6g", report.samples.back().mean);
        if (ImGui::CollapsingHeader("Final shape measurements"))
            draw_shape_summary(report.samples.back().morphology, report.config,
                               report.options.activity_threshold);
        if (ImGui::Button("Load initial field"))
            batch_field(false);
        ImGui::SameLine();
        if (ImGui::Button("Load final field"))
            batch_field(true);
        ImGui::TextWrapped("Loads paused and becomes the Reset starting field. You can evolve it and save "
                           "interesting outcomes as gallery entries.");
        ImGui::BeginDisabled(!writable);
        ImGui::InputTextWithHint("##outcome-label", "Your label: tubes, spheres, transient, unknown...",
                                 batch_label, sizeof batch_label);
        if (ImGui::Button("Save outcome label"))
            batch_annotation();
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
}
} // namespace lenia
