#include "gallery_panel.h"
#include "imgui.h"
#include "study_panel.h"
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
    if (work.valid())
        work.wait();
}
bool GalleryPanel::busy() const {
    return work.valid() || capture.valid() || restore.valid();
}
void GalleryPanel::open() {
    opened = true;
    if (!busy())
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
    message.clear();
    job = Job::Select;
    work = std::async(std::launch::async, [path = root, id] {
        Result r;
        r.entry = read_gallery_entry(path, id);
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
        }
        if (ready(restore)) {
            restore.get();
            views[restore_mode] = restore_view;
            opened = false;
            message = "Loaded saved field and view; paused.";
        }
    } catch (const std::exception &e) {
        message = e.what();
        if (job == Job::Save || job == Job::Edit)
            write_error = "Gallery save failed: " + message;
        job = Job::None;
        if (smoke_phase)
            throw;
    }
}
void GalleryPanel::draw(Runner &runner, const std::array<ViewSettings, 2> &views, StudyPanel &study,
                        float dpi, bool closing) {
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
    ImGui::BeginDisabled(busy() || closing);
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
    if (editing) {
        ImGui::BeginDisabled(busy() || closing);
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
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Your observation duration, not the field's age. Zero means unrecorded.");
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
        ImGui::BeginDisabled(busy() || closing);
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
            ImGui::TextDisabled("No saved patterns.");
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

            if (image) {
                float w = std::min(ImGui::GetContentRegionAvail().x, 360 * dpi);
                ImGui::Image((ImTextureID)image.Get(), {w, w / image_aspect});
            }
            ImGui::BeginDisabled(busy() || closing);
            if (ImGui::Button("Load field + view"))
                load(runner);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Loads paused and becomes the starting field for Reset. CPU is selected if "
                                  "required to preserve saved float64 values exactly. The other simulation "
                                  "session is retained.");
            ImGui::BeginDisabled(!study.can_change_source());
            if (ImGui::Button("Load and study")) {
                study.open_source(root, e.id);
                opened = false;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("Load this pattern into the current workspace and open Study.");
            ImGui::EndDisabled();
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
            ImGui::Text("%s   mu %.6g   sigma %.6g", c.rule == Rule::Classic ? "Classic" : "Smooth reaction",
                        c.mu, c.sigma);
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
                ImGui::TextWrapped("%d patches; sides %.3g - %.3g x R%d; values %.3g - %.3g", a.patches,
                                   a.size_min, a.size_max, s.initialization_radius, a.value_min, a.value_max);
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
        }
        ImGui::EndChild();
    }
    ImGui::EndPopup();
}
bool GalleryPanel::smoke(Runner &runner, const std::array<ViewSettings, 2> &views, StudyPanel &study) {
    if (!writable)
        throw std::runtime_error("Gallery smoke requires a writable isolated workspace");
    if (busy())
        return false;
    if (smoke_phase == 5 && !ready(smoke_check))
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
        if (!study.can_change_source())
            return false;
        if (!ready(smoke_check))
            return false;
        if (signature(smoke_check.get()) != smoke_signature)
            throw std::runtime_error("Gallery restore changed captured field");
        study.open_source(root, selected->id);
        ++smoke_phase;
        break;
    default:
        return study.smoke(runner, views);
    }
    return false;
}
} // namespace lenia
