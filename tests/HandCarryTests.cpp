#include "preyvr/HandCarry.h"
#include "preyvr/InteractionUse.h"
#include "preyvr/StereoCamera.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::carry;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
bool NearV(Vec3 a, Vec3 b, float e = 1e-3f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e); }
Quaternion AxisAngle(Vec3 axis, float radians)
{
    const float s = std::sin(radians * .5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians * .5f)};
}
constexpr std::int64_t kFrame = 11111111;   // 90 Hz, ns

// --- motion -------------------------------------------------------------------------

void TestConstantVelocity()
{
    MotionHistory h;
    const Vec3 v{1.0f, 2.0f, -0.5f};
    for (int i = 0; i < 30; ++i) {
        MotionSample s{};
        s.time = 1000000000ll + i * kFrame;
        s.position = Scale(v, static_cast<float>(i * kFrame) * 1e-9f);
        h.Push(s);
    }
    const Motion m = h.Estimate();
    Require(m.valid && !m.fromRuntime, "finite differences give a motion");
    Require(NearV(m.velocity, v, 0.02f), "constant velocity recovered");
}

// The hand brakes as it opens: the estimate must be the peak, not the release.
void TestPeakNotRelease()
{
    MotionHistory h;
    float x = 0;
    for (int i = 0; i < 40; ++i) {
        const float t = static_cast<float>(i) / 39.0f;
        // speed ramps up to 6 m/s at 70% of the motion, then down to 1.5 m/s
        const float speed = t < 0.7f ? 6.0f * t / 0.7f : 6.0f - 4.5f * (t - 0.7f) / 0.3f;
        x += speed * static_cast<float>(kFrame) * 1e-9f;
        MotionSample s{};
        s.time = 2000000000ll + i * kFrame;
        s.position = {x, 0, 0};
        h.Push(s);
    }
    const Motion m = h.Estimate();
    Require(m.valid, "valid");
    Require(m.speed > 5.0f, "the peak of the last 0.12 s, not the braking release speed");
    Require(m.velocity.x > 0, "direction kept");
}

void TestRuntimePreferred()
{
    MotionHistory h;
    for (int i = 0; i < 20; ++i) {
        MotionSample s{};
        s.time = 3000000000ll + i * kFrame;
        s.position = {0, 0, static_cast<float>(i) * 0.01f};   // poses say 0.9 m/s
        s.velocityValid = true;
        s.velocity = {0, 3.0f, 0};                            // runtime says 3 m/s up
        s.angular = {0, 0, 2.0f};
        h.Push(s);
    }
    const Motion m = h.Estimate();
    Require(m.valid && m.fromRuntime, "runtime velocity used when reported");
    Require(NearV(m.velocity, {0, 3, 0}, 1e-3f) && NearV(m.angular, {0, 0, 2}, 1e-3f), "runtime values");
}

void TestAngular()
{
    MotionHistory h;
    const float rate = 3.0f;   // rad/s about +Y (tracking)
    for (int i = 0; i < 20; ++i) {
        MotionSample s{};
        s.time = 4000000000ll + i * kFrame;
        s.orientation = AxisAngle({0, 1, 0}, rate * static_cast<float>(i * kFrame) * 1e-9f);
        h.Push(s);
    }
    const Motion m = h.Estimate();
    Require(m.valid && NearV(m.angular, {0, rate, 0}, 0.05f), "angular velocity from orientations");
}

void TestGapResets()
{
    MotionHistory h;
    for (int i = 0; i < 10; ++i) {
        MotionSample s{};
        s.time = 5000000000ll + i * kFrame;
        s.position = {static_cast<float>(i), 0, 0};   // 90 m/s: garbage before the gap
        h.Push(s);
    }
    MotionSample s{};
    s.time = 5000000000ll + 10 * kFrame + 200000000ll;   // a 0.2 s stall
    h.Push(s);
    Require(h.Size() == 1, "a gap restarts the history");
    Require(!h.Estimate().valid, "one sample is no motion");
    // Out of order is ignored.
    MotionSample old{};
    old.time = s.time - 1;
    h.Push(old);
    Require(h.Size() == 1, "an older sample is ignored");
}

