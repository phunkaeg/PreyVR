#include "preyvr/BodyPose.h"

#include "preyvr/ArmPose.h"

#include <algorithm>
#include <cmath>

namespace preyvr::bodypose {
namespace {

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
bool Finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

Quaternion AxisAngle(Vec3 axis, float angle)
{
    const float s = std::sin(angle * 0.5f);
    return Normalize({axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)});
}
Rigid About(Vec3 pivot, Quaternion q) { return {q, Sub(pivot, Rotate(q, pivot))}; }
Rigid Shift(Vec3 d) { return {Quaternion{}, d}; }

// The upper body for a bend `b` (radians; its direction is where the top goes)
// on top of `base`, the hips moved by `shift` after it. Fills hip, spine, neck
// and returns where the head's pivot lands.
Vec3 Chain(const Skeleton& s, const Rigid& base, Vec3 b, Vec3 shift, const Limits& limits, Plan& plan)
{
    plan.hip = Then(base, Shift(shift));
    const float theta = std::sqrt(b.x * b.x + b.y * b.y);
    // Turning about (-b.y, b.x, 0) by +theta tips +Z towards b.
    const Vec3 axis = theta > 1e-6f ? Vec3{-b.y / theta, b.x / theta, 0.0f} : Vec3{1, 0, 0};
    Rigid t = plan.hip;
    for (int k = 0; k < 4; ++k) {
        const Vec3 pivot = Apply(t, s.spine[k]);
        t = Then(t, About(pivot, AxisAngle(axis, theta * limits.share[k])));
        plan.spine[k] = t;
    }
    // The neck rides the top of the spine but not its turn: the head faces where
    // the player looks, and stands back up by part of the bend.
    const Quaternion neckQ = AxisAngle(axis, theta * (1.0f - limits.neckUpright));
    plan.neck = {neckQ, Sub(Apply(t, s.neck), Rotate(neckQ, s.neck))};
    return Apply(plan.neck, s.head);
}

// Two-bone leg onto a planted foot; the knee keeps the animation's bend plane.
bool Leg(const Skeleton& s, int k, const Rigid& turn, const Rigid& hip, const Vec3& forward, const Goal& goal,
         Plan& plan)
{
    const Vec3 thigh0 = Apply(turn, s.thigh[k]), knee0 = Apply(turn, s.knee[k]), natural = Apply(turn, s.foot[k]);
    Vec3 foot = goal.footSet[k] ? goal.foot[k] : natural;
    const Vec3 a = Apply(hip, s.thigh[k]);
    const float upper = Length(Sub(s.knee[k], s.thigh[k])), lower = Length(Sub(s.foot[k], s.knee[k]));
    if (!Finite(foot) || !Finite(a) || !std::isfinite(upper) || !std::isfinite(lower) ||
        !(upper > 1e-3f) || !(lower > 1e-3f)) { return false; }
    Vec3 d = Sub(foot, a);
    float reach = Length(d);
    if (!std::isfinite(reach)) { return false; }
    if (reach < 1e-4f) {
        d = Sub(natural, thigh0); reach = Length(d);
        if (reach < 1e-4f) { d = {0, 0, -1}; reach = 1; }
    }
    const Vec3 dir = Mul(d, 1.0f / reach);
    const float minimum = std::fabs(upper - lower) + 1e-4f, maximum = (upper + lower) * 0.9999f;
    if (minimum > maximum) { return false; }
    const float dist = std::clamp(reach, minimum, maximum);
    // Clamp the actual endpoint too, not just the triangle used for the knee.
    // Otherwise the foot separates from the rigid shin at unreachable targets.
    foot = Add(a, Mul(dir, dist));
    const Rigid footRigid = Then(turn, Shift(Sub(foot, natural)));
    // The way the animation bends this knee (square to its own thigh-foot line);
    // a leg the animation holds nearly straight folds forward, as knees do.
    const Vec3 animated = Sub(natural, thigh0);
    const float al = Length(animated);
    Vec3 pole = Sub(knee0, thigh0);
    if (al > 1e-4f) { pole = Sub(pole, Mul(animated, Dot(pole, animated) / (al * al))); }
    pole = Add(pole, Mul(forward, std::max(0.0f, 0.02f - Length(pole))));
    pole = Sub(pole, Mul(dir, Dot(pole, dir)));
    if (Length(pole) < 1e-4f) { pole = Sub(forward, Mul(dir, Dot(forward, dir))); }
    float pl = Length(pole);
    if (pl < 1e-6f) {
        const Vec3 axis = std::fabs(dir.z) < .9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
        pole = Sub(axis, Mul(dir, Dot(axis, dir))); pl = Length(pole);
    }
    pole = Mul(pole, 1.0f / pl);
    const float cosA = std::clamp((upper * upper + dist * dist - lower * lower) / (2.0f * upper * dist), -1.0f, 1.0f);
    const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
    const Vec3 knee = Add(a, Add(Mul(dir, upper * cosA), Mul(pole, upper * sinA)));
    const Quaternion rt = armpose::FromTo(Sub(knee0, thigh0), Sub(knee, a));
    // thigh(p) = a + rt * (turn(p) - thigh0)
    plan.thigh[k] = Then(turn, Rigid{rt, Sub(a, Rotate(rt, thigh0))});
    const Quaternion rs = armpose::FromTo(Rotate(rt, Sub(natural, knee0)), Sub(foot, knee));
    const Quaternion q = Multiply(rs, rt);
    plan.shin[k] = Then(turn, Rigid{q, Sub(knee, Rotate(q, knee0))});
    plan.foot[k] = footRigid;
    return true;
}

} // namespace

