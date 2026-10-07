#include "preyvr/HandPose.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace preyvr::handpose {
namespace {

constexpr float kDegrees = 3.14159265358979f / 180.0f;

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
bool Unit(Vec3& a) {
    const float n = Length(a);
    if (!std::isfinite(n) || n < 1e-6f) { return false; }
    a = Mul(a, 1.0f / n);
    return true;
}

// Rotation matrix (columns = images of the unit axes) -> quaternion.
Quaternion FromColumns(Vec3 c0, Vec3 c1, Vec3 c2)
{
    const float m00 = c0.x, m10 = c0.y, m20 = c0.z;
    const float m01 = c1.x, m11 = c1.y, m21 = c1.z;
    const float m02 = c2.x, m12 = c2.y, m22 = c2.z;
    const float trace = m00 + m11 + m22;
    Quaternion q{};
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return Normalize(q);
}

Quaternion AboutZ(float radians) { return {0.0f, 0.0f, std::sin(radians * 0.5f), std::cos(radians * 0.5f)}; }

// The left hand's relaxed pose. Phalanges: flexion about local Z in degrees
// (negative = towards the palm), a cascade that curls the little finger most,
// as a hand hanging at rest does. Metacarpals and the thumb's root keep the
// rig's natural palm arch: measured on the first-person rig (left hand, GLOO
// idle, 2026-10-06), where they are a few degrees from the bind pose.
struct Relaxed { Quaternion rotation; float flexDegrees; };
const Relaxed kLeft[kJointCount] = {
    /* thumb1 */ {{0.7630f, -0.3729f, 0.2534f, 0.4633f}, 0.0f},
    /* thumb2 */ {{}, -12.0f},
    /* thumb3 */ {{}, -8.0f},
    /* indexBase  */ {{0.0522f, -0.0170f, 0.0482f, 0.9973f}, 0.0f},
    /* index1/2/3 */ {{}, -8.0f}, {{}, -14.0f}, {{}, -8.0f},
    /* middleBase */ {{-0.0149f, 0.0216f, 0.0393f, 0.9989f}, 0.0f},
    /* middle1/2/3 */ {{}, -12.0f}, {{}, -20.0f}, {{}, -10.0f},
    /* ringBase   */ {{-0.0716f, 0.0452f, -0.0018f, 0.9964f}, 0.0f},
    /* ring1/2/3 */ {{}, -16.0f}, {{}, -26.0f}, {{}, -12.0f},
    /* pinkyBase  */ {{-0.1240f, 0.0845f, 0.0121f, 0.9886f}, 0.0f},
    /* pinky1/2/3 */ {{}, -20.0f}, {{}, -30.0f}, {{}, -14.0f},
};

// A closed hand around something held (a carried object's handle, a fist):
// the same metacarpals, the phalanges flexed most at the middle joint, the
// thumb folded over the index and middle fingers. Left hand, like kLeft.
const Relaxed kFist[kJointCount] = {
    /* thumb1 */ {{0.7630f, -0.3729f, 0.2534f, 0.4633f}, -18.0f},
    /* thumb2 */ {{}, -32.0f},
    /* thumb3 */ {{}, -38.0f},
    /* indexBase  */ {{0.0522f, -0.0170f, 0.0482f, 0.9973f}, 0.0f},
    /* index1/2/3 */ {{}, -72.0f}, {{}, -92.0f}, {{}, -50.0f},
    /* middleBase */ {{-0.0149f, 0.0216f, 0.0393f, 0.9989f}, 0.0f},
    /* middle1/2/3 */ {{}, -78.0f}, {{}, -96.0f}, {{}, -50.0f},
    /* ringBase   */ {{-0.0716f, 0.0452f, -0.0018f, 0.9964f}, 0.0f},
    /* ring1/2/3 */ {{}, -82.0f}, {{}, -96.0f}, {{}, -50.0f},
    /* pinkyBase  */ {{-0.1240f, 0.0845f, 0.0121f, 0.9886f}, 0.0f},
    /* pinky1/2/3 */ {{}, -86.0f}, {{}, -92.0f}, {{}, -48.0f},
};

const char* const kRoleNames[kJointCount] = {
    "thumb1", "thumb2", "thumb3",
    "indexBase", "index1", "index2", "index3",
    "middleBase", "middle1", "middle2", "middle3",
    "ringBase", "ring1", "ring2", "ring3",
    "pinkyBase", "pinky1", "pinky2", "pinky3",
};

} // namespace

