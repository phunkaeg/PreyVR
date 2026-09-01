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

// The property A2b rests on: with the asymmetry dialled out, the two eyes get
// the same frustum, so a translation is the only thing left that can differ
// between the images.
//
// This exists because the first two A2 runs were unjudgeable (F-011). A 10-degree
// frustum asymmetry moved every pixel 462 px sideways and buried the 64 mm eye
// separation the run was meant to measure. The fix is to be able to switch the
// asymmetry off -- which is worth nothing unless "off" really is off.
void TestSymmetricScaleMakesBothEyesIdentical()
{
    const EyeView left = SyntheticEyeView(0, 50.0f, 1.0f);
    const EyeView right = SyntheticEyeView(1, 50.0f, 1.0f);

    Require(Near(left.tanLeft, right.tanLeft), "at scale 1.0 the left edges match");
    Require(Near(left.tanRight, right.tanRight), "at scale 1.0 the right edges match");
    Require(Near(left.tanUp, right.tanUp), "the vertical extent never depends on the eye");
    Require(Near(left.tanDown, right.tanDown), "the vertical extent never depends on the eye");
    Require(Near(left.tanRight, -left.tanLeft), "a scale of 1.0 is genuinely symmetric");

    // The tangents are only an intermediate. What reaches the engine is the
    // projection, so that is what has to be identical -- a difference could
    // otherwise be reintroduced by the conversion.
    const auto leftProjection = ProjectionFromTangents(left, 0.1f);
    const auto rightProjection = ProjectionFromTangents(right, 0.1f);
    Require(leftProjection.has_value() && rightProjection.has_value(),
        "a symmetric synthetic frustum converts");
    Require(Near(leftProjection->fov, rightProjection->fov), "identical vertical FOV");
    Require(Near(leftProjection->projectionRatio, rightProjection->projectionRatio),
        "identical projection ratio");
    Require(Near(leftProjection->asymmetry.left, 0.0f) &&
            Near(leftProjection->asymmetry.right, 0.0f),
        "no horizontal shift survives at scale 1.0, so nothing shears the image");
    Require(Near(rightProjection->asymmetry.left, 0.0f) &&
            Near(rightProjection->asymmetry.right, 0.0f),
        "and the same for the other eye");
}

void TestAsymmetricScaleMirrorsTheEyes()
{
    // The default. Checked against the angles directly rather than against
    // whatever the function returns, so a change to the formula has to be
    // deliberate.
    const float outer = std::tan(55.0f * 3.14159265358979323846f / 180.0f);
    const float inner = std::tan(45.0f * 3.14159265358979323846f / 180.0f);

    const EyeView left = SyntheticEyeView(0, 50.0f, 1.1f);
    const EyeView right = SyntheticEyeView(1, 50.0f, 1.1f);

    Require(Near(left.tanLeft, -outer), "the left eye extends outward to the left");
    Require(Near(left.tanRight, inner), "and is narrower on the inside");
    Require(Near(right.tanLeft, -inner), "the right eye is the mirror image");
    Require(Near(right.tanRight, outer), "the right eye is the mirror image");
    Require(Near(left.tanUp, right.tanUp), "the asymmetry is horizontal only");

    // The 462 px measured live came from exactly this gap between the eyes'
    // frustum centres, so it is worth stating in the test rather than leaving
    // it to a document.
    const float leftCentre = (left.tanLeft + left.tanRight) * 0.5f;
    const float rightCentre = (right.tanLeft + right.tanRight) * 0.5f;
    Require(Near(rightCentre - leftCentre, outer - inner),
        "the eyes' frustum centres differ by tan(outer) - tan(inner), which is the shear");
    Require(rightCentre > leftCentre, "and the right eye's frustum sits to the right");
}

// ---------------------------------------------------------------------------
// Declaring the frustum we actually rendered
// ---------------------------------------------------------------------------

