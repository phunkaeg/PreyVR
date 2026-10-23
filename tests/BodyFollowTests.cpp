#include "preyvr/BodyFollow.h"
#include "preyvr/StereoCamera.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::body;

namespace {
constexpr float kPi = 3.14159265358979323846f;
void Require(bool ok, const char* why) { if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); } }
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool Near(Vec3 a, Vec3 b, float eps = 1e-4f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }
float Deg(float d) { return d * kPi / 180.0f; }
// OpenXR head orientations: yaw about +Y, then pitch about the head's +X, then roll about its -Z.
Quaternion AxisAngle(Vec3 axis, float angle)
{
    const float s = std::sin(angle * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
}
Quaternion Head(float yaw, float pitch, float roll = 0.0f)
{
    return Normalize(Multiply(Multiply(AxisAngle({0, 1, 0}, yaw), AxisAngle({1, 0, 0}, pitch)),
                              AxisAngle({0, 0, -1}, roll)));
}
} // namespace

int main()
{
    // Wrap.
    Require(Near(Wrap(Deg(190)), Deg(-170)), "wrap past +pi");
    Require(Near(Wrap(Deg(-190)), Deg(170)), "wrap past -pi");
    Require(Near(Wrap(Deg(720 + 10)), Deg(10)), "wrap many turns");

    // Heading agrees with the recenter yaw wherever the latter is defined and level.
    for (float yaw : {-170.f, -90.f, -30.f, 0.f, 45.f, 120.f, 179.f}) {
        const Pose level{Head(Deg(yaw), 0), {}};
        const auto heading = HeadHeading(level.orientation);
        const auto recenter = stereo::RecenterYawFromHeadPose(level);
        Require(heading && recenter && Near(Wrap(*heading - *recenter), 0), "heading == recenter yaw when level");
        Require(Near(Wrap(*heading - Deg(yaw)), 0), "heading is the yaw when level");
        // Pitched down or up, rolled: the heading stays the yaw.
        for (float pitch : {-89.f, -80.f, -45.f, 30.f, 70.f, 89.f}) {
            const auto h = HeadHeading(Head(Deg(yaw), Deg(pitch)));
            Require(h && Near(Wrap(*h - Deg(yaw)), 0, 2e-3f), "heading survives pitch");
        }
        for (float roll : {-30.f, 25.f}) {
            const auto h = HeadHeading(Head(Deg(yaw), 0, Deg(roll)));
            Require(h && Near(Wrap(*h - Deg(yaw)), 0, 2e-3f), "heading survives roll when level");
        }
    }
    // Straight down is where the forward axis alone has no yaw; the heading still has one.
    {
        const auto down = HeadHeading(Head(Deg(60), Deg(-90)));
        Require(down && Near(Wrap(*down - Deg(60)), 0, 2e-3f), "heading defined looking straight down");
        Require(!stereo::RecenterYawFromHeadPose(Pose{Head(Deg(60), Deg(-90)), {}}),
                "(the forward axis alone is undefined there)");
    }

    // The neck stays put while the head pitches and turns about it.
    const float up = 0.075f, fwd = 0.08f;
    const Vec3 neck{0.3f, 1.5f, -0.2f};
    for (float yaw : {-120.f, 0.f, 75.f}) {
        for (float pitch : {-80.f, -30.f, 0.f, 40.f}) {
            const Quaternion q = Head(Deg(yaw), Deg(pitch));
            const Vec3 lever = Rotate(q, Vec3{0, up, -fwd});
            const Pose head{q, {neck.x + lever.x, neck.y + lever.y, neck.z + lever.z}};
            Require(Near(NeckPoint(head, up, fwd), neck), "neck is invariant under head rotation");
        }
    }
    // Level head: the anchor rule and the neck rule agree.
    {
        const Pose head{Head(Deg(35), 0), {1.0f, 1.7f, 2.0f}};
        const Vec3 n = NeckPoint(head, up, fwd);
        const Vec3 a = NeckAnchor(head.position, Deg(35), fwd);
        Require(Near(n.x, a.x) && Near(n.z, a.z), "neck anchor of a level head is its neck");
        const Vec3 f = TrackingForward(Deg(35));
        const Vec3 engine = stereo::ToEngineSpace(f);
        Require(Near(engine.x, -std::sin(Deg(35))) && Near(engine.y, std::cos(Deg(35))), "tracking forward matches engine yaw");
    }

    // Yaw steps.
    {
        bool catching = false;
        Require(Near(YawStep(Deg(5), 0.011f, {}, catching), Deg(5)), "no deadzone: the body turns at once");
        YawPolicy p{Deg(30), 0};
        catching = false;
        Require(YawStep(Deg(20), 0.011f, p, catching) == 0 && !catching, "inside the deadzone nothing moves");
        Require(Near(YawStep(Deg(35), 0.011f, p, catching), Deg(35)) && catching, "leaving it, the body catches up");
        Require(Near(YawStep(Deg(10), 0.011f, p, catching), Deg(10)) && catching, "and keeps catching up inside it");
        Require(YawStep(Deg(2), 0.011f, p, catching) == 0 && !catching, "until within a tenth of it");
        YawPolicy rate{Deg(30), Deg(180)};
        catching = false;
        Require(Near(YawStep(Deg(-90), 0.1f, rate, catching), Deg(-18)), "rate limits the catch-up");
        catching = false;
        Require(Near(YawStep(Deg(350), 0.011f, {}, catching), Deg(-10)), "the short way round");
    }

    // Room steps.
    {
        RoomPolicy p{};
        bool moving = false;
        Require(RoomStep({0.01f, 0}, p, moving).x == 0 && !moving, "below start: still");
        const Vec2 s = RoomStep({0.03f, 0.0f}, p, moving);
        Require(moving && Near(s.x, 0.03f), "past start: the whole offset");
        Require(Near(RoomStep({0.01f, 0}, p, moving).x, 0.01f) && moving, "moving continues below start");
        Require(RoomStep({0.003f, 0}, p, moving).x == 0 && !moving, "stops at stop");
        moving = false;
        const Vec2 big = RoomStep({0.3f, 0.4f}, p, moving);
        Require(Near(std::sqrt(big.x * big.x + big.y * big.y), p.maxStep), "a step is bounded");
        Require(Near(big.x / big.y, 0.75f), "in the offset's direction");
    }

    // Sliding along a wall.
    {
        const Vec2 s = Slide({0.03f, 0.04f}, {0, -1});
        Require(Near(s.x, 0.03f) && Near(s.y, 0), "into the wall: only the tangent remains");
        const Vec2 away = Slide({0.03f, -0.04f}, {0, -1});
        Require(Near(away.y, -0.04f), "away from the wall: untouched");
    }

    // Frame conversions agree with the view composition (EyePoseInWorld).
    for (float play : {-2.5f, 0.f, 0.7f, 3.0f}) {
        const Vec3 d{0.12f, -0.05f, 0.31f};
        stereo::ReferenceFrame reference{};
        reference.yawRadians = play;
        reference.worldPosition = {10, 20, 30};
        const Pose world = stereo::EyePoseInWorld(reference, Pose{{}, d});
        const Vec3 w = TrackingToWorld(d, play);
        Require(Near(world.position, Vec3{10 + w.x, 20 + w.y, 30 + w.z}), "tracking -> world matches the view");
        Require(Near(WorldToTracking(w, play), d), "world -> tracking inverts it");
    }
    std::cout << "body follow tests passed\n";
    return 0;
}
