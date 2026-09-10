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
void SetHudLayerPresentation(bool active, float width=0, float height=0, float distance=2);
bool HudLayerReticle(float tanX, float tanY, float& x, float& y);
}
