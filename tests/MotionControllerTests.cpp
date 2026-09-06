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

// Quaternions double-cover rotations: q and -q are the same orientation, so a
// comparison that ignores that reports a false mismatch on half its inputs.
bool NearQuat(Quaternion a, Quaternion b, float epsilon = 1e-4f)
{
    const float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    return std::fabs(a.x - sign*b.x) < epsilon && std::fabs(a.y - sign*b.y) < epsilon &&
           std::fabs(a.z - sign*b.z) < epsilon && std::fabs(a.w - sign*b.w) < epsilon;
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

// The composition-order test, written because this failure is silent and a
// project on another engine lost real time to it.
void TestMountCompositionOrder()
{
    // A 90-degree yaw authored mount, and no controller movement since
    // calibration: the mount must come back unchanged.
    const float halfPi = 3.14159265358979f * 0.25f;
    const Quaternion mount{0.0f, 0.0f, std::sin(halfPi), std::cos(halfPi)};
    const Quaternion aim{0.0f, 0.0f, 0.0f, 1.0f};
    const auto still = MountRotationFromControllerDelta(mount, aim, aim);
    Require(still.has_value(), "a zero delta is composable");
    Require(NearQuat(*still, mount),
        "with no controller movement the authored mount survives unchanged");

    // Now rotate the controller by 90 degrees about Z. Composed correctly the
    // result is delta * mount = 180 degrees; composed the WRONG way round it is
    // also 180 here, so a symmetric case cannot tell them apart -- which is
    // exactly why the real symptom is so confusing. Use an asymmetric axis.
    const Quaternion pitch{std::sin(halfPi), 0.0f, 0.0f, std::cos(halfPi)};
    const auto turned = MountRotationFromControllerDelta(mount, aim, pitch);
    Require(turned.has_value(), "a real delta is composable");
    const Quaternion right = Normalize(Multiply(pitch, mount));
    const Quaternion wrong = Normalize(Multiply(mount, pitch));
    Require(NearQuat(*turned, right), "the authored mount composes on the RIGHT");
    Require(!NearQuat(right, wrong),
        "the two orders differ on this input, so the test can actually fail");
}

// Fail closed: a degenerate rotation must not produce a plausible mount.
void TestMountCompositionFailsClosed()
{
    const Quaternion good{0.0f, 0.0f, 0.0f, 1.0f};
    const Quaternion zero{0.0f, 0.0f, 0.0f, 0.0f};
    Require(!MountRotationFromControllerDelta(zero, good, good).has_value(),
        "a zero-length authored mount is refused");
    Require(!MountRotationFromControllerDelta(good, zero, good).has_value(),
        "a zero-length calibration reference is refused");
    Require(!MountRotationFromControllerDelta(good, good, zero).has_value(),
        "a zero-length live aim is refused, so an untracked controller writes nothing");
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

// --- rigid subtree rotation -------------------------------------------------

Quaternion AxisAngle(Vec3 axis, float radians)
{
    const float len = std::sqrt(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
    const float s = std::sin(radians * 0.5f) / len;
    return Quaternion{axis.x*s, axis.y*s, axis.z*s, std::cos(radians * 0.5f)};
}

// **The driven joint must not move.** It is the pivot, so a rotation about it

// `WorldDeltaToModel` from the hand lane, which lives in the DLL. Duplicated
// rather than linked so this stays a pure test; if it drifts from the original
// the agreement test below is what fails.
Vec3 ToBodyFrame(const Vec3& delta, float bodyYaw)
{
    const float s = std::sin(bodyYaw), c = std::cos(bodyYaw);
    return Vec3{delta.x * c + delta.y * s, delta.x * -s + delta.y * c, delta.z};
}

// leaves its position alone and turns only its orientation. If this fails the
// hand translates when the player only rotated their wrist.
void TestPivotJointStaysPut()
{
    const Vec3 pivot{1.0f, 2.0f, 3.0f};
    JointPose wrist;
    wrist.position = pivot;
    wrist.rotation = Quaternion{0.0f, 0.0f, 0.0f, 1.0f};

    const Quaternion turn = AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 1.2f);
    const JointPose out = RotateJointAboutPivot(wrist, pivot, turn);
    Require(Near(out.position.x, pivot.x) && Near(out.position.y, pivot.y) &&
            Near(out.position.z, pivot.z), "the pivot joint must not translate");
    Require(NearQuat(out.rotation, turn), "and its rotation must be the applied turn");
}

// A child orbits the pivot: distance preserved, angle applied.
void TestChildOrbitsThePivot()
{
    const Vec3 pivot{0.0f, 0.0f, 0.0f};
    JointPose finger;
    finger.position = Vec3{1.0f, 0.0f, 0.0f};
    finger.rotation = Quaternion{0.0f, 0.0f, 0.0f, 1.0f};

    // 90 degrees about Z takes +X to +Y.
    const Quaternion turn = AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 3.14159265f * 0.5f);
    const JointPose out = RotateJointAboutPivot(finger, pivot, turn);
    Require(Near(out.position.x, 0.0f, 1e-4f) && Near(out.position.y, 1.0f, 1e-4f),
            "a child must orbit the pivot, not stay behind");

    // **Rigid** means the distance to the pivot is unchanged. A subtree that
    // stretches is the tearing failure in a different disguise.
    const float before = 1.0f;
    const float after = std::sqrt(out.position.x*out.position.x +
                                  out.position.y*out.position.y +
                                  out.position.z*out.position.z);
    Require(Near(after, before, 1e-4f), "the rotation must be rigid");
}

// Two joints keep their separation: the hand does not deform.
void TestSubtreeKeepsItsShape()
{
    const Vec3 pivot{0.5f, -0.25f, 2.0f};
    const Quaternion turn = AxisAngle(Vec3{0.3f, 1.0f, -0.2f}, 0.9f);

    JointPose a; a.position = Vec3{0.9f, 0.1f, 2.4f};
    JointPose b; b.position = Vec3{1.3f, -0.6f, 1.7f};
    const auto dist = [](const Vec3& p, const Vec3& q) {
        const float dx = p.x-q.x, dy = p.y-q.y, dz = p.z-q.z;
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    };
    const float before = dist(a.position, b.position);
    const JointPose ra = RotateJointAboutPivot(a, pivot, turn);
    const JointPose rb = RotateJointAboutPivot(b, pivot, turn);
    Require(Near(dist(ra.position, rb.position), before, 1e-4f),
            "the distance between two joints must survive the rotation");
}

// Identity is a no-op, which is the control: a correct implementation with no
// rotation must be indistinguishable from not running at all.
void TestIdentityChangesNothing()
{
    JointPose j;
    j.position = Vec3{2.0f, -1.0f, 0.5f};
    j.rotation = AxisAngle(Vec3{1.0f, 0.0f, 0.0f}, 0.4f);
    const JointPose out = RotateJointAboutPivot(j, Vec3{9.0f, 9.0f, 9.0f},
                                                Quaternion{0.0f, 0.0f, 0.0f, 1.0f});
    Require(Near(out.position.x, j.position.x) && Near(out.position.y, j.position.y) &&
            Near(out.position.z, j.position.z), "identity must not move a joint");
    Require(NearQuat(out.rotation, j.rotation), "identity must not turn a joint");
}

// Composition order, the same trap the weapon mount has: R*rot, not rot*R.
void TestCompositionOrderIsBasisChangeOnTheLeft()
{
    JointPose j;
    j.position = Vec3{};
    j.rotation = AxisAngle(Vec3{1.0f, 0.0f, 0.0f}, 1.1f);
    const Quaternion turn = AxisAngle(Vec3{0.2f, 1.0f, 0.4f}, 0.7f);
    const JointPose out = RotateJointAboutPivot(j, Vec3{}, turn);
    Require(NearQuat(out.rotation, Normalize(Multiply(turn, j.rotation))),
            "the turn composes on the left");
    Require(!NearQuat(Normalize(Multiply(turn, j.rotation)),
                      Normalize(Multiply(j.rotation, turn))),
            "and the two orders must actually differ, or this proves nothing");
}


// The wearer's 2026-09-07 report, encoded as a test: with the model frame a half
// turn from the body frame, a wrist yaw survives while pitch and roll come back
// inverted. This is the *symptom*, and asserting it here is what makes the fix a
// frame conversion rather than two fitted sign flips -- the signs would only be
// right on the axes they were fitted on, and this shows why.
void TestMissingYawInvertsPitchAndRoll()
{
    const float kPi = 3.14159265358979f;
    // A pure yaw passes through a half-turn conjugation untouched.
    const Quaternion yaw = AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 0.7f);
    Require(NearQuat(WorldTurnToModel(yaw, kPi), yaw, 1e-4f),
            "yaw must survive the frame change, which is why it read as correct");

    // Pitch (about X) and roll (about Y) come back negated.
    const Quaternion pitch = AxisAngle(Vec3{1.0f, 0.0f, 0.0f}, 0.7f);
    Require(NearQuat(WorldTurnToModel(pitch, kPi),
                     AxisAngle(Vec3{1.0f, 0.0f, 0.0f}, -0.7f), 1e-4f),
            "pitch must invert under a half turn");
    const Quaternion roll = AxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 0.7f);
    Require(NearQuat(WorldTurnToModel(roll, kPi),
                     AxisAngle(Vec3{0.0f, 1.0f, 0.0f}, -0.7f), 1e-4f),
            "and roll with it");
}

