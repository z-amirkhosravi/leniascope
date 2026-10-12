#include "editor_panel.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lenia {
namespace {
template <class T> bool ready(std::future<T> &f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
int wrap(int v, int n) {
    return (v % n + n) % n;
}
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace
EditorPanel::EditorPanel(std::shared_ptr<GpuContext> gpu) : preview(std::move(gpu)) {}
bool EditorPanel::busy() const {
    return capture.valid() || applying.valid();
}
void EditorPanel::open(Runner &runner, const ViewSettings &settings) {
    if (opened || busy())
        return;
    capture = runner.capture_current();
    opened = true;
    message.clear();
    draft.reset();
    tool = 0;
    dragging = false;
    view = settings;
    view.view = 0;
    view.axis = 2;
    view.zoom = 1;
}
void EditorPanel::tick() {
    try {
        if (ready(capture)) {
            auto state = capture.get();
            float32 = state.backend.find("GPU") == 0;
            draft = std::make_unique<FieldDraft>(std::move(state), float32);
            const auto &c = draft->state().config;
            origin[0] = wrap(int(std::floor(view.offset[0] * c.width)), c.width);
            origin[1] = wrap(int(std::floor(view.offset[1] * c.height)), c.height);
            selection = {0, 0, std::min(c.radius * 2, c.width), std::min(c.radius * 2, c.height)};
            center[0] = c.width * .5;
            center[1] = c.height * .5;
            patch_size[0] = patch_size[1] = c.radius;
            if (clipboard) {
                try {
                    transform();
                } catch (const std::exception &) {
                    rotated.reset();
                }
            }
            upload = true;
            message = "Draft ready. Select an organism or make a random patch.";
        }
        if (ready(applying)) {
            applying.get();
            opened = false;
            draft.reset();
        }
    } catch (const std::exception &e) {
        message = e.what();
        if (smoke_phase)
            throw;
    }
}
void EditorPanel::copy() {
    clipboard = copy_patch(draft->state(), selection);
    angle = 0;
    center[0] = selection.x + (selection.width - 1) * .5;
    center[1] = selection.y + (selection.height - 1) * .5;
    transform();
    tool = 1;
    upload = true;
    message = "Copied. Move over the field to preview; click to place a copy.";
}
void EditorPanel::transform() {
    rotated.reset();
    upload = true;
    if (!clipboard)
        return;
    auto next = rotate_patch(*clipboard, angle);
    const auto &c = draft->state().config;
    if (next.width > c.width || next.height > c.height) {
        rotated.reset();
        upload = true;
        throw std::invalid_argument(
            "Rotated copy is larger than the grid. Select a smaller region or change the angle.");
    }
    rotated = std::move(next);
    upload = true;
}
std::pair<int, int> EditorPanel::anchor() const {
    check(rotated && draft, "Copy a selection or create a patch first");
    const auto &c = draft->state().config;
    check(std::isfinite(center[0]) && std::isfinite(center[1]), "Placement coordinates must be finite");
    return {wrap(int(std::round(std::fmod(std::fmod(center[0], c.width) + c.width, c.width) -
                                (rotated->width - 1) * .5)),
                 c.width),
            wrap(int(std::round(std::fmod(std::fmod(center[1], c.height) + c.height, c.height) -
                                (rotated->height - 1) * .5)),
                 c.height)};
}
void EditorPanel::stamp() {
    auto [x, y] = anchor();
    draft->stamp(*rotated, x, y, PatchBlend(blend));
    upload = true;
    message = "Placed in the draft. Apply field when the construction is ready.";
}
void EditorPanel::apply(Runner &runner) {
    if (!draft || busy())
        return;
    if (!draft->changed()) {
        discard();
        return;
    }
    applying = runner.restore(draft->result());
}
void EditorPanel::discard() {
    if (busy())
        return;
    opened = false;
    draft.reset();
    dragging = false;
}
void EditorPanel::draw(Runner &runner, float dpi, bool closing) {
    if (!opened) {
        if (ImGui::BeginPopupModal("Edit 2D field", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return;
    }
    auto screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({screen.x * .96f, screen.y * .94f}, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({screen.x * .5f, screen.y * .5f}, ImGuiCond_Appearing, {.5f, .5f});
    ImGui::OpenPopup("Edit 2D field");
    if (!ImGui::BeginPopupModal("Edit 2D field", nullptr, ImGuiWindowFlags_NoSavedSettings))
        return;
    ImGui::TextWrapped(
        closing ? "Finish or discard this draft to close the application."
                : "Paused draft. Apply starts a new experiment at t=0; Discard keeps the original field.");
    auto action = [&](auto &&f) {
        try {
            f();
        } catch (const std::exception &e) {
            message = e.what();
        }
    };
    ImGui::BeginDisabled(busy());
    ImGui::BeginDisabled(!draft);
    if (ImGui::Button("Apply field"))
        action([&] { apply(runner); });
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Discard draft"))
        discard();
    if (draft) {
        ImGui::SameLine();
        ImGui::BeginDisabled(!draft->undo_count());
        if (ImGui::Button("Undo"))
            action([&] {
                draft->undo();
                upload = true;
            });
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!draft->redo_count());
        if (ImGui::Button("Redo"))
            action([&] {
                draft->redo();
                upload = true;
            });
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", busy() ? "Preparing field..." : message.c_str());
    if (!draft) {
        ImGui::EndPopup();
        return;
    }
    ImGui::Separator();
    const auto &c = draft->state().config;
    ImGui::BeginDisabled(busy());
    ImGui::BeginChild("Edit tools", {std::min(300 * dpi, screen.x * .30f), 0}, ImGuiChildFlags_Borders);
    ImGui::PushItemWidth(-1);
    try {
        if (ImGui::RadioButton("Select region", tool == 0)) {
            tool = 0;
            upload = true;
        }
        if (ImGui::RadioButton("Place copy / patch", tool == 1)) {
            tool = 1;
            upload = true;
        }
        if (tool == 0) {
            ImGui::TextDisabled("SELECTION (cells)");
            int region[4]{selection.x, selection.y, selection.width, selection.height};
            ImGui::TextDisabled("Top-left X, Y");
            bool changed = ImGui::InputInt2("##select-origin", region);
            ImGui::TextDisabled("Width, height");
            changed |= ImGui::InputInt2("##select-size", region + 2);
            if (changed)
                selection = {region[0], region[1], region[2], region[3]};
            if (ImGui::Button("Copy selection", {-1, 0}))
                copy();
            if (ImGui::Button("Erase selection", {-1, 0})) {
                draft->erase(selection);
                upload = true;
            }
            if (ImGui::Button("Keep selection only", {-1, 0})) {
                draft->erase(selection, true);
                upload = true;
            }
            if (ImGui::Button("Clear field", {-1, 0})) {
                draft->clear();
                upload = true;
            }
        }
        if (tool == 1 && clipboard) {
            ImGui::Separator();
            ImGui::TextDisabled("PLACEMENT");
            ImGui::TextDisabled("Clockwise degrees");
            if (ImGui::InputDouble("##clockwise", &angle, 1, 15, "%.2f"))
                transform();
            if (ImGui::Button("Rotate 90 degrees", {-1, 0})) {
                angle = std::fmod(angle + 90., 360.);
                transform();
            }
            ImGui::TextWrapped(
                "Right angles preserve cells. Other angles interpolate from the original copy.");
            ImGui::TextDisabled("Centre X");
            bool move = ImGui::InputDouble("##centre-x", &center[0], 1, c.radius, "%.2f");
            ImGui::TextDisabled("Centre Y");
            move |= ImGui::InputDouble("##centre-y", &center[1], 1, c.radius, "%.2f");
            if (move) {
                tool = 1;
                upload = true;
            }
            ImGui::TextDisabled("Overlap");
            if (ImGui::Combo("##overlap", &blend, "Maximum\0Replace\0"))
                upload = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Maximum preserves existing values where they are higher.\n"
                                  "Replace overwrites the covered footprint, including copied zeros.");
            ImGui::BeginDisabled(!rotated);
            if (ImGui::Button("Place at coordinates", {-1, 0}))
                action([&] { stamp(); });
            ImGui::EndDisabled();
            if (rotated) {
                auto [x, y] = anchor();
                ImGui::Text("Snapped centre: %.1f, %.1f", std::fmod(x + (rotated->width - 1) * .5, c.width),
                            std::fmod(y + (rotated->height - 1) * .5, c.height));
                ImGui::Text("Footprint: %d x %d", rotated->width, rotated->height);
            }
        }
        if (tool == 1 && !clipboard)
            ImGui::TextWrapped("Copy a selection first, or make a random patch below.");
        if (ImGui::CollapsingHeader("Make random patch")) {
            ImGui::TextDisabled("Shape");
            ImGui::Combo("##shape", &shape, "Rectangle\0Ellipse\0");
            ImGui::TextDisabled("Width, height (cells)");
            ImGui::InputInt2("##patch-size", patch_size);
            ImGui::TextDisabled("Minimum value");
            ImGui::InputDouble("##min-value", &value_min, 0, 0, "%.3f");
            ImGui::TextDisabled("Maximum value");
            ImGui::InputDouble("##max-value", &value_max, 0, 0, "%.3f");
            ImGui::TextDisabled("Patch seed");
            ImGui::InputScalar("##patch-seed", ImGuiDataType_U64, &patch_seed);
            if (ImGui::Button("Make patch for placement", {-1, 0})) {
                auto p = random_patch(patch_size[0], patch_size[1], PatchShape(shape), patch_seed, value_min,
                                      value_max);
                check(p.width <= c.width && p.height <= c.height, "Patch is larger than the grid");
                clipboard = std::move(p);
                angle = 0;
                transform();
                tool = 1;
                upload = true;
                message = "Patch ready. Click the field to place it; the seed controls its contents.";
            }
        }
        if (ImGui::CollapsingHeader("View origin")) {
            if (ImGui::InputInt2("##edit-origin", origin)) {
                origin[0] = wrap(origin[0], c.width);
                origin[1] = wrap(origin[1], c.height);
            }
            if (ImGui::Button("Centre selection in view", {-1, 0})) {
                selection.validate(c);
                origin[0] = wrap(wrap(selection.x, c.width) + selection.width / 2 - c.width / 2, c.width);
                origin[1] = wrap(wrap(selection.y, c.height) + selection.height / 2 - c.height / 2, c.height);
            }
            ImGui::TextWrapped(
                "Only changes the view. Selections and placements wrap across periodic edges.");
        }
        if (ImGui::CollapsingHeader("Construction log"))
            ImGui::TextUnformatted(draft->operations().c_str());
        ImGui::TextWrapped(
            "Edits use %s. Undo retains up to 128 MiB / 32 operations; unapplied drafts are not autosaved.",
            float32 ? "float32, matching the GPU field" : "float64");
    } catch (const std::exception &e) {
        message = e.what();
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Edit scene", {0, 0}, ImGuiChildFlags_Borders);
    try {
        auto available = ImGui::GetContentRegionAvail();
        float height = std::max(1.f, std::min(available.y - 50 * dpi, available.x * c.height / c.width));
        ImVec2 size{float(std::max(1, int(height * c.width / c.height))), float(std::max(1, int(height)))};
        auto at = ImGui::GetCursorScreenPos();
        // Match the renderer's slice projection exactly, including aspect-ratio rounding.
        auto mouse = ImGui::GetMousePos();
        int mx = std::clamp(
            int(std::floor(std::clamp((mouse.x - at.x - size.x * .5f) * c.height / size.y + c.width * .5f,
                                      0.f, float(c.width - 1)))),
            0, c.width - 1);
        int my = std::clamp(
            int(std::floor(std::clamp((mouse.y - at.y) * c.height / size.y, 0.f, float(c.height - 1)))), 0,
            c.height - 1);
        ImGui::InvisibleButton("Field canvas", size);
        if (!busy()) {
            bool hovered = ImGui::IsItemHovered();
            if (tool == 0) {
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    dragging = true;
                    drag_start[0] = mx;
                    drag_start[1] = my;
                }
                if (dragging) {
                    selection = {wrap(std::min(mx, drag_start[0]) + origin[0], c.width),
                                 wrap(std::min(my, drag_start[1]) + origin[1], c.height),
                                 std::abs(mx - drag_start[0]) + 1, std::abs(my - drag_start[1]) + 1};
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        dragging = false;
                }
            } else if (hovered && rotated && !smoke_phase) {
                double x = wrap(mx + origin[0], c.width), y = wrap(my + origin[1], c.height);
                if (center[0] != x || center[1] != y) {
                    center[0] = x;
                    center[1] = y;
                    upload = true;
                }
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    stamp();
            }
            auto &io = ImGui::GetIO();
            if (!io.WantTextInput && io.KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_C))
                    copy();
                if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
                    draft->undo();
                    upload = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
                    draft->redo();
                    upload = true;
                }
            }
        }
        if (upload) {
            auto state = draft->state();
            if (tool == 1 && rotated) {
                auto [x, y] = anchor();
                place_patch(state, *rotated, x, y, PatchBlend(blend), float32);
            }
            preview.upload(state);
            upload = false;
        }
        view.offset[0] = float(origin[0]) / c.width;
        view.offset[1] = float(origin[1]) / c.height;
        auto image = preview.draw(view, int(size.x), int(size.y));
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)image, at, {at.x + size.x, at.y + size.y});
        Region2D box = selection;
        if (tool == 1 && rotated) {
            auto [x, y] = anchor();
            box = {x, y, rotated->width, rotated->height};
        }
        if (box.width > 0 && box.width <= c.width && box.height > 0 && box.height <= c.height) {
            auto *draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(at, {at.x + size.x, at.y + size.y}, true);
            int x = wrap(wrap(box.x, c.width) - origin[0], c.width),
                y = wrap(wrap(box.y, c.height) - origin[1], c.height);
            float scale = size.y / c.height;
            for (int dy : {-c.height, 0, c.height})
                for (int dx : {-c.width, 0, c.width}) {
                    ImVec2 first{at.x + size.x * .5f + (x + dx - c.width * .5f) * scale,
                                 at.y + (y + dy) * scale};
                    draw->AddRect(
                        first, {first.x + box.width * scale, first.y + box.height * scale},
                        ImGui::GetColorU32(tool == 0 ? ImVec4{1, .8f, .25f, 1} : ImVec4{.38f, .85f, .77f, 1}),
                        0, 0, 2 * dpi);
                }
            draw->PopClipRect();
        }
        ImGui::TextWrapped(tool == 0
                               ? "Drag a rectangle. Ctrl+C copies; Ctrl+Z / Ctrl+Y undo / redo."
                               : "Preview only until placed. Click to stamp; Apply field commits the draft.");
    } catch (const std::exception &e) {
        message = e.what();
    }
    ImGui::EndChild();
    ImGui::EndDisabled();
    ImGui::EndPopup();
}
bool EditorPanel::smoke(Runner &runner, const ViewSettings &settings) {
    if (busy())
        return false;
    switch (smoke_phase) {
    case 0:
        open(runner, settings);
        ++smoke_phase;
        break;
    case 1:
        check(bool(draft), "Editor failed to capture field");
        smoke_source = draft->state();
        draft->clear();
        discard(); // Cancellation must not mutate the simulation.
        verification = runner.capture_current();
        ++smoke_phase;
        break;
    case 2:
        if (!ready(verification))
            return false;
        check(verification.get().values == smoke_source.values, "Discard changed the live field");
        open(runner, settings);
        smoke_phase = 3;
        smoke_frames = 0;
        break;
    case 3:
        check(bool(draft), "Editor failed to reopen");
        if (smoke_frames == 0) {
            const auto &c = draft->state().config;
            selection = {c.width - 1, c.height - 1, 3, 2};
            copy();
            angle = 90;
            transform();
            draft->clear();
            auto cleared = draft->state().values;
            center[0] = c.width - 1;
            center[1] = c.height - 1;
            stamp();
            auto placed = draft->state().values;
            draft->undo();
            check(draft->state().values == cleared, "Editor undo failed");
            draft->redo();
            check(draft->state().values == placed, "Editor redo failed");
            clipboard = random_patch(3, 3, PatchShape::Ellipse, 718, .2, .8);
            angle = 17;
            transform();
            center[0] = center[1] = 2;
            stamp();
            smoke_expected = draft->result();
            save_state(smoke_expected, "editor-expected.lenia");
            tool = 1;
            upload = true;
        }
        if (++smoke_frames < 5)
            return false;
        preview.export_bmp("editor-preview.bmp");
        apply(runner);
        ++smoke_phase;
        break;
    case 4:
        verification = runner.capture_current();
        ++smoke_phase;
        break;
    case 5:
        if (!ready(verification))
            return false;
        {
            auto actual = verification.get();
            check(actual.values == smoke_expected.values && actual.edits == smoke_expected.edits &&
                      actual.steps == 0 && actual.time == 0 && !actual.initialization,
                  "Editor apply changed field values or lost construction history");
        }
        ++smoke_phase;
        return true;
    default:
        return true;
    }
    return false;
}
} // namespace lenia
