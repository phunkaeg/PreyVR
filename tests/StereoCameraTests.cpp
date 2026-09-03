#include "preyvr/CameraEdit.h"
#include "preyvr/StereoCamera.h"

#include <cmath>
#include <limits>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using namespace preyvr;
using namespace preyvr::stereo;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool Near(float a, float b, float epsilon = 1e-5f)
{
    return std::fabs(a - b) <= epsilon;
}

bool NearVec(Vec3 a, Vec3 b, float epsilon = 1e-5f)
{
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
}

Quaternion AxisAngle(Vec3 axis, float radians)
{
    const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    const float half = radians * 0.5f;
    const float s = std::sin(half) / length;
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
}

void TestAxisConversion()
{
    // The three that define the basis change. If any of these is wrong the world
    // renders on its side or back to front, so they are asserted individually
    // rather than via a matrix identity.
    Require(NearVec(ToEngineSpace(Vec3{1.0f, 0.0f, 0.0f}), Vec3{1.0f, 0.0f, 0.0f}),
        "OpenXR right maps to engine right");
    Require(NearVec(ToEngineSpace(Vec3{0.0f, 1.0f, 0.0f}), Vec3{0.0f, 0.0f, 1.0f}),
        "OpenXR up (Y) maps to engine up (Z)");
    Require(NearVec(ToEngineSpace(Vec3{0.0f, 0.0f, -1.0f}), Vec3{0.0f, 1.0f, 0.0f}),
        "OpenXR forward (-Z) maps to engine forward (+Y)");
}

void TestConversionIsAProperRotation()
{
    // A mirror would invert every cross product downstream, and UpdateFrustum
    // reacts to a non-proper basis by negating all six plane normals. Checking
    // that the converted axes remain right-handed catches that.
    const Vec3 x = ToEngineSpace(Vec3{1.0f, 0.0f, 0.0f});
    const Vec3 y = ToEngineSpace(Vec3{0.0f, 1.0f, 0.0f});
    const Vec3 z = ToEngineSpace(Vec3{0.0f, 0.0f, 1.0f});
    const Vec3 cross{
        x.y * y.z - x.z * y.y,
        x.z * y.x - x.x * y.z,
        x.x * y.y - x.y * y.x,
    };
    Require(NearVec(cross, z), "the basis change preserves handedness");
}

void TestRotationConversionInvariant()
{
    // The defining property of a conjugation basis change: converting a rotated
    // vector must equal rotating the converted vector by the converted rotation.
    //
    // This is the test that catches the tempting mistake of rotating the
    // quaternion's axis instead of conjugating -- that alternative agrees on
    // simple cases and diverges once the head tilts.
    const Quaternion q = AxisAngle({0.3f, -0.7f, 0.5f}, 0.9f);
    const Vec3 v{1.5f, -2.25f, 0.75f};

    const Vec3 viaConversion = ToEngineSpace(Rotate(q, v));
    const Vec3 viaEngine = Rotate(ToEngineSpace(q), ToEngineSpace(v));
    Require(NearVec(viaConversion, viaEngine),
        "converting a rotated vector equals rotating the converted vector");
}

void TestAxisAlignedRotationsMapAsExpected()
{
    // A pitch about OpenXR +X should stay a pitch about engine +X, because the
    // basis change is itself a rotation about X and rotations about a shared
    // axis commute.
    const Quaternion pitch = AxisAngle({1.0f, 0.0f, 0.0f}, 0.4f);
    const Quaternion converted = ToEngineSpace(pitch);
    Require(Near(converted.x, pitch.x) && Near(converted.y, 0.0f) &&
            Near(converted.z, 0.0f) && Near(converted.w, pitch.w),
        "an OpenXR pitch stays a pitch about the same axis");

    // A yaw about OpenXR +Y (its up) must become a yaw about engine +Z (its up).
    const Quaternion yaw = AxisAngle({0.0f, 1.0f, 0.0f}, 0.4f);
    const Quaternion convertedYaw = ToEngineSpace(yaw);
    Require(Near(convertedYaw.x, 0.0f) && Near(convertedYaw.y, 0.0f) &&
            Near(convertedYaw.z, yaw.y) && Near(convertedYaw.w, yaw.w),
        "an OpenXR yaw about Y becomes an engine yaw about Z");

    Require(Near(ToEngineSpace(Quaternion{}).w, 1.0f), "identity converts to identity");
}

