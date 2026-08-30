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

    // Build the orientation from the basis by going through a matrix, so this
    // uses the same column convention as everything else rather than a second
    // hand-rolled quaternion construction.
    stereo::Matrix34 matrix{};
    matrix[0] = right->x;   matrix[1] = forward->x;  matrix[2] = up.x;
    matrix[4] = right->y;   matrix[5] = forward->y;  matrix[6] = up.y;
    matrix[8] = right->z;   matrix[9] = forward->z;  matrix[10] = up.z;

    // Quaternion from an orthonormal basis, branching on the largest diagonal
    // term for numerical stability -- the naive single-branch form loses
    // precision when the trace approaches zero, which happens at exactly the
    // 180-degree orientations a player reaches by turning around.
    const float m00 = matrix[0], m01 = matrix[1], m02 = matrix[2];
    const float m10 = matrix[4], m11 = matrix[5], m12 = matrix[6];
    const float m20 = matrix[8], m21 = matrix[9], m22 = matrix[10];

    Quaternion q{};
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }

    Pose pose{};
    pose.orientation = Normalize(q);
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
