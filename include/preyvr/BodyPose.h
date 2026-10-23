#pragma once

#include "preyvr/VrMath.h"

// The pure half of the visible body (src/dll/AnimIkTakeover.cpp): where the
// first-person body's joints go so that **its neck is always where the
// player's is**, the way full-body VR mods do it (VRIK, FinalIK's VRIK, Quake
// VR 1.0.1; docs/BODY-POSE-2026-10-17.md):
//
//  * the torso faces the player's torso estimate (armpose::TorsoEstimator),
//    turned about the vertical through the head's pivot -- the head itself
//    keeps facing where the player looks, so the neck twists as a real one does;
//  * the spine bends (most of it low, as a hip hinge) to put the head's pivot
//    over the player's horizontally: a lean over a table, or the half step the
//    body has not caught up with yet;
//  * the hips drop or rise to put it at the player's height, sitting back as
//    they drop, and the legs fold with a two-bone solve onto feet that stay
//    where the animation planted them.
//
// Everything is in the character's model space: Z up, +Y forward, metres,
// as the absolute pose array holds it. A Rigid maps a joint's animated
// absolute transform to its new one: position p -> q * p + t, rotation
// r -> q * r.
namespace preyvr::bodypose {

struct Rigid {
    Quaternion q{};
    Vec3 t{};
};
Vec3 Apply(const Rigid& r, const Vec3& p);
// `second` after `first`.
Rigid Then(const Rigid& first, const Rigid& second);

// The animated pose's key joints, model space.
struct Skeleton {
    Vec3 hip{};
    Vec3 spine[4]{};
    Vec3 neck{};
    Vec3 head{};                    // the head's pivot (head_jnt)
    Vec3 thigh[2]{}, knee[2]{}, foot[2]{};   // upperLeg, lowerLeg, foot: 0 right, 1 left
    bool legs = false;
};

struct Goal {
    float yaw = 0.0f;               // the torso's turn about +Z (positive turns left)
    Vec3 head{};                    // where the head's pivot must be
    float weight = 1.0f;            // 0 = the animation, 1 = all of it
    // Where each foot must stand (FootPlanter), instead of where the turned
    // animation puts it; the foot keeps the animation's rotation.
    bool footSet[2]{};
    Vec3 foot[2]{};
};

struct Limits {
    // The spine's bend, radians, by direction in the turned torso's frame.
    float forward = 0.80f, back = 0.26f, side = 0.45f;
    // How the bend is shared down the spine (spine1 is the hip hinge).
    float share[4] = {0.40f, 0.25f, 0.20f, 0.15f};
    // The neck takes back this part of the bend, so the head stays nearer upright.
    float neckUpright = 0.5f;
    // The hips go back by this part of a drop, as a squat's do.
    float sitBack = 0.35f;
    float maxSitBack = 0.20f;
    // The hips may slide this far horizontally when the spine has bent all it can.
    float slide = 0.08f;
    float maxDrop = 0.65f;          // the deepest squat
    float straight = 0.995f;        // the longest a leg may get, of its full length
};

struct Plan {
    bool valid = false;
    Rigid hip, spine[4], neck, thigh[2], shin[2], foot[2];
    // What it did: the bend (radians) and the hips' move, and how far the head
    // pivot ends from the goal (what the body could not reach).
    float bend = 0.0f;
    Vec3 hipShift{};
    Vec3 headReached{};
    float miss = 0.0f;
};

Plan Solve(const Skeleton& skeleton, const Goal& goal, const Limits& limits = Limits{});

// The torso's turn alone (what Solve starts from): where the turned animation
// would put a joint, before any bend or drop.
Rigid TorsoTurn(const Skeleton& skeleton, const Goal& goal);

// **Planted feet** (FinalIK VRIK's locomotion, Quake VR's planted feet): each
// foot stays where it was put while the body sways and turns over it, and steps
// -- one at a time, in a short arc -- once it has drifted too far from where the
// body would stand it or the leg could no longer reach it. World space, Z up.
struct FootSettings {
    float drift = 0.20f;            // metres from where the body would stand it
    float reach = 0.97f;            // of the full leg, from the thigh
    float lift = 0.07f;             // the arc's height
    float duration = 0.32f;         // seconds, shorter as the body moves faster
    float minDuration = 0.18f;
};
class FootPlanter {
public:
    // natural: where each foot would be; thigh: each leg's root; leg: full lengths.
    // free: follow the natural feet (the game is animating a walk).
    void Update(const Vec3 natural[2], const Vec3 thigh[2], const float leg[2], float bodySpeed, float dt,
                bool free, const FootSettings& s, Vec3 out[2]);
    void Reset() { valid_ = false; stepping_[0] = stepping_[1] = false; }
    bool Stepping(int k) const { return stepping_[k]; }
    unsigned long long Steps() const { return steps_; }
private:
    bool valid_ = false;
    Vec3 planted_[2]{};
    bool stepping_[2]{};
    Vec3 from_[2]{};
    float t_[2]{}, duration_[2]{};
    unsigned long long steps_ = 0;
};

} // namespace preyvr::bodypose