Vec3 Apply(const Rigid& r, const Vec3& p) { return Add(Rotate(r.q, p), r.t); }

Rigid TorsoTurn(const Skeleton& s, const Goal& goal)
{
    const float w = std::isfinite(goal.weight) ? std::clamp(goal.weight, 0.0f, 1.0f) : 0.0f;
    return About({s.head.x, s.head.y, 0.0f}, AxisAngle({0, 0, 1}, std::isfinite(goal.yaw) ? goal.yaw * w : 0.0f));
}

void FootPlanter::Update(const Vec3 natural[2], const Vec3 thigh[2], const float leg[2], float bodySpeed, float dt,
                         bool free, const FootSettings& s, Vec3 out[2])
{
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f;
    bool far = false;
    for (int k = 0; k < 2; ++k) { far = far || !Finite(natural[k]) || Length(Sub(planted_[k], natural[k])) > 1.0f; }
    if (free || !valid_ || far) {
        for (int k = 0; k < 2; ++k) { planted_[k] = out[k] = natural[k]; stepping_[k] = false; }
        valid_ = true;
        return;
    }
    // A step lands where the body would stand the foot now.
    for (int k = 0; k < 2; ++k) {
        if (!stepping_[k]) { continue; }
        t_[k] += dt / std::max(duration_[k], 1e-3f);
        if (t_[k] >= 1.0f) { stepping_[k] = false; planted_[k] = natural[k]; ++steps_; }
    }
    // The next step: the foot most in need, one at a time unless a leg cannot reach.
    float need[2]{};
    bool reachless[2]{};
    for (int k = 0; k < 2; ++k) {
        const Vec3 d = Sub(planted_[k], natural[k]);
        need[k] = std::sqrt(d.x * d.x + d.y * d.y) / std::max(s.drift, 1e-3f);
        // Past what the leg can reach -- or past what the animation's own stance
        // asks of it, which may already hold a leg nearly straight.
        const float limit = std::max(s.reach * leg[k], Length(Sub(natural[k], thigh[k])) + 0.03f);
        reachless[k] = Length(Sub(planted_[k], thigh[k])) > limit;
    }
    for (int k = 0; k < 2; ++k) {
        const int other = 1 - k;
        const bool wants = need[k] > 1.0f || reachless[k];
        const bool first = need[k] >= need[other] || !(need[other] > 1.0f || reachless[other]);
        if (!stepping_[k] && wants && (reachless[k] || (!stepping_[other] && first))) {
            stepping_[k] = true;
            from_[k] = planted_[k];
            t_[k] = 0.0f;
            duration_[k] = std::clamp(s.duration - 0.1f * std::max(bodySpeed, 0.0f), s.minDuration, s.duration);
        }
    }
    for (int k = 0; k < 2; ++k) {
        if (stepping_[k]) {
            const float t = std::clamp(t_[k], 0.0f, 1.0f);
            const float e = t * t * (3.0f - 2.0f * t);
            out[k] = Add(from_[k], Mul(Sub(natural[k], from_[k]), e));
            out[k].z += s.lift * std::sin(3.14159265f * t);
        } else {
            // Planted where it stands; its height follows the floor the body stands on.
            out[k] = {planted_[k].x, planted_[k].y, natural[k].z};
        }
    }
}

