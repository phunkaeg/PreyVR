#include "preyvr/AnimIk.h"

#include "preyvr/StereoCamera.h"

#include <cmath>

namespace preyvr::animik {

namespace {

Quaternion ConjugateOf(const Quaternion& q) { return Quaternion{-q.x, -q.y, -q.z, q.w}; }

} // namespace

bool ValidLocation(const Location& location)
{
    const auto& q = location.q;
    const float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return std::isfinite(norm) && norm > 0.81f && norm < 1.21f &&
        std::isfinite(location.t.x) && std::isfinite(location.t.y) &&
        std::isfinite(location.t.z) && std::isfinite(location.s) && location.s > 1e-6f;
}

bool ValidJoint(int index, unsigned int count)
{
    return index >= 0 && static_cast<unsigned int>(index) < count;
}

Vec3 WorldToModel(const Location& location, const Vec3& world)
{
    const Vec3 offset{world.x - location.t.x, world.y - location.t.y, world.z - location.t.z};
    const Vec3 turned = Rotate(ConjugateOf(Normalize(location.q)), offset);
    const float inverseScale = (std::isfinite(location.s) && std::fabs(location.s) > 1e-6f)
                                   ? 1.0f / location.s : 1.0f;
    return Vec3{turned.x * inverseScale, turned.y * inverseScale, turned.z * inverseScale};
}

Quaternion WorldToModel(const Location& location, const Quaternion& world)
{
    return Normalize(Multiply(ConjugateOf(Normalize(location.q)), Normalize(world)));
}

Vec3 ModelToWorld(const Location& location, const Vec3& model)
{
    const Vec3 scaled{model.x * location.s, model.y * location.s, model.z * location.s};
    const Vec3 turned = Rotate(Normalize(location.q), scaled);
    return Vec3{turned.x + location.t.x, turned.y + location.t.y, turned.z + location.t.z};
}

Pose ControllerWorldFromHead(float yawRadians, const Vec3& eyeWorld,
                             const Pose& openXrHead, const Pose& openXrController)
{
    // The difference is taken in OpenXR space and converted once, inside
    // EyePoseInWorld, so the axis change happens in exactly one place.
    Pose relative;
    relative.orientation = openXrController.orientation;
    relative.position = Vec3{openXrController.position.x - openXrHead.position.x,
                             openXrController.position.y - openXrHead.position.y,
                             openXrController.position.z - openXrHead.position.z};
    stereo::ReferenceFrame reference{};
    reference.yawRadians = yawRadians;
    reference.worldPosition = eyeWorld;
    return stereo::EyePoseInWorld(reference, relative);
}

Vec3 ClampToReach(const Vec3& upperJoint, const Vec3& goal, float reach)
{
    const Vec3 d{goal.x - upperJoint.x, goal.y - upperJoint.y, goal.z - upperJoint.z};
    const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!std::isfinite(length) || !std::isfinite(reach) || reach <= 0.0f || length <= reach) {
        return goal;
    }
    const float k = reach / length;
    return Vec3{upperJoint.x + d.x * k, upperJoint.y + d.y * k, upperJoint.z + d.z * k};
}

Quaternion CalibrateRotationOffset(const Quaternion& controllerModel,
                                   const Quaternion& wristModel)
{
    return Normalize(Multiply(ConjugateOf(Normalize(controllerModel)), Normalize(wristModel)));
}

Quaternion ApplyRotationOffset(const Quaternion& controllerModel, const Quaternion& offset)
{
    return Normalize(Multiply(Normalize(controllerModel), Normalize(offset)));
}

float YawOf(const Quaternion& q)
{
    return std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z));
}

} // namespace preyvr::animik
