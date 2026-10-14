#include "src/morphology.h"
#include "src/renderer.h"
#include "src/runner.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <thread>
using namespace lenia;
std::vector<uint8_t> pixels(ID3D11Device *device, ID3D11DeviceContext *context,
                            ID3D11ShaderResourceView *view) {
    ComPtr<ID3D11Resource> resource;
    view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    resource.As(&texture);
    D3D11_TEXTURE2D_DESC d{};
    texture->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check(device->CreateTexture2D(&d, nullptr, &staging), "Readback test image");
    context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map test image");
    std::vector<uint8_t> result(size_t(d.Width) * d.Height * 4);
    for (UINT y = 0; y < d.Height; ++y)
        std::memcpy(result.data() + size_t(y) * d.Width * 4,
                    static_cast<const char *>(mapped.pData) + size_t(y) * mapped.RowPitch, d.Width * 4);
    context->Unmap(staging.Get(), 0);
    return result;
}
void require(bool b, const char *message) {
    if (!b)
        throw std::runtime_error(message);
}
void component_tests() {
    auto gpu = std::make_shared<GpuContext>();
    Renderer renderer(gpu);
    State state;
    state.config = {8, 8, 1, 1};
    state.values.assign(64, 0);
    state.values[2 * 8] = state.values[2 * 8 + 7] = .5 + 1e-12; // Same float as threshold.
    state.values[2 * 8 + 3] = .5; // Strictly inactive, despite identical float32 appearance.
    state.values[5 * 8 + 4] = 1;
    auto map = label_components(state, .5);
    require(map.summary.components == 2 && map.labels[16] == map.labels[23], "Invalid label test mask");
    renderer.upload(state);
    renderer.upload_components(state.config, map.labels);
    auto pixel_is = [&](const std::vector<uint8_t> &image, int i, uint32_t id, float light = 1) {
        auto color = id ? Renderer::component_color(id) : std::array<float, 3>{.035f, .048f, .063f};
        for (int channel = 0; channel < 3; ++channel)
            require(std::abs(int(image[i * 4 + channel]) - int(std::round(color[channel] * light * 255))) <=
                        1,
                    "Rendered component/color differs from integer cell membership");
    };
    ViewSettings v;
    v.view = 0;
    auto image = pixels(gpu->device.Get(), gpu->context.Get(), renderer.draw_components(v, 8, 8));
    for (int i = 0; i < 64; ++i)
        pixel_is(image, i, map.labels[i]);
    auto isolated = pixels(gpu->device.Get(), gpu->context.Get(), renderer.draw_components(v, 8, 8, 2));
    for (int i = 0; i < 64; ++i)
        pixel_is(isolated, i, map.labels[i] == 2 ? 2 : 0);
    v.offset[0] = .5;
    auto shifted = pixels(gpu->device.Get(), gpu->context.Get(), renderer.draw_components(v, 8, 8));
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            pixel_is(shifted, y * 8 + x, map.labels[y * 8 + (x + 4) % 8]);
    // Non-cubic volume, front and occluded rear one-cell slabs. Isolating the rear must
    // traverse the front and empty space without skipping its single voxel of thickness.
    Config c{32, 16, 8, 1};
    std::vector<uint32_t> labels(c.count());
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x) {
            labels[(6 * 16 + y) * 32 + x] = 17;
            labels[(1 * 16 + y) * 32 + x] = 9;
        }
    renderer.upload_components(c, labels);
    v = {};
    v.yaw = v.pitch = 0;
    float light = .5f + .5f / std::sqrt(1.65f);
    auto render = [&](uint32_t id) {
        return pixels(gpu->device.Get(), gpu->context.Get(), renderer.draw_components(v, 33, 33, id));
    };
    pixel_is(render(0), 16 * 33 + 16, 17, light);
    pixel_is(render(9), 16 * 33 + 16, 9, light);
    pixel_is(render(123), 16 * 33 + 16, 0);
    v.clip = .4f;
    pixel_is(render(0), 16 * 33 + 16, 9, light);
    v.clip = 0;
    pixel_is(render(0), 16 * 33 + 16, 0);
    v.clip = 1;
    v.view = 0;
    v.slice = 1.5f / 8;
    pixel_is(render(0), 16 * 33 + 16, 9);
    v.slice = 6.5f / 8;
    pixel_is(render(0), 16 * 33 + 16, 17);
    v.slice = 4.5f / 8;
    pixel_is(render(0), 16 * 33 + 16, 0);
    // Returning to scalar rendering must retain its own field dimensions and values.
    renderer.draw({}, 8, 8);
    auto before = renderer.capture_bmp();
    renderer.draw_components(v, 33, 33, 9);
    renderer.draw({}, 8, 8);
    require(before == renderer.capture_bmp(), "Component preview corrupted scalar display");
    std::cout << "PASS exact component masks, colors, seam IDs, isolation, periodic origin, thin/occluded "
                 "voxels, non-cubic DDA, slices, cutaway and independent scalar display\n";
}
void gpu_display_tests() {
    auto shared = std::make_shared<GpuContext>();
    Renderer renderer(shared);
    for (Config config : {Config{16, 8, 1, 2}, Config{32, 16, 8, 3}}) {
        GpuSolver solver(random_state(config, 42), shared);
        auto first = solver.display_snapshot();
        require(first && first->metadata.values.empty(), "GPU display unexpectedly contains CPU field");
        auto state = solver.snapshot();
        auto expected = measure(state.values);
        require(std::abs(expected.mean - first->measures.mean) < 1e-7 &&
                    expected.minimum == first->measures.minimum &&
                    expected.maximum == first->measures.maximum,
                "GPU statistics disagree with reference");
        for (int view : {0, 1, 2}) {
            ViewSettings settings;
            settings.view = view;
            renderer.use_gpu_image(first);
            auto resident =
                pixels(shared->device.Get(), shared->context.Get(), renderer.draw(settings, 192, 160));
            renderer.upload(state);
            auto transferred =
                pixels(shared->device.Get(), shared->context.Get(), renderer.draw(settings, 192, 160));
            require(resident == transferred, "GPU-resident image differs from readback/upload image");
        }
        renderer.use_gpu_image(first);
        auto before =
            pixels(shared->device.Get(), shared->context.Get(), renderer.draw(ViewSettings{}, 192, 160));
        solver.step();
        auto second = solver.display_snapshot();
        solver.step();
        auto third = solver.display_snapshot();
        solver.step();
        require(!solver.display_snapshot(), "GPU display pool grew beyond three leases");
        auto after =
            pixels(shared->device.Get(), shared->context.Get(), renderer.draw(ViewSettings{}, 192, 160));
        require(before == after && first->metadata.steps == 0,
                "Retained GPU display snapshot was overwritten");
        second.reset();
        auto reused = solver.display_snapshot();
        require(reused && reused->metadata.steps == 3,
                "Slow viewer prevented numerical evolution or pool reuse");
        auto checkpoint = solver.snapshot();
        require(checkpoint.steps == 3 && checkpoint.values.size() == config.count(),
                "Display lease interfered with full checkpoint");
    }
    // Concurrent worker and renderer share one immediate context. Check ordered captures,
    // mode/backend changes, and restoration while the display uses texture leases.
    SessionStates initial;
    initial[0] = std::make_shared<State>(random_state({32, 32, 1, 6}, 10));
    initial[1] = std::make_shared<State>(random_state({32, 32, 32, 6}, 11));
    Runner runner(true, initial, true, {}, shared);
    auto wait_frame = [&](auto predicate) {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < end) {
            auto f = runner.frame();
            if (f && predicate(*f))
                return f;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("Shared-device runner timeout: " + runner.message());
    };
    wait_frame([](const Frame &f) { return bool(f.gpu_image); });
    runner.speed(120);
    runner.play(true);
    uint64_t serial = 0;
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
    while (std::chrono::steady_clock::now() < until) {
        auto f = runner.frame();
        if (f->serial != serial) {
            renderer.use_gpu_image(f->gpu_image);
            serial = f->serial;
        }
        renderer.draw(ViewSettings{}, 256, 192);
        {
            auto lock = shared->lock();
            shared->context->Flush();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    auto capture = runner.snapshot_sessions();
    require(capture.wait_for(std::chrono::seconds(10)) == std::future_status::ready, "Live capture blocked");
    auto captured = capture.get();
    require(captured.states[1]->steps > 5 && captured.states[1]->values.size() == 32768 &&
                captured.states[0]->values.size() == 1024,
            "GPU recovery captured incomplete or wrong session");
    auto paused = runner.pause_and_snapshot().get();
    require(paused.states[1]->steps >= captured.states[1]->steps, "Capture reordered simulation");
    runner.mode(false);
    runner.step();
    auto both = runner.pause_and_snapshot().get();
    require(!both.volume && both.states[0]->steps == 1 && both.states[1]->steps == paused.states[1]->steps,
            "GPU mode switch lost checkpoint state");
    runner.backend(false);
    auto cpu = runner.pause_and_snapshot().get();
    require(cpu.states[0]->values == both.states[0]->values &&
                cpu.states[1]->values == both.states[1]->values,
            "GPU-to-CPU transition changed checkpoint");
    auto cpu_frame = wait_frame([](const Frame &f) { return !f.gpu; });
    renderer.upload(cpu_frame->state);
    runner.backend(true);
    auto gpu = runner.pause_and_snapshot().get();
    require(gpu.states[0]->values == cpu.states[0]->values && gpu.states[1]->values == cpu.states[1]->values,
            "CPU-to-GPU transition changed representable checkpoint");
    auto gpu_frame = wait_frame([](const Frame &f) { return bool(f.gpu_image); });
    renderer.use_gpu_image(gpu_frame->gpu_image);
    renderer.draw(ViewSettings{}, 256, 192);
    Runner restored(true, gpu.states, false, {}, shared);
    auto roundtrip = restored.pause_and_snapshot().get();
    require(roundtrip.states[0]->values == gpu.states[0]->values &&
                roundtrip.states[1]->values == gpu.states[1]->values,
            "GPU workspace restore changed fields");
    std::cout << "PASS GPU image equivalence, statistics, immutable bounded leases, concurrent rendering, "
                 "exact live capture and backend/restore transitions\n";
}
// Optional read-only diagnostic for checking a real saved field outside the interactive UI.
void inspect_components(const char *path, const char *output, double threshold) {
    auto state = load_state(path);
    auto map = label_components(state, threshold);
    auto folder = std::filesystem::u8path(output);
    std::filesystem::create_directories(folder);
    std::ofstream table(folder / "components.csv");
    table << "id,cells,winding_axes\n";
    uint32_t largest = 0;
    for (uint32_t i = 0; i < map.components.size(); ++i) {
        const auto &c = map.components[i];
        table << i + 1 << ',' << c.cells << ',' << c.winding_axes << '\n';
        if (!largest || c.cells > map.components[largest - 1].cells)
            largest = i + 1;
    }
    auto gpu = std::make_shared<GpuContext>();
    Renderer renderer(gpu);
    renderer.upload_components(state.config, map.labels);
    renderer.upload(state);
    ViewSettings v;
    v.threshold = float(threshold);
    renderer.draw(v, 800, 600);
    renderer.export_bmp((folder / "scalar.bmp").u8string());
    renderer.draw_components(v, 800, 600);
    renderer.export_bmp((folder / "components.bmp").u8string());
    renderer.draw_components(v, 800, 600, largest);
    renderer.export_bmp((folder / "largest.bmp").u8string());
    v.view = 0;
    renderer.draw_components(v, 800, 600);
    renderer.export_bmp((folder / "slice.bmp").u8string());
    std::cout << morphology_csv(map.summary) << '\n';
}
int main(int argc, char **argv) {
    try {
        if (argc == 5 && std::string(argv[1]) == "--inspect-components") {
            inspect_components(argv[2], argv[3], std::stod(argv[4]));
            return 0;
        }
        component_tests();
        gpu_display_tests();
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        make_device(device, context);
        Renderer renderer(device.Get(), context.Get());
        State state;
        state.config = {32, 32, 32, 6};
        state.values.resize(state.config.count());
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) {
                    double r = std::sqrt(std::pow((x - 15.5) / 32, 2) + std::pow((y - 15.5) / 32, 2) +
                                         std::pow((z - 15.5) / 32, 2));
                    state.values[(z * 32 + y) * 32 + x] = std::exp(-std::pow((r - .25) / .07, 2));
                }
        renderer.upload(state);
        ViewSettings v;
        v.opacity = .5f;
        v.distance = 1.9f;
        auto image = pixels(device.Get(), context.Get(), renderer.draw(v, 384, 384));
        renderer.export_bmp("render-volume.bmp");
        double sum = 0;
        for (size_t i = 0; i < image.size(); i += 4)
            sum += image[i] + image[i + 1] + image[i + 2];
        require(sum > 384 * 384 * 40., "Volume render was empty");
        v.clip = 0;
        auto clipped = pixels(device.Get(), context.Get(), renderer.draw(v, 384, 384));
        require(clipped != image, "Cutaway did not affect image");
        v.clip = 1;
        v.quality = .4f;
        auto finer = pixels(device.Get(), context.Get(), renderer.draw(v, 384, 384));
        double difference = 0;
        for (size_t i = 0; i < image.size(); ++i)
            difference += std::abs(int(image[i]) - int(finer[i]));
        require(difference / image.size() < 3, "Opacity changed excessively with sample spacing");
        v.view = 2;
        v.threshold = .3f;
        auto surface = pixels(device.Get(), context.Get(), renderer.draw(v, 384, 384));
        require(surface != image && surface != clipped, "Surface view missing");
        renderer.export_bmp("render-surface.bmp");
        v.view = 0;
        v.slice = .5f;
        auto slice = pixels(device.Get(), context.Get(), renderer.draw(v, 384, 384));
        require(slice != surface, "Slice view missing");
        renderer.export_bmp("render-slice.bmp");
        state.config = {16, 8, 1, 2};
        state.values.assign(state.config.count(), .5);
        renderer.upload(state);
        renderer.draw(v, 640, 480);
        renderer.export_bmp("render-2d.bmp");
        for (int i = 0; i < int(Renderer::palette_names().size()); ++i) {
            v.palette = i;
            renderer.draw(v, 64, 64);
        }
        std::cout << "PASS slice, volume, isosurface, cutaway, opacity sampling, rectangular resize, all "
                     "palettes\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