void TestMatrixFromPose()
{
    const Pose pose{Quaternion{}, Vec3{325.847f, 740.602f, 482.790f}};
    const Matrix34 matrix = MatrixFromPose(pose);

    Require(NearVec(RightOf(matrix), Vec3{1.0f, 0.0f, 0.0f}), "identity right is +X");
    Require(NearVec(ForwardOf(matrix), Vec3{0.0f, 1.0f, 0.0f}), "identity forward is +Y");
    Require(NearVec(UpOf(matrix), Vec3{0.0f, 0.0f, 1.0f}), "identity up is +Z");
    Require(NearVec(PositionOf(matrix), pose.position), "the position lands in column 3");

    // Column 3 is m[3], m[7], m[11] -- the layout CCamera::UpdateFrustum walks.
    Require(Near(matrix[3], 325.847f) && Near(matrix[7], 740.602f) && Near(matrix[11], 482.790f),
        "the translation occupies column 3 of a row-major 3x4");
}

void TestBuiltMatrixIsSafeToWrite()
{
    // Cross-checks this module against the engine's own predicate: a camera
    // matrix we construct must satisfy the check that decides whether
    // UpdateFrustum negates the frustum planes. A handedness error here would
    // show up as an inverted image rather than a crash, so it is worth catching
    // in a unit test rather than in a headset.
    for (const float angle : {0.0f, 0.3f, 1.1f, -2.0f, 3.0f}) {
        const Pose pose{AxisAngle({0.26f, 0.53f, -0.81f}, angle), Vec3{1.0f, 2.0f, 3.0f}};
        const Matrix34 matrix = MatrixFromPose(pose);

        std::vector<std::uint8_t> camera(cameraedit::kCameraSize, 0);
        Require(WriteMatrix(camera, matrix), "the matrix writes into a camera blob");
        Require(cameraedit::RotationIsSafeToWrite(camera),
            "a pose-built matrix passes the engine's orthonormality gate");
    }
}