// Builds a CCamera block with the four fields TangentsFromCamera reads.
std::vector<std::uint8_t> MakeCameraBlock(
    float fov, float projectionRatio, float nearPlane,
    float asymL, float asymR, float asymB, float asymT)
{
    std::vector<std::uint8_t> camera(preyvr::engine::CameraLayout::size, 0);
    const auto put = [&camera](std::size_t offset, float value) {
        std::memcpy(camera.data() + offset, &value, sizeof(value));
    };
    put(preyvr::engine::CameraLayout::fov, fov);
    put(preyvr::engine::CameraLayout::projectionRatio, projectionRatio);
    put(preyvr::engine::CameraLayout::edgeNearLeftTop + 4, nearPlane);  // GetNearPlane is .y
    put(preyvr::engine::CameraLayout::asymLeft, asymL);
    put(preyvr::engine::CameraLayout::asymRight, asymR);
    put(preyvr::engine::CameraLayout::asymBottom, asymB);
    put(preyvr::engine::CameraLayout::asymTop, asymT);
    return camera;
}

void TestSymmetricCameraGivesSymmetricTangents()
{
    // A synthetic 55-degree vertical camera. The stored fov is VERTICAL, so the
    // expected values are computed from the engine's own construction rather
    // than from the function under test.
    //
    // (This comment used to describe these numbers as Prey's live camera at "88
    // degrees horizontal". They are not Prey's, and 88 is not horizontal -- see
    // TestLiveMeasuredCameraDeclaresTheSliderValue for the real ones.)
    const float fov = 0.9599311f;            // ~55 degrees vertical
    const float ratio = 1.7777778f;          // 16:9
    const auto camera = MakeCameraBlock(fov, ratio, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f);

    const auto view = TangentsFromCamera(camera);
    Require(view.has_value(), "a plain symmetric camera converts");

    const float expectedV = std::tan(fov * 0.5f);
    const float expectedH = expectedV * ratio;
    Require(Near(view->tanUp, expectedV), "vertical half-extent is tan(fov/2)");
    Require(Near(view->tanDown, -expectedV), "and symmetric below");
    Require(Near(view->tanRight, expectedH), "horizontal is the vertical times the projection ratio");
    Require(Near(view->tanLeft, -expectedH), "and symmetric to the left");
}

void TestAsymmetryIsANearPlaneEdgeOffset()
{
    // **The semantics CryEngine's own source supplies**: the asymmetry fields are
    // frustum-edge offsets in NEAR-PLANE UNITS added to the computed edges, not
    // angles. So a shift of `asym` moves the tangent by exactly asym/nearPlane --
    // and that is what makes this test independent of our own recomputation, which
    // the previous asymmetry test was not.
    const float fov = 0.9599311f;
    const float ratio = 1.7777778f;
    const float nearPlane = 0.25f;
    const float shift = 0.05f;               // near-plane units

    const auto plain = MakeCameraBlock(fov, ratio, nearPlane, 0, 0, 0, 0);
    const auto shifted = MakeCameraBlock(fov, ratio, nearPlane, 0, shift, 0, 0);

    const auto a = TangentsFromCamera(plain);
    const auto b = TangentsFromCamera(shifted);
    Require(a.has_value() && b.has_value(), "both cameras convert");
    Require(Near(b->tanRight - a->tanRight, shift / nearPlane),
        "a right-edge offset moves the tangent by offset/nearPlane, not by an angle");
    Require(Near(b->tanLeft, a->tanLeft), "and leaves the opposite edge alone");
    Require(Near(b->tanUp, a->tanUp), "and does not touch the vertical");
}

void TestNearPlaneScalesTheAsymmetry()
{
    // The same stored offset means a DIFFERENT angle at a different near plane.
    // Getting this backwards is the tangent-space trap F-011 recorded, and it is
    // silent: both answers look plausible.
    const float fov = 0.9599311f, ratio = 1.7777778f, shift = 0.05f;
    const auto shallow = TangentsFromCamera(MakeCameraBlock(fov, ratio, 0.1f, 0, shift, 0, 0));
    const auto deep = TangentsFromCamera(MakeCameraBlock(fov, ratio, 0.5f, 0, shift, 0, 0));
    Require(shallow.has_value() && deep.has_value(), "both convert");
    const auto base = TangentsFromCamera(MakeCameraBlock(fov, ratio, 1.0f, 0, 0, 0, 0));
    Require(base.has_value(), "baseline converts");
    Require(shallow->tanRight > deep->tanRight,
        "the same edge offset is a larger tangent shift at a nearer plane");
}

