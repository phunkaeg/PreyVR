#include "preyvr/VrMath.h"

#include <algorithm>
#include <cmath>

namespace preyvr {
namespace {

bool IsFinite(float value)
{
    return std::isfinite(value);
}

bool IsFinite(const Vec3& value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

bool IsFinite(const Quaternion& value)
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

Vec3 Cross(Vec3 left, Vec3 right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

} // namespace

Quaternion Normalize(Quaternion value)
{
    const float lengthSquared =
        value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    if (!IsFinite(value) || !IsFinite(lengthSquared) || lengthSquared <= 1.0e-12f) {
        return {};
    }

    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return {
        value.x * inverseLength,
        value.y * inverseLength,
        value.z * inverseLength,
        value.w * inverseLength,
    };
}

Quaternion Multiply(Quaternion parent, Quaternion child)
{
    parent = Normalize(parent);
    child = Normalize(child);
    return Normalize({
        parent.w * child.x + parent.x * child.w + parent.y * child.z - parent.z * child.y,
        parent.w * child.y - parent.x * child.z + parent.y * child.w + parent.z * child.x,
        parent.w * child.z + parent.x * child.y - parent.y * child.x + parent.z * child.w,
        parent.w * child.w - parent.x * child.x - parent.y * child.y - parent.z * child.z,
    });
}

Vec3 Rotate(Quaternion rotation, Vec3 value)
{
    rotation = Normalize(rotation);
    const Vec3 axis{rotation.x, rotation.y, rotation.z};
    const Vec3 doubledCross = [&] {
        const Vec3 cross = Cross(axis, value);
        return Vec3{2.0f * cross.x, 2.0f * cross.y, 2.0f * cross.z};
    }();
    const Vec3 secondCross = Cross(axis, doubledCross);
    return {
        value.x + rotation.w * doubledCross.x + secondCross.x,
        value.y + rotation.w * doubledCross.y + secondCross.y,
        value.z + rotation.w * doubledCross.z + secondCross.z,
    };
}

Pose Compose(Pose parent, Pose child)
{
    const Vec3 rotatedChild = Rotate(parent.orientation, child.position);
    return {
        Multiply(parent.orientation, child.orientation),
        {
            parent.position.x + rotatedChild.x,
            parent.position.y + rotatedChild.y,
            parent.position.z + rotatedChild.z,
        },
    };
}

Vec2 ApplyRadialDeadzone(Vec2 input, float deadzone)
{
    if (!IsFinite(input.x) || !IsFinite(input.y) || !IsFinite(deadzone)) {
        return {};
    }

    deadzone = std::clamp(deadzone, 0.0f, 0.9999f);
    const float magnitude = std::sqrt(input.x * input.x + input.y * input.y);
    if (!IsFinite(magnitude) || magnitude <= deadzone || magnitude <= 1.0e-8f) {
        return {};
    }

    const float clampedMagnitude = std::min(magnitude, 1.0f);
    const float outputMagnitude = (clampedMagnitude - deadzone) / (1.0f - deadzone);
    const float scale = outputMagnitude / magnitude;
    return {input.x * scale, input.y * scale};
}

bool IsPoseUsable(const Pose& pose, const PoseValidity& validity, std::uint64_t maxAgeNanoseconds)
{
    const float orientationLengthSquared =
        pose.orientation.x * pose.orientation.x +
        pose.orientation.y * pose.orientation.y +
        pose.orientation.z * pose.orientation.z +
        pose.orientation.w * pose.orientation.w;
    return validity.orientationValid && validity.positionValid &&
        validity.orientationTracked && validity.positionTracked &&
        validity.ageNanoseconds <= maxAgeNanoseconds &&
        IsFinite(pose.orientation) && IsFinite(pose.position) &&
        IsFinite(orientationLengthSquared) && orientationLengthSquared > 1.0e-12f;
}

} // namespace preyvr
