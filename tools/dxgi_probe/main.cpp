#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <cstdint>
#include <iomanip>
#include <iostream>

namespace {

void print_method(const char* name, void* address, HMODULE dxgi_module) {
    const auto absolute = reinterpret_cast<std::uintptr_t>(address);
    const auto base = reinterpret_cast<std::uintptr_t>(dxgi_module);

    std::cout << name
              << " address=0x" << std::hex << absolute
              << " dxgi_rva=0x" << (absolute - base)
              << std::dec << '\n';
}

} // namespace

int main() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const HWND window = CreateWindowExW(
        0,
        L"STATIC",
        L"PreyVR DXGI probe",
        WS_OVERLAPPED,
        0,
        0,
        2,
        2,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (window == nullptr) {
        std::cerr << "CreateWindowExW failed: " << GetLastError() << '\n';
        return 1;
    }

    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferDesc.Width = 2;
    description.BufferDesc.Height = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 1;
    description.OutputWindow = window;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swap_chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL feature_level{};

    const HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &description,
        &swap_chain,
        &device,
        &feature_level,
        &context);

    if (FAILED(result)) {
        std::cerr << "D3D11CreateDeviceAndSwapChain failed: 0x"
                  << std::hex << static_cast<unsigned long>(result) << std::dec << '\n';
        DestroyWindow(window);
        return 2;
    }

    const HMODULE dxgi_module = GetModuleHandleW(L"dxgi.dll");
    if (dxgi_module == nullptr) {
        std::cerr << "dxgi.dll is not loaded\n";
        context->Release();
        device->Release();
        swap_chain->Release();
        DestroyWindow(window);
        return 3;
    }

    void** vtable = *reinterpret_cast<void***>(swap_chain);
    std::cout << "dxgi_base=0x" << std::hex
              << reinterpret_cast<std::uintptr_t>(dxgi_module) << std::dec << '\n';
    print_method("IDXGISwapChain::Present", vtable[8], dxgi_module);
    print_method("IDXGISwapChain::ResizeBuffers", vtable[13], dxgi_module);

    context->Release();
    device->Release();
    swap_chain->Release();
    DestroyWindow(window);
    return 0;
}