// --- frames ---------------------------------------------------------------------------

void TestTrackingToWorld()
{
    // OpenXR forward (-Z) is engine forward (+Y); up (+Y) is engine up (+Z).
    Require(NearV(TrackingToWorld({0, 0, -1}, 0), {0, 1, 0}), "forward");
    Require(NearV(TrackingToWorld({0, 1, 0}, 0), {0, 0, 1}), "up");
    Require(NearV(TrackingToWorld({1, 0, 0}, 0), {1, 0, 0}), "right");
    // The play space turned 90 degrees left: forward is engine -X.
    Require(NearV(TrackingToWorld({0, 0, -1}, 1.5707963f), {-1, 0, 0}), "yawed");
}

void TestMatrix()
{
    const Quaternion q = Normalize(Quaternion{0.2f, -0.4f, 0.1f, 0.85f});
    const Vec3 x = Rotate(q, {1, 0, 0}), y = Rotate(q, {0, 1, 0}), z = Rotate(q, {0, 0, 1});
    // Row-major 3x4, columns are the axes, scaled by 2 (scale must not matter).
    const float m[12] = {2 * x.x, 2 * y.x, 2 * z.x, 5, 2 * x.y, 2 * y.y, 2 * z.y, 6, 2 * x.z, 2 * y.z, 2 * z.z, 7};
    const Quaternion r = FromMatrix34(m);
    const Vec3 probe{0.3f, -0.7f, 0.2f};
    Require(NearV(Rotate(r, probe), Rotate(q, probe), 1e-3f), "matrix -> quaternion");
}

// --- kinds ---------------------------------------------------------------------------

void TestClassify()
{
    ClassifyInput in{};
    in.mass = 1.0f;
    in.diagonal = 0.3f;
    Require(Classify(in) == Kind::Light, "a shoe is light");
    in.hold = 0.75f;
    Require(Classify(in) == Kind::Heavy, "a leverage hold is heavy");
    in.hold = 0.33f;
    Require(Classify(in) == Kind::Light, "the generic hold mode alone is not");
    in.hold = 0;
    in.mass = 40;
    Require(Classify(in) == Kind::Heavy, "mass");
    in.mass = 2;
    in.diagonal = 1.5f;
    Require(Classify(in) == Kind::Heavy, "size");
    in.articulated = true;
    Require(Classify(in) == Kind::Corpse, "a ragdoll is a body");
    in.articulated = false;
    in.safeCarry = true;
    Require(Classify(in) == Kind::Native, "turrets keep the native placement");
    in.safeCarry = false;
    in.handValid = false;
    Require(Classify(in) == Kind::Native, "no hand, native");
}

// --- placement -------------------------------------------------------------------------

void TestLightHold()
{
    Pose grip{};
    grip.position = {1, 2, 3};
    const Vec3 palm{1, 0, 0};
    // A thin box (2 cm thick along the palm): its centre in the fist.
    Box thin{{-0.01f, -0.1f, -0.05f}, {0.01f, 0.1f, 0.05f}};
    EntityPose p = LightHold(grip, palm, Quaternion{}, thin);
    Require(NearV(p.position, grip.position), "thin: centre on the grip point");
    // A 20 cm cube: pushed out of the palm until its side rests on it.
    Box cube{{-0.1f, -0.1f, -0.1f}, {0.1f, 0.1f, 0.1f}};
    p = LightHold(grip, palm, Quaternion{}, cube);
    Require(NearV(p.position, {1 + 0.1f - 0.035f, 2, 3}), "thick: near side on the palm");
    // The pivot is not the centre: the centre is what goes in the hand.
    Box offset{{0, 0, 0}, {0.02f, 0.02f, 0.30f}};
    p = LightHold(grip, palm, Quaternion{}, offset);
    const Vec3 centre = Add(p.position, Rotate(p.rotation, Centre(offset)));
    Require(Near(centre.y, 2) && Near(centre.z, 3), "the bounds centre sits at the hand");
    // The object turns with the hand, keeping the grab's relative rotation.
    const Quaternion rel = AxisAngle({0, 0, 1}, 0.5f);
    grip.orientation = AxisAngle({1, 0, 0}, 1.2f);
    p = LightHold(grip, palm, rel, thin);
    const Quaternion expected = Multiply(grip.orientation, rel);
    const Vec3 probe{0.1f, 0.2f, 0.3f};
    Require(NearV(Rotate(p.rotation, probe), Rotate(expected, probe)), "rotation = hand * relative");
}

