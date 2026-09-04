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

// Called from the aim takeover with the ray it just wrote, so the crosshair and
// the shot cannot disagree about direction.
bool WriteReticleScreenPosition(void* player, const Vec3& worldDirection);

unsigned long long ReticleFollowAppliedCount();

// Frames where the ray pointed behind the camera, or outside the view, so no
// honest screen position exists. The crosshair is left where the engine put it
// rather than clamped to an edge, because a crosshair pinned to the screen border
// claims the target is there when it is not.
unsigned long long ReticleFollowOffScreenCount();

// The last screen position written, in thousandths of a viewport fraction --
// 500,500 is dead centre. Reading ~500,500 with the controller pointed straight
// ahead confirms the projection before anything is judged by eye.
DWORD ReticleFollowLastX();
DWORD ReticleFollowLastY();

} // namespace preyvr::dll
