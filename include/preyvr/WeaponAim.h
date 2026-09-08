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

// **A viewport fraction is not what the HUD movie wants.** Prey's HUD draws into
// a 16:9 canvas scaled to COVER the frame and centred, so at any aspect other
// than 16:9 the canvas is larger than the frame in one axis and only its middle
// is visible. Handing the movie a plain viewport fraction therefore places the
// reticle correctly at the centre and increasingly wrongly toward the edges.
//
// Measured in-game at 2688x2880 (R-118), reticle sprite centre in frame pixels:
//
//   viewport 0.39486 -> wanted 1061, the movie drew it at ~800
//   viewport 0.50090 -> wanted 1346, drawn ~1340   (centre agrees, as it must)
//   viewport 0.60514 -> wanted 1627, drawn ~1898
//
// A line through those points gives a canvas 5222 px wide against the 5120 that
// `height * 16/9` predicts, and an implied centre within 5 px of the frame's.
// At 2560x1440 the same three angles landed on the naive prediction exactly,
// because there the canvas and the frame coincide.
//
// **Cover, not contain.** R-114 measured the MENU letterboxing at this aspect,
// which is the opposite fit. That is not a contradiction: Scaleform elements
// carry their own scale mode, and the menu and the HUD need not share one. The
// HUD's behaviour is what these numbers describe, and only the HUD's.
struct CanvasPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// Converts a viewport fraction into the fraction the HUD movie should be given
// so the symbol lands at the intended viewport position. Reduces to the identity
// at 16:9, and refuses nothing -- a degenerate frame size returns the input
// unchanged, because writing a corrected value from a bad size would be worse
// than writing the uncorrected one.
CanvasPoint ViewportToHudCanvas(float viewportX, float viewportY,
                                float frameWidth, float frameHeight);

} // namespace preyvr::aim