void TestPoseMatrixRoundTrip()
{
    // The inverse must actually invert, including at the orientations where the
    // naive quaternion-from-matrix form loses precision: near 180 degrees the
    // trace approaches zero and the single-branch version degrades badly.
    const float angles[] = {0.0f, 0.5f, 1.5f, 3.0f, 3.1415f, -2.7f};
    const Vec3 axes[] = {
        {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {0.577f, 0.577f, 0.577f}, {-0.26f, 0.53f, -0.81f},
    };

    for (const Vec3& axis : axes) {
        for (const float angle : angles) {
            const Pose original{AxisAngle(axis, angle), Vec3{12.0f, -34.0f, 56.0f}};
            const Matrix34 matrix = MatrixFromPose(original);
            const Pose recovered = PoseFromMatrix(matrix);

            Require(NearVec(recovered.position, original.position, 1e-3f),
                "the position round-trips through the matrix");

            // Compare the rotations by what they do, not by their components: q
            // and -q are the same rotation, so comparing components directly
            // would report a spurious failure at half the orientations.
            const Vec3 probe{0.3f, -0.6f, 0.74f};
            Require(NearVec(Rotate(original.orientation, probe),
                            Rotate(recovered.orientation, probe), 1e-3f),
                "the rotation round-trips through the matrix");
        }
    }
}

void TestLocalFrameOffset()
{
    // Half the IPD along the camera's own right axis is exactly a synthetic eye,
    // and it must follow the camera's orientation rather than the world axes.
    const Pose facingY{Quaternion{}, Vec3{100.0f, 200.0f, 300.0f}};
    const Pose right = OffsetInLocalFrame(facingY, Vec3{0.032f, 0.0f, 0.0f});
    Require(NearVec(right.position, Vec3{100.032f, 200.0f, 300.0f}, 1e-4f),
        "a local right offset moves along world X when the camera faces +Y");

    // Turn the camera 90 degrees about up: its right axis now points along -X.
    const Pose facingX{AxisAngle({0.0f, 0.0f, 1.0f}, -1.57079632679f), Vec3{0.0f, 0.0f, 0.0f}};
    const Pose turned = OffsetInLocalFrame(facingX, Vec3{0.032f, 0.0f, 0.0f});
    Require(NearVec(turned.position, Vec3{0.0f, -0.032f, 0.0f}, 1e-4f),
        "the local offset follows the camera's orientation, not the world axes");
}

void TestEyePoseComposition()
{
    // With an identity reference the eye pose is just the converted pose.
    const Pose openXrEye{Quaternion{}, Vec3{0.032f, 0.0f, 0.0f}}; // half IPD to the right
    const Pose plain = EyePoseInWorld(ReferenceFrame{}, openXrEye);
    Require(NearVec(plain.position, Vec3{0.032f, 0.0f, 0.0f}),
        "an identity reference leaves the converted eye offset alone");

    // Translating the reference must translate the eye, not orbit it. Getting
    // the composition order backwards makes the camera swing around the world
    // origin as the player walks.
    ReferenceFrame moved{};
    moved.worldPosition = Vec3{100.0f, 200.0f, 300.0f};
    const Pose translated = EyePoseInWorld(moved, openXrEye);
    Require(NearVec(translated.position, Vec3{100.032f, 200.0f, 300.0f}, 1e-3f),
        "the reference position translates the eye rather than orbiting it");

    // A 90-degree reference yaw must swing a rightward eye offset onto +Y.
    ReferenceFrame turned{};
    turned.yawRadians = 1.57079632679f;
    const Pose yawed = EyePoseInWorld(turned, openXrEye);
    Require(NearVec(yawed.position, Vec3{0.0f, 0.032f, 0.0f}, 1e-4f),
        "a 90-degree reference yaw rotates the eye offset into +Y");

    // And the two compose: yaw first about the reference origin, then translate.
    ReferenceFrame both{};
    both.yawRadians = 1.57079632679f;
    both.worldPosition = Vec3{10.0f, 20.0f, 30.0f};
    const Pose composed = EyePoseInWorld(both, openXrEye);
    Require(NearVec(composed.position, Vec3{10.0f, 20.032f, 30.0f}, 1e-4f),
        "yaw applies to the offset before the world translation is added");
}

void TestCyclopsPose()
{
    const Pose left{Quaternion{}, Vec3{-0.032f, 5.0f, 1.7f}};
    const Pose right{Quaternion{}, Vec3{0.032f, 5.0f, 1.7f}};
    const Pose centre = CyclopsPose(left, right);

    Require(NearVec(centre.position, Vec3{0.0f, 5.0f, 1.7f}),
        "the gameplay eye point is the midpoint of the two eyes");
    Require(Near(centre.orientation.w, 1.0f), "the orientation comes through unchanged");
}

void TestWriteMatrixIsMinimal()
{
    std::vector<std::uint8_t> camera(cameraedit::kCameraSize, 0xEE);
    const Matrix34 matrix = MatrixFromPose(Pose{Quaternion{}, Vec3{1.0f, 2.0f, 3.0f}});
    Require(WriteMatrix(camera, matrix), "the write succeeds");

    // Everything past the matrix must be untouched: fov, dimensions, near/far,
    // the asymmetry shifts and all the cached derived state belong to the engine.
    for (std::size_t i = 12 * sizeof(float); i < cameraedit::kCameraSize; ++i) {
        Require(camera[i] == 0xEE, "the write touches only the twelve matrix floats");
    }

    const Matrix34 read = ReadMatrix(camera);
    for (std::size_t i = 0; i < matrix.size(); ++i) {
        Require(Near(read[i], matrix[i]), "the matrix round-trips through the camera blob");
    }
}

void TestWriteMatrixRefusesNonFinite()
{
    std::vector<std::uint8_t> camera(cameraedit::kCameraSize, 0);
    Matrix34 matrix = MatrixFromPose(Pose{});
    matrix[5] = std::nanf("");
    Require(!WriteMatrix(camera, matrix), "a NaN matrix is refused");

    matrix[5] = INFINITY;
    Require(!WriteMatrix(camera, matrix), "an infinite matrix is refused");

    std::vector<std::uint8_t> tooSmall(8, 0);
    Require(!WriteMatrix(tooSmall, MatrixFromPose(Pose{})), "a short buffer is refused");
}

} // namespace

// ---------------------------------------------------------------------------
// Recenter: the yaw-only reference
// ---------------------------------------------------------------------------

