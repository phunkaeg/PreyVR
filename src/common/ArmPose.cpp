#include "preyvr/ArmPose.h"

#include <algorithm>
#include <cmath>

namespace preyvr::armpose {
namespace {

constexpr float kPi = 3.14159265358979f;

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
Vec3 Unit(Vec3 a, Vec3 fallback)
{
    const float l = Length(a);
    return l > 1e-6f && std::isfinite(l) ? Mul(a, 1.0f / l) : fallback;
}
// The part of v perpendicular to the unit axis.
Vec3 Perp(Vec3 v, Vec3 axis) { return Sub(v, Mul(axis, Dot(v, axis))); }
float Wrap(float a)
{
    while (a > kPi) { a -= 2.0f * kPi; }
    while (a < -kPi) { a += 2.0f * kPi; }
    return a;
}
// Any unit vector perpendicular to the unit v.
Vec3 AnyPerp(Vec3 v)
{
    const Vec3 other = std::fabs(v.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    return Unit(Cross(v, other), Vec3{1, 0, 0});
}

} // namespace

float YawOf(const Vec3& forward)
{
    return std::atan2(-forward.x, -forward.z);
}

TorsoFrame TorsoFromYaw(float yaw)
{
    const float s = std::sin(yaw), c = std::cos(yaw);
    TorsoFrame t;
    t.forward = {-s, 0.0f, -c};
    t.right = {c, 0.0f, -s};
    t.up = {0.0f, 1.0f, 0.0f};
    return t;
}

float HeadYaw(const Quaternion& head)
{
    // Looking down, the head's up vector points the way the face is turned;
    // looking up, away from it. Weighted by the pitch, the sum stays
    // horizontal-forward through +-90 degrees.
    const Vec3 f = Rotate(head, Vec3{0, 0, -1});
    const Vec3 u = Rotate(head, Vec3{0, 1, 0});
    Vec3 h{f.x - f.y * u.x, 0.0f, f.z - f.y * u.z};
    if (Length(h) < 1e-5f) { h = {f.x, 0.0f, f.z}; }
    return YawOf(h);
}

float TorsoYaw::Update(float headYaw, float dtSeconds, float deadzone, float relax)
{
    if (!std::isfinite(headYaw)) { return yaw; }
    if (!valid || !std::isfinite(yaw)) {
        yaw = headYaw;
        valid = true;
        return yaw;
    }
    float diff = Wrap(headYaw - yaw);
    if (diff > deadzone) { yaw = Wrap(headYaw - deadzone); }
    else if (diff < -deadzone) { yaw = Wrap(headYaw + deadzone); }
    diff = Wrap(headYaw - yaw);
    const float dt = std::clamp(dtSeconds, 0.0f, 0.25f);
    yaw = Wrap(yaw + diff * std::min(1.0f, relax * dt));
    return yaw;
}

Vec3 ShoulderTarget(const Vec3& eye, const TorsoFrame& torso, Side side, const Body& body)
{
    return Add(Add(Add(eye, Mul(torso.forward, body.shoulderForward)),
                   Mul(torso.right, static_cast<float>(side) * body.shoulderOut)),
               Mul(torso.up, body.shoulderUp));
}

Vec3 PlaceShoulder(const Vec3& clavicleRoot, float clavicleLength, const Vec3& target, float stretch)
{
    const Vec3 d = Sub(target, clavicleRoot);
    const float l = Length(d);
    if (!(l > 1e-6f) || !(clavicleLength > 0.0f)) { return target; }
    const float s = std::clamp(stretch, 0.0f, 0.9f);
    const float length = std::clamp(l, clavicleLength * (1.0f - s), clavicleLength * (1.0f + s));
    return Add(clavicleRoot, Mul(d, length / l));
}

Vec3 BodyPole(const TorsoFrame& torso, Side side)
{
    return Unit(Add(Add(Mul(torso.forward, -0.2f), Mul(torso.right, 0.4f * static_cast<float>(side))),
                    Mul(torso.up, -0.8f)),
                Vec3{0, -1, 0});
}

float WristFlexionDeg(const Vec3& forearm, const Vec3& length, const Vec3& palm)
{
    // Flexed by a towards the palm, the forearm (in the hand's frame) is
    // length*cos(a) - palm*sin(a).
    return std::atan2(-Dot(forearm, palm), Dot(forearm, length)) * 57.29578f;
}

float WristDeviationDeg(const Vec3& forearm, const Vec3& length, const Vec3& palm)
{
    const Vec3 side = Unit(Cross(length, palm), Vec3{0, 0, 1});
    return std::asin(std::clamp(std::fabs(Dot(Unit(forearm, length), side)), 0.0f, 1.0f)) * 57.29578f;
}

float WristRadialDeg(const Vec3& forearm, const Vec3& length, const Vec3& palm, Side side)
{
    // Deviated by d towards the thumb, the hand's length is forearm*cos(d) +
    // thumb*sin(d): the forearm leans away from the thumb.
    const Vec3 thumb = Mul(Unit(Cross(length, palm), Vec3{0, 0, 1}), side >= 0 ? 1.0f : -1.0f);
    return std::asin(std::clamp(-Dot(Unit(forearm, length), thumb), -1.0f, 1.0f)) * 57.29578f;
}

Vec3 ChooseElbowPole(const Vec3& shoulder, const Vec3& wrist, float upper, float fore, float maxStretch,
                     const Vec3& handLength, const Vec3& handPalm, Side side, const Vec3& bodyPole,
                     const WristLimits& limits, const Vec3* previousPole)
{
    const Vec3 axis = Unit(Sub(wrist, shoulder), Vec3{0, -1, 0});
    Vec3 body = Perp(bodyPole, axis);
    body = Length(body) > 1e-4f ? Unit(body, AnyPerp(axis)) : AnyPerp(axis);
    const Vec3 length = Unit(handLength, axis);
    Vec3 palm = Perp(handPalm, length);
    palm = Length(palm) > 1e-4f ? Unit(palm, AnyPerp(length)) : AnyPerp(length);
    // The circle: centre and radius from the same solve SolveElbow does.
    const ArmSolution reference = SolveElbow(shoulder, wrist, upper, fore, body, maxStretch);
    const Vec3 offset = Sub(reference.elbow, shoulder);
    const Vec3 centre = Add(shoulder, Mul(axis, Dot(offset, axis)));
    const float radius = Length(Perp(offset, axis));
    if (radius < 1e-3f) { return body; }
    const Vec3 u = body, v = Cross(axis, body);
    // The previous choice as an angle on this circle (none if unusable).
    bool hold = false;
    float held = 0.0f;
    if (previousPole != nullptr && limits.holdSigmaDeg > 0.0f) {
        const Vec3 pp = Perp(*previousPole, axis);
        if (Length(pp) > 0.2f) { hold = true; held = std::atan2(Dot(pp, v), Dot(pp, u)); }
    }
    // 2 (1 - cos a) ~ a^2: a Gaussian in the angle, without a seam at +-180.
    const auto swivel = [](float a, float sigmaDeg) {
        const float s = sigmaDeg / 57.29578f;
        return 2.0f * (1.0f - std::cos(a)) / (s * s);
    };
    const auto cost = [&](float phi) {
        const Vec3 dir = Add(Mul(u, std::cos(phi)), Mul(v, std::sin(phi)));
        const Vec3 forearm = Unit(Sub(wrist, Add(centre, Mul(dir, radius))), axis);
        const float flex = WristFlexionDeg(forearm, length, palm);
        const float rad = WristRadialDeg(forearm, length, palm, side);
        const float f = flex / limits.flexionSigmaDeg;
        const float d = (rad - limits.deviationRestDeg) / limits.deviationSigmaDeg;
        const float over = std::max({0.0f, flex - limits.flexionMaxDeg, -flex - limits.extensionMaxDeg,
                                     rad - limits.radialMaxDeg, -rad - limits.ulnarMaxDeg}) / limits.wallDeg;
        float c = f * f + d * d + over * over + swivel(phi, limits.swivelSigmaDeg);
        if (hold) { c += swivel(phi - held, limits.holdSigmaDeg); }
        return c;
    };
    constexpr int kSteps = 72;
    float best = 0.0f, bestCost = cost(0.0f);
    for (int i = 1; i < kSteps; ++i) {
        const float phi = 2.0f * kPi * static_cast<float>(i) / kSteps;
        const float c = cost(phi);
        if (c < bestCost) { bestCost = c; best = phi; }
    }
    // Parabolic refinement between the neighbours.
    const float h = 2.0f * kPi / kSteps;
    const float cm = cost(best - h), cp = cost(best + h);
    const float den = cm - 2.0f * bestCost + cp;
    if (den > 1e-9f) { best += 0.5f * h * (cm - cp) / den; }
    return Unit(Add(Mul(u, std::cos(best)), Mul(v, std::sin(best))), body);
}

ArmSolution SolveElbow(const Vec3& shoulder, const Vec3& wrist, float upper, float fore,
                       const Vec3& pole, float maxStretch)
{
    ArmSolution out;
    const Vec3 along = Sub(wrist, shoulder);
    const float d = Length(along);
    if (!(d > 1e-5f) || !(upper > 0.0f) || !(fore > 0.0f)) {
        out.elbow = Add(shoulder, Mul(Unit(pole, Vec3{0, -1, 0}), upper));
        return out;
    }
    const Vec3 axis = Mul(along, 1.0f / d);
    const float reach = 0.995f * (upper + fore);
    if (d > reach) {
        // Lengthen both segments to meet the wrist; past maxStretch the caller
        // was supposed to have kept the wrist within reach -- stay connected
        // anyway, and say so.
        out.scale = d / reach;
        out.straight = out.scale > maxStretch;
    }
    const float l1 = upper * out.scale, l2 = fore * out.scale;
    const float ca = std::clamp((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f);
    const float sa = std::sqrt(std::max(0.0f, 1.0f - ca * ca));
    Vec3 perp = Perp(pole, axis);
    perp = Length(perp) > 1e-5f ? Unit(perp, AnyPerp(axis)) : AnyPerp(axis);
    out.elbow = Add(shoulder, Add(Mul(axis, l1 * ca), Mul(perp, l1 * sa)));
    return out;
}

Quaternion FromTo(const Vec3& from, const Vec3& to)
{
    const Vec3 a = Unit(from, Vec3{1, 0, 0}), b = Unit(to, Vec3{1, 0, 0});
    const float d = Dot(a, b);
    if (d < -0.999999f) {
        const Vec3 axis = AnyPerp(a);
        return {axis.x, axis.y, axis.z, 0.0f};
    }
    const Vec3 c = Cross(a, b);
    return Normalize(Quaternion{c.x, c.y, c.z, 1.0f + d});
}

Vec3 SmoothDirection(const Vec3& previous, const Vec3& current, float dtSeconds, float tau)
{
    if (!(Length(previous) > 0.5f) || !(tau > 0.0f)) { return Unit(current, previous); }
    const float a = 1.0f - std::exp(-std::clamp(dtSeconds, 0.0f, 0.25f) / tau);
    return Unit(Add(previous, Mul(Sub(current, previous), a)), current);
}

} // namespace preyvr::armpose
