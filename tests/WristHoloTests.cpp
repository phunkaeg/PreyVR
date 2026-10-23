#include "preyvr/WristHolo.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::wrist;

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
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// The watch check: left forearm across the chest at 1.25 m, hand to the right,
// back of the forearm up; eye 35 cm above and a little behind.
Arm WatchArm()
{
    Arm arm{};
    arm.elbow = {-0.20f, 1.25f, -0.30f};
    arm.wrist = {0.05f, 1.25f, -0.30f};
    arm.palm = {0.0f, -1.0f, 0.0f};   // palm down = back of the forearm up
    return arm;
}
Pose EyeAbove(Vec3 target, Vec3 eye)
{
    // A head at `eye` looking straight at `target` (no roll).
    const float dx = target.x - eye.x, dy = target.y - eye.y, dz = target.z - eye.z;
    const float yaw = std::atan2(-dx, -dz);
    const float pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
    const Quaternion qy{0.0f, std::sin(yaw / 2), 0.0f, std::cos(yaw / 2)};
    const Quaternion qx{std::sin(pitch / 2), 0.0f, 0.0f, std::cos(pitch / 2)};
    return Pose{Multiply(qy, qx), eye};
}

void TestPlacementOnTheBackOfTheForearm()
{
    Placement p{};
    p.maxTiltDeg = 0.0f;
    const Card c = PlaceCard(WatchArm(), Pose{{}, {0.0f, 1.60f, -0.20f}}, p);
    Require(c.valid, "a forearm gives a card");
    Require(NearV(c.axis, {1, 0, 0}), "the card runs along the forearm towards the hand");
    Require(NearV(c.dorsal, {0, 1, 0}), "the back of the forearm is opposite the palm");
    Require(NearV(c.pose.position, {0.05f - p.along, 1.25f + p.hover, -0.30f}), "the card sits behind the wrist, over the forearm");
    Require(NearV(Rotate(c.pose.orientation, {0, 0, 1}), {0, 1, 0}), "untilted, the face is the back of the forearm");
    Require(NearV(Rotate(c.pose.orientation, {1, 0, 0}), {1, 0, 0}), "the text runs elbow -> hand");
    Require(NearV(Rotate(c.pose.orientation, {0, 1, 0}), {0, 0, -1}), "the text's up points away from the body");
}

void TestTiltTowardsTheEye()
{
    Placement p{};
    p.maxTiltDeg = 40.0f;
    // Eye 30 cm towards the body and 20 cm above: 56 deg off the back -> clamps at 40.
    const Arm arm = WatchArm();
    const Card c = PlaceCard(arm, Pose{{}, {-0.015f, 1.49f, 0.0f}}, p);
    Require(c.valid, "valid");
    Require(Near(std::fabs(c.tiltDeg), 40.0f, 0.05f), "tilt clamps at the limit");
    const Vec3 face = Rotate(c.pose.orientation, {0, 0, 1});
    Require(face.z > 0.5f && face.y > 0.5f, "the face turns towards the body, still up");
    Require(Near(Dot(face, c.axis), 0.0f), "the tilt is about the forearm only");
    // Eye almost straight above: the tilt is small and exact.
    const Card d = PlaceCard(arm, Pose{{}, {-0.015f, 1.70f, -0.28f}}, p);
    Require(std::fabs(d.tiltDeg) < 5.0f, "straight above, almost no tilt");
}

void TestTwistFollowsTheHand()
{
    Arm arm = WatchArm();
    arm.palm = {0.0f, 0.0f, 1.0f};   // palm towards the body: back faces away
    Placement p{};
    p.maxTiltDeg = 0.0f;
    const Card c = PlaceCard(arm, Pose{{}, {0.0f, 1.6f, 0.0f}}, p);
    Require(NearV(c.dorsal, {0, 0, -1}), "the back turns with the hand");
    // A palm with a component along the forearm is made perpendicular.
    arm.palm = {0.5f, -1.0f, 0.0f};
    const Card d = PlaceCard(arm, Pose{{}, {0.0f, 1.6f, 0.0f}}, p);
    Require(NearV(d.dorsal, {0, 1, 0}), "the forearm's own back, without its length");
}