Quaternion HeadAxisAngle(Vec3 axis, float radians)
{
    const float half = radians * 0.5f;
    const float sine = std::sin(half);
    return Normalize(Quaternion{axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half)});
}

// OpenXR is Y-up with -Z forward, so yaw turns about +Y, pitch about +X and
// roll about the forward axis.
Pose HeadPose(float yaw, float pitch, float roll)
{
    Quaternion q = HeadAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yaw);
    q = Multiply(q, HeadAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, pitch));
    q = Multiply(q, HeadAxisAngle(Vec3{0.0f, 0.0f, -1.0f}, roll));
    return Pose{Normalize(q), Vec3{}};
}

void TestRecenterYawTracksYaw()
{
    const float angles[] = {0.0f, 0.5f, 1.5f, -0.9f, 3.0f};
    for (const float yaw : angles) {
        const auto extracted = RecenterYawFromHeadPose(HeadPose(yaw, 0.0f, 0.0f));
        Require(extracted.has_value(), "a level head pose yields a yaw");
        Require(Near(*extracted, yaw, 1e-3f), "the extracted yaw is the yaw applied");
    }
}

// **FAIL-CAM-019.** A headset that was crooked when recenter was pressed -- on a
// desk, or sitting askew on the player's head -- must not bake that tilt into the
// reference, because a reference holding the whole head orientation applies it as
// a permanent offset to every later frame and the world stays tilted forever.
//
// The extraction is immune by construction rather than by a stripping step: roll
// turns about the forward axis and so cannot move it, and pitch moves it within
// the vertical plane that already contains it. Neither changes its horizontal
// direction. This test exists so that a later rewrite to Euler decomposition --
// which would leak both -- fails loudly instead of shipping.
void TestRecenterYawIgnoresRollAndPitch()
{
    const float yaw = 0.7f;
    const auto level = RecenterYawFromHeadPose(HeadPose(yaw, 0.0f, 0.0f));
    Require(level.has_value(), "the level reference extracts");

    const float rolls[] = {0.3f, -0.6f, 1.0f};
    for (const float roll : rolls) {
        const auto tilted = RecenterYawFromHeadPose(HeadPose(yaw, 0.0f, roll));
        Require(tilted.has_value(), "a rolled head pose still yields a yaw");
        Require(Near(*tilted, *level, 1e-3f), "roll does not leak into the stored yaw");
    }

    const float pitches[] = {0.4f, -0.8f, 1.1f};
    for (const float pitch : pitches) {
        const auto tilted = RecenterYawFromHeadPose(HeadPose(yaw, pitch, 0.0f));
        Require(tilted.has_value(), "a pitched head pose still yields a yaw");
        Require(Near(*tilted, *level, 1e-3f), "pitch does not leak into the stored yaw");
    }

    const auto both = RecenterYawFromHeadPose(HeadPose(yaw, 0.5f, -0.7f));
    Require(both.has_value() && Near(*both, *level, 1e-3f),
        "roll and pitch together still leave the yaw alone");
}

// The playbook's rule is reject, don't invent: looking straight up or down leaves
// almost no horizontal component, so any yaw returned would be noise amplified by
// atan2. A caller must keep the reference it has.
void TestNearVerticalHeadPoseIsRefused()
{
    const float straightUp = 1.5707963f;
    Require(!RecenterYawFromHeadPose(HeadPose(0.0f, straightUp, 0.0f)).has_value(),
        "looking straight up has no usable yaw");
    Require(!RecenterYawFromHeadPose(HeadPose(0.0f, -straightUp, 0.0f)).has_value(),
        "looking straight down has no usable yaw");
    // Just inside the limit still answers, so ordinary head poses are not refused.
    Require(RecenterYawFromHeadPose(HeadPose(0.0f, 1.4f, 0.0f)).has_value(),
        "a steep but usable pitch is still accepted");
}

