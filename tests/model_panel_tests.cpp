#include "imgui_internal.h"
#include "src/model_panel.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace lenia;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void preview_tests() {
    for (int depth : {1, 16}) {
        Config config{16, 16, depth, 4};
        auto kernel = make_kernel(config);
        const double peak = *std::max_element(kernel.begin(), kernel.end());
        const auto preview = kernel_preview(config, 8);
        const int n = preview.config.width;
        for (int z = 0; z < preview.config.depth; ++z)
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x) {
                    auto cell = [n](int i) { return (int(std::round((i - n / 2) * 16. / n)) + 16) % 16; };
                    const int zz = depth == 1 ? 0 : cell(z);
                    const auto expected = kernel[(zz * 16 + cell(y)) * 16 + cell(x)] / peak;
                    require(std::abs(preview.values[(z * n + y) * n + x] - expected) < 1e-12,
                            "Kernel preview differs from the simulation's lattice weights");
                }
        Config changed = config;
        changed.mu = .23;
        changed.sigma = .04;
        changed.dt = .2;
        changed.rule = Rule::SmoothReaction;
        require(kernel_preview(changed, 8).values == preview.values,
                "Growth parameters changed the spatial kernel preview");
        changed.radius = 6;
        require(kernel_preview(changed, 8).values != preview.values, "Radius did not change kernel preview");
        changed.radius = 1;
        const auto smallest = kernel_preview(changed, 8);
        require(*std::max_element(smallest.values.begin(), smallest.values.end()) == 1,
                "One-cell kernel disappeared from preview");
    }
}
} // namespace
void model_panel_tests() {
    preview_tests();
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device,
                            nullptr, &context),
          "Create model panel test device");
    ModelPanel panel(std::make_shared<GpuContext>(device.Get(), context.Get()));
    Config applied{16, 16, 16, 4}, draft = applied;
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {1400, 900};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = nullptr;
    io.ConfigInputTrickleEventQueue = false;
    io.Fonts->Build();
    int apply_count = 0;
    bool disabled = false;
    auto frame = [&] {
        ImGui::NewFrame();
        if (panel.draw(draft, applied, 1, disabled))
            ++apply_count;
        ImGui::Render();
    };
    auto click = [&](float x, float y) {
        io.AddMousePosEvent(x, y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    panel.toggle();
    frame();
    frame();
    auto *window = ImGui::FindWindowByName("Model parameters");
    require(window && window->Active && GImGui->OpenPopupStack.empty(),
            "Model panel is not a floating window");
    const auto &style = ImGui::GetStyle();
    auto row_y = [&](int row) {
        return window->Pos.y + window->TitleBarHeight + style.WindowPadding.y +
               row * ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight() * .5f;
    };
    click(window->Pos.x + 80, row_y(5));
    require(apply_count == 0, "Apply enabled before any model change");
    click(window->Pos.x + 65, row_y(0));
    require(!same_model_parameters(draft, applied) && applied.mu == .15,
            "Editing the preview changed the active model or failed to change the draft");
    click(window->Pos.x + 80, row_y(5));
    require(apply_count == 1, "Apply did not submit edited parameters");
    applied = draft;
    frame();
    click(window->Pos.x + 80, row_y(5));
    require(apply_count == 1, "Apply remained enabled after the edited model became active");
    draft.mu = .3;
    frame();
    draft.mu = float(applied.mu);
    frame();
    click(window->Pos.x + 80, row_y(5));
    require(apply_count == 1, "Returning to the original slider value left Apply enabled");
    draft.radius = 6;
    panel.toggle();
    frame();
    panel.toggle();
    frame();
    require(draft.radius == 6 && window->Active, "Closing/reopening discarded pending model edits");
    disabled = true;
    const auto unchanged = draft;
    click(window->Pos.x + 100, row_y(0));
    click(window->Pos.x + 80, row_y(5));
    require(draft == unchanged && apply_count == 1, "Unavailable model controls accepted edits");
    disabled = false;
    applied = draft = Config{16, 16, 1, 4};
    frame();
    require(window->Active && same_model_parameters(draft, applied),
            "2D mode did not retain the model window");
    applied = draft = Config{16, 16, 16, 4};
    draft.rule = Rule::SmoothReaction;
    frame();
    click(window->Pos.x + 80, row_y(6));
    require(apply_count == 2, "Smooth rule controls displaced Apply incorrectly");
    applied = draft;
    frame();
    auto curve_vertices = [&](ImU32 color) {
        std::vector<ImVec2> points;
        for (const auto &vertex : window->DrawList->VtxBuffer)
            if (vertex.col == color)
                points.push_back(vertex.pos);
        require(!points.empty(), "Growth response curve is missing");
        return points;
    };
    auto same_curve = [](const auto &a, const auto &b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](ImVec2 p, ImVec2 q) {
                   return std::abs(p.x - q.x) < 1e-4f && std::abs(p.y - q.y) < 1e-4f;
               });
    };
    const float slider_left = window->Pos.x + style.WindowPadding.x;
    click(slider_left + 8, row_y(5));
    const float low_sharpness = float(draft.smoothness);
    const auto soft_curve = curve_vertices(IM_COL32(97, 217, 196, 255));
    const auto applied_curve = curve_vertices(IM_COL32(145, 155, 165, 255));
    click(slider_left + (window->Size.x - 2 * style.WindowPadding.x) * .65f - 8, row_y(5));
    require(draft.smoothness > low_sharpness + 20 &&
                !same_curve(soft_curve, curve_vertices(IM_COL32(97, 217, 196, 255))),
            "Sharpness slider did not visibly change the preview response");
    require(same_curve(applied_curve, curve_vertices(IM_COL32(145, 155, 165, 255))),
            "Editing sharpness changed the applied comparison curve");
    draft.smoothness = 1;
    applied = draft;
    frame();
    const auto before_cell_change = curve_vertices(IM_COL32(97, 217, 196, 255));
    const float cell_row = window->Pos.y + window->TitleBarHeight + style.WindowPadding.y +
                           7 * ImGui::GetFrameHeightWithSpacing() +
                           3 * ImGui::GetTextLineHeightWithSpacing() + 190 + style.ItemSpacing.y;
    click(slider_left + 8, cell_row + ImGui::GetFrameHeight() * .5f);
    const auto empty_cell_curve = curve_vertices(IM_COL32(97, 217, 196, 255));
    require(!same_curve(before_cell_change, empty_cell_curve),
            "Preview cell control did not update the graph");
    const float graph_top = cell_row + ImGui::GetFrameHeightWithSpacing();
    for (const auto &point : empty_cell_curve)
        require(point.y >= graph_top + 6 && point.y <= graph_top + 92,
                "Smooth response at a boundary cell escaped the graph's vertical scale");
    click(window->Pos.x + 80, row_y(6));
    require(apply_count == 2 && same_model_parameters(draft, applied),
            "Preview cell value changed the simulation parameters or enabled Apply");
    io.DisplaySize = {500, 420};
    frame();
    frame();
    require(window->Pos.x >= 0 && window->Pos.y >= 0 && window->Pos.x + window->Size.x <= io.DisplaySize.x &&
                window->Pos.y + window->Size.y <= io.DisplaySize.y,
            "Model window became unreachable after resizing");
    ImGui::DestroyContext();
    std::cout << "PASS model panel: independent floating window, draft/apply/revert, 2D/3D kernel weights, "
                 "mode changes and viewport constraints\n";
}
