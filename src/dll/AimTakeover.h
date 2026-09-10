#pragma once

#include <windows.h>
#include "XrInput.h"
#include "preyvr/WeaponAim.h"

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

// Native producer output plus one XR publication, captured before aim writes.
struct GameplayPoseFrame {
    TrackingFrame tracking{};
    // Historical name: this currently holds cachedReticleOrigin, an unprojected
    // reticle-ray point, NOT the camera centre. A fresh pre-write snapshot does
    // not make it independent of aim.reticle. Anchor separation is still pending;
    // see docs/RE-IK-CROSSTALK-2026-09-09.md before treating this as a head anchor.
    Vec3 nativeEye{};
    // **The camera centre, which is what a head-relative offset must be rotated
    // about.** `nativeEye` above is the native cached RETICLE ray origin -- an
    // unprojected screen point that moves when the reticle moves, and our own
    // reticle lane writes that reticle. Using it as the shared hand anchor feeds
    // the right controller's aim back into BOTH hands' world positions, which is
    // the reported left-hand drag (RE-IK-CROSSTALK-2026-09-09).
    //
    // Read from the same view camera as the declared frustum, so the anchor and
    // the projection cannot come from two different frames. Invalid means the
    // camera could not be read: consumers must refuse rather than fall back to
    // the reticle point, because falling back silently restores the defect.
    Vec3 cameraCentre{};
    bool cameraCentreValid = false;
    // The play-space -> engine-world yaw, the one every lane must rotate a
    // head-relative offset by. See SetAimBodyYaw for why it is not simply
    // cameraYaw - referenceYaw.
    float yaw = 0.0f;
    float cameraYaw = 0.0f;
    float headYaw = 0.0f;
    bool headYawUsable = false;
    float referenceYaw = 0.0f;
    std::uint64_t referenceGeneration = 0;
    std::uintptr_t player = 0;
    std::uint64_t publishedNs = 0;
};
bool EnsureGameplayPoseObservation();
bool TryGetGameplayPoseFrame(GameplayPoseFrame& out, bool requireTracking = true);

// Which yaw rotates a head-relative controller offset into the world.
//
// **1 (default): body yaw.** The engine's view camera CARRIES head tracking --
// this mod writes it -- so its yaw is `body + (head - reference)`. Rotating an
// offset that is already measured from the head by that angle applies the head
// twice, and the hand and weapon swing with the headset. A wearer reported
// exactly that on 2026-09-07: "the hand and gun yaw WITH the hmd yaw."
// Subtracting the head's own yaw cancels it: `playSpace = camera - head`.
// This is the composition the old hand lane called BodyYaw(), quoting the
// fleet playbook -- parenting the shoulders to the HMD is the obvious
// implementation and it is wrong.
//
// **0: the previous camera-relative yaw**, kept so the two can be compared
// live rather than argued about. Falls back to 0's behaviour whenever the head
// yaw is unusable (near-vertical), whose fix is to reject rather than invent.
DWORD SetAimBodyYaw(unsigned int enabled);
unsigned int AimBodyYawEnabled();
// Frames that held the last defined head yaw because the wearer was looking
// near-vertical, and frames refused because no head yaw has ever been defined.
// Held frames are correct, not degraded: the body has not turned.
unsigned long long AimHeadYawHeldCount();
unsigned long long AimHeadYawUnavailableCount();
int AimCameraYawMilliDegrees();
int AimHeadYawMilliDegrees();
int AimPlaySpaceYawMilliDegrees();
// The world anchor the hand is placed against (Prey's cached eye) and the
// tracked head it is measured from, both in millimetres. The composition
// assumes these translate together; where they do not, the difference appears
// as hand movement driven by head movement. Correlating each against head yaw
// with a motionless controller localises that in one measurement.
int AimNativeEyeMillimetres(unsigned int axis);
int AimTrackedHeadMillimetres(unsigned int axis);

// The one per-frame answer to where the weapon aims, for every lane that needs
// it. Published by the aim hook because that is where a coherent controller
// sample, the engine's own eye anchor and the reference frame already meet.
//
// Consumers must call `preyvr::aim::Usable` with the CURRENT generations rather
// than trusting the sample they were handed: it is a snapshot, and a weapon
// change or a recentre invalidates it without anything notifying them.
bool TryGetAimSample(preyvr::aim::Sample& out);
// Main render seam only, after installing the camera for the eye being drawn.
void UpdateAimReticleForRender();
unsigned long long AimSamplePublishedCount();

// The published world aim direction, and the raw controller orientation behind
// it, in thousandths. Under a pure head rotation both must hold still: the
// controller pose is located in LOCAL space and the reference yaw is the body
// yaw. A headset measurement says otherwise, so these exist to name which.
int AimDirectionMilli(unsigned int axis);
int AimRawOrientationMilli(unsigned int axis);

DWORD SetAimTakeoverEnabled(unsigned int enabled);
// 0 preserves native origin; 1 uses the tracked aim point (diagnostic).
// The controller point is NOT the authored muzzle; per-weapon barrel alignment
// and native wall-clip fallback require independent validation.
// 0 keeps Prey's native eye origin, 1 uses the tracked hand, 2 uses the
// CALIBRATED MUZZLE and falls back to the hand when none is available.
//
// Mode 2 is the reconciliation: Prey's projectile already leaves the weapon's
// authored muzzle helper while the reticle ray starts at the eye, so the two
// agree at exactly one distance. Sharing an origin removes that.
DWORD SetAimOriginFromHand(unsigned int mode);
unsigned int AimOriginMode();
unsigned long long AimMuzzleOriginAppliedCount();
// Frames that asked for the muzzle and did not get one -- uncalibrated, or a
// weapon change since. Counted, because falling back silently to a less
// accurate origin is how a lane looks fine and aims wrong.
unsigned long long AimMuzzleUnavailableCount();
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