const char* JointName(Side side, int joint)
{
    static char names[2][kJointCount][32];
    static bool built = false;
    if (!built) {
        for (int s = 0; s < 2; ++s) {
            for (int j = 0; j < kJointCount; ++j) {
                std::snprintf(names[s][j], sizeof(names[s][j]), "%s_%s_jnt", s == 1 ? "l" : "r", kRoleNames[j]);
            }
        }
        built = true;
    }
    if (joint < 0 || joint >= kJointCount) { return ""; }
    return names[side == Side::left ? 1 : 0][joint];
}

const char* WristName(Side side) { return side == Side::left ? "l_hand_jnt" : "r_hand_jnt"; }

int ParentRole(int joint)
{
    switch (joint) {
    case kThumb1: case kIndexBase: case kMiddleBase: case kRingBase: case kPinkyBase: return -1;
    default: return joint - 1;
    }
}

Quaternion RelaxedRelative(Side side, int joint, float curl)
{
    if (joint < 0 || joint >= kJointCount) { return {}; }
    const Relaxed& r = kLeft[joint];
    Quaternion q = Normalize(Multiply(r.rotation, AboutZ(r.flexDegrees * curl * kDegrees)));
    if (side == Side::right) {
        // The two hands are mirror images through the local XY plane (thumb on
        // +Z vs -Z; length +X and palm -Y on both): reflect the rotation axis.
        q = {-q.x, -q.y, q.z, q.w};
    }
    return q;
}

Quaternion ShapeRelative(Side side, int joint, float curl, float fist)
{
    if (joint < 0 || joint >= kJointCount) { return {}; }
    const float closed = std::clamp(fist, 0.0f, 1.0f);
    // Blend the flexion angles, not the quaternions: every phalanx flexes about
    // the same local axis, so the in-between hand is a real hand.
    const Relaxed& open = kLeft[joint];
    const Relaxed& shut = kFist[joint];
    const float flex = open.flexDegrees * curl * (1.0f - closed) + shut.flexDegrees * closed;
    Quaternion q = Normalize(Multiply(open.rotation, AboutZ(flex * kDegrees)));
    if (side == Side::right) { q = {-q.x, -q.y, q.z, q.w}; }
    return q;
}

bool MeasureHandFrame(Side side, const Vec3 baseOffset[4], const Quaternion baseRotation[4],
                      const Vec3 firstOffset[4], Frame& frame, float& knuckleDistance)
{
    Vec3 knuckle[4];
    Vec3 mean{};
    for (int f = 0; f < 4; ++f) {
        knuckle[f] = Add(baseOffset[f], Rotate(baseRotation[f], firstOffset[f]));
        mean = Add(mean, Mul(knuckle[f], 0.25f));
    }
    Vec3 length = mean;
    knuckleDistance = Length(mean);
    if (!Unit(length)) { return false; }
    // Index is the thumb's neighbour on both hands.
    Vec3 across = Sub(knuckle[0], knuckle[3]);
    across = Sub(across, Mul(length, Dot(across, length)));
    if (!Unit(across)) { return false; }
    frame.length = length;
    frame.thumb = across;
    frame.palm = side == Side::left ? Cross(length, across) : Cross(across, length);
    return Unit(frame.palm);
}