void TestHeavyShift()
{
    const Vec3 head{10, 20, 1.6f};
    HeavyGrab grab{};
    grab.handOffset = {0, 0.4f, -0.4f};
    grab.yaw = 0;
    // The hand where it was at the grab: the game's own place.
    Require(NearV(HeavyShift(Add(head, grab.handOffset), head, 0, grab), {}), "a resting hand leaves it native");
    // Moved 30 cm left and 10 cm up: the object moves the same.
    Require(NearV(HeavyShift(Add(head, {-0.3f, 0.4f, -0.3f}), head, 0, grab), {-0.3f, 0, 0.1f}), "steered");
    // Limits.
    Vec3 v = HeavyShift(Add(head, {2.0f, 0.4f, -0.4f}), head, 0, grab);
    Require(Near(Length(Horizontal(v)), 0.6f), "max shift");
    v = HeavyShift(Add(head, {0, 0.4f, 1.0f}), head, 0, grab);
    Require(Near(v.z, 0.45f), "max rise");
    v = HeavyShift(Add(head, {0, 0.4f, -2.0f}), head, 0, grab);
    Require(Near(v.z, -0.45f), "max drop");
    // A snap turn of 90 degrees left, the hand turned with the player: still native.
    const Vec3 turned = Rotate(YawRotation(1.5707963f), grab.handOffset);
    Require(NearV(HeavyShift(Add(head, turned), head, 1.5707963f, grab), {}, 1e-3f), "turns with the play space");
}

void TestCorpse()
{
    const CorpseSettings cs{};
    const Vec3 feet{0, 0, 0};
    const Vec3 native{0, 0, 1.25f};   // the game drags at the player's chest
    // In front, inside the limits: the hand itself.
    Require(NearV(CorpsePoint({0.2f, 0.9f, 0.8f}, feet, native), {0.2f, 0.9f, 0.8f}), "the hand");
    // Pulled into the player: pushed out to the minimum distance.
    Vec3 p = CorpsePoint({0, 0.2f, 0.8f}, feet, native);
    Require(Near(std::sqrt(p.x * p.x + p.y * p.y), cs.minDistance, 1e-3f), "min distance");
    // Lifted overhead: no higher than the native height + margin.
    p = CorpsePoint({0, 0.9f, 2.5f}, feet, native);
    Require(Near(p.z, 1.25f + cs.maxAboveNative, 1e-3f), "max height");
    // Into the floor (the reach limit widened, so only the floor limit acts).
    CorpseSettings wide = cs;
    wide.maxFromNative = 5.0f;
    p = CorpsePoint({0, 0.9f, -0.5f}, feet, native, wide);
    Require(Near(p.z, cs.minHeight, 1e-3f), "min height");
    // Far to the side: kept inside the drag's reach.
    p = CorpsePoint({2.5f, 0.5f, 1.0f}, feet, native);
    Require(Near(Length(Sub(p, native)), cs.maxFromNative, 1e-3f), "max from native");
}

void TestFollow()
{
    Follow f;
    Require(NearV(f.Update({1, 0, 0}, 0.01f, 0.1f), {1, 0, 0}), "primes on the first value");
    const Vec3 v = f.Update({2, 0, 0}, 0.1f, 0.1f);
    Require(v.x > 1.5f && v.x < 1.7f, "one time constant: 63%");
}

// --- release ---------------------------------------------------------------------------

