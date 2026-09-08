#pragma once

#include "preyvr/VrMath.h"

#include <windows.h>

// The indicative overlay: make Prey's own reticle sit where the controller points.
//
// **Using the game's crosshair rather than drawing a beam.** A world-space laser
// needs `IRenderAuxGeom`, whose vtable slot is not known here, and hunting it is a
// separate task. Prey already draws a reticle from a screen position it caches at
// `ArkPlayer+0x17EC` (R-012), computed by the same function whose ray output the
// aim takeover overwrites. Writing that position in step with the ray makes the
// existing crosshair the indicator -- no renderer hook, no new draw call, and it
// is exactly the surface the player already reads.
//
// **It also closes a real inconsistency rather than only adding a visual.** The
// takeover currently rewrites the ray and leaves the screen position where the
// engine put it, so the crosshair points at the head's direction while shots go
// where the hand points. Anyone judging aim by the crosshair is being told the
// wrong thing. This makes the two agree.
//
// Projected with the live camera's own fov and projection ratio, so the mapping
// matches whatever the player has their FOV slider set to rather than assuming
// the 120 degrees measured on one machine.
//
// Off by default and independent of the takeover, so the two can be A/B'd apart:
// a crosshair that follows while shots do not, or the reverse, says immediately
// which half is wrong.
namespace preyvr::dll {

DWORD SetReticleFollowEnabled(unsigned int enabled);

// **The field write alone is not enough, and this is the correction that makes
// the lane real.** The engine's own reticle reset writes the two cached fields
// and then dispatches `reticleXOffset` / `reticleYOffset` on the HUD element,
// in one function (R-109). The field holds the value; the dispatch is what the
// movie reads. On by default, because a write without a dispatch is the defect
// the reticle report named. Switchable so the two halves can be attributed
// separately when the crosshair does not move.
DWORD SetReticleDispatchEnabled(unsigned int enabled);

// Called from the aim takeover with the ray it just wrote, so the crosshair and
// the shot cannot disagree about direction.
bool WriteReticleScreenPosition(void* player, const Vec3& worldDirection);

unsigned long long ReticleFollowAppliedCount();

// Frames where both dispatches returned zero, and frames where either did not.
// A zero return is not visual acceptance -- it says the ABI and the element were
// right (F-011) -- but a non-zero says plainly that the call did not happen.
unsigned long long ReticleDispatchedCount();
unsigned long long ReticleDispatchFailedCount();

// Frames where the ray pointed behind the camera, or outside the view, so no
// honest screen position exists. A ray behind the camera returns without
// writing; a ray merely outside the frustum is CLAMPED to the edge it left by,
// so the symbol reads as "off that way" rather than staying parked over whatever
// it happened to be over. Both are counted here.
unsigned long long ReticleFollowOffScreenCount();

// The last screen position written, in thousandths of a viewport fraction --
// 500,500 is dead centre. Reading ~500,500 with the controller pointed straight
// ahead confirms the projection before anything is judged by eye.
DWORD ReticleFollowLastX();
DWORD ReticleFollowLastY();

} // namespace preyvr::dll
