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
// The text panel: note/book, subtitle and tutorial-card movies, captured with
// the HUD layer on into one texture. Same frame-boundary rule as the HUD.
// RefuseTextLayer returns them to the scene until the HUD layer is re-enabled.
ID3D11Texture2D* TextLayerTexture();
void RefuseTextLayer(const char* reason);
// Step 2 (DualOverlay.h): the render-target substitution and movie display hooks
// the per-eye redraw needs, installed together. Then a draw made between the
// two targeted binds runs with `from` rebound to `to` wherever it asks for it.
// False when the hooks are missing or a capture is already in flight.
bool EnsureOverlayHooks();
// Step 3 (DebugOverlay.h VR reticle): takes Prey's crosshair off the HUD panel,
// installing the sprite filter it rides on (false when that is unavailable).
// NativeReticleOffPanel is true when this frame's panel draw left it out, with
// `visible` saying whether Prey was showing it. Render thread, frame-fresh.
bool SetNativeReticleOffPanel(bool off);
bool NativeReticleOffPanel(bool& visible);
bool DrawRedirected(ID3D11DeviceContext* context, ID3D11RenderTargetView* from, ID3D11DepthStencilView* fromDepth,
                    ID3D11RenderTargetView* to, ID3D11DepthStencilView* toDepth, void (*draw)(void*), void* argument);
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
// Additive native status copy. Request expires if XR stops consuming; no native
// status is removed. Calls and borrowed texture consumption are render-thread only.
void RequestNativeWristCapture(bool active);
ID3D11Texture2D* NativeWristLayerTexture();
void ResetNativeWristCapture(); // render-thread session teardown
// hud.var / hud.setvar (diagnostics): queued, run in the next gameplay HUD
// display callback. The report lists each path's value after the operation.
DWORD QueueHudVariable(const std::string& path,bool set,double value);
std::string HudVariableReport();
}
