#pragma once
#include <windows.h>
#include <d3d11.h>
#include <string>
namespace preyvr::dll {
DWORD SetHudLayerEnabled(unsigned enabled);
bool HudLayerEnabled();
void RefuseHudLayer(const char* reason);
std::string HudLayerReport();
// Render-thread calls only. The borrowed texture remains owned by the capture.
ID3D11Texture2D* HudLayerTexture();
// The inventory/PDA movie, captured on the opposite gate to the gameplay HUD:
// that one wants gameplay input allowed, this one wants a modal screen up. Null
// until DaniellePDA has been identified AND captured this frame -- check
// HudLayerReport() for which of the two is missing rather than assuming.
ID3D11Texture2D* InventoryLayerTexture();
void SetHudLayerPresentation(bool active, float width=0, float height=0, float distance=2);
bool HudLayerReticle(float tanX, float tanY, float& x, float& y);
}
