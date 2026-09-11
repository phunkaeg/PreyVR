#pragma once
#include <windows.h>
#include <d3d11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vector>

namespace preyvr::dll {
// Explicit dispatch also lets offline tests exercise the real owner with a
// simulated XR runtime and real D3D11 textures, without starting a VR session.
struct InventorySwapchainApi {
    PFN_xrEnumerateSwapchainFormats formats;
    PFN_xrCreateSwapchain create;
    PFN_xrEnumerateSwapchainImages images;
    PFN_xrAcquireSwapchainImage acquire;
    PFN_xrWaitSwapchainImage wait;
    PFN_xrReleaseSwapchainImage release;
    PFN_xrDestroySwapchain destroy;
};
bool InventoryTextureCompatible(const D3D11_TEXTURE2D_DESC& a,
                                const D3D11_TEXTURE2D_DESC& b);
class InventorySwapchain {
public:
    enum class Upload { ready, failed, sessionFault };
    explicit InventorySwapchain(InventorySwapchainApi api):api_(api){}
    ~InventorySwapchain(){Reset();}
    InventorySwapchain(const InventorySwapchain&)=delete;
    InventorySwapchain& operator=(const InventorySwapchain&)=delete;
    // Match the active main/menu swapchain's interpretation of copied pixels.
    // The resource's UNORM spelling alone does not establish its encoding.
    bool Prepare(XrSession session, const D3D11_TEXTURE2D_DESC& source, DXGI_FORMAT presentationFormat);
    Upload Copy(ID3D11DeviceContext* context, ID3D11Texture2D* source);
    void Reset(); // Must run before destroying the owning XR session.
    XrSwapchain Handle() const {return chain_;}
    const D3D11_TEXTURE2D_DESC& Description() const {return desc_;}
private:
    InventorySwapchainApi api_;
    XrSession session_=XR_NULL_HANDLE;
    XrSwapchain chain_=XR_NULL_HANDLE;
    D3D11_TEXTURE2D_DESC desc_{};
    DXGI_FORMAT presentationFormat_=DXGI_FORMAT_UNKNOWN;
    std::vector<XrSwapchainImageD3D11KHR> images_;
    bool attempted_=false, fault_=false;
};
}
