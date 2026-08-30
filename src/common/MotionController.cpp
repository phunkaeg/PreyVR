#include "preyvr/MotionController.h"

#include <cmath>

namespace preyvr::controller {
namespace {

constexpr float kPi = 3.14159265358979323846f;

Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float Length(const Vec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

std::optional<Vec3> TryNormalize(const Vec3& v, float minimumLength = 1e-6f)
{
    const float length = Length(v);
    if (!std::isfinite(length) || length < minimumLength) {
        return std::nullopt;
    }
    return Vec3{v.x / length, v.y / length, v.z / length};
}

} // namespace

Vec3 ForwardOf(const Pose& engineSpacePose)
{
    return Rotate(Normalize(engineSpacePose.orientation), Vec3{0.0f, 1.0f, 0.0f});
}

Pose ControllerPoseInWorld(
    const stereo::ReferenceFrame& reference,
    const Pose& openXrControllerPose)
{
    // Deliberately the same composition the eyes use. If the hand and the view
    // were built from different reference frames they would drift apart the
    // moment the player snap-turned, which reads as the weapon lagging the world.
    return stereo::EyePoseInWorld(reference, openXrControllerPose);
}

std::optional<AimRay> AimFromController(
    const stereo::ReferenceFrame& reference,
    const Pose& openXrControllerPose,
    const PoseValidity& validity,
    std::uint64_t maxAgeNanoseconds)
{
    if (!IsPoseUsable(openXrControllerPose, validity, maxAgeNanoseconds)) {
        return std::nullopt;
    }
    const Pose world = ControllerPoseInWorld(reference, openXrControllerPose);
    const auto direction = TryNormalize(ForwardOf(world));
    if (!direction) {
        return std::nullopt;
    }
    return AimRay{world.position, *direction};
}

Pose WeaponPoseFromController(
    const stereo::ReferenceFrame& reference,
    const Pose& openXrControllerPose,
    const GripTransform& grip)
{
    const Pose hand = ControllerPoseInWorld(reference, openXrControllerPose);
    return Compose(hand, grip.controllerToWeapon);
}

std::optional<Pose> TwoHandedWeaponPose(
    const Vec3& rearHandWorld,
    const Vec3& frontHandWorld,
    const Vec3& referenceUp,
    float minimumSeparation)
{
    const Vec3 along{
        frontHandWorld.x - rearHandWorld.x,
        frontHandWorld.y - rearHandWorld.y,
        frontHandWorld.z - rearHandWorld.z,
    };
    if (Length(along) < minimumSeparation) {
        // Below this the direction between the hands is mostly tracking noise
        // and the weapon would spin. Refusing is better than producing a pose
        // that jitters.
        return std::nullopt;
    }
    const auto forward = TryNormalize(along);
    if (!forward) {
        return std::nullopt;
    }

    // Engine basis: column 0 right, column 1 forward, column 2 up, right-handed.
    // right = forward x up, then up = right x forward re-orthogonalises.
    const auto right = TryNormalize(Cross(*forward, referenceUp));
    if (!right) {
        return std::nullopt; // hands aligned with the up axis: roll is undefined
    }
    const Vec3 up = Cross(*right, *forward);

    // Shared with StereoCamera rather than hand-rolled a second time: two copies
    // of quaternion-from-basis is two chances to get the branch conditions
    // subtly different, and they would disagree only at rare orientations.
    Pose pose{};
    pose.orientation = stereo::QuaternionFromBasis(*right, *forward, up);
    pose.position = frontHandWorld;
    return pose;
}

float WrapAngle(float radians)
{
    if (!std::isfinite(radians)) {
        return 0.0f;
    }
    while (radians > kPi) {
        radians -= 2.0f * kPi;
    }
    while (radians <= -kPi) {
        radians += 2.0f * kPi;
    }
    return radians;
}

float SnapTurn(float currentYawRadians, int steps, float stepRadians)
{
    if (!std::isfinite(currentYawRadians) || !std::isfinite(stepRadians)) {
        return 0.0f;
    }
    return WrapAngle(currentYawRadians + static_cast<float>(steps) * stepRadians);
}

float SmoothTurn(float currentYawRadians, float stickX, float radiansPerSecond, float deltaSeconds)
{
    if (!std::isfinite(currentYawRadians) || !std::isfinite(stickX) ||
        !std::isfinite(radiansPerSecond) || !std::isfinite(deltaSeconds)) {
        return WrapAngle(currentYawRadians);
    }
    if (deltaSeconds < 0.0f) {
        return WrapAngle(currentYawRadians);
    }
    return WrapAngle(currentYawRadians + stickX * radiansPerSecond * deltaSeconds);
}

} // namespace preyvr::controller
