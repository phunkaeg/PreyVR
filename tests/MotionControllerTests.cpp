#include "preyvr/MotionController.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using namespace preyvr;
using namespace preyvr::controller;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool Near(float a, float b, float epsilon = 1e-5f)
{
    return std::fabs(a - b) <= epsilon;
}

bool NearVec(Vec3 a, Vec3 b, float epsilon = 1e-5f)
{
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
}

PoseValidity GoodTracking()
{
    PoseValidity validity{};
    validity.orientationValid = true;
    validity.positionValid = true;
    validity.orientationTracked = true;
    validity.positionTracked = true;
    validity.ageNanoseconds = 1000;
    return validity;
}

void TestAimRayFromController()
{
    // An identity OpenXR controller pose points along -Z, which is engine +Y.
    const Pose openXrPose{Quaternion{}, Vec3{0.2f, -0.1f, -0.3f}};
    const auto ray = AimFromController(
        stereo::ReferenceFrame{}, openXrPose, GoodTracking(), 100'000'000);

    Require(ray.has_value(), "a well-tracked controller yields a ray");
    Require(NearVec(ray->direction, Vec3{0.0f, 1.0f, 0.0f}),
        "an identity controller aims along engine forward (+Y)");
    Require(Near(std::sqrt(ray->direction.x * ray->direction.x +
                           ray->direction.y * ray->direction.y +
                           ray->direction.z * ray->direction.z), 1.0f),
        "the direction is unit length");

    // OpenXR (0.2, -0.1, -0.3) -> engine (0.2, 0.3, -0.1).
    Require(NearVec(ray->origin, Vec3{0.2f, 0.3f, -0.1f}),
        "the origin is the converted controller position");
}

void TestAimFailsClosed()
{
    const Pose pose{Quaternion{}, Vec3{}};

    PoseValidity untracked = GoodTracking();
    untracked.positionTracked = false;
    Require(!AimFromController(stereo::ReferenceFrame{}, pose, untracked, 100'000'000),
        "an untracked position produces no ray");

    PoseValidity invalid = GoodTracking();
    invalid.orientationValid = false;
    Require(!AimFromController(stereo::ReferenceFrame{}, pose, invalid, 100'000'000),
        "an invalid orientation produces no ray");

    PoseValidity stale = GoodTracking();
    stale.ageNanoseconds = 500'000'000;
    Require(!AimFromController(stereo::ReferenceFrame{}, pose, stale, 100'000'000),
        "a stale pose produces no ray rather than a plausible wrong one");
}

void TestHandAndEyeShareAReferenceFrame()
{
    // If the hand and the view were composed through different frames they would
    // separate on a snap turn, which reads as the weapon lagging the world.
    stereo::ReferenceFrame reference{};
    reference.yawRadians = 1.57079632679f;
    reference.worldPosition = Vec3{50.0f, 60.0f, 70.0f};

    const Pose openXrPose{Quaternion{}, Vec3{0.5f, 0.0f, 0.0f}};
    const Pose hand = ControllerPoseInWorld(reference, openXrPose);
    const Pose eye = stereo::EyePoseInWorld(reference, openXrPose);

    Require(NearVec(hand.position, eye.position),
        "the hand and the eye are composed through the same reference frame");
    Require(NearVec(hand.position, Vec3{50.0f, 60.5f, 70.0f}, 1e-4f),
        "a 90-degree reference yaw swings a rightward hand offset onto +Y");
}

void TestWeaponPoseAppliesGrip()
{
    // With an identity grip the weapon sits exactly on the hand.
    const Pose openXrPose{Quaternion{}, Vec3{1.0f, 2.0f, 3.0f}};
    const Pose plain = WeaponPoseFromController(
        stereo::ReferenceFrame{}, openXrPose, GripTransform{});
    const Pose hand = ControllerPoseInWorld(stereo::ReferenceFrame{}, openXrPose);
    Require(NearVec(plain.position, hand.position), "an identity grip leaves the weapon on the hand");

    // A grip offset is applied in the hand's frame, so it follows the hand's
    // rotation rather than staying axis-aligned to the world.
    GripTransform grip{};
    grip.controllerToWeapon.position = Vec3{0.0f, 0.15f, 0.0f}; // 15cm forward of the grip
    const Pose offset = WeaponPoseFromController(stereo::ReferenceFrame{}, openXrPose, grip);
    Require(NearVec(offset.position, Vec3{hand.position.x, hand.position.y + 0.15f, hand.position.z},
                    1e-4f),
        "the grip offset is applied along the hand's own forward axis");
}

