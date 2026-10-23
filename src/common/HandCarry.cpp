#include "preyvr/HandCarry.h"

#include "preyvr/InteractionUse.h"
#include "preyvr/StereoCamera.h"

#include <algorithm>
#include <cmath>

namespace preyvr::carry {

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Finite(Quaternion q)
{
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w) &&
           q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w > 1e-6f;
}
Quaternion Inverse(Quaternion q) { return {-q.x, -q.y, -q.z, q.w}; }

Quaternion FromMatrix34(const float m[12])
{
    // Columns are the rotated axes; dividing by their length removes scale.
    float r[3][3];
    for (int c = 0; c < 3; ++c) {
        const float x = m[c], y = m[4 + c], z = m[8 + c];
        const float l = std::sqrt(x * x + y * y + z * z);
        const float k = l > 1e-6f ? 1.0f / l : 0.0f;
        r[0][c] = x * k;
        r[1][c] = y * k;
        r[2][c] = z * k;
    }
    Quaternion q{};
    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (r[2][1] - r[1][2]) / s;
        q.y = (r[0][2] - r[2][0]) / s;
        q.z = (r[1][0] - r[0][1]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
        q.w = (r[2][1] - r[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (r[0][1] + r[1][0]) / s;
        q.z = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
        q.w = (r[0][2] - r[2][0]) / s;
        q.x = (r[0][1] + r[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (r[1][2] + r[2][1]) / s;
    } else {
        const float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
        q.w = (r[1][0] - r[0][1]) / s;
        q.x = (r[0][2] + r[2][0]) / s;
        q.y = (r[1][2] + r[2][1]) / s;
        q.z = 0.25f * s;
    }
    return Normalize(q);
}

Vec3 TrackingToWorld(Vec3 v, float yawRadians)
{
    return Rotate(stereo::YawQuaternion(yawRadians), stereo::ToEngineSpace(v));
}

Vec3 Horizontal(Vec3 v) { return {v.x, v.y, 0.0f}; }
Quaternion YawRotation(float radians) { return stereo::YawQuaternion(radians); }
float YawOf(Vec3 d) { return std::atan2(d.y, d.x); }

// --- motion ---------------------------------------------------------------------------

namespace {

constexpr float kNs = 1e-9f;

// World-frame angular velocity taking `from` to `to` in `dt` seconds.
Vec3 AngularBetween(Quaternion from, Quaternion to, float dt)
{
    if (!(dt > 0.0f)) { return {}; }
    Quaternion d = Multiply(to, Inverse(Normalize(from)));
    if (d.w < 0.0f) { d = {-d.x, -d.y, -d.z, -d.w}; }
    const float s = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (s < 1e-7f) { return {}; }
    const float angle = 2.0f * std::atan2(s, d.w);
    return Scale(Vec3{d.x / s, d.y / s, d.z / s}, angle / dt);
}

}  // namespace

const MotionSample& MotionHistory::At(unsigned age) const
{
    return samples_[(head_ + kCapacity - 1 - age) % kCapacity];
}

void MotionHistory::Reset()
{
    head_ = 0;
    count_ = 0;
}

bool MotionHistory::Latest(MotionSample& out) const
{
    if (!count_) { return false; }
    out = At(0);
    return true;
}

void MotionHistory::Push(const MotionSample& s, const MotionSettings& settings)
{
    if (!Finite(s.position) || !Finite(s.orientation)) { return; }
    if (count_) {
        const auto& last = At(0);
        if (s.time <= last.time) { return; }
        if (static_cast<float>(s.time - last.time) * kNs > settings.maxGapSeconds) { Reset(); }
    }
    samples_[head_] = s;
    head_ = (head_ + 1) % kCapacity;
    count_ = std::min(count_ + 1, kCapacity);
}

Motion MotionHistory::Estimate(std::int64_t now, const MotionSettings& settings) const
{
    Motion m{};
    if (count_ < 2) { return m; }
    if (!now) { now = At(0).time; }
    const auto window = static_cast<std::int64_t>(settings.windowSeconds * 1e9f);
    const auto span = static_cast<std::int64_t>(settings.spanSeconds * 1e9f);
    // Samples at or before `now`, newest first.
    unsigned first = 0;
    while (first < count_ && At(first).time > now) { ++first; }
    if (first >= count_) { return m; }

    // Runtime velocities, when the runtime gives them.
    unsigned best = count_, inWindow = 0;
    float bestSpeed = -1.0f;
    for (unsigned a = first; a < count_ && now - At(a).time <= window; ++a) {
        ++inWindow;
        const auto& s = At(a);
        if (!s.velocityValid || !Finite(s.velocity)) { continue; }
        const float speed = Length(s.velocity);
        if (speed > bestSpeed) { bestSpeed = speed; best = a; }
    }
    m.samples = inWindow;
    if (best < count_) {
        // The peak and its neighbours, averaged: one sample's noise is not the throw.
        Vec3 v{}, w{};
        int n = 0;
        for (int d = -1; d <= 1; ++d) {
            const int a = static_cast<int>(best) + d;
            if (a < static_cast<int>(first) || a >= static_cast<int>(count_)) { continue; }
            const auto& s = At(static_cast<unsigned>(a));
            if (!s.velocityValid || now - s.time > window + span) { continue; }
            v = Add(v, s.velocity);
            w = Add(w, Finite(s.angular) ? s.angular : Vec3{});
            ++n;
        }
        m.valid = n > 0;
        m.fromRuntime = true;
        m.velocity = Scale(v, 1.0f / static_cast<float>(std::max(n, 1)));
        m.angular = Scale(w, 1.0f / static_cast<float>(std::max(n, 1)));
        m.speed = Length(m.velocity);
        m.at = At(best).time;
        return m;
    }

    // Finite differences over at least `span`.
    struct Candidate { Vec3 v{}, w{}; float speed = -1; std::int64_t at = 0; };
    std::array<Candidate, kCapacity> c{};
    unsigned nc = 0, peak = 0;
    for (unsigned a = first; a < count_ && now - At(a).time <= window; ++a) {
        const auto& s = At(a);
        unsigned b = a + 1;
        while (b < count_ && s.time - At(b).time < span) { ++b; }
        if (b >= count_) { break; }
        const auto& o = At(b);
        const float dt = static_cast<float>(s.time - o.time) * kNs;
        if (!(dt > 0.0f)) { continue; }
        Candidate k{};
        k.v = Scale(Sub(s.position, o.position), 1.0f / dt);
        k.w = AngularBetween(o.orientation, s.orientation, dt);
        k.speed = Length(k.v);
        k.at = s.time;
        if (!Finite(k.v) || !Finite(k.w)) { continue; }
        if (k.speed > c[peak].speed || nc == 0) { peak = nc; }
        c[nc++] = k;
    }
    if (!nc) { return m; }
    Vec3 v{}, w{};
    int n = 0;
    for (int d = -1; d <= 1; ++d) {
        const int i = static_cast<int>(peak) + d;
        if (i < 0 || i >= static_cast<int>(nc)) { continue; }
        v = Add(v, c[static_cast<unsigned>(i)].v);
        w = Add(w, c[static_cast<unsigned>(i)].w);
        ++n;
    }
    m.valid = true;
    m.velocity = Scale(v, 1.0f / static_cast<float>(n));
    m.angular = Scale(w, 1.0f / static_cast<float>(n));
    m.speed = Length(m.velocity);
    m.at = c[peak].at;
    return m;
}

// --- kinds ------------------------------------------------------------------------------

const char* KindName(Kind kind)
{
    switch (kind) {
    case Kind::Light: return "light";
    case Kind::Heavy: return "heavy";
    case Kind::Corpse: return "corpse";
    default: return "native";
    }
}

Kind Classify(const ClassifyInput& in, const KindSettings& s)
{
    if (in.articulated) { return in.handValid ? Kind::Corpse : Kind::Native; }
    if (in.safeCarry || !in.handValid) { return Kind::Native; }
    if (in.hold >= s.heavyHold || in.mass >= s.heavyMass || in.diagonal >= s.heavyDiagonal) { return Kind::Heavy; }
    return Kind::Light;
}

// --- placement ----------------------------------------------------------------------------

Vec3 Centre(const Box& b) { return Scale(Add(b.min, b.max), 0.5f); }
Vec3 HalfExtents(const Box& b)
{
    return {std::fabs(b.max.x - b.min.x) * 0.5f, std::fabs(b.max.y - b.min.y) * 0.5f,
            std::fabs(b.max.z - b.min.z) * 0.5f};
}

float Support(const Box& box, Quaternion rotation, Vec3 direction)
{
    const Vec3 local = Rotate(Inverse(Normalize(rotation)), direction);
    const Vec3 e = HalfExtents(box);
    return std::fabs(local.x) * e.x + std::fabs(local.y) * e.y + std::fabs(local.z) * e.z;
}

EntityPose LightHold(const Pose& grip, Vec3 palm, Quaternion handToObject, const Box& local, const HoldSettings& s)
{
    EntityPose out{};
    out.rotation = Multiply(grip.orientation, handToObject);
    const float l = Length(palm);
    const Vec3 n = l > 1e-6f ? Scale(palm, 1.0f / l) : Vec3{};
    const float push = std::clamp(Support(local, out.rotation, n) - s.palmClearance, 0.0f, s.maxPush);
    const Vec3 centre = Add(grip.position, Scale(n, push));
    out.position = Sub(centre, Rotate(out.rotation, Centre(local)));
    return out;
}

EntityPose LightHoldAt(const Pose& grip, Quaternion handToObject, Vec3 grabLocal)
{
    EntityPose out{};
    out.rotation = Multiply(grip.orientation, handToObject);
    out.position = Sub(grip.position, Rotate(out.rotation, grabLocal));
    return out;
}

bool GrabPointLocal(const float m[12], const Box& local, Vec3 origin, Vec3 direction, Vec3& out, float inset)
{
    // local = A^-1 (world - t), A the 3x3 in the matrix's first three columns.
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!std::isfinite(det) || std::fabs(det) < 1e-9f || !Finite(origin) || !Finite(direction)) { return false; }
    const float k = 1.0f / det;
    const float inv[9] = {(e * i - f * h) * k, (c * h - b * i) * k, (b * f - c * e) * k,
                          (f * g - d * i) * k, (a * i - c * g) * k, (c * d - a * f) * k,
                          (d * h - e * g) * k, (b * g - a * h) * k, (a * e - b * d) * k};
    const auto apply = [&](Vec3 v) {
        return Vec3{inv[0] * v.x + inv[1] * v.y + inv[2] * v.z, inv[3] * v.x + inv[4] * v.y + inv[5] * v.z,
                    inv[6] * v.x + inv[7] * v.y + inv[8] * v.z};
    };
    const Vec3 o = apply(Sub(origin, Vec3{m[3], m[7], m[11]}));
    Vec3 dir = apply(direction);
    const float l = Length(dir);
    if (!(l > 1e-6f)) { return false; }
    dir = Scale(dir, 1.0f / l);
    out = use::PointerEnd(o, dir, local.min, local.max);
    const Vec3 in = Sub(Centre(local), out);
    const float depth = Length(in);
    if (depth > 1e-6f && inset > 0) { out = Add(out, Scale(in, std::min(inset, depth * 0.5f) / depth)); }
    return Finite(out);
}

Vec3 HeavyShift(Vec3 hand, Vec3 head, float yaw, const HeavyGrab& grab, const HoldSettings& s)
{
    // Where the hand would be had it not moved: the grab's offset, turned with
    // the play space (a snap turn swings it round with the player).
    const Vec3 neutral = Rotate(YawRotation(yaw - grab.yaw), grab.handOffset);
    const Vec3 d = Sub(Sub(hand, head), neutral);
    Vec3 h = Horizontal(d);
    const float l = Length(h);
    if (l > s.heavyMaxShift) { h = Scale(h, s.heavyMaxShift / l); }
    return {h.x, h.y, std::clamp(d.z, -s.heavyMaxDrop, s.heavyMaxRise)};
}

// --- body drag -------------------------------------------------------------------------------

Vec3 CorpsePoint(Vec3 hand, Vec3 feet, Vec3 native, const CorpseSettings& s)
{
    Vec3 p = hand;
    // Out of the player.
    Vec3 d = Horizontal(Sub(p, feet));
    float l = Length(d);
    if (l < s.minDistance) {
        Vec3 dir = l > 1e-3f ? Scale(d, 1.0f / l) : Horizontal(Sub(native, feet));
        const float dl = Length(dir);
        dir = dl > 1e-6f ? Scale(dir, 1.0f / dl) : Vec3{0, 1, 0};
        p.x = feet.x + dir.x * s.minDistance;
        p.y = feet.y + dir.y * s.minDistance;
    }
    // Not into the floor, not lifted above the native drag height.
    p.z = std::clamp(p.z, feet.z + s.minHeight, std::max(feet.z + s.minHeight, native.z + s.maxAboveNative));
    // Never further from the native point than the drag can stretch.
    const Vec3 off = Sub(p, native);
    const float ol = Length(off);
    if (ol > s.maxFromNative) { p = Add(native, Scale(off, s.maxFromNative / ol)); }
    return p;
}

Vec3 Follow::Update(Vec3 target, float dt, float tau)
{
    if (!primed_ || !Finite(value_)) {
        Reset(target);
        return value_;
    }
    if (!(dt > 0.0f) || !(tau > 0.0f)) {
        value_ = target;
        return value_;
    }
    const float k = 1.0f - std::exp(-dt / tau);
    value_ = Add(value_, Scale(Sub(target, value_), k));
    return value_;
}

// --- release -----------------------------------------------------------------------------------

Release ComputeRelease(Vec3 hand, Vec3 spin, Vec3 player, Kind kind, const ThrowSettings& s)
{
    Release r{};
    if (!Finite(hand)) { hand = {}; }
    if (!Finite(spin)) { spin = {}; }
    if (!Finite(player)) { player = {}; }
    r.handSpeed = Length(hand);
    r.thrown = r.handSpeed >= s.throwSpeed;
    // Only a throw is scaled: a hand set down gently puts the object down gently.
    Vec3 v = r.thrown ? Scale(hand, s.gain) : hand;
    const float cap = kind == Kind::Heavy ? s.heavyMaxSpeed : s.maxSpeed;
    const float speed = Length(v);
    if (speed > cap) {
        v = Scale(v, cap / speed);
        r.capped = true;
    }
    Vec3 w = kind == Kind::Heavy ? Scale(spin, s.heavySpinScale) : spin;
    const float spinRate = Length(w);
    if (spinRate > s.maxSpin) { w = Scale(w, s.maxSpin / spinRate); }
    r.velocity = Add(player, v);
    r.angular = w;
    return r;
}

}  // namespace preyvr::carry
