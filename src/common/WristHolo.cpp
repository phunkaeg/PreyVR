#include "preyvr/WristHolo.h"

#include <algorithm>
#include <cmath>

namespace preyvr::wrist {
namespace {

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
bool Finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
bool Unit(Vec3 a, Vec3& out, float minimum = 1e-5f)
{
    const float l = Length(a);
    if (!std::isfinite(l) || l < minimum) { return false; }
    out = Scale(a, 1.0f / l);
    return true;
}

// The rotation whose columns are the orthonormal basis (x, y, z).
Quaternion FromBasis(Vec3 x, Vec3 y, Vec3 z)
{
    const float m00 = x.x, m01 = y.x, m02 = z.x;
    const float m10 = x.y, m11 = y.y, m12 = z.y;
    const float m20 = x.z, m21 = y.z, m22 = z.z;
    Quaternion q{};
    const float trace = m00 + m11 + m22;
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

float SmoothStep(float x)
{
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

}  // namespace

Card PlaceCard(const Arm& arm, const Pose& head, const Placement& placement)
{
    Card card{};
    const Vec3 eye = head.position;
    if (!Finite(arm.wrist) || !Finite(arm.elbow) || !Finite(arm.palm) || !Finite(eye)) { return card; }
    Vec3 axis{};
    // A forearm shorter than 10 cm is not a forearm.
    if (!Unit(Sub(arm.wrist, arm.elbow), axis, 0.10f)) { return card; }
    // The back of the forearm: the drawn hand's back (-palm) made perpendicular
    // to the forearm. The forearm's twist follows the hand's (pronation happens
    // in the forearm), so the hand says which way its back faces.
    const Vec3 back = Scale(arm.palm, -1.0f);
    Vec3 dorsal{};
    if (!Unit(Sub(back, Scale(axis, Dot(back, axis))), dorsal, 1e-3f)) { return card; }
    const Vec3 centre = Add(Add(arm.wrist, Scale(axis, -placement.along)), Scale(dorsal, placement.hover));

    // Turn the face about the forearm towards the eye, at most maxTiltDeg: read
    // without contorting the wrist, still clearly mounted on the arm.
    Vec3 face = dorsal;
    float tilt = 0.0f;
    const Vec3 toEye = Sub(eye, centre);
    Vec3 eyeward{};
    if (Unit(Sub(toEye, Scale(axis, Dot(toEye, axis))), eyeward, 1e-3f)) {
        const Vec3 side = Cross(axis, dorsal);   // completes (axis, side, dorsal)
        const float angle = std::atan2(Dot(eyeward, side), Dot(eyeward, dorsal));
        // Only while the back is turned towards the eye (within 100 deg): past
        // that the card is hidden anyway, and turning it would only make it flip.
        if (std::fabs(angle) < 1.745f) {
            const float limit = placement.maxTiltDeg * 0.0174532925f;
            tilt = std::clamp(angle, -limit, limit);
            face = Add(Scale(dorsal, std::cos(tilt)), Scale(side, std::sin(tilt)));
        }
    }
    // Reading direction: along the forearm, levelled towards the viewer's
    // horizontal when the forearm points away from it (an arm held out in
    // front reads sideways otherwise).
    Vec3 across = axis;   // perpendicular to `face` already: the tilt turns about the axis
    float roll = 0.0f;
    Vec3 right = Rotate(head.orientation, {1.0f, 0.0f, 0.0f});
    Vec3 level{};
    if (placement.upright > 0 && Unit(Sub(right, Scale(face, Dot(right, face))), level, 1e-3f)) {
        const float angle = std::acos(std::clamp(Dot(axis, level), -1.0f, 1.0f));
        const float from = placement.uprightFromDeg * 0.0174532925f;
        const float to = std::max(placement.uprightToDeg * 0.0174532925f, from + 1e-3f);
        const float weight = placement.upright >= 2 ? 1.0f : SmoothStep((angle - from) / (to - from));
        const float sign = Dot(Cross(axis, level), face) >= 0.0f ? 1.0f : -1.0f;
        roll = weight * angle * sign;
        across = Add(Scale(axis, std::cos(roll)), Scale(Cross(face, axis), std::sin(roll)));
    }
    const Vec3 up = Cross(face, across);   // quad +Y = +Z x +X
    card.pose.orientation = FromBasis(across, up, face);
    card.rollDeg = roll * 57.2957795f;
    card.pose.position = centre;
    card.axis = axis;
    card.dorsal = dorsal;
    card.tiltDeg = tilt * 57.2957795f;
    card.valid = true;
    return card;
}

View Measure(const Card& card, Pose head)
{
    View view{};
    const Vec3 toEye = Sub(head.position, card.pose.position);
    view.distance = Length(toEye);
    if (!std::isfinite(view.distance) || view.distance <= 1e-4f) { return view; }
    const Vec3 unit = Scale(toEye, 1.0f / view.distance);
    view.facing = Dot(card.dorsal, unit);
    const Vec3 look = Rotate(head.orientation, {0.0f, 0.0f, -1.0f});
    view.viewing = -Dot(look, unit);
    return view;
}

void Presenter::Update(const View& view, const Conditions& c, float dt, const Thresholds& t)
{
    if (!std::isfinite(dt) || dt < 0.0f) { dt = 0.0f; }
    dt = std::min(dt, 0.1f);
    const bool inRange = std::isfinite(view.distance) && view.distance >= t.minDistance && view.distance <= t.maxDistance;
    const bool keep = c.eligible && inRange && view.facing >= t.exitFacing && view.viewing >= t.exitViewing;
    const bool enter = keep && view.facing >= t.enterFacing && view.viewing >= t.enterViewing &&
                       !c.occluded && c.handSpeed <= t.maxEnterSpeed;
    candidate_ = keep;
    // Something in front of it, or the gameplay gate closing: it must not
    // linger over what now covers it.
    const bool fast = c.occluded || !c.eligible;
    if (target_) {
        if (!keep || c.occluded) { target_ = false; }
    } else {
        held_ = enter ? held_ + dt : 0.0f;
        if (held_ >= t.dwell) {
            target_ = true;
            rising_ = alpha_ < 0.5f;
        }
    }
    if (!target_) { held_ = std::min(held_, t.dwell); }
    if (target_) {
        alpha_ = std::min(1.0f, alpha_ + dt / std::max(t.fadeIn, 1e-3f));
        if (alpha_ >= 1.0f) { rising_ = false; }
    } else {
        const float out = fast ? t.fastOut : t.fadeOut;
        alpha_ = std::max(0.0f, alpha_ - dt / std::max(out, 1e-3f));
        if (fast && !c.eligible) { alpha_ = 0.0f; }
    }
}

float Presenter::Scale() const
{
    return rising_ ? 0.9f + 0.1f * SmoothStep(alpha_) : 1.0f;
}

bool SegmentNearPoint(Vec3 a, Vec3 b, Vec3 point, float radius)
{
    const Vec3 d = Sub(b, a);
    const float l2 = Dot(d, d);
    float s = l2 > 1e-12f ? Dot(Sub(point, a), d) / l2 : 0.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    const Vec3 closest = Add(a, Scale(d, s));
    return Length(Sub(point, closest)) <= radius;
}

bool SegmentNearSegment(Vec3 a, Vec3 b, Vec3 p, Vec3 q, float radius)
{
    // Closest points between two segments (Ericson, Real-Time Collision Detection 5.1.9).
    const Vec3 d1 = Sub(b, a), d2 = Sub(q, p), r = Sub(a, p);
    const float aa = Dot(d1, d1), ee = Dot(d2, d2), f = Dot(d2, r);
    float s = 0.0f, u = 0.0f;
    if (aa <= 1e-12f && ee <= 1e-12f) { return Length(r) <= radius; }
    if (aa <= 1e-12f) {
        u = std::clamp(f / ee, 0.0f, 1.0f);
    } else {
        const float c = Dot(d1, r);
        if (ee <= 1e-12f) {
            s = std::clamp(-c / aa, 0.0f, 1.0f);
        } else {
            const float bb = Dot(d1, d2), denom = aa * ee - bb * bb;
            s = denom > 1e-12f ? std::clamp((bb * f - c * ee) / denom, 0.0f, 1.0f) : 0.0f;
            u = (bb * s + f) / ee;
            if (u < 0.0f) { u = 0.0f; s = std::clamp(-c / aa, 0.0f, 1.0f); }
            else if (u > 1.0f) { u = 1.0f; s = std::clamp((bb - c) / aa, 0.0f, 1.0f); }
        }
    }
    const Vec3 c1 = Add(a, Scale(d1, s)), c2 = Add(p, Scale(d2, u));
    return Length(Sub(c1, c2)) <= radius;
}

}  // namespace preyvr::wrist