void TestTwoHandedPose()
{
    // Rear hand behind, front hand ahead: forward must run between them.
    const Vec3 rear{0.0f, 0.0f, 1.2f};
    const Vec3 front{0.0f, 0.4f, 1.2f};
    const auto pose = TwoHandedWeaponPose(rear, front);
    Require(pose.has_value(), "two separated hands yield a pose");

    const Vec3 forward = ForwardOf(*pose);
    Require(NearVec(forward, Vec3{0.0f, 1.0f, 0.0f}, 1e-4f),
        "forward runs from the rear hand to the front hand");
    Require(NearVec(pose->position, front), "the weapon origin sits at the front hand");

    // A diagonal hold: forward should still point hand-to-hand.
    const auto diagonal = TwoHandedWeaponPose({0.0f, 0.0f, 1.0f}, {0.3f, 0.3f, 1.0f});
    Require(diagonal.has_value(), "a diagonal hold yields a pose");
    const Vec3 diagonalForward = ForwardOf(*diagonal);
    const float expected = 0.70710678f;
    Require(NearVec(diagonalForward, Vec3{expected, expected, 0.0f}, 1e-4f),
        "a diagonal hold points between the hands");
}

void TestTwoHandedFailsClosed()
{
    // Hands together: the direction between them is tracking noise, and a pose
    // built from it would spin.
    Require(!TwoHandedWeaponPose({0.0f, 0.0f, 1.0f}, {0.0f, 0.02f, 1.0f}),
        "hands closer than the minimum separation produce no pose");
    Require(!TwoHandedWeaponPose({1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}),
        "coincident hands produce no pose");

    // Hands stacked vertically: forward is parallel to the up reference, so roll
    // is undefined and there is no correct answer to invent.
    Require(!TwoHandedWeaponPose({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.5f}),
        "a vertical hold leaves roll undefined and is refused");
}

void TestTurning()
{
    const float step = 0.5235987756f; // 30 degrees
    Require(Near(SnapTurn(0.0f, 1, step), step), "one snap step turns by the step");
    Require(Near(SnapTurn(0.0f, -2, step), -2.0f * step), "negative steps turn the other way");

    // A long session must not accumulate an unbounded angle and lose precision.
    float yaw = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        yaw = SnapTurn(yaw, 1, step);
    }
    Require(std::fabs(yaw) <= 3.14159265f + 1e-4f, "repeated snapping stays wrapped");

    Require(Near(SmoothTurn(0.0f, 1.0f, 2.0f, 0.5f), 1.0f), "smooth turn integrates rate by time");
    Require(Near(SmoothTurn(0.0f, 0.0f, 2.0f, 0.5f), 0.0f), "a centred stick does not turn");

    Require(Near(WrapAngle(3.14159265f * 3.0f), 3.14159265f, 1e-4f), "wrapping folds to (-pi, pi]");
    Require(Near(SmoothTurn(std::nanf(""), 1.0f, 1.0f, 1.0f), 0.0f), "a non-finite yaw fails closed");
    Require(Near(SmoothTurn(0.5f, std::nanf(""), 1.0f, 1.0f), 0.5f),
        "a non-finite stick leaves the yaw unchanged");
}

} // namespace

int main()
{
    TestAimRayFromController();
    TestAimFailsClosed();
    TestHandAndEyeShareAReferenceFrame();
    TestWeaponPoseAppliesGrip();
    TestTwoHandedPose();
    TestTwoHandedFailsClosed();
    TestTurning();
    std::cout << "PreyVR motion controller tests passed\n";
    return 0;
}
