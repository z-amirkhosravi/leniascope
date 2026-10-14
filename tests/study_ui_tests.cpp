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
    std::string controls;
    auto frame = [&] {
        study.tick(runner, views);
        auto current = runner.frame();
        if (current)
            live.upload(current->state);
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
        ImGui::LogToBuffer(0);
        if (tab == 1)
            study.draw_controls(runner, views, 1, false);
        controls = GImGui->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Scene", {0, 700}, ImGuiChildFlags_Borders);
        study.draw_scene(live, current.get(), current ? current->state.config : original.config,
                         views[current && current->state.config.depth > 1], viewer, 1, false);
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
    auto measure = [&] {
        click({controls_origin.x + 60, controls_origin.y + 2 * ImGui::GetFrameHeightWithSpacing() + 1 +
                                           ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeight() * .5f});
        until([&] { return !study.busy(); });
        require(study.showing_components(runner.frame().get()),
                "Measure shape did not measure the current field");
    };
    auto choose_tool = [&](int index) {
        click({controls_origin.x + 80, controls_origin.y + 9});
        require(GImGui->OpenPopupStack.Size == 1, "Study tool selector did not open");
        auto *popup = GImGui->OpenPopupStack[0].Window;
        click({popup->Pos.x + 80, popup->Pos.y + ImGui::GetStyle().WindowPadding.y +
                                      (index + .5f) * ImGui::GetTextLineHeightWithSpacing()});
    };
    frame();
    until([&] { return runner.frame() != nullptr; });
    const auto original_min = canvas_min, original_max = canvas_max;
    auto before_pixels = live.capture_bmp();
    click({tab_origin.x + 110, tab_origin.y + 9});
    require(tab == 1 && controls.find("Measure shape") != std::string::npos,
            "Study tools require a preparatory action");
    require(controls.find("Take snapshot") == std::string::npos &&
                controls.find("Load into Simulation") == std::string::npos &&
                controls.find("Saved snapshots") == std::string::npos,
            "A second workspace is exposed in Study");
    require(live.capture_bmp() == before_pixels && canvas_min.x == original_min.x &&
                canvas_min.y == original_min.y && canvas_max.x == original_max.x &&
                canvas_max.y == original_max.y && views[1].yaw == -.8f,
            "Panel switch changed the image, camera or viewer size");
    choose_tool(1);
    require(controls.find("Run study") != std::string::npos,
            "Behavior tools are hidden without a saved source");
    choose_tool(2);
    require(controls.find("Run batch") != std::string::npos, "Batch tools are hidden without a saved source");
    choose_tool(0);
    // Shape inspection is in-memory, even with another window owning workspace writes.
    measure();
    require(!fs::exists(directory / "studies") && !fs::exists(directory / "gallery"),
            "Measuring a shape unexpectedly saved a pattern or snapshot");
    same_field(runner.capture_current().get(), original, "Shape measurement changed the field");
    require(canvas_min.x == original_min.x && canvas_min.y == original_min.y &&
                canvas_max.x == original_max.x && canvas_max.y == original_max.y,
            "Measuring the current field moved its viewport");
    views[1].yaw = .4f;
    click({tab_origin.x + 35, tab_origin.y + 9});
    require(tab == 0 && study.showing_components(runner.frame().get()) && views[1].yaw == .4f,
            "Simulation tab replaced the measured workspace or camera");
    click({tab_origin.x + 110, tab_origin.y + 9});
    require(study.showing_components(runner.frame().get()), "Study tab lost the shared measurement");
    owner.reset();
    require(access.retry(), "Workspace access did not recover");
    study.allow_saving();
    // Playing/stepping updates the same workspace; stale component masks must never cover it.
    runner.play(true);
    until([&] { return runner.frame()->running && runner.frame()->state.steps > 0; });
    require(!study.showing_components(runner.frame().get()), "Play retained an obsolete component overlay");
    click({tab_origin.x + 35, tab_origin.y + 9});
    click({tab_origin.x + 110, tab_origin.y + 9});
    require(runner.frame()->running, "Changing tabs paused the simulation");
    measure();
    require(!runner.frame()->running, "Measurement did not pause at a step boundary");
    const auto revision = runner.frame()->field_revision;
    auto captured = runner.capture_field().get();
    require(captured.revision == revision && captured.state.steps > 0,
            "Measurement captured the wrong field revision");
    runner.step();
    until([&] { return runner.frame()->field_revision != revision; });
    require(!study.showing_components(runner.frame().get()), "A step retained old shape colors");
    measure();
    auto config = original.config;
    config.mu += .01;
    runner.parameters(config);
    auto changed = runner.capture_field().get();
    frame();
    require(!study.showing_components(runner.frame().get()), "Parameter changes retained old measurements");
    runner.randomize(original.config, 91);
    changed = runner.capture_field().get();
    require(changed.state.seed == 91, "Randomization did not change the shared workspace");
    // Invalidate even when replacement occurs while a measurement is pending.
    click({controls_origin.x + 60, controls_origin.y + 2 * ImGui::GetFrameHeightWithSpacing() + 1 +
                                       ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeight() * .5f});
    runner.restore(original).get();
    until([&] { return !study.busy(); });
    require(!study.showing_components(runner.frame().get()),
            "An old async measurement covered a replacement field");
    measure();
    auto sessions = runner.snapshot_sessions().get();
    same_field(*sessions.states[0], inactive, "Study modified the inactive simulation");
    same_field(*sessions.reset_states[1], original, "Study modified Reset");
    runner.step();
    runner.reset(91);
    same_field(runner.capture_current().get(), original, "Study changed the Reset starting field");
    require(!fs::exists(directory / "studies"), "Shape operations created a snapshot collection");
    // Run a behavior study directly, then change the workspace while it calculates.
    // Its report must retain the input it captured, without restoring that input over newer work.
    choose_tool(1);
    auto run_study = [&] {
        const float fh = ImGui::GetFrameHeightWithSpacing(), th = ImGui::GetTextLineHeightWithSpacing();
        click({controls_origin.x + 50, controls_origin.y + 2 * fh + 1 + ImGui::GetStyle().ItemSpacing.y +
                                           4 * (th + fh) + th + ImGui::GetFrameHeight() * .5f});
    };
    runner.randomize(config, 101);
    auto input = runner.capture_current().get();
    run_study();
    runner.randomize(config, 102);
    auto newer = runner.capture_current().get();
    until([&] { return !study.busy(); });
    auto reports = list_gallery(directory / "studies");
    require(reports.entries.size() == 1, "Run study did not capture its own input automatically");
    same_field(load_gallery_state(directory / "studies", reports.entries[0].id), input,
               "Study used a different field from the one present when Run was pressed");
    require(list_gallery_studies(directory / "studies", reports.entries[0].id).entries.size() == 1,
            "Direct current-field study did not save its report");
    same_field(runner.capture_current().get(), newer, "Study restored an old source over newer work");
    require(controls.find("Run study") != std::string::npos, "Completing a study collapsed its controls");
    run_study();
    until([&] { return !study.busy(); });
    reports = list_gallery(directory / "studies");
    require(reports.entries.size() == 2, "A subsequent study reused the old workspace field");
    same_field(load_gallery_state(directory / "studies", reports.entries.front().id), newer,
               "Second study did not use the current field");
    require(!fs::exists(directory / "gallery"), "Direct studies added patterns to the gallery");
    ImGui::DestroyContext();
    std::cout << "PASS shared Study workspace: immediate tools, read-only measurement, stable tabs/camera, "
                 "pause/play/step, field replacement, stale-result rejection, current-field reports, "
                 "inactive field and Reset\n";
}