void TestRoundTripThroughTheEngineFormula()
{
    // Tangents -> the engine's camera fields -> back to tangents. This crosses
    // both directions rather than recomputing one, so it catches a sign or ratio
    // error that a single-direction check would agree with.
    EyeView original{};
    original.tanLeft = -1.4281480f;   // tan(55 degrees)
    original.tanRight = 1.0f;         // tan(45 degrees)
    original.tanDown = -1.1917536f;   // tan(50 degrees)
    original.tanUp = 1.1917536f;

    const float nearPlane = 0.1f;
    const auto projection = ProjectionFromTangents(original, nearPlane);
    Require(projection.has_value(), "the asymmetric frustum converts to engine fields");

    auto camera = MakeCameraBlock(projection->fov, projection->projectionRatio, nearPlane,
                                  projection->asymmetry.left, projection->asymmetry.right,
                                  projection->asymmetry.bottom, projection->asymmetry.top);
    const auto recovered = TangentsFromCamera(camera);
    Require(recovered.has_value(), "and back again");
    Require(Near(recovered->tanLeft, original.tanLeft, 1e-3f), "left edge survives the round trip");
    Require(Near(recovered->tanRight, original.tanRight, 1e-3f), "right edge survives");
    Require(Near(recovered->tanDown, original.tanDown, 1e-3f), "bottom edge survives");
    Require(Near(recovered->tanUp, original.tanUp, 1e-3f), "top edge survives");
}

void TestCameraConversionFailsClosed()
{
    // Submitting a frustum derived from an unreadable camera is worse than
    // dropping the frame: it is wrong in a way the runtime cannot detect.
    Require(!TangentsFromCamera(MakeCameraBlock(0.96f, 1.78f, 0.0f, 0, 0, 0, 0)).has_value(),
        "a zero near plane is refused rather than dividing by it");
    Require(!TangentsFromCamera(MakeCameraBlock(0.0f, 1.78f, 0.1f, 0, 0, 0, 0)).has_value(),
        "a zero FOV is not a frustum");
    Require(!TangentsFromCamera(MakeCameraBlock(0.96f, 0.0f, 0.1f, 0, 0, 0, 0)).has_value(),
        "a zero projection ratio is not a frustum");
    const std::vector<std::uint8_t> tooSmall(16, 0);
    Require(!TangentsFromCamera(tooSmall).has_value(), "a short buffer is refused");
    // An asymmetry large enough to cross the edges over.
    Require(!TangentsFromCamera(MakeCameraBlock(0.96f, 1.78f, 0.1f, 0, -1.0f, 0, 0)).has_value(),
        "a frustum whose edges have crossed is refused");
}