// The conversion must agree with the position lane it is paired with. Both take
// the same `bodyYaw`, so rotating a vector by the turn and then converting must
// equal converting both and rotating -- otherwise a hand's travel and its twist
// diverge whenever the player is not facing the calibration heading, which is
// the failure that only shows up after someone walks around a corner.
void TestTurnConversionAgreesWithTheDeltaLane()
{
    const float bodyYaw = 0.9f;
    const Quaternion turn = AxisAngle(Vec3{0.3f, -0.5f, 0.8f}, 1.1f);
    const Vec3 v{0.4f, -0.2f, 0.7f};

    // Rotate in world, then change frame.
    const Vec3 worldThenModel = ToBodyFrame(Rotate(turn, v), bodyYaw);
    // Change frame, then rotate with the converted turn.
    const Vec3 modelThenRotate =
        Rotate(WorldTurnToModel(turn, bodyYaw), ToBodyFrame(v, bodyYaw));

    Require(Near(worldThenModel.x, modelThenRotate.x, 1e-4f) &&
            Near(worldThenModel.y, modelThenRotate.y, 1e-4f) &&
            Near(worldThenModel.z, modelThenRotate.z, 1e-4f),
            "the turn and delta lanes must live in the same frame");
}

// Zero offset must be exactly a no-op, so the tunable knob cannot quietly rotate
// anything when nobody has set it.
void TestZeroYawIsIdentity()
{
    const Quaternion turn = AxisAngle(Vec3{0.2f, 0.9f, -0.3f}, 0.6f);
    Require(NearQuat(WorldTurnToModel(turn, 0.0f), Normalize(turn), 1e-5f),
            "an unset offset must change nothing");
}

int main()
{
    TestAimRayFromController();
    TestAimFailsClosed();
    TestHandAndEyeShareAReferenceFrame();
    TestWeaponPoseAppliesGrip();
    TestPivotJointStaysPut();
    TestChildOrbitsThePivot();
    TestSubtreeKeepsItsShape();
    TestIdentityChangesNothing();
    TestCompositionOrderIsBasisChangeOnTheLeft();
    TestMountCompositionOrder();
    TestMountCompositionFailsClosed();
    TestTwoHandedPose();
    TestTwoHandedFailsClosed();
    TestTurning();
    TestMissingYawInvertsPitchAndRoll();
    TestTurnConversionAgreesWithTheDeltaLane();
    TestZeroYawIsIdentity();
    std::cout << "PreyVR motion controller tests passed\n";
    return 0;
}
