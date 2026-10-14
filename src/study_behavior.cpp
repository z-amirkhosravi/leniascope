#include "imgui.h"
#include "study_backend.h"
#include "study_panel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <shellapi.h>

namespace lenia {
void StudyPanel::begin_study() {
    if (!selected || !writable || busy() || pending_study)
        return;
    study_options.validate();
    study_stop = false;
    study_progress = 0;
    message.clear();
    job = Job::Study;
    work = std::async(
        std::launch::async, [this, id = selected->id, options = study_options, backend = study_backend] {
            auto entry = read_gallery_entry(root, id);
            auto source = load_gallery_state(root, id);
            auto solver = study_solver(source, backend == 2 ? "cuda" : backend == 1 ? "gpu" : "cpu");
            Result r;
            r.report = behavior_study(source, *solver, options, [this](int step) {
                study_progress = step;
                return !study_stop.load();
            });
            r.report->source_id = id;
            r.report->source_hash = entry.field_hash;
            return r;
        });
}
void StudyPanel::save_study() {
    if (!pending_study || busy())
        return;
    write_error.clear();
    job = Job::SaveStudy;
    work = std::async(std::launch::async, [path = root, report = *pending_study] {
        Result r;
        r.studies_source = report.source_id;
        auto saved = save_gallery_study(path, report.source_id, report);
        try {
            r.studies = list_gallery_studies(path, report.source_id);
        } catch (const std::exception &e) {
            r.studies.emplace().entries.push_back(std::move(saved));
            r.studies->errors.push_back(std::string("Report saved, but refresh failed: ") + e.what());
        }
        return r;
    });
}
void StudyPanel::draw_study(float dpi, bool closing) {
    ImGui::SetNextItemOpen(study_settings_open);
    study_settings_open = ImGui::CollapsingHeader("Study settings");
    if (study_settings_open) {
        ImGui::BeginDisabled(busy() || closing || !writable || bool(pending_study));
        ImGui::TextUnformatted("Study steps");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##Study steps", &study_options.steps);
        ImGui::TextUnformatted("Sample every");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("##Sample every", &study_options.sample_every);
        ImGui::TextUnformatted("Activity threshold");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputDouble("##Activity threshold", &study_options.activity_threshold, 0, 0, "%.4f");
        ImGui::TextUnformatted("Study backend");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##Study backend", &study_backend,
#ifdef LENIA_ENABLE_CUDA
                     "CPU reference\0DirectCompute\0CUDA / cuFFT\0"
#else
                     "CPU reference\0DirectCompute\0"
#endif
        );
        if (study_backend && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("GPU evolution uses float32; initial rounding is recorded in the report.");
        std::string error;
        try {
            study_options.validate();
        } catch (const std::exception &e) {
            error = e.what();
        }
        if (error.empty())
            ImGui::TextWrapped(
                "Duration %.6g; %d measured lags", study_options.steps * selected->state.config.dt,
                (study_options.steps + study_options.sample_every - 1) / study_options.sample_every);
        else
            ImGui::TextWrapped("%s", error.c_str());
        ImGui::BeginDisabled(!error.empty());
        if (ImGui::Button("Run study")) {
            try {
                begin_study();
            } catch (const std::exception &e) {
                message = e.what();
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Evolve and measure a separate copy of this snapshot.");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }
    if (job == Job::Study) {
        ImGui::ProgressBar(float(study_progress.load()) / study_options.steps, {-1, 0});
        if (ImGui::Button("Stop study (keep results)"))
            stop_study();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Finish the current step or measurement, then save the results.");
    }
    if (job == Job::SaveStudy)
        ImGui::TextDisabled("Saving report...");
}
void StudyPanel::draw_study_results(float dpi) {
    for (const auto &e : studies.errors)
        ImGui::TextWrapped("Study skipped: %s", e.c_str());
    if (studies.entries.empty())
        return;
    study_index = std::clamp(study_index, 0, int(studies.entries.size()) - 1);
    auto label = [&](int i) {
        auto &r = studies.entries[i].report;
        return std::to_string(i + 1) + ": " + std::to_string(r.completed_steps) + " steps / " +
               (r.backend.find("cuFFT") != std::string::npos ? "CUDA"
                : r.backend.find("GPU") != std::string::npos ? "DirectCompute"
                                                             : "CPU") +
               (r.stopped ? " / stopped" : "");
    };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##study-report", label(study_index).c_str())) {
        for (int i = 0; i < int(studies.entries.size()); ++i)
            if (ImGui::Selectable(label(i).c_str(), i == study_index))
                study_index = i;
        ImGui::EndCombo();
    }
    const auto &saved = studies.entries[study_index];
    const auto &r = saved.report;
    auto &last = r.samples.back();
    ImGui::TextWrapped("%s", r.backend.c_str());
    ImGui::Text("Measured t = 0..%.6g; %zu samples", last.elapsed, r.samples.size());
    ImGui::Text("Initial rounding RMS: %.6g", r.initial_rounding_rms);
    ImGui::Checkbox("Magnify variations (independent Y scales)", &study_zoom);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Magnification fills each plot's height, even for very small changes.\n"
                          "Uncheck to restore zero-based scales extending to at least 100%%.");
    // A stopped run may end with a shorter interval; the horizontal axis is sample index.
    ImGui::TextDisabled("X: samples 0..%zu (t = 0..%.6g)", r.samples.size() - 1, last.elapsed);
    auto plot = [&](const char *id, const char *title, double BehaviorSample::*metric) {
        auto range = std::minmax_element(r.samples.begin(), r.samples.end(),
                                         [&](const auto &a, const auto &b) { return a.*metric < b.*metric; });
        double minimum = (*range.first).*metric, maximum = (*range.second).*metric;
        double low = study_zoom ? minimum : 0, high = study_zoom ? maximum : std::max(1., maximum);
        if (high == low) {
            double padding = std::max(1e-6, std::abs(low) * .05);
            low = std::max(0., low - padding);
            high += padding;
        }
        // Normalize in double before converting to float, preserving small variations when magnified.
        std::vector<float> values;
        for (const auto &s : r.samples)
            values.push_back(float((s.*metric - low) / (high - low)));
        ImGui::TextWrapped("%s", title);
        if (metric == &BehaviorSample::relative_rms && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Aligned RMS difference as a percentage of the starting field's RMS.\n"
                              "Alignment uses one integer-cell translation of the whole field.");
        ImGui::Text("Start %.6g%% -> final %.6g%%; peak %.6g%%", 100 * (r.samples.front().*metric),
                    100 * (last.*metric), 100 * maximum);
        char scale[96];
        std::snprintf(scale, sizeof scale, "Y: %.6g%% to %.6g%%", 100 * low, 100 * high);
        ImGui::PlotLines(id, values.data(), int(values.size()), 0, scale, 0, 1, {-1, 75 * dpi});
        if (ImGui::IsItemHovered()) {
            float padding = ImGui::GetStyle().FramePadding.x;
            float fraction = (ImGui::GetMousePos().x - ImGui::GetItemRectMin().x - padding) /
                             std::max(1.f, ImGui::GetItemRectSize().x - 2 * padding);
            int i = std::clamp(int(std::round(fraction * (values.size() - 1))), 0, int(values.size()) - 1);
            const auto &s = r.samples[i];
            ImGui::SetTooltip("Sample %d / step %d / lag %.6g\nValue %.9g%% (ratio %.9g)", i, s.step,
                              s.elapsed, 100 * (s.*metric), s.*metric);
        }
    };
    if (r.reference_rms > 0)
        plot("##shape", "Relative RMS mismatch from start", &BehaviorSample::relative_rms);
    else
        ImGui::TextWrapped("The reference is empty: normalized mismatch is undefined.");
    plot("##activity", "Mean activity", &BehaviorSample::mean);
    plot("##coverage", "Fraction above activity threshold", &BehaviorSample::active_fraction);
    ImGui::Text("Threshold %.6g; final aligned RMS %.6g", r.options.activity_threshold, last.aligned_rms);
    if (last.shift_valid) {
        ImGui::Text("Final shift from start: (%d, %d, %d) cells", last.dx, last.dy, last.dz);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip(
                "Whole-field translation, modulo the grid; individual organisms are not tracked.");
    } else
        ImGui::TextWrapped("Final shift is undefined or ambiguous (flat field or tied correlation peaks).");
    if (ImGui::CollapsingHeader("Connected components and periodic loops")) {
        draw_shape_summary(last.morphology, r.config, r.options.activity_threshold);
        if (last.morphology) {
            std::vector<float> counts;
            uint64_t maximum = 1;
            for (const auto &sample : r.samples) {
                counts.push_back(float(sample.morphology->components));
                maximum = std::max(maximum, sample.morphology->components);
            }
            char scale[96];
            std::snprintf(scale, sizeof scale, "Components: 0 to %llu",
                          static_cast<unsigned long long>(maximum));
            ImGui::PlotLines("##components", counts.data(), int(counts.size()), 0, scale, 0, float(maximum),
                             {-1, 75 * dpi});
            if (ImGui::IsItemHovered()) {
                float padding = ImGui::GetStyle().FramePadding.x;
                float fraction = (ImGui::GetMousePos().x - ImGui::GetItemRectMin().x - padding) /
                                 std::max(1.f, ImGui::GetItemRectSize().x - 2 * padding);
                int i =
                    std::clamp(int(std::round(fraction * (counts.size() - 1))), 0, int(counts.size()) - 1);
                const auto &sample = r.samples[i];
                ImGui::SetTooltip(
                    "Step %d / lag %.6g\nComponents %llu; with periodic loops %llu\nLoop directions: %s",
                    sample.step, sample.elapsed,
                    static_cast<unsigned long long>(sample.morphology->components),
                    static_cast<unsigned long long>(sample.morphology->winding_components),
                    winding_axes_text(sample.morphology->winding_axes).c_str());
            }
        }
    }
    if (ImGui::Button("Open report folder")) {
        auto path = root / selected->id / "studies" / saved.id;
        if (reinterpret_cast<INT_PTR>(
                ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            message = "Could not open report folder: " + path.u8string();
    }
    ImGui::Separator();
}
} // namespace lenia
