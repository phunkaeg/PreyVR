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

bool ValidLocation(const Location& location);
bool ValidJoint(int index, unsigned int count);

// Caller serializes this state across commands and animation jobs. A request
// belongs to each selected hand and is consumed only by that hand's write.
struct CalibrationState {
    unsigned int pending = 0;        // asked for by ik.calibrate
    unsigned int autoPending = 0;    // re-queued by a re-bind, after settling
    unsigned int settleFrames = 0;
    unsigned int calibrated = 0;
    Quaternion offsets[2]{};
    std::uint64_t ownerGeneration = 0, referenceGeneration = 0, trackingEpoch = 0;
    // Re-binding after a weapon change, a recentre or a focus loss invalidates
    // the rotation offset -- it maps the controller onto *this* weapon's
    // authored grip, and the next weapon's grip is different.
    //
    // **Re-take it rather than merely dropping it.** Dropping alone leaves the
    // hand tracking position while its rotation silently stops, which a wearer
    // reported on 2026-09-08 after swapping weapons: "it seemed to reset the
    // rotational tracking and it was only tracking positionally". A hand that
    // half-works reads as a bug, not as a prompt to recalibrate, and asking a
    // player to recalibrate after every weapon swap is not a shippable answer.
    //
    // Two properties are preserved from the original contract:
    //
    //  * **No stale offset carries over** -- `calibrated` is still cleared.
    //  * **No stale *request* carries over** -- an outstanding request that
    //    never completed belonged to the old rig and is discarded. Only hands
    //    with a COMPLETED calibration are re-queued.
    //
    // And the re-take waits: `kSettleFrames` matched frames must pass first, so
    // the capture lands on the settled grip rather than midway through the
    // equip animation, which would bake a transient wrist pose into the offset.
    // A manual `ik.calibrate` is deliberate and commits on the next frame.
    static constexpr unsigned int kSettleFrames = 90;

    bool Bind(std::uint64_t owner, std::uint64_t reference, std::uint64_t epoch) {
        if (ownerGeneration == owner && referenceGeneration == reference && trackingEpoch == epoch) { return false; }
        pending = 0;                 // whatever the old rig had outstanding
        // A second rebind may arrive before the first automatic retake. In
        // that interval calibrated is zero, but autoPending still records a
        // hand which completed calibration before the transition. Preserve
        // that recovery intent across churn; never carry its old offset as valid.
        autoPending = (ownerGeneration != 0) ? (calibrated | autoPending) : 0u;
        settleFrames = 0;
        calibrated = 0;
        ownerGeneration = owner; referenceGeneration = reference; trackingEpoch = epoch;
        return true;
    }

    // One matched frame on the owning rig.
    void Tick() { if (settleFrames < kSettleFrames) { ++settleFrames; } }

    void Request(unsigned int hands) { pending |= hands & 3u; }
    // Explicit rig-signature reset is different from Bind's automatic recovery.
    void Invalidate() { pending = autoPending = settleFrames = calibrated = 0; }
    bool Pending(unsigned int hand) const {
        if (hand >= 2) { return false; }
        if (pending & (1u << hand)) { return true; }   // asked for deliberately
        return (autoPending & (1u << hand)) && settleFrames >= kSettleFrames;
    }
    // Queued by a re-bind but still settling. Reported so a wearer whose
    // rotation has not resumed yet can see that it is coming, not broken.
    bool Settling(unsigned int hand) const {
        return hand < 2 && (autoPending & (1u << hand)) && settleFrames < kSettleFrames;
    }
    void Commit(unsigned int hand, const Quaternion& offset) {
        if (hand >= 2) { return; }
        offsets[hand] = offset;
        calibrated |= 1u << hand;
        pending &= ~(1u << hand);
        autoPending &= ~(1u << hand);
    }
};

// Restore only our still-present output before a producer that can retain its
// previous value on failure. Another writer's replacement must be preserved.
struct RayOriginEdit {
    std::uintptr_t owner = 0;
    Vec3 native{};
    Vec3 written{};
    bool Restore(std::uintptr_t currentOwner, Vec3& current) const {
        if (!owner || owner != currentOwner || current.x != written.x ||
            current.y != written.y || current.z != written.z) { return false; }
        current = native;
        return true;
    }
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
// The caller must supply the actual cyclops/head world anchor. Differencing
// the controller against it removes the play-space origin and floor height.
// Prey's cached reticle-ray origin is NOT that anchor: native unprojection
// moves it with the reticle (RE-IK-CROSSTALK-2026-09-09). A live per-eye render
// camera also introduces half-IPD translation (FAIL-HAND-037). Capture a stable
// pre-eye anchor with the tracking/reference frame that produced it.
Pose ControllerWorldFromHead(float yawRadians, const Vec3& eyeWorld,
                             const Pose& openXrHead, const Pose& openXrController);

// Keeps the goal within the limb's authored reach. Prey's two-bone leaf
// stretches the forearm up to 1.25x rather than refuse an unreachable goal
// (H-018), which reads as a rubber arm. BioshockVR clamped for the same reason
// against its native AimIK.
Vec3 ClampToReach(const Vec3& upperJoint, const Vec3& goal, float reach);

// Maps a player's arm span onto the character's.
//
// A wearer measured about 70% of frames clamping on 2026-09-08: "yes, i am
// reaching the limit, i guess my arms are a bit longer than the characters".
// Clamping is the honest fallback but a poor experience -- the hand stops while
// the controller keeps going, so the last part of every reach is dead travel.
//
// Scaling the goal's distance from the shoulder trades exact 1:1 placement for
// continuous motion across the whole of the player's reach. At 1.0 behavior is
// unchanged. The reference contributes (1-scale) of its own movement to the
// result even without clamping; an aim-animated shoulder is not a stable body
// anchor for independent controller reach mapping (RE-IK-CROSSTALK-2026-09-09).
Vec3 ScaleReach(const Vec3& upperJoint, const Vec3& goal, float scale);

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
