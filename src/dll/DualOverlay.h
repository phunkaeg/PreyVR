#pragma once

#include <windows.h>

#include <string>

namespace preyvr::dll {

// In a both-eyes frame the first eye is held right after its scene submission,
// so everything the game draws after the world reached only the second eye:
// Flash movies left in the scene (objective markers, the death screen, the HUD
// when it is not on its panel) and 2D images (the flow-graph and TrackView
// screen fades). With this on, each such draw into the frame's own backbuffer
// is made a second time, in the same frame, into the held first eye
// (XrSessionHost HeldFirstEyeTarget): 2D images by RT_Draw2dImageInternal here,
// Flash movies by HudLayer's movie display hook.
//
// Screen-space by construction: both eyes get the overlay at the same pixels,
// so it reads as far away. Per-eye depth for the reticle and markers is later.
DWORD SetDualOverlayRedraw(unsigned int enabled);
bool DualOverlayRedrawEnabled();

// The census. HudLayer reports each Flash movie drawn while the window is open:
// redrawn, or refused with why. The first of each per movie is logged.
void NoteFlashOverlay(const char* movie, bool redrawn, const char* refusal);

std::string DualOverlayReport();

} // namespace preyvr::dll
