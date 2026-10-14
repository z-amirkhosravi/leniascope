#define WIN32_LEAN_AND_MEAN
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "desktop_workspace.h"
#include "editor_panel.h"
#include "gallery_panel.h"
#include "imgui.h"
#include "renderer.h"
#include "runner.h"
#include "viewer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <commdlg.h>
#include <dxgi1_3.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <windows.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace {
using namespace lenia;
UINT pending_width = 0, pending_height = 0;
bool minimized = false;
bool close_requested = false;
float requested_dpi = 0;
LRESULT WINAPI window_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l))
        return 1;
    switch (m) {
    case WM_CLOSE:
        close_requested = true;
        return 0;
    case WM_APP + 1:
        return 0; // A completed worker state also wakes the paused UI.
    case WM_SIZE:
        minimized = w == SIZE_MINIMIZED;
        if (!minimized) {
            pending_width = LOWORD(l);
            pending_height = HIWORD(l);
        }
        return 0;
    case WM_DPICHANGED: {
        requested_dpi = HIWORD(w) / 96.f;
        auto r = reinterpret_cast<RECT *>(l);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    }
    return DefWindowProcW(h, m, w, l);
}
std::string utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size_t(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, result.data(), n, nullptr, nullptr);
    result.pop_back();
    return result;
}
std::string choose_file(HWND window, bool save, bool image = false) {
    wchar_t filename[32768] = {};
    OPENFILENAMEW f{};
    f.lStructSize = sizeof f;
    f.hwndOwner = window;
    f.lpstrFile = filename;
    f.nMaxFile = 32768;
    f.lpstrFilter =
        image ? L"Bitmap image\0*.bmp\0\0" : L"Leniascope checkpoint\0*.lenia;*.len\0All files\0*.*\0\0";
    f.lpstrDefExt = image ? L"bmp" : L"lenia";
    f.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    bool ok = save ? GetSaveFileNameW(&f) : GetOpenFileNameW(&f);
    return ok ? utf8(filename) : std::string();
}
void style(float scale) {
    ImGui::GetStyle() = ImGuiStyle();
    ImGui::StyleColorsDark();
    auto &s = ImGui::GetStyle();
    s.WindowRounding = 0;
    s.ChildRounding = 6;
    s.FrameRounding = 4;
    s.GrabRounding = 4;
    s.WindowBorderSize = 0;
    s.FramePadding = {10, 7};
    s.ItemSpacing = {10, 9};
    s.WindowPadding = {18, 16};
    s.Colors[ImGuiCol_WindowBg] = {.055f, .069f, .09f, 1};
    s.Colors[ImGuiCol_PopupBg] = s.Colors[ImGuiCol_WindowBg];
    s.Colors[ImGuiCol_TitleBgActive] = {.13f, .23f, .28f, 1};
    s.Colors[ImGuiCol_ModalWindowDimBg] = {0, 0, 0, .6f};
    s.Colors[ImGuiCol_ChildBg] = {.073f, .088f, .11f, 1};
    s.Colors[ImGuiCol_FrameBg] = {.12f, .15f, .19f, 1};
    s.Colors[ImGuiCol_Button] = {.13f, .23f, .28f, 1};
    s.Colors[ImGuiCol_ButtonHovered] = {.16f, .37f, .42f, 1};
    s.Colors[ImGuiCol_ButtonActive] = {.15f, .46f, .48f, 1};
    s.Colors[ImGuiCol_CheckMark] = {.38f, .85f, .77f, 1};
    s.Colors[ImGuiCol_SliderGrab] = {.38f, .85f, .77f, 1};
    s.Colors[ImGuiCol_Text] = {.89f, .93f, .95f, 1};
    s.Colors[ImGuiCol_TextDisabled] = {.48f, .56f, .64f, 1};
    s.ScaleAllSizes(scale);
}
void save_backbuffer(ID3D11Device *device, ID3D11DeviceContext *context, IDXGISwapChain *swap,
                     const std::string &path) {
    ComPtr<ID3D11Texture2D> buffer, copy;
    check(swap->GetBuffer(0, IID_PPV_ARGS(&buffer)), "Get screenshot buffer");
    D3D11_TEXTURE2D_DESC d{};
    buffer->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&d, nullptr, &copy), "Create screenshot buffer");
    context->CopyResource(copy.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    check(context->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &m), "Read screenshot");
    std::vector<uint8_t> pixels(size_t(d.Width) * d.Height * 4);
    for (UINT y = 0; y < d.Height; ++y)
        for (UINT x = 0; x < d.Width; ++x) {
            const auto *src = static_cast<const uint8_t *>(m.pData) + size_t(y) * m.RowPitch + x * 4;
            auto *dst = pixels.data() + (size_t(y) * d.Width + x) * 4;
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = 255;
        }
    context->Unmap(copy.Get(), 0);
    BITMAPFILEHEADER f{};
    f.bfType = 0x4d42;
    f.bfOffBits = 54;
    f.bfSize = 54 + DWORD(pixels.size());
    BITMAPINFOHEADER i{};
    i.biSize = 40;
    i.biWidth = d.Width;
    i.biHeight = -LONG(d.Height);
    i.biPlanes = 1;
    i.biBitCount = 32;
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    out.write(reinterpret_cast<char *>(&f), sizeof f);
    out.write(reinterpret_cast<char *>(&i), sizeof i);
    out.write(reinterpret_cast<char *>(pixels.data()), pixels.size());
    if (!out)
        throw std::runtime_error("Screenshot write failed");
}
int run(HINSTANCE instance, int show, const StartupOptions &options) {
    const bool smoke = options.smoke;
    std::unique_ptr<WorkspaceAccess> access;
    std::unique_ptr<WorkspaceWriter> writer;
    RestoredWorkspace restored;
    if (!smoke || !options.workspace.empty()) {
        try {
            access = std::make_unique<WorkspaceAccess>(options.workspace);
            restored = restore_workspace(access->directory);
            if (access->writable())
                writer = std::make_unique<WorkspaceWriter>(access->directory);
            else
                restored.message += " " + access->message;
        } catch (const std::exception &e) {
            restored.message = std::string("Workspace unavailable: ") + e.what();
        }
    }
    auto preferences = restored.preferences;
    if (options.cuda) {
        preferences.gpu = true;
        preferences.cuda = true;
    }
    for (auto &view : preferences.views)
        if (size_t(view.palette) >= Renderer::palette_names().size())
            view.palette = 0;
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc{sizeof wc,
                   CS_CLASSDC,
                   window_proc,
                   0,
                   0,
                   instance,
                   nullptr,
                   LoadCursor(nullptr, IDC_ARROW),
                   nullptr,
                   nullptr,
                   L"LeniascopeModern",
                   nullptr};
    RegisterClassExW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"Leniascope | Laboratory", WS_OVERLAPPEDWINDOW, 100, 80,
                                1440, 940, nullptr, nullptr, instance, nullptr);
    if (!window)
        throw std::runtime_error("Cannot create window");
    auto gpu_context = std::make_shared<GpuContext>();
    auto device = gpu_context->device;
    auto context = gpu_context->context;
    auto adapter = gpu_context->adapter;
    ComPtr<IDXGIDevice> dxgi_device;
    device.As(&dxgi_device);
    ComPtr<IDXGIAdapter> dxgi_adapter;
    dxgi_device->GetAdapter(&dxgi_adapter);
    ComPtr<IDXGIFactory2> factory;
    check(dxgi_adapter->GetParent(IID_PPV_ARGS(&factory)), "Get swap chain factory");
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.BufferCount = 2;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SampleDesc.Count = 1;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    ComPtr<IDXGISwapChain1> swap1;
    check(factory->CreateSwapChainForHwnd(device.Get(), window, &desc, nullptr, nullptr, &swap1),
          "Create swap chain");
    ComPtr<IDXGISwapChain2> swap;
    check(swap1.As(&swap), "Get presentation pacing interface");
    check(swap->SetMaximumFrameLatency(1), "Set presentation latency");
    std::unique_ptr<void, decltype(&CloseHandle)> frame_ready(swap->GetFrameLatencyWaitableObject(),
                                                              CloseHandle);
    if (!frame_ready)
        throw std::runtime_error("Presentation wait handle is unavailable");
    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    ComPtr<ID3D11RenderTargetView> screen;
    auto make_target = [&] {
        ComPtr<ID3D11Texture2D> back;
        check(swap->GetBuffer(0, IID_PPV_ARGS(&back)), "Get window buffer");
        check(device->CreateRenderTargetView(back.Get(), nullptr, &screen), "Create window target");
    };
    make_target();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    float dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(window);
    style(dpi);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int ww = std::min(int(preferences.window_width * dpi), int((work.right - work.left) * .94)),
        wh = std::min(int(preferences.window_height * dpi), int((work.bottom - work.top) * .94));
    SetWindowPos(window, nullptr, work.left + (work.right - work.left - ww) / 2,
                 work.top + (work.bottom - work.top - wh) / 2, ww, wh, SWP_NOZORDER);
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17 * dpi);
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device.Get(), context.Get());
    Renderer renderer(gpu_context);
    Runner runner(
        preferences.gpu, std::move(restored.states), preferences.volume,
        [window] { PostMessageW(window, WM_APP + 1, 0, 0); }, options.cpu_transfer ? nullptr : gpu_context,
        preferences.seeds, preferences.cuda, std::move(restored.reset_states));
    runner.speed(preferences.steps_per_second);
    auto &views = preferences.views;
    GalleryPanel gallery(access ? access->directory / L"gallery" : std::filesystem::path{},
                         access && access->writable(), gpu_context);
    bool gallery_smoke_done = false;
    EditorPanel editor(gpu_context);
    bool editor_smoke_done = false;
    bool done = false, dirty_config = false;
    Config edit;
    uint64_t shown = 0, seed = 1, shown_generation = UINT64_MAX;
    int grid_power = 7;
    uint64_t seen_initialization[2]{UINT64_MAX, UINT64_MAX};
    float rate = float(preferences.steps_per_second);
    std::string local_message = restored.message, previous_local_message;
    auto local_message_until = std::chrono::steady_clock::now();
    int smoke_frames = 0;
    bool smoke_exported = false, profile_started = false;
    struct ProfileRow {
        double elapsed, interval, compute, preparation, draw_submit, present;
        uint64_t step;
        bool gpu, cuda;
    };
    std::vector<ProfileRow> profile;
    auto profile_start = std::chrono::steady_clock::now(), profile_previous = profile_start;
    auto milliseconds = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    auto gallery_trace = [&](const char *event) {
        if (options.gallery_smoke)
            std::ofstream("gallery-smoke.log", std::ios::app) << GetTickCount64() << ' ' << event << '\n';
    };
    std::vector<float> history;
    uint64_t history_step = UINT64_MAX;
    bool history_volume = true;
    auto next_recovery = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto next_preferences = std::chrono::steady_clock::now();
    std::string sent_preferences;
    uint64_t acknowledged_checkpoints = 0;
    int closing_stage = 0;
    std::future<SessionSnapshot> closing_snapshot;
    std::future<SessionSnapshot> recovery_snapshot;
    std::optional<SessionSnapshot> closing_sessions;
    std::string closing_error;
    auto persist_workspace = [&](const std::shared_ptr<const Frame> &frame) {
        if (recovery_snapshot.valid() &&
            recovery_snapshot.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto snapshot = recovery_snapshot.get();
                if (writer && !close_requested && preferences.recovery)
                    writer->request(preferences, std::move(snapshot));
            } catch (const std::exception &e) {
                local_message = e.what();
            }
        }
        if (!close_requested) {
            preferences.steps_per_second = rate;
            if (frame) {
                preferences.gpu = frame->gpu;
                preferences.cuda = frame->cuda;
                preferences.volume = frame->state.config.depth > 1;
            }
            WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
            if (GetWindowPlacement(window, &placement) && placement.showCmd != SW_SHOWMINIMIZED) {
                preferences.window_width = std::clamp(
                    int((placement.rcNormalPosition.right - placement.rcNormalPosition.left) / dpi), 320,
                    16384);
                preferences.window_height = std::clamp(
                    int((placement.rcNormalPosition.bottom - placement.rcNormalPosition.top) / dpi), 240,
                    16384);
                preferences.maximized = placement.showCmd == SW_SHOWMAXIMIZED;
            }
            if (writer && frame) {
                try {
                    auto now = std::chrono::steady_clock::now();
                    auto encoded = encode_preferences(preferences);
                    bool recover = preferences.recovery && now >= next_recovery;
                    if (recover || (encoded != sent_preferences && now >= next_preferences)) {
                        writer->request(preferences);
                        if (recover && !recovery_snapshot.valid())
                            recovery_snapshot = runner.snapshot_sessions();
                        sent_preferences = std::move(encoded);
                        next_preferences = now + std::chrono::seconds(2);
                        if (recover)
                            next_recovery = now + std::chrono::seconds(30);
                    }
                } catch (const std::exception &e) {
                    local_message = e.what();
                }
            }
        }
    };
    ShowWindow(window, preferences.maximized ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(window);
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;
        if (close_requested)
            gallery.stop_study();
        gallery.tick(runner, views);
        editor.tick();
        auto frame = runner.frame();
        if (options.gallery_smoke && frame && !gallery_smoke_done)
            gallery_smoke_done = gallery.smoke(runner, views);
        persist_workspace(frame);
        if (options.profile && frame && !profile_started) {
            runner.play(true);
            profile_started = true;
            profile_start = profile_previous = std::chrono::steady_clock::now();
        }
        if (minimized && !close_requested) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
            continue;
        }
        if (pending_width && pending_height) {
            auto lock = gpu_context->lock();
            context->OMSetRenderTargets(0, nullptr, nullptr);
            screen.Reset();
            check(swap->ResizeBuffers(0, pending_width, pending_height, DXGI_FORMAT_UNKNOWN, desc.Flags),
                  "Resize window");
            pending_width = pending_height = 0;
            make_target();
        }
        if (requested_dpi > 0 && requested_dpi != dpi) {
            dpi = requested_dpi;
            style(dpi);
            auto lock = gpu_context->lock();
            ImGui_ImplDX11_InvalidateDeviceObjects();
            io.Fonts->Clear();
            io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17 * dpi);
            requested_dpi = 0;
        }
        if (close_requested && closing_stage == 0 && !gallery.busy() && !editor.active()) {
            gallery_trace("close: capturing workspace");
            try {
                closing_snapshot = runner.pause_and_snapshot();
                closing_stage = 1;
            } catch (const std::exception &e) {
                closing_error = e.what();
                closing_stage = 3;
            }
        }
        if (closing_stage == 1 &&
            closing_snapshot.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                closing_sessions = closing_snapshot.get();
                gallery_trace("close: writing workspace");
                preferences.volume = closing_sessions->volume;
                preferences.steps_per_second = rate;
                if (auto final_frame = runner.frame()) {
                    preferences.gpu = final_frame->gpu;
                    preferences.cuda = final_frame->cuda;
                }
                if (writer)
                    writer->request(preferences, preferences.recovery ? closing_sessions : std::nullopt);
                closing_stage = 2;
            } catch (const std::exception &e) {
                closing_error = e.what();
                closing_stage = 3;
            }
        }
        if (closing_stage == 2 && !gallery.busy() && (!writer || !writer->status().busy)) {
            closing_error = writer ? writer->status().error : std::string();
            if (closing_error.empty())
                closing_error = gallery.close_error();
            if (closing_error.empty())
                gallery_trace("close: workspace ready");
            if (closing_error.empty())
                break;
            closing_stage = 3;
        }
        if (smoke && closing_stage == 3)
            throw std::runtime_error(closing_error);
        // Wait before acquiring the context: simulation can submit freely during vertical blank.
        // Message wakeups keep controls and worker notifications responsive while waiting.
        if (!minimized) {
            HANDLE ready = frame_ready.get();
            auto wait = MsgWaitForMultipleObjectsEx(1, &ready, 1000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (wait == WAIT_FAILED)
                throw std::runtime_error("Presentation wait failed");
            if (wait != WAIT_OBJECT_0)
                continue;
        }
        frame = runner.frame(); // Fetch the newest completed step after the presentation wait.
        if (options.editor_smoke && frame && !editor_smoke_done)
            editor_smoke_done = editor.smoke(runner, views[0]);
        bool is_volume = frame ? frame->state.config.depth > 1 : true;
        auto &view = views[is_volume];
        auto &seed_settings = preferences.seeds[is_volume];
        if (frame && seen_initialization[is_volume] != frame->initialization_revision) {
            if (frame->state.initialization)
                seed_settings = *frame->state.initialization;
            seen_initialization[is_volume] = frame->initialization_revision;
        }
        if (frame && frame->serial != shown) {
            if (history_volume != is_volume || frame->state.steps < history_step ||
                shown_generation != frame->generation)
                history.clear();
            if (history.empty() || frame->state.steps != history_step) {
                history.push_back(float(frame->measures.mean));
                if (history.size() > 512)
                    history.erase(history.begin());
                history_step = frame->state.steps;
                history_volume = is_volume;
            }
            if (frame->gpu_image)
                renderer.use_gpu_image(frame->gpu_image);
            else
                renderer.upload(frame->state);
            if (shown_generation != frame->generation || edit.width != frame->state.config.width ||
                edit.depth != frame->state.config.depth) {
                dirty_config = false;
                edit = frame->state.config;
                seed = frame->state.seed;
                grid_power = int(std::log2(edit.width));
            } else if (!dirty_config)
                edit = frame->state.config;
            shown = frame->serial;
            shown_generation = frame->generation;
        }
        {
            auto lock = gpu_context->lock();
            ImGui_ImplDX11_NewFrame();
        }
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        const auto display = io.DisplaySize;
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(display);
        ImGui::Begin("Laboratory", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::TextColored({.38f, .85f, .77f, 1}, "LENIASCOPE");
        ImGui::SameLine();
        ImGui::TextDisabled(" /  laboratory");
        ImGui::SameLine(300 * dpi);
        ImGui::BeginDisabled(close_requested || !frame || editor.active());
        try {
            if (ImGui::Button(frame && frame->running ? "Pause" : "Play", {88 * dpi, 0}) ||
                (frame && !close_requested && !gallery.active() && !editor.active() &&
                 !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                 ImGui::IsKeyPressed(ImGuiKey_Space) && !io.WantTextInput))
                runner.play(!(frame && frame->running));
            ImGui::SameLine();
            if (ImGui::Button("Step"))
                runner.step();
            ImGui::SameLine();
            if (ImGui::Button("Reset"))
                runner.reset(seed, seed_settings);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    frame && frame->has_initial_field
                        ? "Restore the loaded or edited starting field with the current model parameters."
                        : "Regenerate the random field using the current seed controls and model "
                          "parameters.");
            ImGui::SameLine();
            if (ImGui::Button("New seed")) {
                ++seed;
                runner.randomize(frame ? frame->state.config : edit, seed, seed_settings);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180 * dpi);
            if (ImGui::SliderFloat("steps/s", &rate, 1, 240, "%.0f"))
                runner.speed(rate);
        } catch (const std::exception &e) {
            local_message = e.what();
        }
        ImGui::Separator();
        float status_height = 42 * dpi;
        float available_height = std::max(100.f, ImGui::GetContentRegionAvail().y - status_height);
        float panel_width = std::min(320 * dpi, display.x * .36f);
        ImGui::BeginChild("Inspector", {panel_width, available_height}, ImGuiChildFlags_Borders);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .60f);
        try {
            ImGui::TextDisabled("SIMULATION");
            int mode = is_volume ? 1 : 0;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##dimension", &mode, "2D field\0 3D volume\0")) {
                runner.mode(mode == 1);
                dirty_config = false;
            }
