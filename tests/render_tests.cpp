#include "src/renderer.h"
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
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
int main() {
    try {
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
