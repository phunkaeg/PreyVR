#pragma once
#include <windows.h>
#include <d3d11.h>
#include <string>
namespace preyvr::dll {
DWORD SetHudLayerEnabled(unsigned enabled);
bool HudLayerEnabled();
// Off by default. Capture also needs a fresh consumer lease, so enabling it
// cannot redirect the movie into a texture that has no prepared swapchain.
DWORD SetInventoryCaptureEnabled(unsigned enabled);
bool InventoryCaptureEnabled();
void RefuseInventoryCapture(const char* reason);
void RefuseHudLayer(const char* reason);
std::string HudLayerReport();
// Render-thread frame-boundary calls only. Drain once even when XR skips a frame.
// The borrowed texture remains owned by the capture.
ID3D11Texture2D* HudLayerTexture();
// The inventory/PDA movie, captured on the opposite gate to the gameplay HUD:
// that one wants gameplay input allowed, this one wants a modal screen up. Null
// until DaniellePDA has been identified AND captured this frame -- check
// HudLayerReport() for which of the two is missing rather than assuming.
ID3D11Texture2D* InventoryLayerTexture(ID3D11Texture2D** rightEye=nullptr);
// Non-consuming diagnostic read: both textures belong to the same callback.
bool PeekInventoryPair(ID3D11Texture2D** left,ID3D11Texture2D** right);
DWORD SetInventoryStereoEnabled(unsigned enabled);
bool InventoryStereoEnabled();
DWORD SetInventoryDepthPercent(unsigned percent);
std::string InventoryStereoReport();
// Render-thread consumer lease. Null revokes; format/dimensions must match the
// native destination before capture can redirect it. Expires during XR stalls.
void SetInventoryConsumerReady(const D3D11_TEXTURE2D_DESC* description,
                              float panelWidth=0, float eyeSeparation=0, bool stereo=false);
void SetHudLayerPresentation(bool active, float width=0, float height=0, float distance=2);
bool HudLayerReticle(float tanX, float tanY, float& x, float& y);
}