void TestRelease()
{
    ThrowSettings s{};
    // Set down: not a throw, not scaled, the player's velocity kept.
    Release r = ComputeRelease({0.5f, 0, 0}, {}, {0, 1, 0}, Kind::Light, s);
    Require(!r.thrown && NearV(r.velocity, {0.5f, 1, 0}), "a gentle release");
    // Thrown: scaled by the gain.
    r = ComputeRelease({0, 5, 0}, {0, 0, 10}, {}, Kind::Light, s);
    Require(r.thrown && NearV(r.velocity, {0, 5 * s.gain, 0}, 1e-3f), "a throw is scaled");
    Require(NearV(r.angular, {0, 0, 10}), "spin kept");
    // Capped.
    r = ComputeRelease({0, 30, 0}, {0, 0, 80}, {}, Kind::Light, s);
    Require(r.capped && Near(Length(r.velocity), s.maxSpeed, 1e-2f), "speed cap");
    Require(Near(Length(r.angular), s.maxSpin, 1e-2f), "spin cap");
    // Heavy: slower, less spin.
    r = ComputeRelease({0, 8, 0}, {0, 0, 10}, {}, Kind::Heavy, s);
    Require(Near(Length(r.velocity), s.heavyMaxSpeed, 1e-2f), "heavy cap");
    Require(NearV(r.angular, {0, 0, 10 * s.heavySpinScale}), "heavy spin");
    // Garbage in, the player's velocity out.
    r = ComputeRelease({NAN, 0, 0}, {}, {1, 0, 0}, Kind::Light, s);
    Require(NearV(r.velocity, {1, 0, 0}) && !r.thrown, "non-finite hand");
}

// --- the grip that holds -------------------------------------------------------------------

use::ButtonInput Base()
{
    use::ButtonInput in{};
    in.gameplay = true;
    in.leftHand = true;
    in.holdToHold = true;
    return in;
}
use::UseButton Ready()
{
    use::UseButton b;
    b.Update(Base());
    return b;
}

void TestHoldToHold()
{
    auto b = Ready();
    auto in = Base();
    in.target = true;
    in.leftGrip = true;
    auto out = b.Update(in);
    Require(out.down && out.owner == use::Owner::Use, "press picks up");
    // The carry starts while the grip is closed: the grip now holds it.
    in.target = false;
    in.carrying = true;
    out = b.Update(in);
    Require(!out.down && out.released && out.owner == use::Owner::Hold, "button up, grip holds");
    for (int i = 0; i < 20; ++i) {
        out = b.Update(in);
        Require(!out.down && !out.releaseCarry && out.owner == use::Owner::Hold, "held");
    }
    in.leftGrip = false;
    out = b.Update(in);
    Require(out.releaseCarry && out.owner == use::Owner::None && !out.down, "opening the grip lets go");
    out = b.Update(in);
    Require(!out.releaseCarry, "once");
}

void TestHoldPickUpAndLetGoSameFrame()
{
    auto b = Ready();
    auto in = Base();
    in.target = true;
    in.leftGrip = true;
    b.Update(in);
    in.leftGrip = false;
    in.carrying = true;
    const auto out = b.Update(in);
    Require(out.releaseCarry && !out.down, "picked up and let go in one frame");
}

void TestUnownedCarry()
{
    auto b = Ready();
    auto in = Base();
    in.carrying = true;   // carried, but no press of ours started it
    auto out = b.Update(in);
    Require(!out.releaseCarry, "nothing while the grip is open");
    in.leftGrip = true;
    out = b.Update(in);
    Require(out.releaseCarry && !out.down, "the next squeeze lets go; no button");
    out = b.Update(in);
    Require(!out.releaseCarry && !out.down, "and does nothing more while held");
}

void TestCarryEndsWhileHeld()
{
    auto b = Ready();
    auto in = Base();
    in.target = true;
    in.leftGrip = true;
    b.Update(in);
    in.carrying = true;
    b.Update(in);
    // Thrown with the trigger: the carry ends with the grip still closed.
    in.carrying = false;
    in.target = true;
    auto out = b.Update(in);
    Require(out.owner == use::Owner::None && !out.releaseCarry && !out.down, "ended natively");
    out = b.Update(in);
    Require(!out.down, "a grip still closed does not use the next target");
    in.leftGrip = false;
    b.Update(in);
    in.leftGrip = true;
    out = b.Update(in);
    Require(out.down && out.pressed, "a fresh squeeze does");
}