// An arm held out in front, back of the wrist up, looked at from above and
// behind: along the forearm the text would run away from the viewer.
void TestTextLevelsWithTheView()
{
    Arm arm{};
    arm.elbow = {0.0f, 1.25f, -0.10f};
    arm.wrist = {0.0f, 1.25f, -0.35f};   // forearm straight ahead
    arm.palm = {0.0f, -1.0f, 0.0f};
    Placement p{};
    p.maxTiltDeg = 0.0f;
    const Pose head{{}, {0.0f, 1.60f, 0.0f}};
    const Card level = PlaceCard(arm, head, p);
    Require(NearV(Rotate(level.pose.orientation, {1, 0, 0}), {1, 0, 0}), "90 deg off: the text reads level");
    Require(NearV(Rotate(level.pose.orientation, {0, 0, 1}), {0, 1, 0}), "the face does not change");
    Require(Near(std::fabs(level.rollDeg), 90.0f, 0.1f), "rolled 90 in the card's plane");
    p.upright = 0;
    const Card along = PlaceCard(arm, head, p);
    Require(NearV(Rotate(along.pose.orientation, {1, 0, 0}), {0, 0, -1}), "upright 0: along the forearm");
    // Within uprightFrom of the horizontal it stays exactly on the forearm.
    p.upright = 1;
    arm.wrist = {0.25f * std::cos(0.35f), 1.25f, -0.10f - 0.25f * std::sin(0.35f)};   // 20 deg off level
    const Card near = PlaceCard(arm, head, p);
    Require(Near(near.rollDeg, 0.0f, 0.01f), "near the horizontal: on the forearm");
    // A rolled head levels with the head.
    arm = WatchArm();
    const float r = 0.4f;
    const Pose rolled{{0.0f, 0.0f, std::sin(r / 2), std::cos(r / 2)}, {0.0f, 1.6f, -0.2f}};
    p.upright = 2;
    const Card tilted = PlaceCard(arm, rolled, p);
    const Vec3 x = Rotate(tilted.pose.orientation, {1, 0, 0});
    Require(Near(x.y, std::sin(r), 0.02f) || Near(x.z, 0.0f, 0.02f), "upright 2 follows the head's horizontal");
}

void TestDegenerateArms()
{
    Arm arm = WatchArm();
    arm.elbow = arm.wrist;
    Require(!PlaceCard(arm, Pose{{}, {0, 1.6f, 0}}, {}).valid, "no forearm, no card");
    arm = WatchArm();
    arm.palm = {1, 0, 0};   // the palm along the forearm says nothing
    Require(!PlaceCard(arm, Pose{{}, {0, 1.6f, 0}}, {}).valid, "a palm along the forearm is rejected");
    arm = WatchArm();
    arm.wrist.x = NAN;
    Require(!PlaceCard(arm, Pose{{}, {0, 1.6f, 0}}, {}).valid, "NaN is rejected");
}

void TestMeasure()
{
    Placement p{};
    p.maxTiltDeg = 0.0f;
    const Card c = PlaceCard(WatchArm(), Pose{{}, {0.0f, 1.6f, -0.2f}}, p);
    const Vec3 eye{c.pose.position.x, c.pose.position.y + 0.35f, c.pose.position.z};
    const View v = Measure(c, EyeAbove(c.pose.position, eye));
    Require(Near(v.distance, 0.35f), "distance");
    Require(Near(v.facing, 1.0f), "straight above: facing 1");
    Require(Near(v.viewing, 1.0f, 2e-3f), "looked at: viewing 1");
    // Looking 60 deg away.
    Pose away = EyeAbove(c.pose.position, eye);
    away.orientation = Multiply(away.orientation, Quaternion{std::sin(0.5236f), 0, 0, std::cos(0.5236f)});
    Require(Near(Measure(c, away).viewing, 0.5f, 0.01f), "60 deg off the gaze: 0.5");
}

View Looked() { return {0.35f, 0.9f, 0.95f}; }

