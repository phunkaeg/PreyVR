#include "preyvr/CameraEdit.h"

#include <cmath>
#include <cstring>

namespace preyvr::cameraedit {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float ReadFloat(std::span<const std::uint8_t> bytes, std::size_t index)
{
    float value = 0.0f;
    std::memcpy(&value, bytes.data() + index * sizeof(float), sizeof(float));
    return value;
}

void WriteFloat(std::span<std::uint8_t> bytes, std::size_t index, float value)
{
    std::memcpy(bytes.data() + index * sizeof(float), &value, sizeof(float));
}

} // namespace

bool IsWithinBounds(const YawEdit& edit)
{
    if (!std::isfinite(edit.degrees)) {
        return false;
    }
    // A zero edit would make "did the image change?" pass without testing
    // anything, so it is rejected rather than silently accepted as a no-op.
    if (edit.degrees == 0.0f) {
        return false;
    }
    return std::fabs(edit.degrees) <= kMaxYawDegrees;
}

bool ApplyYaw(std::span<std::uint8_t> camera, const YawEdit& edit)
{
    if (camera.size() < kCameraSize || !IsWithinBounds(edit)) {
        return false;
    }

    const float radians = edit.degrees * (kPi / 180.0f);
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    // Row-major Matrix34: row i occupies floats [4i .. 4i+3], with the
    // translation in column 3. Only columns 0 and 1 move under a Z-up yaw.
    for (std::size_t row = 0; row < 3; ++row) {
        const std::size_t c0 = row * 4 + 0;
        const std::size_t c1 = row * 4 + 1;
        const float x = ReadFloat(camera, c0);
        const float y = ReadFloat(camera, c1);
        WriteFloat(camera, c0, c * x + s * y);
        WriteFloat(camera, c1, -s * x + c * y);
    }
    return true;
}

bool RotationIsOrthonormal(std::span<const std::uint8_t> camera, float epsilon)
{
    if (camera.size() < kCameraSize) {
        return false;
    }

    // Transcribed **verbatim** from the engine's own check, rather than
    // re-derived from a geometric description. Each of the nine expressions
    // below is one term of that function, with the same operand order.
    //
    // Doing it any other way is how a sign gets flipped: paraphrasing this as
    // "each column equals the cross product of the other two" produced a negated
    // third block on the first attempt here, which would have passed the
    // identity matrix and every rotation while disagreeing with the engine on
    // exactly the malformed inputs the check exists to catch.
    const float m00 = ReadFloat(camera, 0);
    const float m01 = ReadFloat(camera, 1);
    const float m02 = ReadFloat(camera, 2);
    const float m10 = ReadFloat(camera, 4);
    const float m11 = ReadFloat(camera, 5);
    const float m12 = ReadFloat(camera, 6);
    const float m20 = ReadFloat(camera, 8);
    const float m21 = ReadFloat(camera, 9);
    const float m22 = ReadFloat(camera, 10);

    if (std::fabs(m00 - (m11 * m22 - m21 * m12)) > epsilon ||
        std::fabs(m10 - (m21 * m02 - m01 * m22)) > epsilon ||
        std::fabs(m20 - (m01 * m12 - m11 * m02)) > epsilon) {
        return false;
    }
    if (std::fabs(m01 - (m12 * m20 - m22 * m10)) > epsilon ||
        std::fabs(m11 - (m22 * m00 - m02 * m20)) > epsilon ||
        std::fabs(m21 - (m02 * m10 - m12 * m00)) > epsilon) {
        return false;
    }
    if (std::fabs(m02 - (m21 * m10 - m11 * m20)) > epsilon ||
        std::fabs(m12 - (m20 * m01 - m00 * m21)) > epsilon ||
        std::fabs(m22 - (m11 * m00 - m10 * m01)) > epsilon) {
        return false;
    }
    return true;
}

bool RotationIsDegenerate(std::span<const std::uint8_t> camera, float minAxisLength)
{
    if (camera.size() < kCameraSize) {
        return true;
    }
    const float minimumSquared = minAxisLength * minAxisLength;
    for (std::size_t row = 0; row < 3; ++row) {
        const float x = ReadFloat(camera, row * 4 + 0);
        const float y = ReadFloat(camera, row * 4 + 1);
        const float z = ReadFloat(camera, row * 4 + 2);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            return true;
        }
        if (x * x + y * y + z * z < minimumSquared) {
            return true;
        }
    }
    return false;
}

bool RotationIsSafeToWrite(std::span<const std::uint8_t> camera)
{
    return RotationIsOrthonormal(camera) && !RotationIsDegenerate(camera);
}

bool Capture(std::span<const std::uint8_t> camera, RestorePoint& out)
{
    if (camera.size() < kCameraSize) {
        out.captured = false;
        return false;
    }
    std::memcpy(out.bytes.data(), camera.data(), kCameraSize);
    out.captured = true;
    return true;
}

bool MatchesRestorePoint(std::span<const std::uint8_t> camera, const RestorePoint& point)
{
    if (!point.captured || camera.size() < kCameraSize) {
        return false;
    }
    return std::memcmp(camera.data(), point.bytes.data(), kCameraSize) == 0;
}

std::array<float, 3> PositionOf(std::span<const std::uint8_t> camera)
{
    if (camera.size() < kCameraSize) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {ReadFloat(camera, 3), ReadFloat(camera, 7), ReadFloat(camera, 11)};
}

} // namespace preyvr::cameraedit