// The live camera, measured 2026-09-02 with the in-game FOV slider at 120.
//
// **This is the one test here whose numbers are ground truth rather than
// construction.** Every other case builds a camera from chosen fields and checks
// the maths round-trips, which cannot catch a wrong convention -- if we read the
// vertical fov as horizontal, a self-consistent test still passes. These fields
// were read out of Prey's live CSystem::m_ViewCamera, and the horizontal angle
// they produce is checked against a number the engine never told us: what the
// player's settings menu says.
//
// That is what makes it a real check. 120.000 falling out of fields that never
// contain 120 is only possible if the convention is right.
void TestLiveMeasuredCameraDeclaresTheSliderValue()
{
    const float fov = 1.5447415f;    // 88.507 degrees, VERTICAL
    const float ratio = 1.7777778f;  // 2560x1440
    const auto camera = MakeCameraBlock(fov, ratio, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f);

    const auto view = TangentsFromCamera(camera);
    Require(view.has_value(), "the live camera converts");

    // tan(60 degrees) is the square root of 3, which is what a 120-degree
    // horizontal field gives. Checked as a tangent because that is what the
    // submission path carries.
    Require(Near(view->tanRight, 1.7320508f, 1e-4f), "the right edge is tan(60)");
    Require(Near(view->tanLeft, -1.7320508f, 1e-4f), "the left edge mirrors it");
    Require(Near(view->tanUp, 0.9742790f, 1e-4f), "the top edge matches the live read");
    Require(Near(view->tanDown, -0.9742790f, 1e-4f), "the bottom edge mirrors it");

    const auto angles = AnglesFromTangents(*view);
    const float toDegrees = 57.29577951f;
    Require(Near((angles.angleRight - angles.angleLeft) * toDegrees, 120.0f, 1e-2f),
        "the declared horizontal field is the 120 the player's slider reads");
    Require(Near((angles.angleUp - angles.angleDown) * toDegrees, 88.507f, 1e-2f),
        "and the vertical is the 88.5 the engine stores");
}

// Rung 3's load-bearing invariant: the frustum does not depend on where the
// camera is.
//
// Rung 3 builds each eye by translating the game's camera and leaving its
// projection alone, then declares one frustum for both eyes. That is only honest
// if moving the camera cannot change the frustum it declares -- otherwise the two
// eyes would need two declarations and sharing one would be a lie about the
// right eye.
//
// It holds because TangentsFromCamera reads only projection fields and never the
// pose. Cheap to pin, and expensive to discover broken from inside a headset,
// which is the only place the symptom would show.
void TestFrustumIsIndependentOfCameraPose()
{
    auto atOrigin = MakeCameraBlock(1.5447415f, 1.7777778f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f);
    auto translated = atOrigin;
    // Half a 64 mm IPD along the camera's right axis, plus a large world offset,
    // written into the matrix' translation columns.
    const stereo::Matrix34 moved = stereo::MatrixFromPose(
        Pose{Quaternion{0.0f, 0.0f, 0.0f, 1.0f}, Vec3{0.032f, 1200.0f, -450.0f}});
    Require(stereo::WriteMatrix(translated, moved), "the moved camera is writable");

    const auto a = TangentsFromCamera(atOrigin);
    const auto b = TangentsFromCamera(translated);
    Require(a.has_value() && b.has_value(), "both poses convert");
    Require(a->tanLeft == b->tanLeft && a->tanRight == b->tanRight &&
            a->tanDown == b->tanDown && a->tanUp == b->tanUp,
        "translating the camera leaves the declared frustum bit-identical");
}

void TestAnglesAreArctangents()
{
    EyeView view{};
    view.tanLeft = -1.0f;
    view.tanRight = 1.0f;
    view.tanDown = -0.5f;
    view.tanUp = 2.0f;
    const auto angles = AnglesFromTangents(view);
    Require(Near(angles.angleLeft, -0.7853982f), "atan(-1) is -45 degrees");
    Require(Near(angles.angleRight, 0.7853982f), "atan(1) is 45 degrees");
    Require(Near(angles.angleDown, std::atan(-0.5f)), "down is the arctangent of its tangent");
    Require(Near(angles.angleUp, std::atan(2.0f)), "up likewise");
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
    TestSymmetricCameraGivesSymmetricTangents();
    TestAsymmetryIsANearPlaneEdgeOffset();
    TestNearPlaneScalesTheAsymmetry();
    TestRoundTripThroughTheEngineFormula();
    TestCameraConversionFailsClosed();
    TestLiveMeasuredCameraDeclaresTheSliderValue();
    TestFrustumIsIndependentOfCameraPose();
    TestAnglesAreArctangents();
    TestSymmetricScaleMakesBothEyesIdentical();
    TestAsymmetricScaleMirrorsTheEyes();
    std::cout << "PreyVR stereo frame tests passed\n";
    return 0;
}