void TestDwellFadeAndHysteresis()
{
    Presenter p;
    Conditions c{};
    c.eligible = true;
    const float dt = 1.0f / 90.0f;
    p.Update(Looked(), c, dt);
    Require(!p.Showing() && p.Alpha() == 0.0f, "a glance shorter than the dwell shows nothing");
    for (int i = 0; i < 7; ++i) { p.Update(Looked(), c, dt); }
    Require(p.Showing(), "held past the dwell, it shows");
    for (int i = 0; i < 20; ++i) { p.Update(Looked(), c, dt); }
    Require(Near(p.Alpha(), 1.0f), "fully faded in after fadeIn");
    Require(Near(p.Scale(), 1.0f), "unfolded");
    // Between the exit and entry thresholds it stays.
    p.Update({0.35f, 0.35f, 0.7f}, c, dt);
    Require(p.Showing(), "hysteresis keeps it");
    // Past the exit threshold it fades out over fadeOut.
    p.Update({0.35f, 0.1f, 0.7f}, c, dt);
    Require(!p.Showing() && p.Alpha() < 1.0f && p.Alpha() > 0.8f, "fades, does not snap");
    for (int i = 0; i < 30; ++i) { p.Update({0.35f, 0.1f, 0.7f}, c, dt); }
    Require(p.Alpha() == 0.0f, "gone after fadeOut");
}

void TestUnfoldWhileRising()
{
    Presenter p;
    Conditions c{};
    c.eligible = true;
    const float dt = 1.0f / 90.0f;
    for (int i = 0; i < 9; ++i) { p.Update(Looked(), c, dt); }
    Require(p.Showing() && p.Alpha() > 0.0f && p.Alpha() < 1.0f, "rising");
    Require(p.Scale() < 1.0f && p.Scale() >= 0.9f, "it unfolds as it rises");
}

void TestFastSweepDoesNotOpen()
{
    Presenter p;
    Conditions c{};
    c.eligible = true;
    c.handSpeed = 2.5f;
    for (int i = 0; i < 30; ++i) { p.Update(Looked(), c, 1.0f / 90.0f); }
    Require(!p.Showing(), "a fast hand passing the gaze does not open it");
}

void TestOcclusionAndGateHideFast()
{
    Presenter p;
    Conditions c{};
    c.eligible = true;
    const float dt = 1.0f / 90.0f;
    for (int i = 0; i < 40; ++i) { p.Update(Looked(), c, dt); }
    Require(Near(p.Alpha(), 1.0f), "shown");
    c.occluded = true;
    p.Update(Looked(), c, dt);
    Require(!p.Showing(), "occluded: hides");
    for (int i = 0; i < 6; ++i) { p.Update(Looked(), c, dt); }
    Require(p.Alpha() == 0.0f, "occlusion fades out within fastOut");
    for (int i = 0; i < 30; ++i) { p.Update(Looked(), c, dt); }
    Require(!p.Showing(), "does not come back while occluded");
    c.occluded = false;
    for (int i = 0; i < 40; ++i) { p.Update(Looked(), c, dt); }
    Require(p.Showing(), "back once clear");
    c.eligible = false;   // two-hand aim, a menu
    p.Update(Looked(), c, dt);
    Require(p.Alpha() == 0.0f, "the gate closing removes it at once");
    Require(!p.Candidate(), "and it is no longer a candidate");
}

void TestOcclusionShapes()
{
    const Vec3 eye{0, 1.6f, 0}, card{0, 1.25f, -0.3f};
    Require(SegmentNearPoint(eye, card, {0.0f, 1.42f, -0.15f}, 0.05f), "a hand on the line of sight");
    Require(!SegmentNearPoint(eye, card, {0.2f, 1.42f, -0.15f}, 0.05f), "a hand beside it");
    Require(!SegmentNearPoint(eye, card, {0.0f, 1.10f, -0.43f}, 0.05f), "a hand beyond the card");
    // A gun barrel crossing the line of sight.
    Require(SegmentNearSegment(eye, card, {-0.2f, 1.45f, -0.13f}, {0.2f, 1.45f, -0.13f}, 0.04f), "barrel across");
    Require(!SegmentNearSegment(eye, card, {-0.2f, 1.45f, 0.2f}, {0.2f, 1.45f, 0.2f}, 0.04f), "barrel behind the eye");
    Require(SegmentNearSegment(eye, card, eye, eye, 0.01f), "degenerate segment at the eye");
}

}  // namespace

int main()
{
    TestPlacementOnTheBackOfTheForearm();
    TestTiltTowardsTheEye();
    TestTwistFollowsTheHand();
    TestTextLevelsWithTheView();
    TestDegenerateArms();
    TestMeasure();
    TestDwellFadeAndHysteresis();
    TestUnfoldWhileRising();
    TestFastSweepDoesNotOpen();
    TestOcclusionAndGateHideFast();
    TestOcclusionShapes();
    std::cout << "wrist_holo tests passed\n";
    return 0;
}
