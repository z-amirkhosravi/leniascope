#include "imgui_internal.h"
#include "src/viewer.h"
#include "src/workspace.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace lenia;
namespace {
void require(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
void tests() {
    // WARP makes the interaction tests independent of a physical GPU or a Windows desktop.
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device,
                            nullptr, &context),
          "Create viewer test device");
    auto gpu = std::make_shared<GpuContext>(device.Get(), context.Get());
    Renderer a(gpu), b(gpu);
    State field = random_state({8, 8, 8, 2}, 72);
    a.upload(field);
    b.upload(field);
    std::vector<uint32_t> labels(field.values.size());
    for (size_t i = 0; i < labels.size(); ++i)
        labels[i] = field.values[i] > .1 ? 1 : 0;
    b.upload_components(field.config, labels);
    ViewSettings live, saved;
    ViewerUi live_ui, saved_ui;
    saved.yaw = -.6f;
    saved.threshold = .6f;
    saved.offset[0] = .3f;
    ViewerOptions options;
    options.components = true;
    options.measured_threshold = .1f;
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1400, 900};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->Build();
    const ImVec2 size{320, 240};
    ImVec2 left, right;
    float scroll = 0;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Harness", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::BeginChild("Scrollable host", {1300, 700});
        left = ImGui::GetCursorScreenPos();
        draw_viewer("Live", a, field.config, live, live_ui, size, 1);
        ImGui::SameLine();
        right = ImGui::GetCursorScreenPos();
        draw_viewer("Saved", b, field.config, saved, saved_ui, size, 1, options);
        ImGui::Dummy({1, 900});
        scroll = ImGui::GetScrollY();
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
    };
    auto move = [&](float x, float y) {
        io.AddMousePosEvent(x, y);
        frame();
    };
    auto click = [&](float x, float y) {
        move(x, y);
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    auto escape = [&] {
        io.AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        frame();
    };
    frame();
    frame();
    require(live.distance == 1.9f && saved.threshold == .6f && saved.offset[0] == .3f,
            "Idle viewer changed settings or the saved comparison threshold");
    move(left.x + 100, left.y + 100);
    io.AddMouseWheelEvent(0, -1);
    frame();
    frame();
    require(live.distance > 1.9f && saved.distance == 1.9f && scroll == 0,
            "Canvas wheel scrolled its host or changed another viewer");
    io.AddMouseButtonEvent(0, true);
    frame();
    move(left.x + 125, left.y + 110);
    io.AddMouseButtonEvent(0, false);
    frame();
    require(live.yaw != .65f && saved.yaw == -.6f, "Orbit did not stay with the active canvas");
    // Use real footer and titlebar input: the image must stay interactive while controls are open.
    auto inspector = [&](const char *prefix) -> ImGuiWindow * {
        for (auto *window : GImGui->Windows)
            if (window->Active && std::string(window->Name).find(prefix) == 0)
                return window;
        throw std::runtime_error(std::string("Missing inspector: ") + prefix);
    };
    auto drag = [&](ImVec2 from, ImVec2 to) {
        move(from.x, from.y);
        io.AddMouseButtonEvent(0, true);
        frame();
        move(to.x, to.y);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    click(left.x + size.x - 15, left.y + size.y - 12);
    require(live_ui.controls_open && GImGui->OpenPopupStack.empty(), "View opened as a blocking popup");
    auto *live_window = inspector("Live view###");
    require(!(live_window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Modal)),
            "Inspector is not a normal floating window");
    drag({live_window->Pos.x + 100, live_window->Pos.y + 10}, {950, 40});
    const auto moved = live_window->Pos;
    require(moved.x > 800, "Inspector titlebar could not be dragged away from the image");
    const float before = live.distance, yaw_before = live.yaw;
    move(left.x + 100, left.y + 100);
    io.AddMouseWheelEvent(0, -1);
    frame();
    drag({left.x + 100, left.y + 100}, {left.x + 130, left.y + 110});
    require(live.distance > before && live.yaw != yaw_before && saved.distance == 1.9f && saved.yaw == -.6f &&
                live_ui.controls_open && scroll == 0,
            "Open inspector blocked canvas orbit/zoom or changed another viewer");
    // Refocusing the canvas must not bury the inspector under its full-size parent.
    const float after = live.distance;
    move(moved.x + 30, moved.y + 50);
    require(GImGui->HoveredWindow == live_window, "Canvas focus covered the inspector");
    io.AddMouseWheelEvent(0, -1);
    frame();
    frame();
    require(live.distance == after && saved.distance == 1.9f && scroll == 0,
            "Wheel over inspector leaked into a camera or host");
    // Cutaway is the first control. Changing it must neither orbit nor zoom the scene.
    const float previous_clip = live.clip, previous_yaw = live.yaw;
    click(moved.x + 70, moved.y + live_window->TitleBarHeight + ImGui::GetStyle().WindowPadding.y + 8);
    require(live.clip != previous_clip && live.yaw == previous_yaw && live.distance == after,
            "Inspector slider did not own its mouse input");
    // A section expands the tool to fit its controls instead of reserving an empty overlay.
    const float collapsed_height = live_window->Size.y;
    const auto &style = ImGui::GetStyle();
    const float origin_header_y = moved.y + live_window->TitleBarHeight + style.WindowPadding.y +
                                  2 * ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight() * .5f;
    click(moved.x + 50, origin_header_y);
    frame();
    require(live_window->Size.y > collapsed_height, "Inspector did not expand with its section");
    click(moved.x + 50, origin_header_y);
    frame();
    require(live_window->Size.y == collapsed_height, "Inspector retained empty space after collapsing");
    click(right.x + size.x - 15, right.y + size.y - 12);
    auto *saved_window = inspector("Saved view###");
    require(saved_ui.controls_open && live_ui.controls_open && saved_window != live_window &&
                GImGui->OpenPopupStack.empty(),
            "Viewers did not retain independent inspectors");
    // Closing one with its titlebar X must leave the other open.
    click(saved_window->Pos.x + saved_window->Size.x - ImGui::GetStyle().FramePadding.x -
              ImGui::GetFontSize() * .5f,
          saved_window->Pos.y + ImGui::GetStyle().FramePadding.y + ImGui::GetFontSize() * .5f);
    require(!saved_ui.controls_open && live_ui.controls_open, "Titlebar close affected the wrong viewer");
    click(left.x + size.x - 15, left.y + size.y - 12);
    require(!live_ui.controls_open, "View button did not toggle the inspector closed");
    click(left.x + size.x - 15, left.y + size.y - 12);
    require(live_ui.controls_open && live_window->Pos.x == moved.x && live_window->Pos.y == moved.y,
            "Reopening the inspector lost its position");
    io.DisplaySize = {1000, 650};
    frame();
    require(live_window->Pos.x >= 0 && live_window->Pos.y >= 0 &&
                live_window->Pos.x + live_window->Size.x <= io.DisplaySize.x &&
                live_window->Pos.y + live_window->Size.y <= io.DisplaySize.y,
            "Inspector became unreachable after shrinking the application");
    io.DisplaySize = {1400, 900};
    frame();
    click(left.x + size.x - 15, left.y + size.y - 12);
    require(!live_ui.controls_open, "Inspector did not close after resize");
    // Escape dismisses a mode menu without dismissing the non-modal inspector.
    click(left.x + size.x - 15, left.y + size.y - 12);
    click(right.x + 40, right.y + size.y - 12);
    require(GImGui->OpenPopupStack.Size == 1, "Mode selector did not open beside the inspector");
    escape();
    require(GImGui->OpenPopupStack.empty() && live_ui.controls_open,
            "Escape did not dismiss only the mode menu");
    click(left.x + size.x - 15, left.y + size.y - 12);
    // Choose the first mode (Slice) through the real combo, then zoom this instance only.
    click(right.x + 40, right.y + size.y - 12);
    require(GImGui->OpenPopupStack.Size == 1, "Mode selector did not open");
    frame();
    auto *popup = GImGui->OpenPopupStack[0].Window;
    require(popup, "Mode menu missing its window");
    // Solid is initially selected. Navigate to Slice using the same keyboard controls as the app.
    io.AddKeyEvent(ImGuiKey_UpArrow, true);
    frame();
    io.AddKeyEvent(ImGuiKey_UpArrow, false);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame();
    if (saved.view != 0 || live.view != 1)
        throw std::runtime_error("Slice selection failed: saved=" + std::to_string(saved.view) +
                                 ", live=" + std::to_string(live.view) + ", popup=" + popup->Name);
    move(right.x + 100, right.y + 100);
    io.AddMouseWheelEvent(0, 100);
    frame();
    require(saved.zoom == 4 && live.zoom == 1 && scroll == 0, "Slice zoom or scroll ownership failed");
    Preferences preferences;
    preferences.views[0].view = 0;
    preferences.views[1] = saved;
    encode_preferences(preferences); // Shared controls must stay inside persisted setting limits.
    // The measured threshold is applied only to rendering, without rewriting the view or field.
    options.components = false;
    saved.view = 1;
    frame();
    auto actual = b.capture_bmp();
    auto expected_view = saved;
    expected_view.threshold = .1f;
    b.draw(expected_view, int(size.x), int(size.y - ImGui::GetFontSize() - 10));
    require(actual == b.capture_bmp() && saved.threshold == .6f,
            "Study comparison did not retain its measured threshold independently");
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        tests();
        std::cout << "PASS shared viewer: non-modal inspectors, move/close/reopen/resize, orbit and "
                     "zoom with controls open, input ownership, mode/slice controls, persistence and locked "
                     "comparison\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
