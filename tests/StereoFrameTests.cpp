#include "preyvr/EngineMap.h"
#include "preyvr/RuntimeSnapshot.h"
#include "preyvr/StereoFrame.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using namespace preyvr;
using namespace preyvr::stereoframe;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool Near(float a, float b, float epsilon = 1e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

void SetFloat(std::vector<std::uint8_t>& camera, std::size_t offset, float value)
{
    std::memcpy(camera.data() + offset, &value, sizeof(float));
}

float GetFloat(const std::array<std::uint8_t, kCameraSize>& camera, std::size_t offset)
{
    float value = 0.0f;
    std::memcpy(&value, camera.data() + offset, sizeof(float));
    return value;
}

// A camera matching the values actually captured from Prey on 2026-08-29, so the
// test is exercising realistic magnitudes rather than round numbers.
std::vector<std::uint8_t> MakeLiveLikeCamera()
{
    std::vector<std::uint8_t> camera(kCameraSize, 0);
    SetFloat(camera, 0, 1.0f);
    SetFloat(camera, 5 * sizeof(float), 1.0f);
    SetFloat(camera, 10 * sizeof(float), 1.0f);
    SetFloat(camera, 3 * sizeof(float), 325.847f);
    SetFloat(camera, 7 * sizeof(float), 740.602f);
    SetFloat(camera, 11 * sizeof(float), 482.790f);
    SetFloat(camera, engine::CameraLayout::fov, 1.5447413f);
    SetFloat(camera, engine::CameraLayout::projectionRatio, 1.7777778f);
    SetFloat(camera, engine::CameraLayout::edgeNearLeftTop + sizeof(float), 0.1f);
    SetFloat(camera, engine::CameraLayout::edgeFarLeftTop + sizeof(float), 8000.0f);
    // A marker in the cached region: the plan must inherit it untouched.
    camera[0x1F0] = 0x5A;
    return camera;
}

// Typical asymmetric headset frustum: the outer edge extends further than the
// inner one, and the eyes mirror each other.
EyeView MakeEye(bool isLeft)
{
    EyeView view{};
    view.tanDown = -1.10f;
    view.tanUp = 1.10f;
    view.tanLeft = isLeft ? -1.20f : -0.95f;
    view.tanRight = isLeft ? 0.95f : 1.20f;
    view.openXrPose.position = Vec3{isLeft ? -0.032f : 0.032f, 0.0f, 0.0f};
    return view;
}

void TestProjectionRoundTripsThroughTheEngineFormula()
{
    // The real acceptance test: solve for the shifts, then push them back through
    // the engine's own SetCamera formula and check the frustum that comes out is
    // the one OpenXR asked for. That closes the loop rather than trusting the
    // inverse in isolation.
    const EyeView view = MakeEye(true);
    const float nearPlane = 0.1f;
    const auto projection = ProjectionFromTangents(view, nearPlane);
    Require(projection.has_value(), "a sane frustum yields a projection");

    const float t = std::tan(projection->fov * 0.5f) * nearPlane;
    const float ratio = projection->projectionRatio;

    const float fWL = projection->asymmetry.left - t * ratio;
    const float fWR = t * ratio + projection->asymmetry.right;
    const float fWB = projection->asymmetry.bottom - t;
    const float fWT = t + projection->asymmetry.top;

    Require(Near(fWL, view.tanLeft * nearPlane, 1e-6f), "the left frustum edge reproduces");
    Require(Near(fWR, view.tanRight * nearPlane, 1e-6f), "the right frustum edge reproduces");
    Require(Near(fWB, view.tanDown * nearPlane, 1e-6f), "the bottom frustum edge reproduces");
    Require(Near(fWT, view.tanUp * nearPlane, 1e-6f), "the top frustum edge reproduces");
}

void TestSymmetricFrustumProducesZeroShifts()
{
    // A sign error survives every other check while quietly symmetrising the
    // eye, so this is asserted explicitly.
    EyeView view{};
    view.tanLeft = -1.0f;
    view.tanRight = 1.0f;
    view.tanDown = -0.6f;
    view.tanUp = 0.6f;

    const auto projection = ProjectionFromTangents(view, 0.1f);
    Require(projection.has_value(), "a symmetric frustum yields a projection");
    Require(Near(projection->asymmetry.left, 0.0f, 1e-6f), "a symmetric frustum has no left shift");
    Require(Near(projection->asymmetry.right, 0.0f, 1e-6f), "no right shift");
    Require(Near(projection->asymmetry.bottom, 0.0f, 1e-6f), "no bottom shift");
    Require(Near(projection->asymmetry.top, 0.0f, 1e-6f), "no top shift");
}

void TestProjectionRejectsBadFrusta()
{
    EyeView inverted = MakeEye(true);
    std::swap(inverted.tanLeft, inverted.tanRight);
    Require(!ProjectionFromTangents(inverted, 0.1f), "an inside-out frustum is refused");

    EyeView flat = MakeEye(true);
    flat.tanUp = flat.tanDown;
    Require(!ProjectionFromTangents(flat, 0.1f), "a zero-height frustum is refused");

    EyeView nonFinite = MakeEye(true);
    nonFinite.tanUp = std::nanf("");
    Require(!ProjectionFromTangents(nonFinite, 0.1f), "a non-finite tangent is refused");

    EyeView absurd = MakeEye(true);
    absurd.tanRight = 50.0f;
    Require(!ProjectionFromTangents(absurd, 0.1f), "an absurd half-angle is refused");

    Require(!ProjectionFromTangents(MakeEye(true), 0.0f), "a zero near plane is refused");
    Require(!ProjectionFromTangents(MakeEye(true), -1.0f), "a negative near plane is refused");
}

void TestStereoPlanBuildsBothEyes()
{
    const auto base = MakeLiveLikeCamera();
    stereo::ReferenceFrame reference{};
    reference.worldPosition = Vec3{325.847f, 740.602f, 482.790f};

    const auto plan = BuildStereoPlan(base, reference, MakeEye(true), MakeEye(false));
    Require(plan.has_value(), "a valid pair of eyes produces a plan");

    // The eyes must differ from each other, or nothing is stereo.
    Require(std::memcmp(plan->left.bytes.data(), plan->right.bytes.data(), kCameraSize) != 0,
        "the two eye cameras differ");

    // The IPD shows up as a lateral separation of the two camera positions.
    const float leftX = GetFloat(plan->left.bytes, 3 * sizeof(float));
    const float rightX = GetFloat(plan->right.bytes, 3 * sizeof(float));
    Require(Near(rightX - leftX, 0.064f, 1e-4f), "the eye separation is the IPD");

    // The cyclops point sits between them, which is what gameplay gets.
    Require(Near(plan->cyclops.position.x, reference.worldPosition.x, 1e-4f),
        "the gameplay eye point is centred between the two eyes");

    // Everything not view- or projection-related is inherited untouched.
    Require(plan->left.bytes[0x1F0] == 0x5A, "the left eye inherits the cached region");
    Require(plan->right.bytes[0x1F0] == 0x5A, "the right eye inherits the cached region");
    Require(Near(GetFloat(plan->left.bytes, engine::CameraLayout::edgeFarLeftTop + sizeof(float)),
                 8000.0f),
        "the far plane is inherited");

    // The projection fields were actually written.
    Require(Near(GetFloat(plan->left.bytes, engine::CameraLayout::fov), plan->leftProjection.fov),
        "the left eye's FOV is written into the camera");
    Require(Near(GetFloat(plan->left.bytes, engine::CameraLayout::asymLeft),
                 plan->leftProjection.asymmetry.left),
        "the left eye's asymmetry shift is written into the camera");

    // The two eyes have mirrored asymmetry, which is what an HMD reports.
    Require(Near(plan->leftProjection.asymmetry.left, -plan->rightProjection.asymmetry.right, 1e-6f),
        "the eyes' asymmetry mirrors");
}

void TestStereoPlanIsAllOrNothing()
{
    const auto base = MakeLiveLikeCamera();
    EyeView broken = MakeEye(false);
    broken.tanUp = std::nanf("");

    Require(!BuildStereoPlan(base, stereo::ReferenceFrame{}, MakeEye(true), broken),
        "one bad eye refuses the whole plan rather than returning a half-stereo frame");

    std::vector<std::uint8_t> tooSmall(16, 0);
    Require(!BuildStereoPlan(tooSmall, stereo::ReferenceFrame{}, MakeEye(true), MakeEye(false)),
        "a short base camera is refused");
}

void TestNearPlaneIsReadFromTheLiveCamera()
{
    const auto base = MakeLiveLikeCamera();
    Require(Near(NearPlaneOf(base), 0.1f), "the near plane comes from the camera, not a constant");

    std::vector<std::uint8_t> tooSmall(4, 0);
    Require(Near(NearPlaneOf(tooSmall), 0.0f), "a short buffer reports zero rather than reading past");
}

} // namespace

int main()
{
    TestProjectionRoundTripsThroughTheEngineFormula();
    TestSymmetricFrustumProducesZeroShifts();
    TestProjectionRejectsBadFrusta();
    TestStereoPlanBuildsBothEyes();
    TestStereoPlanIsAllOrNothing();
    TestNearPlaneIsReadFromTheLiveCamera();
    std::cout << "PreyVR stereo frame tests passed\n";
    return 0;
}
