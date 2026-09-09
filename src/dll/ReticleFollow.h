#pragma once

#include "preyvr/VrMath.h"

#include <windows.h>
#include <string>

// The indicative overlay: make Prey's own reticle sit where the controller points.
//
// **Using the game's crosshair rather than drawing a beam.** A world-space laser
// needs `IRenderAuxGeom`, whose vtable slot is not known here, and hunting it is a
// separate task. Prey already draws a reticle from a screen position it caches at
// `ArkPlayer+0x17EC` (R-012), computed by the same function whose ray output the
// aim takeover overwrites. The render seam projects the published ray after
// installing the actual eye camera, then updates the existing native crosshair.
//
// **It also closes a real inconsistency rather than only adding a visual.** The
// takeover currently rewrites the ray and leaves the screen position where the
// engine put it, so the crosshair points at the head's direction while shots go
// where the hand points. Anyone judging aim by the crosshair is being told the
// wrong thing. This makes the two agree.
//
// Projected with the camera for this eye render, including stereo asymmetry.
// Native HUD canvas mapping and visual convergence still need live validation.
//
// Off by default and independent of the takeover, so the two can be A/B'd apart:
// a crosshair that follows while shots do not, or the reverse, says immediately
// which half is wrong.
namespace preyvr::dll {

// Copied from the gameplay publication selected by the render consumer. These
// values travel with the ray; reporting separate latest-value atomics cannot
// establish whether head motion changed that same ray.
struct ReticleAimContext {
    std::uint64_t trackingSequence = 0, trackingEpoch = 0, referenceGeneration = 0;
    long long displayTime = 0;
    float playYaw = 0.0f;
    Pose head{}, controller{};
};

// One coherent, timestamped projection record. Values are floats in engine
// units/radians, quaternion XYZW, camera row-major Matrix34, tangents L,R,D,U.
// This records inputs and dispatch results, not movie pixel acceptance.
std::string ReticleProjectionReport();

DWORD SetReticleFollowEnabled(unsigned int enabled);

// **The field write alone is not enough, and this is the correction that makes
// the lane real.** The engine's own reticle reset writes the two cached fields
// and then dispatches `reticleXOffset` / `reticleYOffset` on the HUD element,
// in one function (R-109). The field holds the value; the dispatch is what the
// movie reads. On by default, because a write without a dispatch is the defect
// the reticle report named. Switchable so the two halves can be attributed
// separately when the crosshair does not move.
DWORD SetReticleDispatchEnabled(unsigned int enabled);

// Called from the aim takeover with the WHOLE ray it just wrote -- origin and
// direction -- so the crosshair and the shot cannot disagree about either.
//
// **Passing the origin is the third side of the reconciliation.** The weapon
// starts the shot at the calibrated muzzle and the aim sample carries that same
// origin, but the crosshair used to be projected from a bare direction, which is
// the screen position of an EYE-origin ray. The symbol and the shot were built
// from different origins and agreed only at infinity -- the same parallax the
// barrel calibration removes from firing, left in place for the crosshair.
bool WriteReticleScreenPosition(void* player, const Vec3& rayOrigin,
                                const Vec3& worldDirection,
                                const ReticleAimContext* context = nullptr);

// The distance along the ray that the crosshair marks. A crosshair marks one
// point, and a muzzle-origin shot reaches a different screen position at every
// distance, so without a raycast no single symbol is right at all of them. This
// picks which distance is exact; the error grows toward the near end, which is
// where the disagreement is largest and therefore where it should be judged.
// Clamped to [0.5 m, 200 m].
DWORD SetReticleConvergenceMillimetres(unsigned int millimetres);
DWORD ReticleConvergenceMillimetres();

// How far the ray origin sits from the camera, in millimetres. Zero means the
// shot starts at the eye, so the origin correction is doing nothing -- which is
// the honest reading when no barrel calibration has been taken.
unsigned long long ReticleOriginOffsetMillimetres();

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
// The viewport fraction the projection produced, and the HUD-canvas fraction
// actually dispatched after correcting for Prey's cover-fitted 16:9 canvas.
// They are equal at 16:9 and diverge with the aspect; seeing both side by side
// is what makes the correction checkable rather than assumed.
// How a viewport fraction becomes the fraction the movie is handed.
//   0 -- raw viewport fraction, the behaviour R-118 found to be wrong
//   1 -- the R-118 cover-canvas reconstruction (default, and measured)
//   2 -- Prey's own ScreenToFlash (R-119), which assumes no canvas aspect
// Kept switchable because two conversions now exist and only one can be right;
// stepping between them in one session is what says which. Measured: at the
// live constraints they agree exactly on both axes, so mode 1 is correct.
// Mode 2 is NOT the safer default -- F-017 found that after a constraint change
// ScreenToFlash follows the constraints while the rendering does not, so mode 2
// is the one that goes wrong there, silently. Mode 2 falls back to
// mode 1 when the native call refuses, and counts that separately rather than
// letting the fallback pass for the native answer.
DWORD SetReticleCanvasMode(unsigned int mode);
DWORD ReticleCanvasMode();

// The engine's own flag on ScreenToFlash, described in the binary as "If flash
// asset uses stage.scaleMode this must be true". Which value suits DanielleHUD
// is not established, so it is switchable rather than baked in.
DWORD SetReticleStageScaleMode(unsigned int enabled);

unsigned long long ReticleNativeCanvasCount();
unsigned long long ReticleNativeCanvasFailedCount();

DWORD ReticleCanvasX();
DWORD ReticleCanvasY();
DWORD ReticleFollowLastX();
DWORD ReticleFollowLastY();

} // namespace preyvr::dll
