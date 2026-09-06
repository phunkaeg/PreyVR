#pragma once

#include "preyvr/StereoCamera.h"
#include "preyvr/VrMath.h"

#include <cstdint>
#include <optional>

// Motion controllers: turning a tracked hand into an aim ray, a weapon pose, and
// a two-handed grip.
//
// Pure, so the geometry can be settled before any of it is wired to Prey. The
// engine-side unknowns are real and stated where they bite -- in particular we
// do not yet know where Prey writes the viewmodel transform each frame, so
// `WeaponPoseFromController` computes the pose that *would* be written rather
// than pretending to know how to apply it.
//
// The fail-closed rule throughout: when a pose is not usable, return nothing and
// let the caller keep the game's own behaviour. A stale or untracked controller
// must never silently produce a plausible-looking ray, because a plausible wrong
// aim is worse than no aim -- it fires.
namespace preyvr::controller {

// A ray in engine world space.
struct AimRay {
    Vec3 origin{};
    Vec3 direction{}; // unit length
};

// Controller forward. OpenXR's grip and aim poses both point along -Z, which
// becomes engine +Y under the basis change -- the same "forward is column 1"
// convention CCamera uses, so the controller's forward and the camera's forward
// are directly comparable without a second correction.
Vec3 ForwardOf(const Pose& engineSpacePose);

// Converts a raw OpenXR controller pose into engine world space using the same
// reference frame the eyes use, so the hand and the view cannot drift apart.
Pose ControllerPoseInWorld(
    const stereo::ReferenceFrame& reference,
    const Pose& openXrControllerPose);

// The aim ray, gated on tracking validity. Returns nothing when the pose is
// unusable; the caller then leaves Prey's own reticle ray (R-012) alone.
std::optional<AimRay> AimFromController(
    const stereo::ReferenceFrame& reference,
    const Pose& openXrControllerPose,
    const PoseValidity& validity,
    std::uint64_t maxAgeNanoseconds);

// ---------------------------------------------------------------------------
// Weapon
// ---------------------------------------------------------------------------

// Where the weapon sits relative to the hand holding it.
//
// This is the piece Crysis' VR mod calls the inverse grip transform, and it is
// per-weapon authored data: the offset from the controller to the weapon's own
// origin so that the modelled grip lands in the hand. Prey's values are unknown
// -- the transform writer has not been located yet -- so this type exists to
// hold them once measured, not to imply we have them.
struct GripTransform {
    Pose controllerToWeapon{};
};

// The weapon mount, recomposed from how far the controller has rotated since
// calibration.
//
// **This exists as a pure function because its failure mode is silent.** The
// authored mount is a *basis change* and must be composed on the right --
// `trackedDelta * authoredMount`. Composed the other way it becomes a meaningless
// residual, and the symptom looks exactly like a sign error: BioshockVR loaded a
// correct +90 degree grip offset, applied it on the wrong side, and then tried
// +90000 and -90000 millidegrees without either being closer.
//
// **The diagnostic that identifies the class:** if both extremes of a signed
// parameter fail symmetrically, the model is wrong rather than the sign.
//
// Used where an absolute model-space controller pose is not available -- the delta
// form needs only a calibration reference, where `WeaponPoseFromController` needs
// a solved model frame.
//
// Returns nothing when any input is not a usable rotation, so a caller keeps the
// engine's own mount rather than writing a degenerate one.
std::optional<Quaternion> MountRotationFromControllerDelta(
    const Quaternion& authoredMount,
    const Quaternion& calibrationAim,
    const Quaternion& currentAim);

Pose WeaponPoseFromController(
    const stereo::ReferenceFrame& reference,
    const Pose& openXrControllerPose,
    const GripTransform& grip);

// Two-handed hold: the forward axis runs from the rear hand to the front hand,
// which is what makes a rifle point where the supporting hand is rather than
// where the trigger hand happens to be twisted.
//
// `referenceUp` orthogonalises the roll. Returns nothing when the hands are too
// close together for the direction between them to be meaningful -- at small
// separations the forward axis is dominated by tracking noise and the weapon
// would spin.
std::optional<Pose> TwoHandedWeaponPose(
    const Vec3& rearHandWorld,
    const Vec3& frontHandWorld,
    const Vec3& referenceUp = Vec3{0.0f, 0.0f, 1.0f},
    float minimumSeparation = 0.10f);

// ---------------------------------------------------------------------------
// Locomotion
// ---------------------------------------------------------------------------

// Snap turn. Returns the new reference yaw.
//
// Snap rather than smooth is the default for a reason that is not stylistic:
// smooth rotation the player's inner ear does not predict is the single largest
// contributor to sim sickness, and a mod that makes people ill does not get
// played. Smooth turning is offered through `SmoothTurn` for those who prefer
// it, and the choice belongs to the player.
float SnapTurn(float currentYawRadians, int steps, float stepRadians);

float SmoothTurn(float currentYawRadians, float stickX, float radiansPerSecond, float deltaSeconds);

// Wraps to (-pi, pi] so the stored yaw cannot grow without bound over a long
// session and lose float precision.
float WrapAngle(float radians);

// ---------------------------------------------------------------------------
// Rigid rotation of a joint subtree
// ---------------------------------------------------------------------------

// One joint of a model-space pose: an absolute rotation and an absolute
// position, which is what `CPoseData`'s absolute array holds (QuatT, stride
// `0x1C`, position at `+0x10`).
struct JointPose {
    Quaternion rotation{};
    Vec3 position{};
};

// Rotates one joint of a subtree rigidly about a pivot.
//
// **Why a pivot at all.** The absolute array stores every joint's own
// model-space transform, so rotating a wrist does *not* carry its children --
// each child holds its own absolute position and would stay exactly where it
// was. Turning the wrist without this is the tearing failure the hand-rig
// header warns about, seen from the other side: the translation case needed the
// subtree displaced, and the rotation case needs it *orbited*.
//
//   position = pivot + R * (position - pivot)
//   rotation = R * rotation
//
// The pivot is the driven joint's own position, so that joint is a fixed point
// and its descendants swing around it. Composition is `R * rotation`, the same
// "basis change on the left" the weapon mount uses -- writing it the other way
// is the error that looks like a sign flip and survives every sign flip.
JointPose RotateJointAboutPivot(const JointPose& joint, const Vec3& pivot,
                                const Quaternion& rotation);

// The rotation counterpart of the hand lane's `WorldDeltaToModel`.
//
// **The two lanes were in different frames, which is a defect on its own.** The
// position delta is projected onto the body basis before it displaces a joint;
// the rotation delta was composed with the joint *raw, in world terms*. A
// displacement and a turn taken from the same controller in the same instant
// were being interpreted against two different bases.
//
// A rotation changes basis by conjugation, `qB * q * qB^-1`. Conjugating by a
// yaw about the model's up axis leaves the rotation's own Z component alone and
// rotates the other two, so the observable signature of a *missing* yaw
// conversion is **correct yaw with wrong pitch and roll** -- and at exactly 180
// degrees, pitch and roll come back cleanly inverted. That is what a wearer
// reported on 2026-09-07 for the first working wrist test, and it is why the
// fix is a frame conversion rather than a pair of sign flips: negating two Euler
// terms is not a rotation, and it would break the moment the wrist left the
// axis the signs were fitted on.
//
// `bodyYaw` is the same angle the position lane uses, so the two cannot drift
// apart again.
Quaternion WorldTurnToModel(const Quaternion& worldTurn, float bodyYaw);

} // namespace preyvr::controller
