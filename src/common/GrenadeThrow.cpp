#include "preyvr/GrenadeThrow.h"

#include "preyvr/HandCarry.h"

#include <algorithm>
#include <cmath>

namespace preyvr::grenade {

using carry::Add;
using carry::Finite;
using carry::Length;
using carry::Scale;
using carry::Sub;

namespace {

Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 Clean(Vec3 v) { return Finite(v) ? v : Vec3{}; }

}  // namespace

float ThrowWeight(float speed, const ThrowSettings& s)
{
    if (!(speed > s.dropSpeed)) { return 0.0f; }
    if (!(s.throwSpeed > s.dropSpeed) || speed >= s.throwSpeed) { return 1.0f; }
    const float t = (speed - s.dropSpeed) / (s.throwSpeed - s.dropSpeed);
    return t * t * (3.0f - 2.0f * t);
}

float RangeOnFloor(float speed, float elevation, float height, float g, float k)
{
    if (!(speed > 0.0f) || !(g > 0.0f)) { return 0.0f; }
    const float h = std::max(height, 0.0f);
    const float vx = speed * std::cos(elevation), vz = speed * std::sin(elevation);
    if (!(k > 1e-6f)) {
        // z(t) = h + vz t - g t^2 / 2 = 0
        const float t = (vz + std::sqrt(vz * vz + 2.0f * g * h)) / g;
        return vx * t;
    }
    // Linear damping, exact: x(t) = vx (1 - e^-kt) / k,
    // z(t) = h + (vz + g/k)(1 - e^-kt)/k - g t / k; z falls monotonically after
    // the apex, so the landing time is found by bisection past it.
    const auto z = [&](float t) {
        const float e = (1.0f - std::exp(-k * t)) / k;
        return h + (vz + g / k) * e - g * t / k;
    };
    const float apex = std::max(0.0f, std::log1p(k * std::max(vz, 0.0f) / g) / k);
    float lo = apex, hi = apex + 0.5f;
    for (int i = 0; i < 40 && z(hi) > 0.0f; ++i) { hi = apex + (hi - apex) * 2.0f; }
    for (int i = 0; i < 48; ++i) {
        const float mid = 0.5f * (lo + hi);
        (z(mid) > 0.0f ? lo : hi) = mid;
    }
    return vx * (1.0f - std::exp(-k * hi)) / k;
}

float SignedRange(Vec3 v, Vec3 heading, float height, float g, float k)
{
    const Vec3 h{v.x, v.y, 0.0f};
    const float hl = Length(h);
    const float speed = Length(v);
    if (!(speed > 1e-5f) || !(hl > 1e-5f)) { return 0.0f; }
    const float elevation = std::asin(std::clamp(v.z / speed, -1.0f, 1.0f));
    return RangeOnFloor(speed, elevation, height, g, k) * carry::Dot(Scale(h, 1.0f / hl), heading);
}

float ParitySpeed(Vec3 direction, Vec3 player, Vec3 real, float height, const ThrowSettings& s, float limit)
{
    // The heading the throw is judged along: the grenade's own horizontal
    // direction (a throw straight up: the real ball's).
    Vec3 heading{direction.x, direction.y, 0.0f};
    if (Length(heading) < 1e-3f) { heading = {real.x, real.y, 0.0f}; }
    const float hl = Length(heading);
    if (!(hl > 1e-5f)) { return Length(Sub(real, player)); }
    heading = Scale(heading, 1.0f / hl);
    // Steep throws have almost no range to match: judged at the steepest
    // elevation that still has one (70 degrees), the speed then applies along
    // the real direction.
    Vec3 d = direction;
    const float steep = std::sin(1.22f);
    if (d.z > steep) {
        const float horizontal = std::cos(1.22f);
        d = {heading.x * horizontal, heading.y * horizontal, steep};
    }
    const float target = SignedRange(real, heading, height, 9.81f, 0.0f);
    const auto range = [&](float speed) {
        return SignedRange(Add(player, Scale(d, speed)), heading, height, s.gravity, s.damping);
    };
    float lo = 0.0f, hi = limit > 0.0f ? limit : 4.0f * Length(Sub(real, player)) + 1.0f;
    if (range(hi) <= target) { return hi; }
    if (range(lo) >= target) { return lo; }
    for (int i = 0; i < 40; ++i) {
        const float mid = 0.5f * (lo + hi);
        (range(mid) < target ? lo : hi) = mid;
    }
    return 0.5f * (lo + hi);
}

float SoftLimit(float speed, float limit, float kneeFraction)
{
    if (!(limit > 0.0f)) { return speed; }
    const float k = std::clamp(kneeFraction, 0.0f, 0.99f);
    const float knee = k * limit;
    if (!(speed > knee)) { return speed; }
    const float room = limit - knee;
    return knee + room * std::tanh((speed - knee) / room);
}

Release ComputeRelease(const ReleaseInput& in, const ThrowSettings& s)
{
    Release r{};
    const Vec3 hand = Clean(in.handVelocity);
    const Vec3 spin = Clean(in.handAngular);
    const Vec3 player = Clean(in.player);
    r.handSpeed = Length(hand);
    // The grenade's own velocity in the fist: the grip origin's plus the
    // rotation about it carrying the grenade round.
    const Vec3 own = s.lever ? Add(hand, Cross(spin, Clean(in.lever))) : hand;
    r.objectSpeed = Length(own);
    // A throw or a drop by the GRENADE's own speed: a flick of the wrist
    // throws it although the grip origin barely moves.
    r.thrown = r.objectSpeed >= s.dropSpeed;
    r.limit = in.nativeMax > 0.0f ? in.nativeMax * s.maxOverNative : s.fallbackMaxSpeed;
    const float weight = ThrowWeight(r.objectSpeed, s);
    float thrown = r.objectSpeed * s.fixedGain;
    if (s.parity && r.objectSpeed > 1e-6f) {
        // A real ball leaves with the body's velocity plus the arm's.
        const Vec3 direction = Scale(own, 1.0f / r.objectSpeed);
        const Vec3 real = Add(player, Scale(own, s.vrFactor));
        thrown = ParitySpeed(direction, player, real, in.height, s, r.limit * 2.0f);
    }
    const float wanted = r.objectSpeed + (thrown - r.objectSpeed) * weight;
    r.gain = r.objectSpeed > 1e-6f ? wanted / r.objectSpeed : 1.0f;
    r.speed = SoftLimit(wanted, r.limit, s.kneeFraction);
    r.limited = r.speed < wanted - 1e-4f;
    const Vec3 v = r.objectSpeed > 1e-6f ? Scale(own, r.speed / r.objectSpeed) : Vec3{};
    r.velocity = Add(player, v);
    const float rate = Length(spin);
    r.angular = rate > s.maxSpin ? Scale(spin, s.maxSpin / rate) : spin;
    return r;
}

Vec3 PullBack(Vec3 head, Vec3 want, float hitFraction, float margin)
{
    if (!Finite(head) || !Finite(want)) { return want; }
    if (!(hitFraction < 1.0f)) { return want; }
    const Vec3 d = Sub(want, head);
    const float l = Length(d);
    if (!(l > 1e-6f)) { return want; }
    const float along = std::max(0.0f, std::clamp(hitFraction, 0.0f, 1.0f) * l - std::max(margin, 0.0f));
    return Add(head, Scale(d, along / l));
}

bool Landing(Vec3 p, Vec3 v, float g, float floorZ, Vec3& at, float& seconds)
{
    if (!Finite(p) || !Finite(v) || !(g > 0.0f)) { return false; }
    // z(t) = p.z + v.z t - g t^2 / 2 = floorZ, the later root.
    const float a = -0.5f * g, b = v.z, c = p.z - floorZ;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) { return false; }
    const float t = (-b - std::sqrt(disc)) / (2.0f * a);
    if (!(t >= 0.0f)) { return false; }
    seconds = t;
    at = {p.x + v.x * t, p.y + v.y * t, floorZ};
    return true;
}

}  // namespace preyvr::grenade
