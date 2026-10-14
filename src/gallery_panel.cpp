#include "gallery_panel.h"
#include "imgui.h"
#include "study_backend.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <shellapi.h>
#include <sstream>

namespace lenia {
namespace {
template <class T> bool ready(std::future<T> &f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
std::string lower(std::string s) {
    for (auto &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
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
GalleryPanel::GalleryPanel(std::filesystem::path path, bool can_write, std::shared_ptr<GpuContext> context)
    : root(std::move(path)), writable(can_write), gpu(std::move(context)) {}
GalleryPanel::~GalleryPanel() {
    study_stop = true;
    if (work.valid())
        work.wait();
}
bool GalleryPanel::busy() const {
    return work.valid() || capture.valid() || restore.valid();
}
void GalleryPanel::open() {
    opened = true;
    if (!busy() && !pending_study)
        scan();
}
void GalleryPanel::scan() {
    if (root.empty()) {
        message = "Gallery unavailable: no workspace directory.";
        return;
    }
    job = Job::Scan;
    work = std::async(std::launch::async, [path = root] {
        Result r;
        r.catalog = list_gallery(path);
        return r;
    });
}
void GalleryPanel::texture(const GalleryPreview &preview) {
    auto lock = gpu->lock();
    D3D11_TEXTURE2D_DESC d{};
    d.Width = preview.width;
    d.Height = preview.height;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_IMMUTABLE;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{preview.bgra.data(), UINT(preview.width * 4), 0};
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> next;
    check(gpu->device->CreateTexture2D(&d, &data, &tex), "Create gallery preview");
    check(gpu->device->CreateShaderResourceView(tex.Get(), nullptr, &next), "Create gallery preview view");
    image = std::move(next);
    image_aspect = float(preview.width) / preview.height;
}
void GalleryPanel::select(std::string id) {
    selected.reset();
    image.Reset();
    studies = {};
    shape.reset();
    batches.clear();
    batch_index = batch_run = 0;
    batch_label[0] = 0;
    study_index = 0;
    study_settings_open = true;
    message.clear();
    job = Job::Select;
    work = std::async(std::launch::async, [path = root, id] {
        Result r;
        r.entry = read_gallery_entry(path, id);
        r.studies_source = id;
        try {
            r.batches = read_batches(path / id / "batches", r.catalog.errors);
        } catch (const std::exception &e) {
            r.catalog.errors.push_back(std::string("Cannot browse batches: ") + e.what());
        }
        try {
            r.studies = list_gallery_studies(path, id);
        } catch (const std::exception &e) {
            r.studies.emplace().errors.push_back(e.what());
        }
        // A damaged preview does not prevent loading an otherwise intact checkpoint.
        try {
            r.preview = load_gallery_preview(path, id);
        } catch (const std::exception &e) {
            r.catalog.errors.push_back(e.what());
        }
        return r;
    });
}
void GalleryPanel::set_notes(const GalleryNotes &a) {
    std::snprintf(title, sizeof title, "%s", a.title.c_str());
    std::snprintf(tags, sizeof tags, "%s", a.tags.c_str());
    std::snprintf(notes, sizeof notes, "%s", a.notes.c_str());
    reference = a.reference;
    observed_time = a.observed_time;
}
void GalleryPanel::begin_capture(Runner &runner, const std::array<ViewSettings, 2> &views) {
    message.clear();
    editing_id.clear();
    captured.reset();
    captured_preview.clear();
    image.Reset();
    capture_views = views;
    capture = runner.capture_current();
    editing = true;
}
void GalleryPanel::save() {
    write_error.clear();
    GalleryNotes a{title, tags, notes, reference, observed_time};
    if (!editing_id.empty() || captured) {
        // Retain the immutable capture so a failed write can be retried without recapturing.
        job = editing_id.empty() ? Job::Save : Job::Edit;
        work = std::async(std::launch::async, [this, a, id = editing_id] {
            Result r;
            r.entry = id.empty()
                          ? save_gallery_entry(root, *captured, capture_views[captured->config.depth > 1], a,
                                               captured_preview)
                          : annotate_gallery_entry(root, id, a);
            try {
                r.catalog = list_gallery(root);
            } catch (const std::exception &e) {
                // A refresh failure after commit must not offer to save the same capture again.
                r.catalog.entries.push_back(*r.entry);
                r.catalog.errors.push_back(std::string("Saved entry, but gallery refresh failed: ") +
                                           e.what());
            }
            return r;
        });
    }
}
void GalleryPanel::load(Runner &) {
    if (!selected)
        return;
    restore_view = selected->view;
    restore_mode = selected->state.config.depth > 1;
    job = Job::Load;
    work = std::async(std::launch::async, [path = root, id = selected->id] {
        Result r;
        r.state = load_gallery_state(path, id);
        return r;
    });
}
void GalleryPanel::begin_study() {
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
void GalleryPanel::save_study() {
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
void GalleryPanel::draw_study(float dpi, bool closing) {
    ImGui::TextWrapped("Evolve a separate copy of this saved field. Your current simulation and gallery "
                       "field are unchanged.");
    ImGui::SetNextItemOpen(study_settings_open);
    study_settings_open = ImGui::CollapsingHeader("Study settings");
    if (study_settings_open) {
        ImGui::BeginDisabled(busy() || closing || !writable || bool(pending_study));
        ImGui::SetNextItemWidth(150 * dpi);
        ImGui::InputInt("Study steps", &study_options.steps);
        ImGui::SetNextItemWidth(150 * dpi);
        ImGui::InputInt("Sample every", &study_options.sample_every);
        ImGui::SetNextItemWidth(150 * dpi);
        ImGui::InputDouble("Activity threshold", &study_options.activity_threshold, 0, 0, "%.4f");
        ImGui::SetNextItemWidth(220 * dpi);
        ImGui::Combo("Study backend", &study_backend,
#ifdef LENIA_ENABLE_CUDA
                     "CPU reference\0DirectCompute\0CUDA / cuFFT\0"
#else
                     "CPU reference\0DirectCompute\0"
#endif
        );
        if (study_backend)
            ImGui::TextWrapped("GPU evolution uses float32; any initial rounding is recorded.");
        std::string error;
        try {
            study_options.validate();
        } catch (const std::exception &e) {
            error = e.what();
        }
        if (error.empty())
            ImGui::Text("Duration %.6g; %d measured lags", study_options.steps * selected->state.config.dt,
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
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }
    if (job == Job::Study) {
        ImGui::ProgressBar(float(study_progress.load()) / study_options.steps, {-1, 0});
        if (ImGui::Button("Stop and keep partial report"))
            stop_study();
        ImGui::TextWrapped("Stopping waits for the current step or measurement. Large 3D fields can take "
                           "time to correlate.");
    }
    if (job == Job::SaveStudy)
        ImGui::TextDisabled("Saving report...");
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
    if (ImGui::CollapsingHeader("How to read these plots")) {
        ImGui::TextWrapped(
            "Mismatch measures accumulated difference from the starting field after one whole-field "
            "translation. 1%% means RMS difference is 1%% of the starting field's RMS; it is not "
            "the fraction of cells that changed.");
        ImGui::TextWrapped(
            "Default Y scales start at 0 and extend to at least 100%%. Magnification stretches "
            "each plot to its own observed range. Read the Y labels before comparing heights.");
    }
    ImGui::Checkbox("Magnify variations (independent Y scales)", &study_zoom);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Magnification fills each plot's height, even for very small changes.\n"
                          "Uncheck to restore zero-based scales extending to at least 100%%.");
    // A stopped run may end with a shorter interval; the horizontal axis is sample index.
    ImGui::TextDisabled("X: samples 0..%zu (t = 0..%.6g). Hover for exact values.", r.samples.size() - 1,
                        last.elapsed);
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
    if (last.shift_valid)
        ImGui::Text("Final shift from start: (%d, %d, %d) cells", last.dx, last.dy, last.dz);
    else
        ImGui::TextWrapped("Final shift is undefined or ambiguous (flat field or tied correlation peaks).");
    ImGui::TextWrapped("Shift is modulo the grid, for the whole field. Low mismatch is a sampled return, not "
                       "proof of periodicity or stability. Integer-cell alignment can itself produce "
                       "oscillations as a shape moves between cells. No individual organism is tracked.");
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
void GalleryPanel::tick(Runner &runner, std::array<ViewSettings, 2> &views) {
    try {
        if (ready(capture)) {
            captured = capture.get();
            Renderer preview(gpu);
            preview.upload(*captured);
            preview.draw(capture_views[captured->config.depth > 1], 384, 288);
            captured_preview = preview.capture_bmp();
            GalleryPreview pixels{384, 288, {captured_preview.begin() + 54, captured_preview.end()}};
            texture(pixels);
            set_notes({std::string(captured->config.depth > 1 ? "3D pattern" : "2D pattern") + " at step " +
                           std::to_string(captured->steps),
                       "", ""});
            message = "Captured at a step boundary. Simulation is paused.";
        }
        if (ready(work)) {
            auto result = work.get();
            auto completed = job;
            job = Job::None;
            if (completed == Job::Scan || completed == Job::Save || completed == Job::Edit)
                catalog = std::move(result.catalog);
            if (completed == Job::Scan && !editing) {
                auto found = std::find_if(catalog.entries.begin(), catalog.entries.end(),
                                          [&](const auto &e) { return selected && e.id == selected->id; });
                if (found == catalog.entries.end())
                    found = catalog.entries.begin();
                if (found != catalog.entries.end())
                    select(found->id);
                else {
                    selected.reset();
                    image.Reset();
                }
            }
            if (result.entry)
                selected = std::move(result.entry);
            if (completed == Job::Save)
                shape.reset();
            if (result.shape) {
                shape = result.shape;
                message = "Measured saved field; no evolution was run.";
            }
            if ((completed == Job::Select || completed == Job::Save) && selected) {
                if (completed == Job::Save) {
                    studies = {};
                    study_index = 0;
                    study_settings_open = true;
                }
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
                if (completed == Job::Save)
                    batches.clear();
                batch_settings_open = true;
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
                }
                const auto &b = batches.at(batch_index).result;
                message = "Batch " + b.status + ": " + std::to_string(b.runs.size()) + " saved runs.";
                if (!b.error.empty())
                    message += " " + b.error;
            }
            if (result.studies && selected && selected->id == result.studies_source) {
                studies = std::move(*result.studies);
                study_index = 0;
                study_settings_open = studies.entries.empty();
            }
            if (result.preview)
                texture(*result.preview);
            if (completed == Job::Select && !result.catalog.errors.empty())
                message = result.catalog.errors.front();
            if (completed == Job::Save || completed == Job::Edit) {
                saved_id = selected->id;
                editing = false;
                editing_id.clear();
                captured.reset();
                captured_preview.clear();
                message = completed == Job::Save ? "Saved an independent gallery entry."
                                                 : "Updated notes; saved field unchanged.";
            }
            if (result.state)
                restore = runner.restore(std::move(*result.state));
            if (result.report) {
                pending_study = std::move(result.report);
                save_study();
            }
            if (completed == Job::SaveStudy) {
                pending_study.reset();
                message = "Behavior report saved; source field and annotations unchanged.";
            }
        }
        if (ready(restore)) {
            restore.get();
            views[restore_mode] = restore_view;
            opened = false;
            message = "Loaded saved field and view; paused.";
        }
    } catch (const std::exception &e) {
        message = e.what();
        if (job == Job::Save || job == Job::Edit || job == Job::SaveStudy)
            write_error = "Gallery save failed: " + message;
        job = Job::None;
        if (smoke_phase)
            throw;
    }
}
void GalleryPanel::draw(Runner &runner, const std::array<ViewSettings, 2> &views, float dpi, bool closing) {
    if (!opened) {
        if (ImGui::BeginPopupModal("Pattern gallery", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return;
    }
    auto screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({std::min(980 * dpi, screen.x * .94f), std::min(780 * dpi, screen.y * .92f)},
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({screen.x * .5f, screen.y * .5f}, ImGuiCond_Appearing, {.5f, .5f});
    ImGui::OpenPopup("Pattern gallery");
    if (!ImGui::BeginPopupModal("Pattern gallery", nullptr, ImGuiWindowFlags_NoSavedSettings))
        return;
    if (closing) {
        opened = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::BeginDisabled(busy() || closing || bool(pending_study));
    if (!editing) {
        ImGui::BeginDisabled(!writable);
        if (ImGui::Button("Add current field"))
            begin_capture(runner, views);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            scan();
        ImGui::SameLine();
        if (ImGui::Button("Close")) {
            opened = false;
            ImGui::CloseCurrentPopup();
        }
    } else {
        if (ImGui::Button("Cancel")) {
            editing = false;
            write_error.clear();
            captured.reset();
            captured_preview.clear();
            if (selected)
                select(selected->id);
        }
    }
    ImGui::EndDisabled();
    if (!writable)
        ImGui::TextWrapped("Gallery is read-only in this window.");
    if (busy())
        ImGui::TextColored({.38f, .85f, .77f, 1}, "Working...");
    else
        ImGui::TextWrapped("%s", message.c_str());
    ImGui::Separator();
    if (pending_study && !busy()) {
        if (ImGui::Button("Retry saving study"))
            save_study();
        ImGui::SameLine();
        if (ImGui::Button("Discard unsaved study")) {
            pending_study.reset();
            write_error.clear();
        }
    }
    if (editing) {
        ImGui::BeginDisabled(busy() || closing || bool(pending_study));
        ImGui::BeginChild("Annotation", {0, 0}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        if (image)
            ImGui::Image((ImTextureID)image.Get(),
                         {std::min(300 * dpi, ImGui::GetContentRegionAvail().x),
                          std::min(300 * dpi, ImGui::GetContentRegionAvail().x) / image_aspect});
        ImGui::TextDisabled("Title");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##title", title, sizeof title);
        ImGui::TextDisabled("Tags");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##tags", tags, sizeof tags);
        ImGui::TextDisabled("Notes");
        ImGui::InputTextMultiline("##notes", notes, sizeof notes, {-1, 100 * dpi});
        ImGui::Checkbox("Reference (your assessment)", &reference);
        ImGui::SetNextItemWidth(160 * dpi);
        ImGui::InputDouble("Observed simulation time", &observed_time, 0, 0, "%.4f");
        ImGui::TextWrapped("Zero means unrecorded. This is your observation duration, not the field's age. "
                           "Labels are not verified automatically.");
        ImGui::BeginDisabled(title[0] == '\0' || (!captured && editing_id.empty()));
        if (ImGui::Button(editing_id.empty() ? "Save entry" : "Save notes"))
            save();
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::EndDisabled();
    } else {
        ImGui::SetNextItemWidth(220 * dpi);
        ImGui::InputTextWithHint("##filter", "Search title, tags or notes", filter, sizeof filter);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140 * dpi);
        ImGui::Combo("##dimensions", &dimension_filter, "All dimensions\0 2D only\0 3D only\0");
        float left = std::min(280 * dpi, ImGui::GetContentRegionAvail().x * .38f);
        ImGui::BeginChild("Entries", {left, 0}, ImGuiChildFlags_Borders);
        ImGui::TextDisabled("%zu saved entries", catalog.entries.size());
        auto query = lower(filter);
        ImGui::BeginDisabled(busy() || closing || bool(pending_study));
        for (const auto &e : catalog.entries) {
            if (dimension_filter && (e.state.config.depth > 1 ? 2 : 1) != dimension_filter)
                continue;
            if (!query.empty() &&
                lower(e.annotation.title + " " + e.annotation.tags + " " + e.annotation.notes).find(query) ==
                    std::string::npos)
                continue;
            ImGui::PushID(e.id.c_str());
            if (ImGui::Selectable(e.annotation.title.c_str(), selected && selected->id == e.id))
                select(e.id);
            ImGui::TextDisabled("%s / %s / step %llu", e.state.config.depth > 1 ? "3D" : "2D",
                                e.annotation.reference ? "Reference" : "Candidate",
                                static_cast<unsigned long long>(e.state.steps));
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        if (catalog.entries.empty())
            ImGui::TextWrapped("Capture a field you want to keep. To import a checkpoint, load it in the "
                               "laboratory, then add it here.");
        for (const auto &error : catalog.errors)
            ImGui::TextWrapped("Skipped: %s", error.c_str());
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Entry details", {0, 0}, ImGuiChildFlags_Borders);
        if (selected) {
            auto &e = *selected;
            auto &s = e.state;
            auto &c = s.config;
            ImGui::TextWrapped("%s", e.annotation.title.c_str());
            if (ImGui::BeginTabBar("Entry tabs")) {
                if (ImGui::BeginTabItem("Pattern")) {
                    if (image) {
                        float w = std::min(ImGui::GetContentRegionAvail().x, 360 * dpi);
                        ImGui::Image((ImTextureID)image.Get(), {w, w / image_aspect});
                    }
                    ImGui::BeginDisabled(busy() || closing || bool(pending_study));
                    if (ImGui::Button("Load field + view"))
                        load(runner);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Loads paused and becomes the starting field for Reset. CPU is selected if "
                            "required to preserve saved float64 values exactly. The other simulation "
                            "session is retained.");
                    if (ImGui::GetContentRegionAvail().x > 380 * dpi)
                        ImGui::SameLine();
                    ImGui::BeginDisabled(!writable);
                    if (ImGui::Button("Edit notes and labels")) {
                        editing_id = e.id;
                        set_notes(e.annotation);
                        editing = true;
                    }
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                    ImGui::TextWrapped("%s", e.annotation.tags.c_str());
                    ImGui::TextWrapped("%s", e.annotation.notes.c_str());
                    ImGui::Separator();
                    ImGui::Text("%d x %d x %d   R = %d", c.width, c.height, c.depth, c.radius);
                    ImGui::Text("%s   mu %.6g   sigma %.6g",
                                c.rule == Rule::Classic ? "Classic" : "Smooth reaction", c.mu, c.sigma);
                    ImGui::Text("dt %.6g   sharpness %.6g", c.dt, c.smoothness);
                    ImGui::Text("Step %llu   t = %.6g", static_cast<unsigned long long>(s.steps), s.time);
                    ImGui::Text("Mean %.6g   range %.6g - %.6g", e.measures.mean, e.measures.minimum,
                                e.measures.maximum);
                    ImGui::TextWrapped("Captured with: %s", s.backend.c_str());
                    ImGui::Text("Seed %llu", static_cast<unsigned long long>(s.seed));
                    if (!s.edits.empty() && ImGui::CollapsingHeader("Field construction"))
                        ImGui::TextUnformatted(s.edits.c_str());
                    if (s.initialization) {
                        auto &a = *s.initialization;
                        ImGui::TextWrapped("%d patches; sides %.3g - %.3g x R%d; values %.3g - %.3g",
                                           a.patches, a.size_min, a.size_max, s.initialization_radius,
                                           a.value_min, a.value_max);
                        ImGui::Text("Placement: %s", seed_layout_name(a.layout));
                        if (a.layout == SeedLayout::Cluster)
                            ImGui::Text("Cluster radius %.3g R", a.spread);
                        if (a.layout == SeedLayout::Pair)
                            ImGui::TextWrapped("Centre separation %.3g R; angle %.3g deg; elevation %.3g deg",
                                               a.spread, a.angle, a.elevation);
                    } else
                        ImGui::TextDisabled("Seed recipe unknown");
                    ImGui::Text("Observed for %.6g simulation time (reported)", e.annotation.observed_time);
                    auto t = std::time_t(e.created);
                    auto tm = std::gmtime(&t);
                    if (tm) {
                        char stamp[64];
                        std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M UTC", tm);
                        ImGui::TextDisabled("Saved %s", stamp);
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Behavior study", nullptr,
                                        smoke_phase >= 6 && smoke_phase < 9 ? ImGuiTabItemFlags_SetSelected
                                                                            : 0)) {
                    draw_study(dpi, closing);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Shape", nullptr,
                                        smoke_phase >= 17 ? ImGuiTabItemFlags_SetSelected : 0)) {
                    draw_shape(dpi, closing);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Batches", nullptr,
                                        smoke_phase >= 9 && smoke_phase < 17 ? ImGuiTabItemFlags_SetSelected
                                                                             : 0)) {
                    draw_batch(dpi, closing);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndPopup();
}
bool GalleryPanel::smoke(Runner &runner, const std::array<ViewSettings, 2> &views) {
    if (!writable)
        throw std::runtime_error("Gallery smoke requires a writable isolated workspace");
    if (busy())
        return false;
    if ((smoke_phase == 5 || smoke_phase == 8 || smoke_phase == 12 || smoke_phase == 14 ||
         smoke_phase == 16) &&
        !ready(smoke_check))
        return false;
    std::ofstream("gallery-smoke.log", std::ios::app)
        << GetTickCount64() << " gallery phase " << smoke_phase << '\n';
    switch (smoke_phase) {
    case 0:
        opened = true;
        begin_capture(runner, views);
        ++smoke_phase;
        break;
    case 1:
        if (!captured)
            throw std::runtime_error("Gallery smoke capture failed: " + message);
        smoke_signature = signature(*captured);
        set_notes({"Smoke reference", "test, periodic", "Exact captured field\nwith saved view", true, 12.5});
        save();
        ++smoke_phase;
        break;
    case 2:
        if (saved_id.empty())
            throw std::runtime_error("Gallery smoke save failed: " + message);
        scan();
        ++smoke_phase;
        break;
    case 3:
        if (!selected || !image)
            throw std::runtime_error("Gallery smoke preview failed: " + message);
        load(runner);
        ++smoke_phase;
        break;
    case 4:
        smoke_check = runner.capture_current();
        ++smoke_phase;
        break;
    case 5:
        if (!ready(smoke_check))
            return false;
        if (signature(smoke_check.get()) != smoke_signature)
            throw std::runtime_error("Gallery restore changed captured field");
        opened = true;
        study_options = {7, 3, .01};
        begin_study();
        ++smoke_phase;
        break;
    case 6:
        if (studies.entries.size() != 1 || studies.entries[0].report.completed_steps != 7 ||
            studies.entries[0].report.samples.size() != 4)
            throw std::runtime_error("Gallery smoke study failed: " + message);
        scan(); // Re-read both the entry and its persisted report.
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
        batch_plan.axis = BatchAxis::Spread;
        batch_plan.reseed = true;
        batch_plan.repetitions = 1;
        batch_plan.first_seed = 216;
        batch_plan.observation = {5, 2, .01};
        std::snprintf(batch_value_text, sizeof batch_value_text, "0.8, 1.5");
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
        scan();
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
        load(runner);
        ++smoke_phase;
        break;
    case 15:
        smoke_check = runner.capture_current();
        ++smoke_phase;
        break;
    case 16:
        if (signature(smoke_check.get()) != smoke_signature)
            throw std::runtime_error("Could not return to gallery source after batch inspection");
        opened = true;
        begin_shape();
        ++smoke_phase;
        break;
    case 17:
        if (!shape || !studies.entries[0].report.samples.front().morphology ||
            morphology_csv(shape->shape) !=
                morphology_csv(studies.entries[0].report.samples.front().morphology))
            throw std::runtime_error("Saved shape measurements disagree with the initial study sample");
        ++smoke_phase;
        return true;
    default:
        return true;
    }
    return false;
}
} // namespace lenia