#ifdef LENIA_ENABLE_CUDA
            int backend = frame && frame->gpu ? (frame->cuda ? 2 : 1) : 0;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##compute", &backend,
                             "CPU reference (float64)\0DirectCompute (float32)\0CUDA / cuFFT (float32)\0"))
                runner.backend(backend != 0, backend == 2);
#else
            bool gpu = frame && frame->gpu;
            if (ImGui::Checkbox("GPU compute (float32)", &gpu))
                runner.backend(gpu);
#endif
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("CPU uses double precision. GPU trajectories may diverge over long runs.");
            ImGui::Spacing();
            ImGui::Text("Model parameters");
            float mu = float(edit.mu), sigma = float(edit.sigma), dt = float(edit.dt);
            if (ImGui::SliderFloat("Mean", &mu, .01f, .5f, "%.4f")) {
                edit.mu = mu;
                dirty_config = true;
            }
            if (ImGui::SliderFloat("Width", &sigma, .001f, .1f, "%.4f")) {
                edit.sigma = sigma;
                dirty_config = true;
            }
            if (ImGui::SliderFloat("Time step", &dt, .001f, .5f, "%.4f")) {
                edit.dt = dt;
                dirty_config = true;
            }
            int max_radius = std::min(edit.width, edit.height) / 2;
            if (edit.depth > 1)
                max_radius = std::min(max_radius, edit.depth / 2);
            if (ImGui::SliderInt("Radius", &edit.radius, 1, max_radius))
                dirty_config = true;
            int rule = int(edit.rule);
            if (ImGui::Combo("Rule", &rule, "Classic Lenia\0Smooth reaction (experiment)\0")) {
                edit.rule = Rule(rule);
                dirty_config = true;
            }
            if (rule == 1) {
                float sharp = float(edit.smoothness);
                if (ImGui::SliderFloat("Sharpness", &sharp, 1, 40)) {
                    edit.smoothness = sharp;
                    dirty_config = true;
                }
                ImGui::TextWrapped(
                    "Smooth bounded birth/death model. Different dynamics; empty regions can nucleate.");
            }
            if (ImGui::Button(dirty_config ? "Apply changes *" : "Apply changes", {-1, 0})) {
                runner.parameters(edit);
                dirty_config = false;
            }
            if (ImGui::CollapsingHeader("Grid and initialization", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderInt("Power of two", &grid_power, 4, is_volume ? 8 : 11);
                ImGui::Text("%d x %d%s", 1 << grid_power, 1 << grid_power, is_volume ? " x same" : "");
                ImGui::Checkbox("Scale radius with grid", &preferences.scale_radius[is_volume]);
                ImGui::BeginDisabled(seed_settings.layout != SeedLayout::Uniform);
                ImGui::Checkbox("Keep seed coverage on resize", &preferences.keep_seed_coverage[is_volume]);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Adjust patch count to preserve expected coverage, accounting for the new "
                        "radius.\nThis creates a new random field; it does not resample the current state.");
                ImGui::EndDisabled();
                ImGui::InputScalar("Seed", ImGuiDataType_U64, &seed);
                int layout = int(seed_settings.layout);
                if (ImGui::Combo("Placement", &layout, "Across grid\0Single cluster\0Isolated pair\0")) {
                    seed_settings.layout = SeedLayout(layout);
                    if (seed_settings.layout == SeedLayout::Pair)
                        seed_settings.patches = 2;
                }
                ImGui::BeginDisabled(seed_settings.layout == SeedLayout::Pair);
                ImGui::InputInt("Patches", &seed_settings.patches);
                seed_settings.patches = std::clamp(seed_settings.patches, 0, 4096);
                ImGui::EndDisabled();
                if (seed_settings.layout != SeedLayout::Uniform) {
                    float spread = float(seed_settings.spread);
                    if (ImGui::DragFloat(seed_settings.layout == SeedLayout::Pair ? "Separation / R"
                                                                                  : "Spread / R",
                                         &spread, .01f, 0, 16, "%.2f", ImGuiSliderFlags_AlwaysClamp))
                        seed_settings.spread = spread;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            seed_settings.layout == SeedLayout::Pair
                                ? "Distance between patch centres, not their edges. Rounded to cells."
                                : "Patch centres are random within this radius of the grid centre.");
                    if (seed_settings.layout == SeedLayout::Pair) {
                        float angle = float(seed_settings.angle), elevation = float(seed_settings.elevation);
                        if (ImGui::SliderFloat("Pair angle", &angle, -180, 180, "%.1f deg"))
                            seed_settings.angle = angle;
                        if (is_volume && ImGui::SliderFloat("Elevation", &elevation, -90, 90, "%.1f deg"))
                            seed_settings.elevation = elevation;
                    }
                    ImGui::TextWrapped("Centred on the grid. Resizing keeps the patch count; spread and size "
                                       "use the kernel radius.");
                }
                float size_min = float(seed_settings.size_min), size_max = float(seed_settings.size_max);
                if (ImGui::DragFloatRange2("Side / radius", &size_min, &size_max, .01f, .05f, 4.f, "min %.2f",
                                           "max %.2f", ImGuiSliderFlags_AlwaysClamp)) {
                    seed_settings.size_min = size_min;
                    seed_settings.size_max = size_max;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Each box side is independently random in this range times the kernel "
                                      "radius.\nValues are rounded down to cells; patches wrap around the "
                                      "periodic boundary.");
                float value_min = float(seed_settings.value_min), value_max = float(seed_settings.value_max);
                if (ImGui::DragFloatRange2("Values", &value_min, &value_max, .01f, 0, 1, "min %.2f",
                                           "max %.2f", ImGuiSliderFlags_AlwaysClamp)) {
                    seed_settings.value_min = value_min;
                    seed_settings.value_max = value_max;
                }
                ImGui::TextWrapped(frame && frame->has_initial_field
                                       ? "Seed controls affect Preview seed, New seed, and Create grid."
                                       : "Seed controls affect Reset, New seed, and Create grid.");
                if (ImGui::Button("Ten-patch defaults", {-1, 0}))
                    seed_settings = SeedSettings{};
                const auto &current = frame ? frame->state.config : edit;
                if (ImGui::Button("Preview seed (paused)", {-1, 0})) {
                    runner.play(false);
                    runner.randomize(current, seed, seed_settings);
                }
                if (seed_settings.layout == SeedLayout::Uniform) {
                    double coverage = seed_coverage(current, seed_settings);
                    ImGui::Text("Expected coverage: %.2f%%", 100 * coverage);
                    ImGui::Text("Expected mean: %.4f",
                                coverage * (seed_settings.value_min + seed_settings.value_max) / 2);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Statistical estimates including patch overlap; individual seeds "
                                          "vary.\nOverlapping "
                                          "patches overwrite values. This is not a prediction of survival.");
                }
                int minimum_side = std::min(current.width, current.height);
                if (current.depth > 1)
                    minimum_side = std::min(minimum_side, current.depth);
                if (seed_settings.layout != SeedLayout::Uniform) {
                    double diameter =
                        seed_settings.spread * (seed_settings.layout == SeedLayout::Cluster ? 2 : 1);
                    if ((diameter + seed_settings.size_max) * current.radius + 1 >= minimum_side)
                        ImGui::TextWrapped("This spread can reach periodic edges. Reduce spread/size or "
                                           "enlarge the grid for more empty space around it.");
                }
                int maximum_side = seed_settings.size_min == seed_settings.size_max
                                       ? int(current.radius * seed_settings.size_max)
                                       : int(std::ceil(current.radius * seed_settings.size_max)) - 1;
                ImGui::Text(
                    "Patch sides: %d..%d cells",
                    std::clamp(int(current.radius * seed_settings.size_min), 1, minimum_side),
                    std::clamp(maximum_side, 1, std::max({current.width, current.height, current.depth})));
                if (frame && frame->has_initial_field)
                    ImGui::TextWrapped("Reset restores the loaded or edited starting field and keeps your "
                                       "applied model parameters.");
                else if (frame && frame->state.initialization &&
                         frame->state.initialization_radius != current.radius)
                    ImGui::TextWrapped(
                        "This field was seeded at radius %d. Reset uses the current radius %d.",
                        frame->state.initialization_radius, current.radius);
                else if (frame && !frame->state.initialization)
                    ImGui::TextWrapped(
                        "This field has no stored seed recipe. Reset uses the controls above.");
                Config c = edit;
                c.width = c.height = 1 << grid_power;
                c.depth = is_volume ? c.width : 1;
                if (preferences.scale_radius[is_volume])
                    c.radius = std::max(1, int(std::round(double(edit.radius) * c.width / edit.width)));
                c.radius = std::min(c.radius, c.width / 2);
                SeedSettings next = seed_settings;
                std::string resize_error;
                try {
                    if (preferences.keep_seed_coverage[is_volume] &&
                        (c.width != current.width || c.height != current.height || c.depth != current.depth))
                        next = resize_seed_settings(current, c, next);
                    ImGui::Text("New grid: radius %d, %d patches", c.radius, next.patches);
                    if (next.layout == SeedLayout::Uniform)
                        ImGui::Text("Expected coverage: %.2f%%", 100 * seed_coverage(c, next));
                } catch (const std::exception &e) {
                    resize_error = e.what();
                    ImGui::TextWrapped("%s. Adjust the recipe or disable coverage preservation.",
                                       resize_error.c_str());
                }
                ImGui::BeginDisabled(!resize_error.empty());
                if (ImGui::Button("Create grid", {-1, 0})) {
                    runner.randomize(c, seed, next);
                    dirty_config = false;
                }
                ImGui::EndDisabled();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("EXPERIMENT");
            if (ImGui::Button("Pattern gallery", {-1, 0}))
                gallery.open();
            ImGui::BeginDisabled(is_volume);
            if (ImGui::Button("Edit 2D field", {-1, 0}))
                editor.open(runner, views[0]);
            ImGui::EndDisabled();
            if (frame && !frame->state.edits.empty() && ImGui::CollapsingHeader("Field construction"))
                ImGui::TextUnformatted(frame->state.edits.c_str());
            if (ImGui::CollapsingHeader("Measurements")) {
                if (!history.empty())
                    ImGui::PlotLines("##mean", history.data(), int(history.size()), 0, "Mean activity", 0,
                                     FLT_MAX, {ImGui::GetContentRegionAvail().x, 80 * dpi});
                if (frame) {
                    ImGui::TextWrapped("%s", frame->state.backend.c_str());
                    if (!frame->gpu)
                        ImGui::Text("FFT %.2f / %.2f ms\nUpdate %.2f ms", frame->timings.forward_ms,
                                    frame->timings.inverse_ms, frame->timings.update_ms);
                }
            }
            if (ImGui::Button("Save state", {-1, 0})) {
                auto path = choose_file(window, true);
                if (!path.empty())
                    runner.save(path);
            }
            if (ImGui::Button("Load state", {-1, 0})) {
                auto path = choose_file(window, false);
                if (!path.empty()) {
                    runner.load(path);
                    dirty_config = false;
                }
            }
            if (ImGui::Button("Export view", {-1, 0})) {
                auto path = choose_file(window, true, true);
                if (!path.empty()) {
                    renderer.export_bmp(path);
                    local_message = "Exported " + path;
                }
            }
            if (ImGui::CollapsingHeader("Workspace")) {
                ImGui::BeginDisabled(!writer);
                ImGui::Checkbox("Autosave and restore sessions", &preferences.recovery);
                ImGui::TextWrapped(
                    "Both fields are saved every 30 seconds and on close. Reopening starts paused.");
                if (ImGui::Button(preferences.recovery ? "Save workspace now" : "Save preferences now",
                                  {-1, 0}) &&
                    writer) {
                    writer->request(preferences);
                    if (preferences.recovery && !recovery_snapshot.valid())
                        recovery_snapshot = runner.snapshot_sessions();
                    next_recovery = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                }
                ImGui::EndDisabled();
                if (access && !access->writable())
                    ImGui::TextWrapped("%s", access->message.c_str());
            }
        } catch (const std::exception &e) {
            local_message = e.what();
        }
        ImGui::PopItemWidth();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Scene", {0, available_height}, ImGuiChildFlags_Borders);
        if (frame) {
            ImGui::Text("%s   %d x %d x %d", is_volume ? "3D LENIA" : "2D LENIA", frame->state.config.width,
                        frame->state.config.height, frame->state.config.depth);
            ImGui::SameLine();
            ImGui::TextDisabled("  t = %.3f   step %llu", frame->state.time,
                                static_cast<unsigned long long>(frame->state.steps));
        } else
            ImGui::Text("Preparing simulation...");
        ImVec2 image_size = ImGui::GetContentRegionAvail();
        image_size.y = std::max(1.f, image_size.y - 118 * dpi);
        image_size.x = std::max(1.f, image_size.x);
        double draw_submit_ms =
            draw_viewer("Live viewer", renderer, frame ? frame->state.config : edit, view, image_size, dpi);
        if (frame) {
            ImGui::Text("%.1f steps/s    %.2f ms compute    %.0f display fps", frame->steps_per_second,
                        frame->timings.total_ms, io.Framerate);
            ImGui::TextDisabled("%.2f ms display preparation    %s", frame->publication_ms,
                                frame->gpu_image ? "GPU resident" : "CPU transfer");
            ImGui::TextDisabled("Mean %.5f    range [%.4f, %.4f]    %s", frame->measures.mean,
                                frame->measures.minimum, frame->measures.maximum,
                                frame->gpu ? "GPU float" : "CPU double");
        }
        ImGui::TextDisabled(is_volume && view.view != 0
                                ? "Drag to orbit  |  Wheel to zoom  |  Space to play/pause"
                                : "Wheel to zoom  |  Space to play/pause");
        ImGui::EndChild();
        ImGui::EndDisabled();
        if (local_message != previous_local_message) {
            previous_local_message = local_message;
            local_message_until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
        if (std::chrono::steady_clock::now() > local_message_until)
            local_message.clear();
        auto persistence_status = writer ? writer->status() : WorkspaceWriter::Status{};
        if (persistence_status.checkpoints != acknowledged_checkpoints) {
            acknowledged_checkpoints = persistence_status.checkpoints;
            local_message = "Workspace saved.";
        }
        auto status = close_requested ? "Saving workspace before closing..."
                      : !persistence_status.error.empty()
                          ? "Workspace save failed: " + persistence_status.error
                      : persistence_status.busy ? "Saving workspace..."
                      : local_message.empty()   ? runner.message()
                                                : local_message;
        ImGui::TextDisabled("%s", status.c_str());
        if (closing_stage == 3)
            ImGui::OpenPopup("Workspace save failed");
        if (ImGui::BeginPopupModal("Workspace save failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", closing_error.c_str());
            ImGui::TextWrapped("Existing complete recovery checkpoints have been preserved.");
            if (ImGui::Button("Retry")) {
                gallery.retry_save();
                closing_stage = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep open")) {
                close_requested = false;
                closing_stage = 0;
                if (!gallery.close_error().empty())
                    gallery.open();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close without saving"))
                done = true;
            ImGui::EndPopup();
        }
        ImGui::End();
        gallery.draw(runner, views, dpi, close_requested);
        editor.draw(runner, dpi, close_requested);
        ImGui::Render();
        HRESULT present;
        double present_ms = 0;
        {
            auto lock = gpu_context->lock();
            ID3D11RenderTargetView *rtv = screen.Get();
            context->OMSetRenderTargets(1, &rtv, nullptr);
            const float clear[] = {.04f, .05f, .07f, 1};
            context->ClearRenderTargetView(screen.Get(), clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if (options.editor_smoke && editor.smoke_preview_ready())
                save_backbuffer(device.Get(), context.Get(), swap.Get(), "editor-ui-smoke.bmp");
            if (smoke && frame && (!options.gallery_smoke || gallery_smoke_done) &&
                (!options.editor_smoke || editor_smoke_done) && (!options.profile || frame->running) &&
                ++smoke_frames == (options.profile ? 330 : 5)) {
                save_backbuffer(device.Get(), context.Get(), swap.Get(), "ui-smoke.bmp");
                renderer.export_bmp("volume-smoke.bmp");
                smoke_exported = true;
                close_requested = true; // Exercise the ordinary pause, save, and close path too.
            }
            auto present_start = std::chrono::steady_clock::now();
            present = swap->Present(1, 0);
            present_ms = milliseconds(present_start, std::chrono::steady_clock::now());
            check(present, "Present frame");
        }
        if (options.profile && frame && frame->running && !close_requested) {
            auto now = std::chrono::steady_clock::now();
            profile.push_back({milliseconds(profile_start, now), milliseconds(profile_previous, now),
                               frame->timings.total_ms, frame->publication_ms, draw_submit_ms, present_ms,
                               frame->state.steps, frame->gpu, frame->cuda});
            profile_previous = now;
        }
        if (present == DXGI_STATUS_OCCLUDED)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!frame || !frame->running)
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
    }
    if (options.profile) {
        std::ofstream out("frame-profile.csv");
        out << "elapsed_ms,interval_ms,compute_ms,publication_wall_ms,draw_submit_wall_ms,present_wall_ms,"
               "step,backend\n";
        // Discard startup/clock warm-up; rows describe actual GUI Present calls.
        for (size_t i = 30; i < profile.size(); ++i) {
            const auto &r = profile[i];
            out << r.elapsed << ',' << r.interval << ',' << r.compute << ',' << r.preparation << ','
                << r.draw_submit << ',' << r.present << ',' << r.step << ','
                << (r.cuda ? "cuda" : (r.gpu ? "gpu" : "cpu")) << '\n';
        }
        if (!out)
            throw std::runtime_error("Cannot write frame profile");
    }
    if (writer) {
        writer->flush();
        if (smoke && !writer->status().error.empty())
            throw std::runtime_error(writer->status().error);
    }
    gallery_trace("close: releasing UI");
    {
        auto lock = gpu_context->lock();
        ImGui_ImplDX11_Shutdown();
    }
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, instance);
    gallery_trace("close: releasing simulation");
    return smoke && !smoke_exported ? 1 : 0;
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR command, int show) {
    try {
        return run(instance, show, startup_options());
    } catch (const std::exception &e) {
        std::ofstream("leniascope-error.log") << e.what();
        if (!std::wcsstr(command, L"--smoke") && !std::wcsstr(command, L"--gallery-smoke") &&
            !std::wcsstr(command, L"--editor-smoke") && !std::wcsstr(command, L"--profile"))
            MessageBoxA(nullptr, e.what(), "Leniascope", MB_ICONERROR);
        return 1;
    }
}
