#pragma once
#include "d3d_support.h"
#include "simulation.h"
#include "view_settings.h"
namespace lenia {
class Renderer {
  public:
    Renderer(ID3D11Device *, ID3D11DeviceContext *);
    static const std::vector<std::string> &palette_names();
    void upload(const State &);
    ID3D11ShaderResourceView *draw(const ViewSettings &, int width, int height);
    void export_bmp(const std::string &path);

  private:
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
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler, palette_sampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
    int width = 0, height = 0, palette_id = -1;
    Config config;
    std::vector<float> upload_values;
    bool available = false;
    void update_palette(int);
};
} // namespace lenia