Rigid Then(const Rigid& first, const Rigid& second)
{
    return {Normalize(Multiply(second.q, first.q)), Add(Rotate(second.q, first.t), second.t)};
}

Plan Solve(const Skeleton& s, const Goal& goal, const Limits& limits)
{
    Plan plan{};
    const float w = std::isfinite(goal.weight) ? std::clamp(goal.weight, 0.0f, 1.0f) : 0.0f;
    if (!Finite(s.head) || !Finite(s.neck) || !Finite(s.hip) || !Finite(goal.head) || !std::isfinite(goal.yaw)) {
        return plan;
    }
    const Vec3 target = Add(s.head, Mul(Sub(goal.head, s.head), w));
    if (Length(Sub(target, s.head)) > 1.5f) { return plan; }   // not a body any more: leave it to the game

    // The turn, about the vertical through the head's pivot.
    const Quaternion yawQ = AxisAngle({0, 0, 1}, goal.yaw * w);
    const Rigid turn = TorsoTurn(s, goal);
    const Vec3 forward = Rotate(yawQ, {0, 1, 0});
    const Vec3 right{forward.y, -forward.x, 0.0f};
    // A squat sits the hips back.
    const float drop = std::max(0.0f, s.head.z - target.z);
    const Rigid base = Then(turn, Shift(Mul(forward, -std::min(limits.maxSitBack, limits.sitBack * drop))));

    // The bend that puts the head's pivot over the goal, within the spine's range.
    const float lever = std::max(0.2f, s.head.z - s.spine[0].z);
    Vec3 b{};
    Vec3 reached = Chain(s, base, b, {}, limits, plan);
    for (int i = 0; i < 8; ++i) {
        const Vec3 e{target.x - reached.x, target.y - reached.y, 0.0f};
        if (Length(e) < 1e-4f) { break; }
        b = Add(b, Mul(e, 1.0f / lever));
        const float bf = Dot(b, forward), bs = Dot(b, right);
        const float lf = bf >= 0.0f ? limits.forward : limits.back;
        const float r = (bf / lf) * (bf / lf) + (bs / limits.side) * (bs / limits.side);
        if (r > 1.0f) { b = Mul(b, 1.0f / std::sqrt(r)); }
        reached = Chain(s, base, b, {}, limits, plan);
    }
    // What the spine could not reach, the hips slide a little towards.
    Vec3 shift{target.x - reached.x, target.y - reached.y, 0.0f};
    const float rest = Length(shift);
    if (rest > limits.slide) { shift = Mul(shift, limits.slide / rest); }

    // The height: the hips drop or rise the rest, as far as the legs allow.
    float dz = std::max(target.z - reached.z, -limits.maxDrop);
    if (s.legs) {
        for (int k = 0; k < 2; ++k) {
            const Vec3 a0 = Add(Apply(base, s.thigh[k]), shift);
            const Vec3 foot = goal.footSet[k] ? goal.foot[k] : Apply(turn, s.foot[k]);
            const float full = (Length(Sub(s.knee[k], s.thigh[k])) + Length(Sub(s.foot[k], s.knee[k]))) *
                               limits.straight;
            const float hx = a0.x - foot.x, hy = a0.y - foot.y;
            const float h2 = hx * hx + hy * hy;
            const float up = full * full > h2 ? foot.z + std::sqrt(full * full - h2) - a0.z : 0.0f;
            dz = std::min(dz, up);
        }
    }
    shift.z = dz;
    reached = Chain(s, base, b, shift, limits, plan);
    if (s.legs) {
        for (int k = 0; k < 2; ++k) {
            if (!Leg(s, k, turn, plan.hip, forward, goal, plan)) { return {}; }
        }
    } else {
        for (int k = 0; k < 2; ++k) { plan.thigh[k] = plan.shin[k] = plan.hip; plan.foot[k] = plan.hip; }
    }
    plan.bend = std::sqrt(b.x * b.x + b.y * b.y);
    plan.hipShift = shift;
    plan.headReached = reached;
    plan.miss = Length(Sub(target, reached));
    plan.valid = Finite(reached);
    return plan;
}

} // namespace preyvr::bodypose
