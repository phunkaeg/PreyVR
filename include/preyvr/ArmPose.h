#pragma once

#include "preyvr/VrMath.h"

// The pure half of the VR arm (jordi/polishing): **the arm comes from where
// the player's shoulder is.**
//
// The game draws its first-person arms from the shoulders its weapon
// animation authored: measured on the mock (docs/ARM-POSE-2026-10-06.md),
// the GLOO's idle protracts the left clavicle 14 cm and retracts the right
// 7 cm, the pistol's pulls the right shoulder 12 cm back and 10 cm inwards,
// and the shoulders never move with the player's body. The native two-bone
// solve then hangs the arm from there, so the hand falls short of the
// controller at full extension and the elbow lands 10-30 cm from where the
// player's is.
//
// What is defined here, as numbers:
//
//  * **The player's torso** -- a yaw-only frame from the HMD, which follows the
//    head's yaw with a deadzone (turning the head alone does not turn the
//    shoulders) and relaxes towards it;
//  * **the player's shoulder** -- an adult's glenohumeral centre relative to
//    the centre eye in that frame (the anthropometry Dishonored VR uses),
//    reached by rotating the rig's clavicle about its root;
//  * **the elbow** -- of the circle of elbows that reach the wrist, the one
//    whose wrist bends the way wrists can (little side deviation, flexion
//    within its range), the anatomical direction (down, out, a little back)
//    breaking ties: the forearm follows the hand's twist.
//
// All in one right-handed frame with +Y up and -Z forward (OpenXR), metres.
namespace preyvr::armpose {

// +1 right arm, -1 left arm.
using Side = int;

struct Body {
    // The shoulder joint from the centre eye, in the torso frame (m):
    // forward, outwards (to this arm's side) and up.
    float shoulderForward = -0.09f;
    float shoulderOut = 0.185f;
    float shoulderUp = -0.23f;
    // The drawn arm's segments (m): shoulder -> elbow, elbow -> wrist.
    float upper = 0.285f;
    float fore = 0.275f;
    // How far the clavicle may lengthen or shorten (fraction) to put the
    // shoulder on its target, and the arm's segments to reach the wrist.
    float clavicleStretch = 0.15f;
    float armStretch = 1.10f;
};

struct TorsoFrame {
    Vec3 forward{0, 0, -1};
    Vec3 right{1, 0, 0};
    Vec3 up{0, 1, 0};
};

// The yaw of a horizontal forward (0 = -Z, positive turning left, about +Y).
float YawOf(const Vec3& forward);
TorsoFrame TorsoFromYaw(float yaw);
// The head's yaw, robust looking straight down or up: the head's up vector
// stands in for its forward as the pitch approaches +-90 degrees.
float HeadYaw(const Quaternion& head);

// The torso follows the head's yaw: never further than `deadzone` from it,
// and relaxing towards it at `relax` (fraction per second) while within.
struct TorsoYaw {
    bool valid = false;
    float yaw = 0.0f;
    float Update(float headYaw, float dtSeconds, float deadzone, float relax);
    void Reset() { valid = false; }
};

// Where the player's shoulder joint is.
Vec3 ShoulderTarget(const Vec3& eye, const TorsoFrame& torso, Side side, const Body& body);

// The clavicle's end: from its root towards `target`, at the clavicle's own
// length stretched by at most `stretch` either way.
Vec3 PlaceShoulder(const Vec3& clavicleRoot, float clavicleLength, const Vec3& target, float stretch);

// The anatomical elbow direction for this arm: down, out and a little back.
Vec3 BodyPole(const TorsoFrame& torso, Side side);

// The wrist's angles for a forearm direction `forearm` (elbow -> wrist, unit)
// and the hand's anatomical frame (`length` wrist -> knuckles, `palm` out of
// the palm, both unit). Flexion bends the hand towards its palm (positive) or
// its back (negative); deviation tilts it towards the thumb or the little
// finger (magnitude only). Degrees.
float WristFlexionDeg(const Vec3& forearm, const Vec3& length, const Vec3& palm);
float WristDeviationDeg(const Vec3& forearm, const Vec3& length, const Vec3& palm);
// Signed: towards the thumb (radial) positive, towards the little finger
// (ulnar) negative. The thumb is on length x palm for the right hand.
float WristRadialDeg(const Vec3& forearm, const Vec3& length, const Vec3& palm, Side side);

// How likely a wrist and an elbow are, as a MAP estimate's priors (degrees).
// A wrist bends freely towards the palm or its back, little sideways, and a
// hand round a controller sits a little ulnar; beyond its range of motion it
// cannot go at all. The elbow swivels about the shoulder-wrist axis around its
// natural place (BodyPole).
struct WristLimits {
    float flexionSigmaDeg = 45.0f;
    float deviationRestDeg = -8.0f;   // ulnar
    float deviationSigmaDeg = 12.0f;
    float swivelSigmaDeg = 25.0f;
    // Range of motion, and how steep its wall is.
    float flexionMaxDeg = 75.0f, extensionMaxDeg = 65.0f;
    float radialMaxDeg = 20.0f, ulnarMaxDeg = 35.0f;
    float wallDeg = 5.0f;
    // The previous frame's elbow, as one more prior: no flips between
    // near-equal choices from frame to frame.
    float holdSigmaDeg = 45.0f;
};
// Tuned offline against known arms (build/jordi-mock/elbowsim.py, 400 each):
// natural arms (swivel, flexion, deviation spread as players make them) mean
// elbow error 4.5 cm, p90 9.7, against 6.9 / 15.1 for the anatomical direction
// alone; controllers held at the edge of the wrist's range with the elbow in
// its natural place 4.6 / 8.7 (the anatomical direction, right by
// construction there, 3.2 / 6.9) -- an impossible wrist does not raise it.

// **The elbow the hand allows.** Around the circle of elbows that reach the
// wrist, the most likely one: the wrist bending as wrists do, within its
// range, and the elbow near its natural place -- so the forearm follows the
// hand's twist, a hand that says nothing leaves the elbow hanging, and a hand
// that would need an impossible wrist raises the elbow as a person's would.
// Returns the pole that selects it (unit, from the circle's centre towards the
// elbow).
Vec3 ChooseElbowPole(const Vec3& shoulder, const Vec3& wrist, float upper, float fore, float maxStretch,
                     const Vec3& handLength, const Vec3& handPalm, Side side, const Vec3& bodyPole,
                     const WristLimits& limits, const Vec3* previousPole = nullptr);

struct ArmSolution {
    Vec3 elbow{};
    float scale = 1.0f;     // applied to both segments to reach
    bool straight = false;  // the wrist is at (or beyond) the stretched reach
};
// Two-bone solve: the elbow on the circle where both segments meet, towards
// `pole`. Beyond the reach the segments lengthen (up to `maxStretch`); beyond
// that the arm points straight at the wrist and the caller decides.
ArmSolution SolveElbow(const Vec3& shoulder, const Vec3& wrist, float upper, float fore,
                       const Vec3& pole, float maxStretch);

// The shortest rotation taking unit `from` onto unit `to`.
Quaternion FromTo(const Vec3& from, const Vec3& to);

// Exponential smoothing of a direction (unit), time constant `tau` seconds.
Vec3 SmoothDirection(const Vec3& previous, const Vec3& current, float dtSeconds, float tau);

} // namespace preyvr::armpose
