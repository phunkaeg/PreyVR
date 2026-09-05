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

} // namespace preyvr::controller
