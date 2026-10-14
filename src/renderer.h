#pragma once
#include "d3d_support.h"
#include "gpu_solver.h"
#include "simulation.h"
#include "view_settings.h"
#include <array>
namespace lenia {
class Renderer {
  public:
    Renderer(ID3D11Device *, ID3D11DeviceContext *);
    explicit Renderer(std::shared_ptr<GpuContext>);
    static const std::vector<std::string> &palette_names();
    void upload(const State &);
    void use_gpu_image(std::shared_ptr<const GpuImage>);
    ID3D11ShaderResourceView *draw(const ViewSettings &, int width, int height);
    // A separate integer texture: no interpolation or re-thresholding of component membership.
    void upload_components(const Config &, const std::vector<uint32_t> &labels);
    ID3D11ShaderResourceView *draw_components(const ViewSettings &, int width, int height,
                                              uint32_t isolate = 0);
    static std::array<float, 3> component_color(uint32_t id);
    void export_bmp(const std::string &path);
    std::vector<uint8_t> capture_bmp();

  private:
    std::shared_ptr<GpuContext> gpu;
    std::shared_ptr<const GpuImage> displayed;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture3D> field;
    ComPtr<ID3D11ShaderResourceView> field_view;
    ComPtr<ID3D11Texture1D> palette;
    ComPtr<ID3D11ShaderResourceView> palette_view;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    ComPtr<ID3D11ShaderResourceView> image;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11PixelShader> component_pixel;
    ComPtr<ID3D11ShaderResourceView> component_view;
    Config component_config;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler, palette_sampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
    int width = 0, height = 0, palette_id = -1;
    Config config;
    std::vector<float> upload_values;
    bool available = false;
    void update_palette(int);
    ID3D11ShaderResourceView *draw_internal(const ViewSettings &, int, int, bool components,
                                            uint32_t isolate);
};
} // namespace lenia
