#include "preyvr/CameraEdit.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using namespace preyvr::cameraedit;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool Near(float left, float right, float epsilon = 1e-5f)
{
    return std::fabs(left - right) <= epsilon;
}

float Get(const std::vector<std::uint8_t>& camera, std::size_t index)
{
    float value = 0.0f;
    std::memcpy(&value, camera.data() + index * sizeof(float), sizeof(float));
    return value;
}

void Set(std::vector<std::uint8_t>& camera, std::size_t index, float value)
{
    std::memcpy(camera.data() + index * sizeof(float), &value, sizeof(float));
}

// Identity rotation with a recognisable translation, plus a marker late in the
// structure so a test can prove the edit does not scribble past the matrix.
std::vector<std::uint8_t> MakeCamera()
{
    std::vector<std::uint8_t> camera(kCameraSize, 0);
    Set(camera, 0, 1.0f);
    Set(camera, 5, 1.0f);
    Set(camera, 10, 1.0f);
    Set(camera, 3, 325.847f);
    Set(camera, 7, 740.602f);
    Set(camera, 11, 482.790f);
    camera[0x200] = 0xAB; // cached derived state lives out here
    camera[0x23F] = 0xCD;
    return camera;
}

void TestBounds()
{
    Require(IsWithinBounds({10.0f}), "ten degrees is within bounds");
    Require(IsWithinBounds({-15.0f}), "the negative bound is inclusive");
    Require(IsWithinBounds({15.0f}), "the positive bound is inclusive");
    Require(!IsWithinBounds({15.1f}), "past the bound is rejected");
    Require(!IsWithinBounds({-90.0f}), "a large negative angle is rejected");
    // A zero edit would make "did the image change?" pass without testing
    // anything at all.
    Require(!IsWithinBounds({0.0f}), "a zero edit is rejected as vacuous");
    Require(!IsWithinBounds({std::nanf("")}), "NaN is rejected");
    Require(!IsWithinBounds({INFINITY}), "infinity is rejected");
}

void TestYawRotatesTheBasisOnly()
{
    auto camera = MakeCamera();
    // Deliberately at the bound rather than at a convenient 90 degrees: the
    // bound is a safety property and the test should exercise what is actually
    // allowed. (90 was the first attempt here and was correctly refused.)
    Require(ApplyYaw(camera, {kMaxYawDegrees}), "a yaw at the bound applies");

    const float c = std::cos(kMaxYawDegrees * 3.14159265358979323846f / 180.0f);
    const float s = std::sin(kMaxYawDegrees * 3.14159265358979323846f / 180.0f);
    Require(Near(Get(camera, 0), c), "m00 becomes cos");
    Require(Near(Get(camera, 1), -s), "m01 picks up -sin");
    Require(Near(Get(camera, 4), s), "m10 picks up sin");
    Require(Near(Get(camera, 5), c), "m11 becomes cos");
    Require(Near(Get(camera, 2), 0.0f), "column 2 of row 0 is untouched");
    Require(Near(Get(camera, 10), 1.0f), "the Z axis is untouched by a Z-up yaw");

    const auto position = PositionOf(camera);
    Require(Near(position[0], 325.847f), "translation X is preserved");
    Require(Near(position[1], 740.602f), "translation Y is preserved");
    Require(Near(position[2], 482.790f), "translation Z is preserved");

    Require(camera[0x200] == 0xAB && camera[0x23F] == 0xCD,
        "the edit touches the matrix only, not the cached derived state");
}

void TestYawIsRejectedOutOfBounds()
{
    auto camera = MakeCamera();
    const auto before = camera;
    Require(!ApplyYaw(camera, {45.0f}), "an out-of-bounds yaw is refused");
    Require(camera == before, "a refused edit leaves the camera byte-identical");

    std::vector<std::uint8_t> tooSmall(kCameraSize - 1, 0);
    Require(!ApplyYaw(tooSmall, {10.0f}), "a short buffer is refused");
}