// The acceptance test the two above are building toward: a reference captured
// from a crooked headset, then used with a level head, must produce a level
// world. If roll had leaked into the reference, the camera's right axis would be
// tilted out of horizontal by exactly that roll.
void TestCrookedRecenterDoesNotTiltTheWorld()
{
    const auto reference = MakeRecenterReference(
        HeadPose(0.6f, 0.2f, 0.5f), Vec3{10.0f, -4.0f, 2.0f});
    Require(reference.has_value(), "a crooked recenter still produces a reference");

    // A level head, looking straight ahead.
    const Pose level = HeadPose(0.0f, 0.0f, 0.0f);
    const Pose world = EyePoseInWorld(*reference, level);

    const Vec3 right = Rotate(world.orientation, Vec3{1.0f, 0.0f, 0.0f});
    Require(std::fabs(right.z) < 1e-3f,
        "the camera's right axis stays horizontal, so the horizon is level");

    const Vec3 up = Rotate(world.orientation, Vec3{0.0f, 0.0f, 1.0f});
    Require(up.z > 0.99f, "and up still points up");
}

void TestRecenterReferenceFailsClosed()
{
    const float notFinite = std::numeric_limits<float>::quiet_NaN();
    Require(!MakeRecenterReference(HeadPose(0.0f, 0.0f, 0.0f),
                                   Vec3{notFinite, 0.0f, 0.0f}).has_value(),
        "a non-finite world position is refused rather than written");
    Require(!MakeRecenterReference(HeadPose(0.0f, 1.5707963f, 0.0f),
                                   Vec3{0.0f, 0.0f, 0.0f}).has_value(),
        "an unusable head pose refuses the whole reference");
}

// The correction to the first head-tracking build, pinned.
//
// That version parked the reference at the recenter yaw, so the rendered camera
// pointed at recenterYaw + headRotation and the player's mouse-look never reached
// it. In game that looks like the body rotating in front of the view -- the body
// still turns with the mouse and the camera does not. The composition has to
// start from the engine's own facing.
void TestHeadAtRecenterReproducesTheGameCamera()
{
    // The engine facing several different ways, as a player turning would.
    const float gameYaws[] = {0.0f, 0.8f, -1.9f, 2.7f};
    const float recenterYaw = 0.35f;
    const Pose headAtRecenter = HeadPose(recenterYaw, 0.0f, 0.0f);

    for (const float gameYaw : gameYaws) {
        const Matrix34 gameCamera = MatrixFromPose(
            Pose{YawQuaternion(gameYaw), Vec3{5.0f, -2.0f, 1.5f}});

        const ReferenceFrame reference =
            ReferenceFrameForHeadTracking(gameCamera, recenterYaw);
        const Pose world = EyePoseInWorld(reference, headAtRecenter);

        Require(Near(CameraYawOf(MatrixFromPose(world)), gameYaw, 1e-3f),
            "a head at its recenter orientation reproduces the engine's own facing");
        Require(NearVec(world.position, Vec3{5.0f, -2.0f, 1.5f}),
            "and the engine keeps its own eye point");
    }
}

// Head rotation adds to the player's facing rather than replacing it: turn the
// head 30 degrees and the camera moves 30 degrees from wherever the mouse left it.
void TestHeadRotationAddsToTheGameCamera()
{
    const float recenterYaw = 0.0f;
    const float gameYaw = 1.2f;
    const float headTurn = 0.5236f;   // 30 degrees
    const Matrix34 gameCamera = MatrixFromPose(Pose{YawQuaternion(gameYaw), Vec3{}});

    const ReferenceFrame reference = ReferenceFrameForHeadTracking(gameCamera, recenterYaw);
    const Pose world = EyePoseInWorld(reference, HeadPose(headTurn, 0.0f, 0.0f));

    Require(Near(CameraYawOf(MatrixFromPose(world)), gameYaw + headTurn, 1e-3f),
        "the head turn composes onto the player's facing");
}

// The property the camera seam depends on: a head back where it was at recenter
// leaves the game's own camera completely alone -- including its pitch, which the
// previous yaw-only composition discarded and which showed in headset as rotating
// away from the torso.
void TestHeadAtRecenterLeavesGameRotationUntouched()
{
    const float recenterYaw = 0.4f;
    const Pose headAtRecenter = HeadPose(recenterYaw, 0.0f, 0.0f);

    // Game orientations including pitch, which is the case that regressed before.
    const Quaternion gameRotations[] = {
        YawQuaternion(0.0f),
        YawQuaternion(1.3f),
        Multiply(YawQuaternion(1.3f), HeadAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, 0.6f)),
        Multiply(YawQuaternion(-2.2f), HeadAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, -0.45f)),
    };

    for (const Quaternion& game : gameRotations) {
        const Quaternion composed =
            ComposeHeadOntoGameRotation(game, headAtRecenter, recenterYaw);
        // Compared by the axes they produce: quaternions double-cover rotations,
        // so q and -q are the same orientation and comparing components would
        // report a difference that does not exist.
        const Vec3 axes[] = {Vec3{1,0,0}, Vec3{0,1,0}, Vec3{0,0,1}};
        for (const Vec3& axis : axes) {
            Require(NearVec(Rotate(composed, axis), Rotate(Normalize(game), axis)),
                "a head at its recenter pose leaves the game's orientation untouched");
        }
    }
}

