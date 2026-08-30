#include "preyvr/StereoCamera.h"

#include <cmath>
#include <cstring>

namespace preyvr::stereo {
namespace {

void WriteFloat(std::span<std::uint8_t> bytes, std::size_t index, float value)
{
    std::memcpy(bytes.data() + index * sizeof(float), &value, sizeof(float));
}

float ReadFloat(std::span<const std::uint8_t> bytes, std::size_t index)
{
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + index * sizeof(float), sizeof(float));
    return value;
}

} // namespace

Quaternion Conjugate(Quaternion value)
{
    return {-value.x, -value.y, -value.z, value.w};
}

Vec3 ToEngineSpace(Vec3 openXr)
{
    return Rotate(kOpenXrToEngine, openXr);
}

Quaternion ToEngineSpace(Quaternion openXr)
{
    return Normalize(Multiply(Multiply(kOpenXrToEngine, openXr), Conjugate(kOpenXrToEngine)));
}

Pose ToEngineSpace(const Pose& openXr)
{
    return {ToEngineSpace(openXr.orientation), ToEngineSpace(openXr.position)};
}

Matrix34 MatrixFromPose(const Pose& pose)
{
    // Built by rotating the unit axes rather than by expanding the quaternion
    // into matrix entries by hand. It is a little more work at runtime and this
    // is not a hot path, but it reuses Rotate, which is already tested, instead
    // of introducing a second and independently wrong copy of the same algebra.
    const Quaternion q = Normalize(pose.orientation);
    const Vec3 right = Rotate(q, Vec3{1.0f, 0.0f, 0.0f});
    const Vec3 forward = Rotate(q, Vec3{0.0f, 1.0f, 0.0f});
    const Vec3 up = Rotate(q, Vec3{0.0f, 0.0f, 1.0f});

    Matrix34 matrix{};
    matrix[0] = right.x;   matrix[1] = forward.x;  matrix[2] = up.x;   matrix[3] = pose.position.x;
    matrix[4] = right.y;   matrix[5] = forward.y;  matrix[6] = up.y;   matrix[7] = pose.position.y;
    matrix[8] = right.z;   matrix[9] = forward.z;  matrix[10] = up.z;  matrix[11] = pose.position.z;
    return matrix;
}

Vec3 RightOf(const Matrix34& matrix)
{
    return {matrix[0], matrix[4], matrix[8]};
}

Vec3 ForwardOf(const Matrix34& matrix)
{
    return {matrix[1], matrix[5], matrix[9]};
}

Vec3 UpOf(const Matrix34& matrix)
{
    return {matrix[2], matrix[6], matrix[10]};
}

Vec3 PositionOf(const Matrix34& matrix)
{
    return {matrix[3], matrix[7], matrix[11]};
}

Quaternion YawQuaternion(float radians)
{
    const float half = radians * 0.5f;
    return {0.0f, 0.0f, std::sin(half), std::cos(half)};
}

Pose EyePoseInWorld(const ReferenceFrame& reference, const Pose& openXrEyePose)
{
    const Pose base{YawQuaternion(reference.yawRadians), reference.worldPosition};
    return Compose(base, ToEngineSpace(openXrEyePose));
}

Pose CyclopsPose(const Pose& leftEye, const Pose& rightEye)
{
    Pose result{};
    result.orientation = Normalize(leftEye.orientation);
    result.position = {
        (leftEye.position.x + rightEye.position.x) * 0.5f,
        (leftEye.position.y + rightEye.position.y) * 0.5f,
        (leftEye.position.z + rightEye.position.z) * 0.5f,
    };
    return result;
}

bool WriteMatrix(std::span<std::uint8_t> camera, const Matrix34& matrix)
{
    if (camera.size() < kCameraSize) {
        return false;
    }
    for (float value : matrix) {
        if (!std::isfinite(value)) {
            // Refused rather than written: a NaN reaching the frustum rebuild
            // propagates into the cached planes and poisons culling in a way
            // that does not look like a camera bug from the outside.
            return false;
        }
    }
    for (std::size_t i = 0; i < matrix.size(); ++i) {
        WriteFloat(camera, i, matrix[i]);
    }
    return true;
}

Matrix34 ReadMatrix(std::span<const std::uint8_t> camera)
{
    Matrix34 matrix{};
    if (camera.size() < kCameraSize) {
        return matrix;
    }
    for (std::size_t i = 0; i < matrix.size(); ++i) {
        matrix[i] = ReadFloat(camera, i);
    }
    return matrix;
}

} // namespace preyvr::stereo