void TestRotationStaysOrthonormal()
{
    // The property that matters: UpdateFrustum negates all six frustum plane
    // normals when this check fails, so a denormalising rotation would invert
    // culling rather than merely look slightly wrong.
    auto camera = MakeCamera();
    Require(RotationIsOrthonormal(camera), "the identity rotation is orthonormal");

    for (const float degrees : {-15.0f, -7.5f, -1.0f, 1.0f, 7.5f, 15.0f}) {
        auto rotated = MakeCamera();
        Require(ApplyYaw(rotated, {degrees}), "the yaw applies");
        Require(RotationIsOrthonormal(rotated),
            "a yawed rotation is still orthonormal, so culling will not invert");
    }

    // Repeated application must not drift out of the check either.
    auto drifting = MakeCamera();
    for (int i = 0; i < 64; ++i) {
        Require(ApplyYaw(drifting, {15.0f}), "repeated yaw applies");
    }
    Require(RotationIsOrthonormal(drifting), "64 chained yaws stay orthonormal");
}

void TestNonOrthonormalIsDetected()
{
    auto scaled = MakeCamera();
    Set(scaled, 0, 2.0f); // scale one axis: no longer a rotation
    Require(!RotationIsOrthonormal(scaled), "a scaled basis is rejected");

    auto mirrored = MakeCamera();
    Set(mirrored, 10, -1.0f); // flip handedness
    Require(!RotationIsOrthonormal(mirrored), "a mirrored basis is rejected");

    // A finding worth pinning rather than papering over: the engine's own check
    // **passes** an all-zero matrix, because each of its nine comparisons
    // reduces to |0 - 0|. That is why orthonormality alone is not the gate.
    std::vector<std::uint8_t> zeroed(kCameraSize, 0);
    Require(RotationIsOrthonormal(zeroed),
        "the engine predicate accepts an all-zero matrix -- this is its hole, not ours");
    Require(RotationIsDegenerate(zeroed), "the degeneracy check catches an all-zero matrix");
    Require(!RotationIsSafeToWrite(zeroed), "an all-zero matrix is not safe to write");

    auto camera = MakeCamera();
    Require(!RotationIsDegenerate(camera), "the identity is not degenerate");
    Require(RotationIsSafeToWrite(camera), "the identity is safe to write");
    Require(ApplyYaw(camera, {12.0f}), "the yaw applies");
    Require(RotationIsSafeToWrite(camera), "a yawed identity is still safe to write");

    auto nonFinite = MakeCamera();
    Set(nonFinite, 5, std::nanf(""));
    Require(RotationIsDegenerate(nonFinite), "a non-finite basis is degenerate");
    Require(!RotationIsSafeToWrite(nonFinite), "a non-finite basis is not safe to write");
}

void TestRestoreIsByteExact()
{
    auto camera = MakeCamera();
    RestorePoint point{};
    Require(Capture(camera, point), "a full camera is captured");
    Require(point.captured, "the restore point records that it captured");
    Require(MatchesRestorePoint(camera, point), "an untouched camera still matches");

    Require(ApplyYaw(camera, {10.0f}), "the yaw applies");
    Require(!MatchesRestorePoint(camera, point), "an edited camera no longer matches");

    std::memcpy(camera.data(), point.bytes.data(), kCameraSize);
    Require(MatchesRestorePoint(camera, point), "restoring the bytes matches again");

    // A single flipped byte anywhere must fail: a partial restore is the failure
    // mode that would poison every later measurement in the same session.
    camera[0x1FF] ^= 0x01;
    Require(!MatchesRestorePoint(camera, point),
        "one differing byte late in the structure fails the restore check");

    RestorePoint empty{};
    Require(!MatchesRestorePoint(camera, empty),
        "an uncaptured restore point never reports a match");
}

} // namespace

int main()
{
    TestBounds();
    TestYawRotatesTheBasisOnly();
    TestYawIsRejectedOutOfBounds();
    TestRotationStaysOrthonormal();
    TestNonOrthonormalIsDetected();
    TestRestoreIsByteExact();
    std::cout << "PreyVR camera edit tests passed\n";
    return 0;
}
