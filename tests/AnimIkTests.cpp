#include "preyvr/AnimIk.h"
#include "preyvr/StereoCamera.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::animik;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

bool Near(float a, float b, float epsilon = 1e-4f) { return std::fabs(a - b) <= epsilon; }

bool NearVec(const Vec3& a, const Vec3& b, float epsilon = 1e-4f)
{
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
}

bool NearQuat(const Quaternion& a, const Quaternion& b, float epsilon = 1e-4f)
{
    // q and -q are the same rotation.
    const bool same = Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) &&
                      Near(a.z, b.z, epsilon) && Near(a.w, b.w, epsilon);
    const bool flipped = Near(a.x, -b.x, epsilon) && Near(a.y, -b.y, epsilon) &&
                         Near(a.z, -b.z, epsilon) && Near(a.w, -b.w, epsilon);
    return same || flipped;
}

Quaternion AxisAngle(Vec3 axis, float radians)
{
    const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    const float s = std::sin(radians * 0.5f) / len;
    return Quaternion{axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f)};
}

// World -> model -> world must be the identity for a scaled, rotated, translated
// location: this is the whole reason the engine's own location is used instead
// of a body-yaw approximation.
void TestLocationRoundTrip()
{
    Location loc;
    loc.q = AxisAngle(Vec3{0.2f, 0.3f, 1.0f}, 0.9f);
    loc.t = Vec3{12.0f, -3.5f, 1.25f};
    loc.s = 1.5f;
    const Vec3 world{14.0f, -2.0f, 2.5f};
    const Vec3 model = WorldToModel(loc, world);
    Require(NearVec(ModelToWorld(loc, model), world, 1e-3f), "world->model->world round trip");
    // A pure translation of the location moves the model point the other way.
    Location shifted = loc;
    shifted.q = Quaternion{};
    shifted.s = 1.0f;
    Require(NearVec(WorldToModel(shifted, Vec3{13.0f, -3.5f, 1.25f}), Vec3{1.0f, 0.0f, 0.0f}),
            "translation-only location subtracts its origin");
}

// The rotation half must compose so that model = inverse(loc) * world exactly.
void TestLocationRotation()
{
    Location loc;
    loc.q = AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 1.1f);
    const Quaternion world = AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 1.6f);
    const Quaternion model = WorldToModel(loc, world);
    Require(NearQuat(model, AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 0.5f)),
            "a yawed location leaves the yaw difference");
    Require(Near(YawOf(model), 0.5f, 1e-4f), "YawOf reads a Z-up yaw");
}

// Head-relative placement: the controller lands at the eye plus the rotated
// head-to-controller vector, and the head's own position cancels out -- which is
// what makes the play-space origin irrelevant.
void TestControllerFromHead()
{
    Pose head;
    head.position = Vec3{0.3f, 1.6f, -0.2f};   // OpenXR: x right, y up, -z forward
    Pose controller;
    controller.position = Vec3{0.3f, 1.2f, -0.7f};   // 40 cm below, 50 cm forward of the head
    const Vec3 eye{100.0f, 200.0f, 1.7f};
    const Pose world = ControllerWorldFromHead(0.0f, eye, head, controller);
    // Engine axes: forward is +Y, up is +Z. -0.5 in OpenXR z is +0.5 engine y.
    Require(NearVec(world.position, Vec3{100.0f, 200.5f, 1.3f}, 1e-4f),
            "controller sits forward and below the eye in engine axes");
    // Moving head and controller together changes nothing.
    head.position.x += 5.0f;
    controller.position.x += 5.0f;
    const Pose moved = ControllerWorldFromHead(0.0f, eye, head, controller);
    Require(NearVec(moved.position, world.position, 1e-4f),
            "a shared translation of head and controller cancels");
    // A half-turn yaw sends forward to backward.
    const Pose turned = ControllerWorldFromHead(3.14159265f, eye, head, controller);
    Require(NearVec(turned.position, Vec3{100.0f, 199.5f, 1.3f}, 1e-3f),
            "the reference yaw rotates the offset about the eye");
}

void TestClampToReach()
{
    const Vec3 upper{1.0f, 1.0f, 1.0f};
    Require(NearVec(ClampToReach(upper, Vec3{1.3f, 1.0f, 1.0f}, 0.6f), Vec3{1.3f, 1.0f, 1.0f}),
            "a reachable goal is untouched");
    const Vec3 clamped = ClampToReach(upper, Vec3{3.0f, 1.0f, 1.0f}, 0.6f);
    Require(NearVec(clamped, Vec3{1.6f, 1.0f, 1.0f}), "an unreachable goal lands on the reach sphere");
    Require(NearVec(ClampToReach(upper, Vec3{3.0f, 1.0f, 1.0f}, 0.0f), Vec3{3.0f, 1.0f, 1.0f}),
            "a zero reach means no information, so no clamp");
}

// Calibration must reproduce the animated wrist exactly at the calibration
// instant, and then follow the controller's delta from there.
void TestRotationCalibration()
{
    const Quaternion controllerAtCal = AxisAngle(Vec3{0.1f, 0.9f, 0.2f}, 0.7f);
    const Quaternion wristAtCal = AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, -1.3f);
    const Quaternion offset = CalibrateRotationOffset(controllerAtCal, wristAtCal);
    Require(NearQuat(ApplyRotationOffset(controllerAtCal, offset), wristAtCal),
            "applying the offset at calibration reproduces the wrist");
    const Quaternion turn = AxisAngle(Vec3{1.0f, 0.0f, 0.0f}, 0.4f);
    const Quaternion controllerLater = Normalize(Multiply(turn, controllerAtCal));
    const Quaternion expected = Normalize(Multiply(turn, wristAtCal));
    Require(NearQuat(ApplyRotationOffset(controllerLater, offset), expected),
            "a controller delta on the left turns the wrist by the same delta");
}

} // namespace

int main()
{
    TestLocationRoundTrip();
    TestLocationRotation();
    TestControllerFromHead();
    TestClampToReach();
    TestRotationCalibration();
    std::cout << "PreyVR anim-ik tests passed\n";
    return 0;
}
