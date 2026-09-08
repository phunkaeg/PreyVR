#include "preyvr/WeaponAim.h"

#include <cmath>

namespace preyvr::aim {

namespace {

Quaternion ConjugateOf(const Quaternion& q) { return Quaternion{-q.x, -q.y, -q.z, q.w}; }

bool Finite(const Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

bool Usable(const Sample& sample,
            std::uint64_t currentEquipGeneration,
            std::uint64_t currentReferenceGeneration,
            std::uint64_t nowNs,
            std::uint64_t maximumAgeNs)
{
    if (sample.confidence == Confidence::none) { return false; }
    if (sample.publishedNs == 0 || nowNs < sample.publishedNs) { return false; }
    if (nowNs - sample.publishedNs > maximumAgeNs) { return false; }
    // A weapon change invalidates the whole sample, not just its rotation: the
    // muzzle moved, the grip moved, and the rig itself is a different object.
    if (sample.equipGeneration != currentEquipGeneration) { return false; }
    // A recentre changes the play-space yaw, so a world direction computed
    // before it is expressed in a frame that no longer exists.
    if (sample.referenceGeneration != currentReferenceGeneration) { return false; }
    return Finite(sample.origin) && Finite(sample.direction);
}

Sample Compose(const Pose& gripWorld,
               const Quaternion& gripToBarrel,
               bool haveBarrelCalibration)
{
    Sample out;
    out.origin = gripWorld.position;
    const Quaternion grip = Normalize(gripWorld.orientation);
    out.orientation = haveBarrelCalibration
                          ? Normalize(Multiply(grip, Normalize(gripToBarrel)))
                          : grip;
    // Engine forward is +Y.
    out.direction = Rotate(out.orientation, Vec3{0.0f, 1.0f, 0.0f});
    const float length = std::sqrt(out.direction.x * out.direction.x +
                                   out.direction.y * out.direction.y +
                                   out.direction.z * out.direction.z);
    if (!std::isfinite(length) || length < 0.9f || length > 1.1f) {
        out.confidence = Confidence::none;
        return out;
    }
    out.direction = Vec3{out.direction.x / length, out.direction.y / length,
                         out.direction.z / length};
    // Without a calibration this is the controller's pointing axis, which is a
    // usable origin and an honest direction, but it is NOT the barrel. Saying so
    // in the type is what stops a consumer treating the two as equivalent.
    out.confidence = haveBarrelCalibration ? Confidence::barrel : Confidence::origin;
    return out;
}

ScreenPoint ProjectToScreen(const Vec3& worldPoint,
                            const Vec3& eyePosition,
                            const Quaternion& eyeOrientation,
                            float tanLeft, float tanRight,
                            float tanUp, float tanDown)
{
    ScreenPoint out;
    const Vec3 offset{worldPoint.x - eyePosition.x,
                      worldPoint.y - eyePosition.y,
                      worldPoint.z - eyePosition.z};
    // Into the eye's own frame. Engine basis: X right, Y forward, Z up.
    const Vec3 local = Rotate(ConjugateOf(Normalize(eyeOrientation)), offset);
    if (!Finite(local)) { return out; }
    // Forward is +Y, and a point at or behind the eye plane has no projection.
    if (local.y <= 1e-4f) {
        out.behind = true;
        return out;
    }
    const float tx = local.x / local.y;
    const float tz = local.z / local.y;
    const float width = tanRight - tanLeft;
    const float height = tanUp - tanDown;
    if (!std::isfinite(width) || !std::isfinite(height) ||
        std::fabs(width) < 1e-6f || std::fabs(height) < 1e-6f) {
        return out;
    }
    out.x = (tx - tanLeft) / width;
    // Screen Y runs down while the frustum's up is +Z, so this inverts.
    out.y = (tanUp - tz) / height;
    out.onScreen = std::isfinite(out.x) && std::isfinite(out.y) &&
                   out.x >= 0.0f && out.x <= 1.0f && out.y >= 0.0f && out.y <= 1.0f;
    return out;
}

} // namespace preyvr::aim
