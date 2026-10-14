#include "imgui_internal.h"
#include "src/desktop_workspace.h"
#include "src/study_panel.h"
#include "src/workspace_ui.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace lenia;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void same_field(const State &a, const State &b, const char *message) {
    require(a.values == b.values && a.steps == b.steps && a.time == b.time && a.seed == b.seed &&
                a.config.width == b.config.width && a.config.depth == b.config.depth &&
                a.config.radius == b.config.radius && a.config.mu == b.config.mu,
            message);
}
} // namespace
void study_ui_tests() {
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() /
                           ("leniascope-study-ui-" +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    } cleanup{directory};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device,
                            nullptr, &context),
          "Create study test device");
    auto gpu = std::make_shared<GpuContext>(device.Get(), context.Get());
    const auto original = random_state({8, 8, 8, 2}, 72);
    const auto inactive = random_state({8, 8, 1, 2}, 13);
    SessionStates states;
    states[0] = std::make_shared<const State>(inactive);
    states[1] = std::make_shared<const State>(original);
    SessionStates initial;
    initial[1] = states[1];
    Runner runner(false, states, true, {}, {}, {}, false, initial);
    std::array<ViewSettings, 2> views;
    views[1].yaw = -.8f;
    views[1].offset[0] = .25f;
    Renderer live(gpu);
    live.upload(original);
    ViewerUi viewer;
    // Reproduce the user's two-window case, then recover the same session after the lock is released.
    auto owner = std::make_unique<WorkspaceAccess>(directory);
    WorkspaceAccess access(directory);
    require(owner->writable() && !access.writable(), "Study test did not start read-only");
    StudyPanel study(directory / "studies", access.writable(), gpu);
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1200, 800};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->Build();
    int tab = 0;
    ImVec2 tab_origin, controls_origin, canvas_min, canvas_max;
    auto frame = [&] {
        study.tick(runner, views);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Study harness", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::BeginChild("Inspector", {320, 700}, ImGuiChildFlags_Borders);
        tab_origin = ImGui::GetCursorScreenPos();
        draw_inspector_tabs(tab);
        controls_origin = ImGui::GetCursorScreenPos();
        if (tab == 1)
            study.draw_controls(runner, views, 1, false);
        else if (study.showing_source())
            study.draw_simulation_actions(false);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Scene", {0, 700}, ImGuiChildFlags_Borders);
        if (study.showing_source())
            study.draw_scene(1, false);
        else {
            ImGui::TextUnformatted("Current simulation");
            ImGui::TextUnformatted("3D LENIA   8 x 8 x 8");
            auto size = ImGui::GetContentRegionAvail();
            size.y -= 118;
            draw_viewer("Simulation", live, original.config, views[1], viewer, size, 1);
        }
        canvas_min = ImGui::GetItemRectMin();
        canvas_max = ImGui::GetItemRectMax();
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
    };
    auto until = [&](auto predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do {
            frame();
            if (predicate())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Study UI timed out waiting for worker");
    };
    auto click = [&](ImVec2 p) {
        io.AddMousePosEvent(p.x, p.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    auto primary = [&] {
        click({controls_origin.x + 100,
               controls_origin.y + ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight() * .5f});
    };
    frame();
    until([&] { return !study.busy() && runner.frame(); });
    const auto original_min = canvas_min, original_max = canvas_max;
    auto before_pixels = live.capture_bmp();
    // Real tab clicks must leave the right pane, camera and paused/running state alone.
    click({tab_origin.x + 110, tab_origin.y + 9});
    require(tab == 1 && !study.showing_source(), "Study tab replaced the current field");
    require(live.capture_bmp() == before_pixels && canvas_min.x == original_min.x &&
                canvas_min.y == original_min.y && canvas_max.x == original_max.x &&
                canvas_max.y == original_max.y && views[1].yaw == -.8f,
            "Panel switch changed the image, camera or viewer size");
    primary();
    require(!study.busy() && list_gallery(directory / "studies").entries.empty(),
            "Disabled snapshot control wrote into a locked workspace");
    owner.reset();
    require(access.retry(), "Workspace access did not recover");
    study.allow_saving();
    primary();
    until([&] { return !study.busy(); });
    require(study.showing_source(), "Take snapshot did not select the captured field");
    require(canvas_min.x == original_min.x && canvas_min.y == original_min.y &&
                canvas_max.x == original_max.x && canvas_max.y == original_max.y,
            "Capturing the current field moved or resized its viewport");
    auto catalog = list_gallery(directory / "studies");
    require(catalog.entries.size() == 1 && !fs::exists(directory / "gallery"),
            "Snapshot capture did not create exactly one separate archive entry");
    auto captured = load_gallery_state(directory / "studies", catalog.entries[0].id);
    same_field(captured, original, "Snapshot changed the paused field");
    require(catalog.entries[0].view.yaw == views[1].yaw &&
                catalog.entries[0].view.offset[0] == views[1].offset[0],
            "Capture lost the camera");
    auto sessions = runner.snapshot_sessions().get();
    same_field(*sessions.states[0], inactive, "Snapshot capture modified the inactive simulation");
    same_field(*sessions.reset_states[1], original, "Snapshot capture modified Reset");
    click({tab_origin.x + 35, tab_origin.y + 9});
    require(tab == 0 && study.showing_source(), "Simulation tab silently replaced the viewed snapshot");
    click({tab_origin.x + 110, tab_origin.y + 9});
    // Choose Current simulation in the actual source menu; selecting a view creates no new snapshot.
    click({controls_origin.x + 100, controls_origin.y + 9});
    require(GImGui->OpenPopupStack.Size == 1, "Study field selector did not open");
    auto *popup = GImGui->OpenPopupStack[0].Window;
    click({popup->Pos.x + 90, popup->Pos.y + ImGui::GetStyle().WindowPadding.y + 7});
    require(!study.showing_source(), "Current simulation did not return to the live field");
    runner.play(true);
    until([&] { return runner.frame()->running && runner.frame()->state.steps > 0; });
    click({tab_origin.x + 35, tab_origin.y + 9});
    click({tab_origin.x + 110, tab_origin.y + 9});
    require(runner.frame()->running, "Changing tabs paused the simulation");
    primary();
    until([&] { return !study.busy(); });
    require(study.showing_source() && !runner.frame()->running, "Capture did not pause a running field");
    catalog = list_gallery(directory / "studies");
    require(catalog.entries.size() == 2, "Explicit second capture did not create a second snapshot");
    captured = load_gallery_state(directory / "studies", catalog.entries[0].id);
    same_field(runner.capture_current().get(), captured, "Capture missed the simulation step boundary");
    // Change the simulation independently; the visible Load action must restore the selected snapshot.
    runner.randomize(original.config, 91);
    auto changed = runner.capture_current().get();
    require(changed.seed == 91, "Test could not prepare a different current simulation");
    primary();
    until([&] { return !study.busy(); });
    require(!study.showing_source() && study.take_simulation_request(), "Load did not return to Simulation");
    same_field(runner.capture_current().get(), captured, "Load into Simulation restored the wrong field");
    runner.step();
    runner.reset(91);
    same_field(runner.capture_current().get(), captured, "Loaded snapshot did not become the Reset target");
    ImGui::DestroyContext();
    std::cout << "PASS Study workflow: independent tabs/view, real snapshot and load buttons, read-only "
                 "recovery, exact paused/running capture, stable camera/viewport, inactive field and Reset\n";
}
