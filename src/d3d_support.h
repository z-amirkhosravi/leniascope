#pragma once
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <wrl/client.h>
namespace lenia {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT result, const char *what) {
    if (FAILED(result))
        throw std::runtime_error(std::string(what) + " (HRESULT " + std::to_string(uint32_t(result)) + ")");
}
inline ComPtr<ID3DBlob> compile_shader(const char *source, const char *entry, const char *target) {
    ComPtr<ID3DBlob> blob, errors;
    HRESULT hr = D3DCompile(source, std::strlen(source), "Leniascope", nullptr, nullptr, entry, target,
                            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
    if (FAILED(hr))
        throw std::runtime_error(
            errors ? std::string(static_cast<char *>(errors->GetBufferPointer()), errors->GetBufferSize())
                   : "Shader compilation failed");
    return blob;
}
inline void make_device(ComPtr<ID3D11Device> &device, ComPtr<ID3D11DeviceContext> &context,
                        std::string *name = nullptr) {
    ComPtr<IDXGIFactory1> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create DXGI factory");
    ComPtr<IDXGIAdapter1> best;
    SIZE_T memory = 0;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;
        DXGI_ADAPTER_DESC1 d{};
        adapter->GetDesc1(&d);
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && (!best || d.DedicatedVideoMemory > memory)) {
            best = adapter;
            memory = d.DedicatedVideoMemory;
        }
    }
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    check(D3D11CreateDevice(best.Get(), best ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1, D3D11_SDK_VERSION, &device, nullptr,
                            &context),
          "Create Direct3D 11 device");
    if (name && best) {
        DXGI_ADAPTER_DESC1 d{};
        best->GetDesc1(&d);
        char buffer[256]{};
        WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, buffer, 256, nullptr, nullptr);
        *name = buffer;
    }
}
// Share resources on one device. Every immediate-context/DXGI submission is serialized;
// expensive CPU work and GPU completion waits must happen outside this lock.
struct GpuContext {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    std::string adapter;
    std::recursive_mutex mutex;
    auto lock() {
        return std::unique_lock<std::recursive_mutex>(mutex);
    }
    GpuContext() {
        make_device(device, context, &adapter);
    }
    GpuContext(ID3D11Device *d, ID3D11DeviceContext *c) : device(d), context(c) {}
};
} // namespace lenia
