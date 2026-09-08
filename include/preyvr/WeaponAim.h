#pragma once

#include "preyvr/VrMath.h"

#include <cstdint>

// One immutable description of where the weapon is aiming, for this frame.
//
// **The reconciliation this project needs.** Three lanes currently answer the
// same question from three different samples: the hand lane places the wrist,
// the aim lane rewrites the cached reticle ray, and the reticle lane projects a
// screen position. Each reads its own controller pose, its own reference frame
// and its own eye anchor, at its own moment. So the rendered barrel, the ray
// gameplay uses, and the crosshair a player aims with can all disagree -- and
// each disagreement looks like a separate bug with its own plausible cause.
//
// H-021 section 3 and the reticle report both arrive at the same instruction:
// establish one per-frame record, and make every consumer use *that sample* or
// state explicitly that it is using a different one.
//
// This type is the record. It is pure, so the arithmetic that builds it is
// testable without the game, and it carries its own provenance so a consumer can
// tell a fresh sample from a stale one rather than assuming.
namespace preyvr::aim {

// What a consumer is allowed to do with a sample.
enum class Confidence : std::uint32_t {
    // No usable pose. Consumers must leave the engine's own values alone.
    none = 0,
    // A tracked controller, but no calibrated grip-to-barrel rotation. The
    // ORIGIN is trustworthy and the direction is the raw pointing axis, which is
    // not the same as where the barrel points.
    origin = 1,
    // Origin and a calibrated barrel direction.
    barrel = 2,
};

struct Sample {
    // Where the shot starts, world space.
    Vec3 origin{};
    // Unit forward along the barrel, world space.
    Vec3 direction{};
    // The full orientation, for consumers that need more than a ray.
    Quaternion orientation{};

    // --- provenance -------------------------------------------------------
    //
    // Identity, not decoration. F-009: the viewmodel is a different object after
    // every weapon change, so a sample outliving its rig is worse than none.
    std::uint64_t equipGeneration = 0;
    std::uint64_t trackingSequence = 0;
    std::uint64_t referenceGeneration = 0;
    std::uint64_t publishedNs = 0;
    Confidence confidence = Confidence::none;
};

// True when `sample` may still be used: it carries a real pose, its rig has not
// been re-equipped, the tracking epoch has not been reset, and it is younger
// than `maximumAgeNs`.
//
// Deliberately takes the current generations rather than reading globals, so the
// rule is testable and a caller cannot forget one of the three checks.
bool Usable(const Sample& sample,
            std::uint64_t currentEquipGeneration,
            std::uint64_t currentReferenceGeneration,
            std::uint64_t nowNs,
            std::uint64_t maximumAgeNs = 200000000ull);

// The barrel direction for a rigid weapon: the tracked grip, then the calibrated
// grip-to-barrel rotation.
//
// **An authored attachment default is not a barrel axis** and neither is the
// model's +Y. The offset has to be established per weapon and carried here
// explicitly, so a consumer can see whether one exists rather than assuming the
// grip forward will do.
Sample Compose(const Pose& gripWorld,
               const Quaternion& gripToBarrel,
               bool haveBarrelCalibration);

// Where a world point lands on screen for one eye, as normalised [0,1] with the
// origin top-left, matching Prey's cached reticle position.
//
// **Per eye, from that eye's own view-projection.** A single screen position
// cannot be correct for both eyes at a depth that is not infinity: projecting
// once and reusing it puts the crosshair at a different world depth than the
// thing it is over, which reads as a reticle that will not fuse.
struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
    bool onScreen = false;
    bool behind = false;
};
ScreenPoint ProjectToScreen(const Vec3& worldPoint,
                            const Vec3& eyePosition,
                            const Quaternion& eyeOrientation,
                            float tanLeft, float tanRight,
                            float tanUp, float tanDown);

} // namespace preyvr::aim
