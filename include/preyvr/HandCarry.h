#pragma once

#include "preyvr/VrMath.h"

#include <array>
#include <cstdint>

// World interaction, phase 2: what is carried sits in the LEFT hand, follows it,
// and leaves it with the hand's own velocity.
//
// Phase 1 (InteractionUse.h) chose what the native selector looks at and when the
// native use button is down; the carry itself stayed the game's, an object
// floating in front of the camera. Here the mod takes only three things from the
// native carry, each at one narrow seam, and leaves the rest -- pick-up rules,
// leverage, collision classes, the grabber's own pull and constraint, sounds,
// callbacks -- to the game:
//
//  * WHERE the carried entity is pulled to (ArkPlayerCarry::GetLerpTargetLocation
//    for props, GetDragCorpseConstraintPos for a dragged body): the hand.
//  * WHEN it is let go: when the grip opens (VR's "hold to hold"), through the
//    game's own Drop / Throw.
//  * HOW FAST it leaves: the hand's velocity and spin at the release, set on the
//    physical entity right after the native release applied its own impulse.
//
// Three kinds of carry, the user's choices (2026-10-07):
//  * Light props (towel, shoe, mug): in the palm, rotating with the hand. The
//    native lerp flies them from up to 2.5 m away into it: the "gravity glove"
//    pull without a wrist flick.
//  * Heavy props (bench, the ones that need the button held): ahead of the hand
//    at a distance, keeping their yaw relative to the player, the grabber's own
//    lag selling the weight. One hand is enough.
//  * Bodies: the drag point follows the hand, low and ahead, limited so it never
//    pulls into the player and never strays far enough to break the drag.
//
// Pure decisions, testable offline; the DLL (CarryLane.cpp) supplies the frame's
// inputs and carries out the outputs.
namespace preyvr::carry {

// --- vector helpers shared with the DLL -----------------------------------------
Vec3 Add(Vec3 a, Vec3 b);
Vec3 Sub(Vec3 a, Vec3 b);
Vec3 Scale(Vec3 a, float s);
float Dot(Vec3 a, Vec3 b);
float Length(Vec3 a);
bool Finite(Vec3 v);
bool Finite(Quaternion q);
Quaternion Inverse(Quaternion q);   // unit quaternions
// Rotation of a Matrix34 (row-major 3x4, engine convention), scale removed.
Quaternion FromMatrix34(const float m[12]);
// A vector in the app's OpenXR tracking space, in the engine world: the axis
// change, then the play space's yaw (GameplayPoseFrame::yaw).
Vec3 TrackingToWorld(Vec3 v, float yawRadians);
// Engine Z is up.
Vec3 Horizontal(Vec3 v);
Quaternion YawRotation(float radians);   // about engine Z
float YawOf(Vec3 horizontalDirection);   // atan2 of a direction in the XY plane

// --- the hand's motion, for throwing ---------------------------------------------
//
// A throw is judged by the hand's velocity at the instant it opens -- and that
// instant is the worst one to read: a hand opening is already braking. Every VR
// game that throws well reads the PEAK of the last ~0.1 s instead. The runtime's
// own velocity (XrSpaceVelocity) is preferred when it reports one: it is
// filtered by the tracker, at the tracker's rate. Without it the velocity comes
// from finite differences of the poses over at least `spanSeconds`, which keeps
// one frame of pose noise from becoming a velocity spike.
struct MotionSample {
    std::int64_t time = 0;   // ns, the display time the poses were located at
    Vec3 position{};         // OpenXR tracking space, metres
    Quaternion orientation{};
    bool velocityValid = false;
    Vec3 velocity{}, angular{};   // runtime-reported, tracking space
};

struct MotionSettings {
    float windowSeconds = 0.12f;
    float spanSeconds = 0.025f;
    float maxGapSeconds = 0.06f;   // a gap this long (tracking lost, a stall) cuts the history
};

struct Motion {
    bool valid = false;
    bool fromRuntime = false;
    Vec3 velocity{}, angular{};   // tracking space; angular in rad/s about tracking axes
    float speed = 0;
    std::int64_t at = 0;          // the sample the estimate is centred on
    unsigned samples = 0;         // samples inside the window
};

class MotionHistory {
public:
    static constexpr unsigned kCapacity = 96;
    // Out-of-order or repeated times are ignored; a gap restarts the history.
    void Push(const MotionSample& sample, const MotionSettings& settings = {});
    void Reset();
    unsigned Size() const { return count_; }
    bool Latest(MotionSample& out) const;
    // The release velocity as of `now` (ns; 0 = the latest sample).
    Motion Estimate(std::int64_t now = 0, const MotionSettings& settings = {}) const;

private:
    const MotionSample& At(unsigned age) const;   // 0 = newest
    std::array<MotionSample, kCapacity> samples_{};
    unsigned head_ = 0, count_ = 0;
};

// --- what is carried ---------------------------------------------------------------
enum class Kind : std::uint8_t { Native, Light, Heavy, Corpse };
const char* KindName(Kind kind);

struct ClassifyInput {
    bool articulated = false;   // a ragdoll: the drag-corpse path
    bool safeCarry = false;     // turrets: the native placement keeps them upright; leave it
    bool handValid = true;
    float mass = 0;             // kg; 0 = unknown
    float diagonal = 0;         // local bounds diagonal, m
    float hold = 0;             // seconds the native button had to be held to carry it
};
struct KindSettings {
    float heavyMass = 25.0f;
    float heavyDiagonal = 1.2f;
    float heavyHold = 0.5f;     // leverage props need ~0.75 s; the generic "hold" mode is ~0.33 s
};
Kind Classify(const ClassifyInput& in, const KindSettings& settings = {});

// --- placement ----------------------------------------------------------------------
struct Box {
    Vec3 min{}, max{};   // entity-local bounds
};
Vec3 Centre(const Box& box);
Vec3 HalfExtents(const Box& box);
// Half the box's thickness along a world direction, the box turned by `rotation`.
float Support(const Box& box, Quaternion rotation, Vec3 direction);

struct EntityPose {
    Vec3 position{};          // the entity's pivot, which is what the native target is
    Quaternion rotation{};
};

struct HoldSettings {
    // The grip pose's origin is the middle of the controller's handle: in the
    // fist, this far out of the palm (HandPose.h GripPoint::outOfPalm).
    float palmClearance = 0.035f;
    float maxPush = 0.20f;       // the furthest a big light object's centre moves out of the palm
    float heavyMaxShift = 0.60f; // heavy: how far the hand moves it from the native place, horizontally
    float heavyMaxRise = 0.45f;  // ... upwards
    float heavyMaxDrop = 0.45f;  // ... downwards
};

// A light object in the palm: the rotation the hand had to the object at the
// grab, kept; its centre in the fist, pushed out along the palm normal until
// its near side rests on the palm.
EntityPose LightHold(const Pose& gripWorld, Vec3 palmNormalWorld, Quaternion handToObject, const Box& local,
                     const HoldSettings& settings = {});

// A heavy object stays where the GAME carries it -- its own carry distance
// and orientation for that object, which keep a 3 m bench clear of the player
// and of the walls (measured 2026-10-07: the native target put the bench's
// centre 0.38 m from the hand; a placement "ahead of the hand by its half
// length" drove it into the wall and the grabber dropped it as stuck) -- moved
// by where the hand has gone since the grab: the hand's offset from the head
// now, minus that offset at the grab turned with the play space. One hand
// steers it around and up and down; letting the hand rest leaves it native.
struct HeavyGrab {
    Vec3 handOffset{};   // hand - head at the grab, world
    float yaw = 0;       // the play space's yaw then
};
Vec3 HeavyShift(Vec3 hand, Vec3 head, float yaw, const HeavyGrab& grab, const HoldSettings& settings = {});

// --- the body drag point ------------------------------------------------------------
struct CorpseSettings {
    float minDistance = 0.75f;   // horizontal, from the player's feet: never pulled into the player
    float minHeight = 0.20f;     // above the feet
    float maxAboveNative = 0.15f;// above the native drag height
    float maxFromNative = 1.10f; // from the native drag point (the player's chest): well inside the
                                 // drag's break distance (2.6 m in this save)
};
Vec3 CorpsePoint(Vec3 hand, Vec3 feet, Vec3 native, const CorpseSettings& settings = {});

// Critically damped follow, for the weight of what is dragged.
class Follow {
public:
    Vec3 Update(Vec3 target, float dt, float tau);
    void Reset(Vec3 value) { value_ = value; primed_ = true; }
    void Clear() { primed_ = false; }
    bool Primed() const { return primed_; }
    Vec3 Value() const { return value_; }

private:
    Vec3 value_{};
    bool primed_ = false;
};

// --- the release ----------------------------------------------------------------------
struct ThrowSettings {
    // A thrown object has no weight in a VR hand, so players throw slower than
    // they would a real one; VR games scale the release a little.
    float gain = 1.4f;
    float throwSpeed = 1.6f;     // hand speed (m/s) from which a release is a THROW, not a drop
    float maxSpeed = 16.0f;      // the fastest a light object leaves the hand
    float heavyMaxSpeed = 5.0f;
    float maxSpin = 25.0f;       // rad/s
    float heavySpinScale = 0.25f;
};
struct Release {
    bool thrown = false;
    float handSpeed = 0;         // m/s, before gain
    Vec3 velocity{};             // world: the player's own velocity + the hand's
    Vec3 angular{};              // world, rad/s
    bool capped = false;
};
Release ComputeRelease(Vec3 handVelocityWorld, Vec3 handAngularWorld, Vec3 playerVelocity, Kind kind,
                       const ThrowSettings& settings = {});

}  // namespace preyvr::carry
