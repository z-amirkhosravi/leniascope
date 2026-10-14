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
    saved.yaw = -.6f;
    saved.threshold = .6f;
    saved.offset[0] = .3f;
    ViewerOptions options;
    options.components = true;
    options.measured_threshold = .1f;
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1000, 800};
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
        ImGui::Begin("Harness", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginChild("Scrollable host", {900, 600});
        left = ImGui::GetCursorScreenPos();
        draw_viewer("Live", a, field.config, live, size, 1);
        ImGui::SameLine();
        right = ImGui::GetCursorScreenPos();
        draw_viewer("Saved", b, field.config, saved, size, 1, options);
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
    // Actual footer clicks open distinct popup IDs for the two instances.
    click(left.x + size.x - 15, left.y + size.y - 12);
    require(GImGui->OpenPopupStack.Size == 1, "Live View menu did not open");
    auto live_popup = GImGui->OpenPopupStack[0].PopupId;
    auto popup = GImGui->OpenPopupStack[0].Window;
    require(popup, "View menu missing its window");
    const float before = live.distance;
    move(popup->Pos.x + 30, popup->Pos.y + 25);
    io.AddMouseWheelEvent(0, -1);
    frame();
    frame();
    require(live.distance == before && saved.distance == 1.9f, "Wheel over popup leaked into a camera");
    escape();
    require(GImGui->OpenPopupStack.empty(), "Escape did not close the View menu");
    click(right.x + size.x - 15, right.y + size.y - 12);
    require(GImGui->OpenPopupStack.Size == 1 && GImGui->OpenPopupStack[0].PopupId != live_popup,
            "Two viewers shared a popup identity");
    escape();
    // Choose the first mode (Slice) through the real combo, then zoom this instance only.
    click(right.x + 40, right.y + size.y - 12);
    require(GImGui->OpenPopupStack.Size == 1, "Mode selector did not open");
    frame();
    popup = GImGui->OpenPopupStack[0].Window;
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
            "Gallery comparison did not retain its measured threshold independently");
    ImGui::DestroyContext();
}
} // namespace
int main() {
    try {
        tests();
        std::cout << "PASS shared viewer: independent cameras/popups, orbit, wheel capture, mode/slice "
                     "controls, Escape, persistence limits and locked gallery comparison\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
