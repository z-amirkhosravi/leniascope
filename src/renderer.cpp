#include "renderer.h"
#include "headers/cmap.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
namespace lenia {
namespace {
const char *source = R"(
cbuffer View : register(b0){float4 dims;float4 camera;float4 right;float4 up;float4 forward;float4 settings;float4 controls;float4 offset;};
Texture3D<float> field:register(t0);Texture1D<float4> palette:register(t1);SamplerState sampling:register(s0);SamplerState paletteSampling:register(s1);
struct VOut{float4 position:SV_POSITION;float2 uv:TEXCOORD;};
VOut vs(uint id:SV_VertexID){VOut v;v.uv=float2((id<<1)&2,id&2);v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;}
float sample_at(float3 p){return field.SampleLevel(sampling,frac(p+offset.xyz),0);}
float3 color(float a){return palette.SampleLevel(paletteSampling,saturate(a),0).rgb;}
float4 ps(VOut input):SV_TARGET{
 float3 background=float3(.035,.048,.063);float2 uv=input.uv;
 if(controls.x==0||dims.z==1){
   float2 sizes=controls.y==0?dims.yz:(controls.y==1?dims.xz:dims.xy);
   float aspect=sizes.x/sizes.y;float2 p=(uv-.5)*float2(right.w,1)/controls.w;p.x/=aspect;
   if(any(abs(p)>.5))return float4(background,1);p+=.5;
   float3 q=controls.y==0?float3(controls.z,p):(controls.y==1?float3(p.x,controls.z,p.y):float3(p,controls.z));
   if(dims.z==1)q=float3(p,.5);return float4(color(sample_at(q)),1);
 }
 float3 extent=dims.xyz/max(dims.x,max(dims.y,dims.z))*.5;
 float3 ro=camera.xyz,rd=normalize(forward.xyz+(uv.x-.5)*right.w*right.xyz-(uv.y-.5)*up.xyz);
 float3 low=-extent,high=extent;high.z=lerp(low.z,high.z,settings.w);
 float3 safe=rd+float3(rd.x==0?1e-8:0,rd.y==0?1e-8:0,rd.z==0?1e-8:0);
 float3 t0=(low-ro)/safe,t1=(high-ro)/safe;float3 near3=min(t0,t1),far3=max(t0,t1);
 float begin=max(max(near3.x,near3.y),max(near3.z,0)),end=min(min(far3.x,far3.y),far3.z);
 if(end<=begin)return float4(background,1);
 float step=max(settings.z/max(dims.x,max(dims.y,dims.z)),(end-begin)/1024);
 float3 accumulated=0;float alpha=0;
 [loop]for(float t=begin+.5*step;t<end&&alpha<.995;t+=step){
   float3 p=(ro+t*rd)/(2*extent)+.5;float value=sample_at(p);
   if(controls.x==2){if(value<settings.y)continue;
     float3 e=1/dims.xyz;float3 normal=float3(sample_at(p+float3(e.x,0,0))-sample_at(p-float3(e.x,0,0)),sample_at(p+float3(0,e.y,0))-sample_at(p-float3(0,e.y,0)),sample_at(p+float3(0,0,e.z))-sample_at(p-float3(0,0,e.z)));
     float light=.3+.7*abs(dot(normalize(normal+1e-7),normalize(float3(.4,.7,1))));return float4(color(value)*light,1);
   }
   float a=1-exp(-max(value-settings.y,0)*settings.x*step*80);
   accumulated+=(1-alpha)*a*color(value);alpha+=(1-alpha)*a;
 }
 return float4(accumulated+(1-alpha)*background,1);
}
)";
struct Constants {
    float dims[4], camera[4], right[4], up[4], forward[4], settings[4], controls[4], offset[4];
};
} // namespace
const std::vector<std::string> &Renderer::palette_names() {
    static const auto names = []() {
        std::vector<std::string> n{"Ocean / gold"};
        for (const auto &key : cmap::cmap_keys) {
            std::string label;
            for (wchar_t ch : key)
                label.push_back(static_cast<char>(ch));
            n.push_back(std::move(label));
        }
        return n;
    }();
    return names;
}
Renderer::Renderer(ID3D11Device *d, ID3D11DeviceContext *c) : Renderer(std::make_shared<GpuContext>(d, c)) {}
Renderer::Renderer(std::shared_ptr<GpuContext> shared)
    : gpu(std::move(shared)), device(gpu->device), context(gpu->context) {
    auto vs = compile_shader(source, "vs", "vs_5_0"), ps = compile_shader(source, "ps", "ps_5_0");
    check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex),
          "Create viewport vertex shader");
    check(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel),
          "Create viewport pixel shader");
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(Constants);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check(device->CreateBuffer(&bd, nullptr, &constants), "Create viewport constants");
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    check(device->CreateSamplerState(&sd, &palette_sampler), "Create palette sampler");
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    check(device->CreateSamplerState(&sd, &sampler), "Create field sampler");
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    check(device->CreateRasterizerState(&rd, &rasterizer), "Create rasterizer");
}
void Renderer::use_gpu_image(std::shared_ptr<const GpuImage> image) {
    if (!image)
        throw std::invalid_argument("Missing GPU display image");
    ComPtr<ID3D11Device> owner;
    image->texture->GetDevice(&owner);
    if (owner.Get() != device.Get())
        throw std::invalid_argument("Display image belongs to another device");
    displayed = std::move(image);
    field_view = displayed->view;
    config = displayed->metadata.config;
    available = true;
}
void Renderer::upload(const State &s) {
    if (s.values.size() != s.config.count())
        throw std::invalid_argument("Incomplete display field");
    auto lock = gpu->lock();
    if (displayed) {
        displayed.reset();
        field.Reset();
        field_view.Reset();
    }

    const auto &c = s.config;
    if (!field || config.width != c.width || config.height != c.height || config.depth != c.depth) {
        field.Reset();
        field_view.Reset();
        D3D11_TEXTURE3D_DESC d{};
        d.Width = c.width;
        d.Height = c.height;
        d.Depth = c.depth;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R32_FLOAT;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture3D(&d, nullptr, &field), "Create display volume");
        check(device->CreateShaderResourceView(field.Get(), nullptr, &field_view),
              "Create display volume view");
    }
    upload_values.resize(s.values.size());
    std::transform(s.values.begin(), s.values.end(), upload_values.begin(),
                   [](double a) { return float(a); });
    context->UpdateSubresource(field.Get(), 0, nullptr, upload_values.data(), c.width * 4,
                               c.width * c.height * 4);
    config = c;
    available = true;
}
void Renderer::update_palette(int id) {
    if (id == palette_id)
        return;
    std::vector<float> colors(4096 * 4);
    if (id == 0) { // A subdued blue/cyan/gold transfer palette.
        const float stops[5][3] = {{.025f, .045f, .09f},
                                   {.11f, .23f, .40f},
                                   {.12f, .58f, .64f},
                                   {.61f, .81f, .61f},
                                   {1.f, .89f, .55f}};
        for (int i = 0; i < 4096; ++i) {
            float p = i * 4.f / 4095;
            int j = std::min(3, int(p));
            float a = p - j;
            for (int c = 0; c < 3; ++c)
                colors[4 * i + c] = stops[j][c] * (1 - a) + stops[j + 1][c] * a;
            colors[4 * i + 3] = 1;
        }
    } else {
        auto name = cmap::cmap_keys.at(size_t(id - 1));
        const auto &m = cmap::cmap_list.at(name);
        cmap::CMap map(m.at("red"), m.at("green"), m.at("blue"));
        for (int i = 0; i < 4096; ++i) {
            auto rgb = map(i / 4095.);
            colors[4 * i] = float((rgb >> 16) & 255) / 255;
            colors[4 * i + 1] = float((rgb >> 8) & 255) / 255;
            colors[4 * i + 2] = float(rgb & 255) / 255;
            colors[4 * i + 3] = 1;
        }
    }
    if (!palette) {
        D3D11_TEXTURE1D_DESC d{};
        d.Width = 4096;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture1D(&d, nullptr, &palette), "Create palette");
        check(device->CreateShaderResourceView(palette.Get(), nullptr, &palette_view), "Create palette view");
    }
    context->UpdateSubresource(palette.Get(), 0, nullptr, colors.data(), 0, 0);
    palette_id = id;
}
ID3D11ShaderResourceView *Renderer::draw(const ViewSettings &v, int w, int h) {
    auto lock = gpu->lock();
    w = std::clamp(w, 1, 4096);
    h = std::clamp(h, 1, 4096);
    if (!target || w != width || h != height) {
        target.Reset();
        target_view.Reset();
        image.Reset();
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&d, nullptr, &target), "Create viewport image");
        check(device->CreateRenderTargetView(target.Get(), nullptr, &target_view), "Create viewport target");
        check(device->CreateShaderResourceView(target.Get(), nullptr, &image), "Create viewport image view");
        width = w;
        height = h;
    }
    ID3D11ShaderResourceView *empty = nullptr;
    context->PSSetShaderResources(0, 1, &empty);
    ID3D11RenderTargetView *rtv = target_view.Get();
    context->OMSetRenderTargets(1, &rtv, nullptr);
    float clear[] = {.035f, .048f, .063f, 1};
    context->ClearRenderTargetView(target_view.Get(), clear);
    if (!available)
        return image.Get();
    update_palette(v.palette);
    float cp = std::cos(v.pitch), sp = std::sin(v.pitch), cy = std::cos(v.yaw), sy = std::sin(v.yaw);
    Constants cb{};
    cb.dims[0] = float(config.width);
    cb.dims[1] = float(config.height);
    cb.dims[2] = float(config.depth);
    cb.camera[0] = sy * cp * v.distance;
    cb.camera[1] = sp * v.distance;
    cb.camera[2] = cy * cp * v.distance;
    cb.forward[0] = -sy * cp;
    cb.forward[1] = -sp;
    cb.forward[2] = -cy * cp;
    cb.right[0] = cy;
    cb.right[2] = -sy;
    cb.right[3] = float(w) / h;
    cb.up[0] = -sy * sp;
    cb.up[1] = cp;
    cb.up[2] = -cy * sp;
    cb.settings[0] = v.opacity;
    cb.settings[1] = v.threshold;
    cb.settings[2] = v.quality;
    cb.settings[3] = v.clip;
    cb.controls[0] = float(v.view);
    cb.controls[1] = float(v.axis);
    cb.controls[2] = v.slice;
    cb.controls[3] = v.zoom;
    std::copy(v.offset, v.offset + 3, cb.offset);
    context->UpdateSubresource(constants.Get(), 0, nullptr, &cb, 0, 0);
    D3D11_VIEWPORT vp{0, 0, float(w), float(h), 0, 1};
    context->RSSetViewports(1, &vp);
    context->RSSetState(rasterizer.Get());
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->OMSetDepthStencilState(nullptr, 0);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertex.Get(), nullptr, 0);
    context->PSSetShader(pixel.Get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer *b = constants.Get();
    context->PSSetConstantBuffers(0, 1, &b);
    ID3D11ShaderResourceView *srvs[] = {field_view.Get(), palette_view.Get()};
    context->PSSetShaderResources(0, 2, srvs);
    ID3D11SamplerState *samplers[] = {sampler.Get(), palette_sampler.Get()};
    context->PSSetSamplers(0, 2, samplers);
    context->Draw(3, 0);
    ID3D11ShaderResourceView *nulls[2] = {};
    context->PSSetShaderResources(0, 2, nulls);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    return image.Get();
}
std::vector<uint8_t> Renderer::capture_bmp() {
    auto lock = gpu->lock();
    if (!target)
        throw std::runtime_error("No rendered image");
    D3D11_TEXTURE2D_DESC d{};
    target->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check(device->CreateTexture2D(&d, nullptr, &staging), "Create image readback");
    context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m), "Read image");
    std::vector<uint8_t> bytes(size_t(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const auto *row = static_cast<const uint8_t *>(m.pData) + size_t(y) * m.RowPitch;
        for (int x = 0; x < width; ++x) {
            auto *p = bytes.data() + (size_t(y) * width + x) * 4;
            p[0] = row[x * 4 + 2];
            p[1] = row[x * 4 + 1];
            p[2] = row[x * 4];
            p[3] = 255;
        }
    }
    context->Unmap(staging.Get(), 0);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof file + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + DWORD(bytes.size());
    BITMAPINFOHEADER info{};
    info.biSize = sizeof info;
    info.biWidth = width;
    info.biHeight = -height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    std::vector<uint8_t> result(file.bfSize);
    std::memcpy(result.data(), &file, sizeof file);
    std::memcpy(result.data() + sizeof file, &info, sizeof info);
    std::memcpy(result.data() + file.bfOffBits, bytes.data(), bytes.size());
    return result;
}
void Renderer::export_bmp(const std::string &path) {
    auto bytes = capture_bmp();
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    out.close();
    if (!out)
        throw std::runtime_error("Image export failed");
}
} // namespace lenia