Frame GripHandFrame(Side side, float tubeLean)
{
    const float c = std::cos(tubeLean), s = std::sin(tubeLean);
    Frame f{};
    f.length = {0.0f, -c, -s};
    f.thumb = {0.0f, s, -c};
    f.palm = side == Side::left ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{-1.0f, 0.0f, 0.0f};
    return f;
}

Quaternion FrameToFrame(const Frame& from, const Frame& to)
{
    // R = [to] * [from]^T, built column by column: R * e_k.
    const Vec3 e[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Vec3 column[3];
    for (int k = 0; k < 3; ++k) {
        const float a = Dot(from.length, e[k]), b = Dot(from.thumb, e[k]), p = Dot(from.palm, e[k]);
        column[k] = Add(Add(Mul(to.length, a), Mul(to.thumb, b)), Mul(to.palm, p));
    }
    return FromColumns(column[0], column[1], column[2]);
}

Vec3 GripPointInWrist(const Frame& hand, float knuckleDistance, const GripPoint& point)
{
    return Add(Add(Mul(hand.length, point.alongHand * knuckleDistance), Mul(hand.palm, point.outOfPalm)),
               Mul(hand.thumb, point.towardThumb));
}

float RollToMatch(const Vec3& axis, const Vec3* from, const Vec3* to, int count)
{
    // Rotating v about the unit axis a by t: v' = v|| + v_|_ cos t + (a x v) sin t.
    // Sum of v'.r is c + A cos t + B sin t, maximal at atan2(B, A).
    float cosine = 0.0f, sine = 0.0f;
    for (int i = 0; i < count; ++i) {
        const Vec3 v = Sub(from[i], Mul(axis, Dot(from[i], axis)));
        const Vec3 r = Sub(to[i], Mul(axis, Dot(to[i], axis)));
        cosine += Dot(v, r);
        sine += Dot(Cross(axis, v), r);
    }
    if (!std::isfinite(cosine) || !std::isfinite(sine) || (std::fabs(cosine) < 1e-9f && std::fabs(sine) < 1e-9f)) {
        return 0.0f;
    }
    return std::atan2(sine, cosine);
}

Quaternion AxisAngle(const Vec3& unitAxis, float radians)
{
    const float s = std::sin(radians * 0.5f);
    return Normalize({unitAxis.x * s, unitAxis.y * s, unitAxis.z * s, std::cos(radians * 0.5f)});
}

Quaternion Slerp(Quaternion a, Quaternion b, float t)
{
    a = Normalize(a);
    b = Normalize(b);
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    t = std::clamp(t, 0.0f, 1.0f);
    float x = 1.0f - t, y = t;
    if (d < 0.9995f) {
        const float angle = std::acos(std::clamp(d, -1.0f, 1.0f));
        const float sine = std::sin(angle);
        x = std::sin((1.0f - t) * angle) / sine;
        y = std::sin(t * angle) / sine;
    }
    return Normalize({a.x * x + b.x * y, a.y * x + b.y * y, a.z * x + b.z * y, a.w * x + b.w * y});
}

float AngleBetween(Quaternion a, Quaternion b)
{
    a = Normalize(a);
    b = Normalize(b);
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.0f * std::acos(std::clamp(d, 0.0f, 1.0f));
}

Vec3 SoftReach(const Vec3& shoulder, const Vec3& goal, float reach, float knee)
{
    const Vec3 d = Sub(goal, shoulder);
    const float distance = Length(d);
    if (!std::isfinite(distance) || !std::isfinite(reach) || reach <= 0.0f || distance < 1e-6f) { return goal; }
    knee = std::clamp(knee, 0.5f, 0.999f);
    const float start = knee * reach;
    if (distance <= start) { return goal; }
    // Slope 1 at the knee, approaching the reach asymptotically: no corner
    // where the hand would visibly stop, and never the straight-arm singularity.
    const float span = reach - start;
    const float mapped = start + span * (1.0f - std::exp(-(distance - start) / span));
    return Add(shoulder, Mul(d, mapped / distance));
}

} // namespace preyvr::handpose
