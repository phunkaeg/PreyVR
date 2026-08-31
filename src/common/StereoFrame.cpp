#include "preyvr/StereoFrame.h"

#include "preyvr/CameraEdit.h"
#include "preyvr/EngineMap.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace preyvr::stereoframe {
namespace {

void WriteFloatAt(std::span<std::uint8_t> bytes, std::size_t offset, float value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(float));
}

float ReadFloatAt(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + offset, sizeof(float));
    return value;
}

bool TangentsAreSane(const EyeView& view)
{
    const float values[] = {view.tanLeft, view.tanRight, view.tanDown, view.tanUp};
    for (const float value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
        // A runtime reporting a half-angle past ~84 degrees is either broken or
        // describing something we should not silently render.
        if (std::fabs(value) > 10.0f) {
            return false;
        }
    }
    // Right must be to the right of left, up above down. A runtime that returns
    // them swapped would otherwise produce an inside-out frustum.
    return view.tanRight > view.tanLeft && view.tanUp > view.tanDown;
}

} // namespace

float NearPlaneOf(std::span<const std::uint8_t> camera)
{
    if (camera.size() < kCameraSize) {
        return 0.0f;
    }
    // m_edge_nlt.y, per CameraLayout.
    return ReadFloatAt(camera, engine::CameraLayout::edgeNearLeftTop + sizeof(float));
}

EyeView SyntheticEyeView(int eye, float halfFovDegrees, float asymmetryScale)
{
    const float radians = halfFovDegrees * 3.14159265358979323846f / 180.0f;
    // Scaled symmetrically about the half-FOV: the outer edge grows by exactly
    // as much as the inner one shrinks, so a scale of 1.0 collapses both to the
    // same angle and the two eyes end up with identical frusta.
    const float outer = std::tan(radians * asymmetryScale);
    const float inner = std::tan(radians * (2.0f - asymmetryScale));
    const float vertical = std::tan(radians);

    EyeView view{};
    view.tanUp = vertical;
    view.tanDown = -vertical;
    view.tanLeft = eye == 0 ? -outer : -inner;
    view.tanRight = eye == 0 ? inner : outer;
    return view;
}

std::optional<EyeProjection> ProjectionFromTangents(const EyeView& view, float nearPlane)
{
    if (!TangentsAreSane(view) || !std::isfinite(nearPlane) || nearPlane <= 0.0f) {
        return std::nullopt;
    }

    // Symmetric envelope: the larger half-extent on each axis. Choosing the
    // larger rather than the average keeps both shifts on the same side of zero
    // and small, which makes an error legible instead of merely different.
    const float verticalTangent = std::max(std::fabs(view.tanUp), std::fabs(view.tanDown));
    const float horizontalTangent = std::max(std::fabs(view.tanLeft), std::fabs(view.tanRight));
    if (verticalTangent <= 0.0f || horizontalTangent <= 0.0f) {
        return std::nullopt;
    }

    EyeProjection projection{};
    projection.fov = 2.0f * std::atan(verticalTangent);
    projection.projectionRatio = horizontalTangent / verticalTangent;
    projection.asymmetry = snapshot::AsymmetryFromFovTangents(
        view.tanLeft, view.tanRight, view.tanDown, view.tanUp,
        projection.fov, projection.projectionRatio, nearPlane);

    if (!std::isfinite(projection.fov) || !std::isfinite(projection.projectionRatio)) {
        return std::nullopt;
    }
    const float shifts[] = {
        projection.asymmetry.left, projection.asymmetry.right,
        projection.asymmetry.bottom, projection.asymmetry.top,
    };
    for (const float shift : shifts) {
        if (!std::isfinite(shift)) {
            return std::nullopt;
        }
    }
    return projection;
}

namespace {

// Produces one eye's camera by copying the base and overwriting only the view
// and projection fields.
std::optional<EyeCamera> BuildEye(
    std::span<const std::uint8_t> baseCamera,
    const stereo::ReferenceFrame& reference,
    const EyeView& view,
    const EyeProjection& projection,
    Pose& worldPoseOut)
{
    EyeCamera eye{};
    std::memcpy(eye.bytes.data(), baseCamera.data(), kCameraSize);

    worldPoseOut = stereo::EyePoseInWorld(reference, view.openXrPose);
    const stereo::Matrix34 matrix = stereo::MatrixFromPose(worldPoseOut);
    if (!stereo::WriteMatrix(eye.bytes, matrix)) {
        return std::nullopt;
    }

    WriteFloatAt(eye.bytes, engine::CameraLayout::fov, projection.fov);
    WriteFloatAt(eye.bytes, engine::CameraLayout::projectionRatio, projection.projectionRatio);
    WriteFloatAt(eye.bytes, engine::CameraLayout::asymLeft, projection.asymmetry.left);
    WriteFloatAt(eye.bytes, engine::CameraLayout::asymRight, projection.asymmetry.right);
    WriteFloatAt(eye.bytes, engine::CameraLayout::asymBottom, projection.asymmetry.bottom);
    WriteFloatAt(eye.bytes, engine::CameraLayout::asymTop, projection.asymmetry.top);

    // The same gate the single-camera write uses. A per-eye matrix that fails it
    // would make UpdateFrustum negate the plane normals for that eye only, which
    // is a genuinely confusing thing to debug from inside a headset.
    if (!cameraedit::RotationIsSafeToWrite(eye.bytes)) {
        return std::nullopt;
    }
    return eye;
}

} // namespace

std::optional<StereoPlan> BuildStereoPlan(
    std::span<const std::uint8_t> baseCamera,
    const stereo::ReferenceFrame& reference,
    const EyeView& leftEye,
    const EyeView& rightEye)
{
    if (baseCamera.size() < kCameraSize) {
        return std::nullopt;
    }
    const float nearPlane = NearPlaneOf(baseCamera);

    const auto leftProjection = ProjectionFromTangents(leftEye, nearPlane);
    const auto rightProjection = ProjectionFromTangents(rightEye, nearPlane);
    if (!leftProjection || !rightProjection) {
        return std::nullopt;
    }

    StereoPlan plan{};
    plan.leftProjection = *leftProjection;
    plan.rightProjection = *rightProjection;

    Pose leftWorld{};
    Pose rightWorld{};
    const auto left = BuildEye(baseCamera, reference, leftEye, *leftProjection, leftWorld);
    const auto right = BuildEye(baseCamera, reference, rightEye, *rightProjection, rightWorld);
    // Both or neither: one good eye and one bad one is worse than none, because
    // it looks like it nearly works.
    if (!left || !right) {
        return std::nullopt;
    }

    plan.left = *left;
    plan.right = *right;
    plan.cyclops = stereo::CyclopsPose(leftWorld, rightWorld);
    return plan;
}

} // namespace preyvr::stereoframe
