#include "preyvr/BodyFollow.h"

#include "preyvr/StereoCamera.h"

#include <algorithm>
#include <cmath>

namespace preyvr::body {
namespace {
constexpr float kPi = 3.14159265358979323846f;
// Inverse of stereo::ToEngineSpace for vectors: OpenXR -Z forward is engine +Y,
// OpenXR +Y up is engine +Z (checked against ToEngineSpace by the tests).
Vec3 ToOpenXr(const Vec3& engine) { return {engine.x, engine.z, -engine.y}; }
}

float Wrap(float radians)
{
    if (!std::isfinite(radians)) { return 0.0f; }
    float r = std::remainder(radians, 2.0f * kPi);
    if (r <= -kPi) { r += 2.0f * kPi; }
    return r;
}

std::optional<float> HeadHeading(const Quaternion& openXrHead)
{
    const Quaternion q = stereo::ToEngineSpace(openXrHead);
    const Vec3 forward = Rotate(q, Vec3{0.0f, 1.0f, 0.0f});
    const Vec3 up = Rotate(q, Vec3{0.0f, 0.0f, 1.0f});
    // Looking down (forward.z < 0) the crown points the way the face did;
    // looking up it points back. Weighted by how far the face has left level.
    const float x = forward.x - up.x * forward.z;
    const float y = forward.y - up.y * forward.z;
    if (!std::isfinite(x) || !std::isfinite(y) || x * x + y * y < 1e-6f) { return std::nullopt; }
    return std::atan2(-x, y);
}

Vec3 NeckPoint(const Pose& openXrHead, float up, float forward)
{
    // Eye relative to the neck, head frame: up +Y, forward -Z.
    const Vec3 lever = Rotate(Normalize(openXrHead.orientation), Vec3{0.0f, up, -forward});
    return {openXrHead.position.x - lever.x, openXrHead.position.y - lever.y,
            openXrHead.position.z - lever.z};
}

Vec3 TrackingForward(float yaw)
{
    return {-std::sin(yaw), 0.0f, -std::cos(yaw)};
}

Vec3 NeckAnchor(const Vec3& origin, float yaw, float forward)
{
    const Vec3 f = TrackingForward(yaw);
    return {origin.x - f.x * forward, origin.y, origin.z - f.z * forward};
}

float YawStep(float error, float dt, const YawPolicy& policy, bool& catching)
{
    error = Wrap(error);
    const float magnitude = std::fabs(error);
    const float deadzone = std::fmax(0.0f, policy.deadzone);
    if (deadzone <= 0.0f) {
        catching = magnitude > 0.0f;
    } else if (!catching && magnitude > deadzone) {
        catching = true;
    } else if (catching && magnitude <= deadzone * 0.1f) {
        catching = false;
    }
    if (!catching) { return 0.0f; }
    if (policy.rate <= 0.0f || !std::isfinite(dt) || dt <= 0.0f) { return error; }
    const float limit = policy.rate * dt;
    return std::fabs(error) <= limit ? error : std::copysign(limit, error);
}

Vec2 RoomStep(Vec2 offset, const RoomPolicy& policy, bool& moving)
{
    const float length = std::sqrt(offset.x * offset.x + offset.y * offset.y);
    if (!std::isfinite(length)) { moving = false; return {}; }
    if (!moving && length > policy.start) { moving = true; }
    if (moving && length <= policy.stop) { moving = false; }
    if (!moving) { return {}; }
    if (length > policy.maxStep && length > 0.0f) {
        const float s = policy.maxStep / length;
        return {offset.x * s, offset.y * s};
    }
    return offset;
}

Vec2 LeanStep(Vec2 offset, float headSpeed, float dt, bool leaning, const LeanPolicy& policy, bool& moving)
{
    const float length = std::sqrt(offset.x * offset.x + offset.y * offset.y);
    if (!std::isfinite(length) || !(length > 0.0f)) { moving = false; return {}; }
    // Past the radius the body walks back to it at once, leaning or not.
    const float excess = std::max(0.0f, length - policy.radius);
    if (excess > 0.0f || (length > policy.start && !leaning)) { moving = true; }
    if (moving && (length <= policy.stop || (leaning && excess <= 0.0f))) { moving = false; }
    float move = excess;
    if (moving && !leaning) {
        // Not leaning: it comes back under the head at the head's own speed (and
        // at least the recentre speed) -- not only the part past the radius.
        const float speed = std::max(policy.recentre, std::isfinite(headSpeed) ? headSpeed * 1.2f : 0.0f);
        move = std::max(move, std::min(length, speed * std::clamp(dt, 0.0f, 0.1f)));
    }
    move = std::min(move, policy.maxStep);
    return {offset.x * move / length, offset.y * move / length};
}

bool Leaning(const Quaternion& openXrHead, Vec2 direction, float neckDrop)
{
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    // A lean drops the neck a few centimetres (a hip hinge of 25 cm, ~5); far
    // more is a squat or a crouch, which the body follows.
    if (!(length > 0.005f) || !(neckDrop > 0.02f) || neckDrop > 0.15f) { return false; }
    const Vec3 up = Rotate(Normalize(openXrHead), Vec3{0.0f, 1.0f, 0.0f});
    // The crown's horizontal lean along the direction (tracking x, z): over ~7 degrees.
    return (up.x * direction.x + up.z * direction.y) / length > 0.12f;
}

Vec2 Slide(Vec2 step, Vec2 wallNormal)
{
    const float n2 = wallNormal.x * wallNormal.x + wallNormal.y * wallNormal.y;
    if (!(n2 > 1e-8f)) { return {}; }
    const float into = (step.x * wallNormal.x + step.y * wallNormal.y) / n2;
    if (into >= 0.0f) { return step; }   // moving away from the wall
    return {step.x - wallNormal.x * into, step.y - wallNormal.y * into};
}

Vec3 WorldToTracking(const Vec3& world, float playYaw)
{
    const Vec3 local = Rotate(stereo::YawQuaternion(-playYaw), world);
    return ToOpenXr(local);
}

Vec3 TrackingToWorld(const Vec3& tracking, float playYaw)
{
    return Rotate(stereo::YawQuaternion(playYaw), stereo::ToEngineSpace(tracking));
}

} // namespace preyvr::body
