#pragma once

#include "preyvr/VrMath.h"

// The pure half of the animation-driven-IK lane (H-021).
//
// Prey drives its first-person arms with CryEngine's animation-driven IK: per
// arm, a *target* joint whose absolute pose is the goal and a *weight* joint
// whose relative X is the blend. `ProcessAnimationDrivenIK` solves the limb to
// the target, slerps the wrist to the target's rotation, and re-propagates the
// fingers. Everything here exists to hand that pass the right numbers and to be
// testable without a game: frame conversion, reach clamping, and the rotation
// calibration that keeps the animators' hand orientation relative to the
// controller.
namespace preyvr::animik {

// `SAnimationPoseModifierParams` +0x14 / +0x24 / +0x30 -- the character's own
// model-to-world transform for this animation frame, as handed to the IK pass.
// Using it, rather than a yaw approximation of the body, is the point of the
// lane: the engine's inverse is exact by construction.
struct Location {
    Quaternion q{};
    Vec3 t{};
    float s = 1.0f;
};

// World -> model, exactly what `COperatorQueue::Execute` does for
// `eOp_OverrideWorld`, plus the scale the engine's own path ignores.
Vec3 WorldToModel(const Location& location, const Vec3& world);
Quaternion WorldToModel(const Location& location, const Quaternion& world);
Vec3 ModelToWorld(const Location& location, const Vec3& model);

// The controller placed relative to the HEAD, at the engine's eye point.
//
//   world = eye + Rz(yaw) * ToEngine(controller - head)
//
// The head is the one tracked point whose world position the engine already
// owns (the eye). Differencing the controller against it means the play-space
// origin and the floor height never enter the calculation, and -- FAIL-HAND-037
// -- the eye point must be the engine's single cached one, never the view
// camera, which alternates by half an IPD per eye under synthetic stereo.
Pose ControllerWorldFromHead(float yawRadians, const Vec3& eyeWorld,
                             const Pose& openXrHead, const Pose& openXrController);

// Keeps the goal within the limb's authored reach. Prey's two-bone leaf
// stretches the forearm up to 1.25x rather than refuse an unreachable goal
// (H-018), which reads as a rubber arm. BioshockVR clamped for the same reason
// against its native AimIK.
Vec3 ClampToReach(const Vec3& upperJoint, const Vec3& goal, float reach);

// Rotation calibration. The controller's grip frame and the hand bone's
// authored frame differ by a constant; capturing it once, as the rotation that
// takes the controller's model-space orientation to the animated wrist's at
// that instant, keeps the animators' hand orientation and lets the controller
// turn it from there.
//
//   offset = conj(controllerModel) * wristModel
//   target = controllerModel * offset
Quaternion CalibrateRotationOffset(const Quaternion& controllerModel,
                                   const Quaternion& wristModel);
Quaternion ApplyRotationOffset(const Quaternion& controllerModel,
                               const Quaternion& offset);

// Yaw of a Z-up rotation, radians; for the report.
float YawOf(const Quaternion& q);

} // namespace preyvr::animik
