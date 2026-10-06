#pragma once

#include "preyvr/VrMath.h"

// The pure half of the VR hand pose (jordi/hand-pose).
//
// **What the player's hand should look like, as numbers.** The arms rig is the
// game's first-person skeleton (101 joints): per hand a wrist (`*_hand_jnt`),
// four metacarpals (`*_indexBase_jnt`...) each followed by three phalanges
// (`*_index1/2/3_jnt`), and a thumb (`*_thumb1/2/3_jnt`). Measured on that rig:
// every finger bone runs along its joint's local +X and flexes about local Z,
// negative towards the palm; the palm faces local -Y on both hands; the thumb
// sits on +Z on the left hand and -Z on the right.
//
// Two things are defined here and nowhere else:
//
//  * **The anatomical frame of a hand** -- length (wrist -> knuckles), thumb
//    side and palm normal (pointing OUT of the palm) -- both for the game's
//    hand, from its own bone offsets, and for the player's real hand, from the
//    OpenXR GRIP pose by the spec's definition of its axes: +X is the palm
//    normal (out of the palm on the left hand, into it on the right), -Z runs
//    through the tube of the closed fingers from pinky to thumb. Mapping one
//    frame onto the other turns the controller into the wrist rotation that
//    puts the game's palm where the real palm is, whatever weapon was held
//    before -- unlike an offset captured from the animation at some instant.
//
//  * **The relaxed open hand**: the relative rotation of every finger joint,
//    one fixed pose, used for a hand that holds nothing.
namespace preyvr::handpose {

enum class Side : int { right = 0, left = 1 };

// Finger joints by role. Parents are listed in ParentRole (-1 = the wrist).
enum Joint : int {
    kThumb1, kThumb2, kThumb3,
    kIndexBase, kIndex1, kIndex2, kIndex3,
    kMiddleBase, kMiddle1, kMiddle2, kMiddle3,
    kRingBase, kRing1, kRing2, kRing3,
    kPinkyBase, kPinky1, kPinky2, kPinky3,
    kJointCount
};

// "l_index1_jnt" etc.; the rig's own names.
const char* JointName(Side side, int joint);
const char* WristName(Side side);
int ParentRole(int joint);

// The relaxed open hand. `curl` scales every flexion (1 = the default relaxed
// hand, 0 = straight fingers). Defined for the left hand, the free one.
Quaternion RelaxedRelative(Side side, int joint, float curl);

// An orthonormal anatomical frame. `palm` points out of the palm.
struct Frame {
    Vec3 length{1, 0, 0};
    Vec3 thumb{0, 0, 1};
    Vec3 palm{0, -1, 0};
};

// The game hand's frame in its wrist's local coordinates, from the knuckles
// (MCP joints): base offset + base rotation * first phalanx offset. Index
// order: index, middle, ring, pinky. `knuckleDistance` is the wrist -> mean
// knuckle distance (model units). False if the geometry is degenerate.
bool MeasureHandFrame(Side side, const Vec3 baseOffset[4], const Quaternion baseRotation[4],
                      const Vec3 firstOffset[4], Frame& frame, float& knuckleDistance);

// The player's real hand in GRIP-local OpenXR coordinates. `tubeLean` is the
// angle by which the fist's tube (pinky -> thumb) leans from perpendicular
// towards the fingers' direction (radians; ~15 deg in a power grip).
Frame GripHandFrame(Side side, float tubeLean);

// The rotation taking `from` to `to` (both orthonormal, same handedness).
Quaternion FrameToFrame(const Frame& from, const Frame& to);

// Where the grip pose's origin (the middle of the controller's handle, in the
// fist) sits in the wrist's local frame: along the hand, out of the palm and
// towards the thumb.
struct GripPoint {
    float alongHand = 0.55f;   // fraction of the wrist -> knuckle distance
    float outOfPalm = 0.035f;  // model units: palm surface + handle radius
    float towardThumb = 0.0f;
};
Vec3 GripPointInWrist(const Frame& hand, float knuckleDistance, const GripPoint& point);

// The roll about `axis` (unit) that best turns the vectors `from` onto `to`
// (least squares over the pairs; radians). Used to roll a held weapon about
// its barrel -- which keeps the barrel on the aim ray -- until the hand that
// holds it lies like the player's real hand.
float RollToMatch(const Vec3& axis, const Vec3* from, const Vec3* to, int count);
Quaternion AxisAngle(const Vec3& unitAxis, float radians);

// Spherical blend, shortest arc.
Quaternion Slerp(Quaternion a, Quaternion b, float t);

// Angle between two rotations, radians.
float AngleBetween(Quaternion a, Quaternion b);

// The reach a hand is given. Mode 0 is the original linear scaling of the
// player's reach about the shoulder (percent); mode 1 keeps the controller's
// distance from the shoulder exactly up to `knee` of the arm's reach and
// compresses only beyond it, approaching the reach smoothly instead of
// stopping dead at it.
Vec3 SoftReach(const Vec3& shoulder, const Vec3& goal, float reach, float knee);

} // namespace preyvr::handpose