// **The frame the head delta is applied in is NOT pinned by these tests, and that
// is deliberate until the design question below is answered.**
//
// A negative control caught this: swapping the multiply order -- applying the head
// in the world frame instead of the camera frame -- still passed everything here.
// Every rotation in these cases turns about Z, and rotations about a shared axis
// commute, so the two orders are indistinguishable. The case that does have pitch
// puts the head at recenter, making the delta identity, which commutes with
// everything.
//
// Discriminating needs a game rotation with pitch AND a non-identity head delta,
// and writing that test means first deciding what *should* happen: whether a head
// yaw turns about the world's up axis (the body's) or about the pitched camera's
// own up. Those differ exactly when the mouse has pitched the view, which is the
// case the headset report was about. It is a design decision, not a maths one, so
// it is left open rather than frozen by a test that assumes an answer.

// And the head genuinely adds: turning the head yaws the view away from the
// game's facing by the same amount.
void TestHeadDeltaAddsToGameRotation()
{
    const float recenterYaw = 0.0f;
    const float headTurn = 0.6f;
    const Quaternion game = YawQuaternion(1.1f);

    const Quaternion composed =
        ComposeHeadOntoGameRotation(game, HeadPose(headTurn, 0.0f, 0.0f), recenterYaw);
    Require(Near(CameraYawOf(MatrixFromPose(Pose{composed, Vec3{}})), 1.1f + headTurn, 1e-3f),
        "the head turn adds to the game's facing");
}

// FAIL-CAM-019 through this seam: a headset rolled when recenter was pressed must
// not tilt the world afterwards. Only the reference's yaw is taken, so the stored
// value carries no tilt to bake in -- while a live roll still reaches the view.
void TestRolledRecenterDoesNotTiltTheComposition()
{
    const float recenterYaw = 0.5f;
    const Quaternion game = YawQuaternion(0.9f);

    // Recenter captured from a rolled headset yields a yaw-only reference.
    const auto reference = RecenterYawFromHeadPose(HeadPose(recenterYaw, 0.0f, 0.7f));
    Require(reference.has_value(), "a rolled recenter still yields a yaw");

    // A level head afterwards must leave the horizon level.
    const Quaternion composed =
        ComposeHeadOntoGameRotation(game, HeadPose(recenterYaw, 0.0f, 0.0f), *reference);
    const Vec3 right = Rotate(composed, Vec3{1.0f, 0.0f, 0.0f});
    Require(std::fabs(right.z) < 1e-3f,
        "a crooked recenter does not tilt the composed camera");
}

int main()
{
    TestHeadAtRecenterLeavesGameRotationUntouched();
    TestHeadDeltaAddsToGameRotation();
    TestRolledRecenterDoesNotTiltTheComposition();
    TestHeadAtRecenterReproducesTheGameCamera();
    TestHeadRotationAddsToTheGameCamera();
    TestRecenterYawTracksYaw();
    TestRecenterYawIgnoresRollAndPitch();
    TestNearVerticalHeadPoseIsRefused();
    TestCrookedRecenterDoesNotTiltTheWorld();
    TestRecenterReferenceFailsClosed();
    TestAxisConversion();
    TestConversionIsAProperRotation();
    TestRotationConversionInvariant();
    TestAxisAlignedRotationsMapAsExpected();
    TestMatrixFromPose();
    TestPoseMatrixRoundTrip();
    TestLocalFrameOffset();
    TestBuiltMatrixIsSafeToWrite();
    TestEyePoseComposition();
    TestCyclopsPose();
    TestWriteMatrixIsMinimal();
    TestWriteMatrixRefusesNonFinite();
    std::cout << "PreyVR stereo camera tests passed\n";
    return 0;
}
