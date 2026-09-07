#pragma once

#include <windows.h>
#include "XrInput.h"

// M2: the weapon points where the controller points, not where the head looks.
//
// **This is the gate that stopped the earlier prototype.** H-004 records it by
// name -- *"the critical detached-aim gate that blocked the earlier fholger
// prototype"* -- and it is already reproduced here: a fixed-camera reticle probe
// changed the cached ray, and the A0b wrench test moved a native wall contact
// `0.127165` units laterally on the same collider. What was missing was a
// controller to drive it with, which now exists.
//
// **Where it writes, and why exactly there.** `ArkPlayer::UpdateCachedReticleViewPosAndDir`
// (R-011) rebuilds the cached ray each frame by unprojecting through the global
// view camera. Its consumers -- weapon firing (R-014), target selection (R-022),
// wrench hits (R-020) -- all read the cached fields afterwards. So the takeover
// goes **after the producer returns and before the consumers read**, which is the
// same seam FarCry2-vr used for Dunia: *"overwriting outB after the provider
// returns and before the caller consumes it is the aim takeover."*
//
// Writing there rather than at each consumer means one write serves all of them,
// and no consumer can be missed.
//
// **Composition follows the fleet's one rule**, because there is only one right
// answer and four projects converged on it: **yaw composes onto the body; pitch
// and roll pass through absolutely.** The same recenter event feeds the view and
// the aim, so the hand and the eye cannot drift apart -- CAM-003, one recenter
// event for all lanes.
//
// Off by default. The A/B that matters is *aim, look away, toggle, aim again*, so
// it is togglable live rather than fixed at startup.
namespace preyvr::dll {

// Clean producer output plus one XR publication, captured before aim writes.
struct GameplayPoseFrame {
    TrackingFrame tracking{};
    Vec3 nativeEye{};
    float yaw = 0.0f;
    float referenceYaw = 0.0f;
    std::uint64_t referenceGeneration = 0;
    std::uintptr_t player = 0;
    std::uint64_t publishedNs = 0;
};
bool EnsureGameplayPoseObservation();
bool TryGetGameplayPoseFrame(GameplayPoseFrame& out, bool requireTracking = true);

DWORD SetAimTakeoverEnabled(unsigned int enabled);
// 0 preserves native origin; 1 uses the tracked aim point (diagnostic).
// The controller point is NOT the authored muzzle; per-weapon barrel alignment
// and native wall-clip fallback require independent validation.
DWORD SetAimOriginFromHand(unsigned int enabled);
unsigned long long AimOriginAppliedCount();

// Frames the cached ray was overwritten.
unsigned long long AimTakeoverAppliedCount();

// **Rejections are split, because they call for opposite responses.** A sleeping
// controller is a tracking problem the player can fix by picking it up; a refused
// composition is a bug in this code; a missing player pointer is a state problem.
// A single "refused" counter would make the three indistinguishable, which is the
// mistake this project already made with the head-rotation counter.
unsigned long long AimTakeoverRejectedNoPose();
unsigned long long AimTakeoverRejectedNoPlayer();
unsigned long long AimTakeoverRejectedCompose();

// The magnitude of the direction the engine produced, in thousandths, sampled
// before any overwrite.
//
// **This exists so the data names itself.** Prey's own field is documented as a
// unit vector; reading it back as 1000 confirms both the offset and the
// convention, exactly as FarCry2-vr identified Dunia's shot direction by printing
// magnitudes rather than trusting a decompiled stack-slot name. A reading far from
// 1000 means the offset is wrong and nothing should be written.
DWORD AimTakeoverNativeDirectionMagnitude();

} // namespace preyvr::dll