void TestHoldThroughMenu()
{
    auto b = Ready();
    auto in = Base();
    in.target = true;
    in.leftGrip = true;
    b.Update(in);
    in.carrying = true;
    b.Update(in);
    in.gameplay = false;   // the inventory opens
    auto out = b.Update(in);
    Require(out.owner == use::Owner::Hold && !out.releaseCarry, "held through a menu");
    in.leftGrip = false;   // let go while it is open
    out = b.Update(in);
    Require(!out.releaseCarry, "judged when play resumes");
    in.gameplay = true;
    out = b.Update(in);
    Require(out.releaseCarry, "and then it lets go");
}

void TestTakeLetsGo()
{
    auto b = Ready();
    auto in = Base();
    in.target = true;
    in.targetId = 64587;
    in.leftGrip = true;
    auto out = b.Update(in);
    Require(out.down, "press on an item");
    // A hold-to-carry object keeps the selection while the button is held.
    for (int i = 0; i < 5; ++i) { Require(b.Update(in).down, "same target: still held"); }
    // Taken into the inventory; the next item is selected.
    in.targetId = 64595;
    out = b.Update(in);
    Require(!out.down && out.released, "taken: the button goes up (no hoover)");
    for (int i = 0; i < 10; ++i) { Require(!b.Update(in).down, "stays up while the grip is closed"); }
    in.leftGrip = false;
    out = b.Update(in);
    Require(!out.down && out.owner == use::Owner::None && !out.released, "grip opened");
    in.leftGrip = true;
    out = b.Update(in);
    Require(out.down && out.pressed, "the next squeeze takes the next one");
    // Toggle mode keeps the phase-1 button (held whatever the target does).
    auto t = Ready();
    auto tin = Base();
    tin.holdToHold = false;
    tin.target = true;
    tin.targetId = 1;
    tin.leftGrip = true;
    t.Update(tin);
    tin.targetId = 2;
    Require(t.Update(tin).down, "toggle: unchanged");
}

void TestPickupTap()
{
    auto b = Ready();
    auto in = Base();
    in.target = true;
    in.targetId = 64587;
    in.tapOnly = true;
    in.leftGrip = true;
    auto out = b.Update(in);
    Require(out.down && out.pressed, "press");
    int frames = 1;
    while (b.Update(in).down) { ++frames; Require(frames < 10, "the tap ends"); }
    Require(frames == use::UseButton::kTapFrames, "down for the tap's frames only");
    for (int i = 0; i < 10; ++i) { Require(!b.Update(in).down, "up while the grip stays closed"); }
    // Toggle mode: no tap.
    auto t = Ready();
    in.holdToHold = false;
    t.Update(in);
    for (int i = 0; i < 10; ++i) { Require(t.Update(in).down, "toggle keeps the phase-1 hold"); }
}

void TestToggleUnchanged()
{
    auto b = Ready();
    auto in = Base();
    in.holdToHold = false;
    in.target = true;
    in.leftGrip = true;
    b.Update(in);
    in.carrying = true;
    auto out = b.Update(in);
    Require(out.down && out.owner == use::Owner::Use && !out.releaseCarry, "toggle: the phase-1 button");
    in.leftGrip = false;
    out = b.Update(in);
    Require(!out.down && !out.releaseCarry, "released, still carried");
}

}  // namespace

int main()
{
    TestConstantVelocity();
    TestPeakNotRelease();
    TestRuntimePreferred();
    TestAngular();
    TestGapResets();
    TestTrackingToWorld();
    TestMatrix();
    TestClassify();
    TestLightHold();
    TestHeavyShift();
    TestCorpse();
    TestFollow();
    TestRelease();
    TestHoldToHold();
    TestHoldPickUpAndLetGoSameFrame();
    TestUnownedCarry();
    TestCarryEndsWhileHeld();
    TestHoldThroughMenu();
    TestTakeLetsGo();
    TestPickupTap();
    TestToggleUnchanged();
    std::cout << "hand_carry: all tests passed\n";
    return 0;
}
