#include "preyvr/FiringPosition.h"
#include <array>
#include "preyvr/AnimIk.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/RigOwnership.h"
#include <atomic>
#include <future>
#include <limits>
#include <thread>

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::animik;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

bool Near(float a, float b, float epsilon = 1e-4f) { return std::fabs(a - b) <= epsilon; }

bool NearVec(const Vec3& a, const Vec3& b, float epsilon = 1e-4f)
{
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
}

bool NearQuat(const Quaternion& a, const Quaternion& b, float epsilon = 1e-4f)
{
    // q and -q are the same rotation.
    const bool same = Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) &&
                      Near(a.z, b.z, epsilon) && Near(a.w, b.w, epsilon);
    const bool flipped = Near(a.x, -b.x, epsilon) && Near(a.y, -b.y, epsilon) &&
                         Near(a.z, -b.z, epsilon) && Near(a.w, -b.w, epsilon);
    return same || flipped;
}

Quaternion AxisAngle(Vec3 axis, float radians)
{
    const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    const float s = std::sin(radians * 0.5f) / len;
    return Quaternion{axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f)};
}

// World -> model -> world must be the identity for a scaled, rotated, translated
// location: this is the whole reason the engine's own location is used instead
// of a body-yaw approximation.
void TestLocationRoundTrip()
{
    Location loc;
    loc.q = AxisAngle(Vec3{0.2f, 0.3f, 1.0f}, 0.9f);
    loc.t = Vec3{12.0f, -3.5f, 1.25f};
    loc.s = 1.5f;
    const Vec3 world{14.0f, -2.0f, 2.5f};
    const Vec3 model = WorldToModel(loc, world);
    Require(NearVec(ModelToWorld(loc, model), world, 1e-3f), "world->model->world round trip");
    // A pure translation of the location moves the model point the other way.
    Location shifted = loc;
    shifted.q = Quaternion{};
    shifted.s = 1.0f;
    Require(NearVec(WorldToModel(shifted, Vec3{13.0f, -3.5f, 1.25f}), Vec3{1.0f, 0.0f, 0.0f}),
            "translation-only location subtracts its origin");
}

// The rotation half must compose so that model = inverse(loc) * world exactly.
void TestLocationRotation()
{
    Location loc;
    loc.q = AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 1.1f);
    const Quaternion world = AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 1.6f);
    const Quaternion model = WorldToModel(loc, world);
    Require(NearQuat(model, AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 0.5f)),
            "a yawed location leaves the yaw difference");
    Require(Near(YawOf(model), 0.5f, 1e-4f), "YawOf reads a Z-up yaw");
}

// Head-relative placement: the controller lands at the eye plus the rotated
// head-to-controller vector, and the head's own position cancels out -- which is
// what makes the play-space origin irrelevant.
void TestControllerFromHead()
{
    Pose head;
    head.position = Vec3{0.3f, 1.6f, -0.2f};   // OpenXR: x right, y up, -z forward
    Pose controller;
    controller.position = Vec3{0.3f, 1.2f, -0.7f};   // 40 cm below, 50 cm forward of the head
    const Vec3 eye{100.0f, 200.0f, 1.7f};
    const Pose world = ControllerWorldFromHead(0.0f, eye, head, controller);
    // Engine axes: forward is +Y, up is +Z. -0.5 in OpenXR z is +0.5 engine y.
    Require(NearVec(world.position, Vec3{100.0f, 200.5f, 1.3f}, 1e-4f),
            "controller sits forward and below the eye in engine axes");
    // Moving head and controller together changes nothing.
    head.position.x += 5.0f;
    controller.position.x += 5.0f;
    const Pose moved = ControllerWorldFromHead(0.0f, eye, head, controller);
    Require(NearVec(moved.position, world.position, 1e-4f),
            "a shared translation of head and controller cancels");
    // A half-turn yaw sends forward to backward.
    const Pose turned = ControllerWorldFromHead(3.14159265f, eye, head, controller);
    Require(NearVec(turned.position, Vec3{100.0f, 199.5f, 1.3f}, 1e-3f),
            "the reference yaw rotates the offset about the eye");
}

void TestClampToReach()
{
    const Vec3 upper{1.0f, 1.0f, 1.0f};
    Require(NearVec(ClampToReach(upper, Vec3{1.3f, 1.0f, 1.0f}, 0.6f), Vec3{1.3f, 1.0f, 1.0f}),
            "a reachable goal is untouched");
    const Vec3 clamped = ClampToReach(upper, Vec3{3.0f, 1.0f, 1.0f}, 0.6f);
    Require(NearVec(clamped, Vec3{1.6f, 1.0f, 1.0f}), "an unreachable goal lands on the reach sphere");
    Require(NearVec(ClampToReach(upper, Vec3{3.0f, 1.0f, 1.0f}, 0.0f), Vec3{3.0f, 1.0f, 1.0f}),
            "a zero reach means no information, so no clamp");
}

// Calibration must reproduce the animated wrist exactly at the calibration
// instant, and then follow the controller's delta from there.
void TestRotationCalibration()
{
    const Quaternion controllerAtCal = AxisAngle(Vec3{0.1f, 0.9f, 0.2f}, 0.7f);
    const Quaternion wristAtCal = AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, -1.3f);
    const Quaternion offset = CalibrateRotationOffset(controllerAtCal, wristAtCal);
    Require(NearQuat(ApplyRotationOffset(controllerAtCal, offset), wristAtCal),
            "applying the offset at calibration reproduces the wrist");
    const Quaternion turn = AxisAngle(Vec3{1.0f, 0.0f, 0.0f}, 0.4f);
    const Quaternion controllerLater = Normalize(Multiply(turn, controllerAtCal));
    const Quaternion expected = Normalize(Multiply(turn, wristAtCal));
    Require(NearQuat(ApplyRotationOffset(controllerLater, offset), expected),
            "a controller delta on the left turns the wrist by the same delta");
}

void TestSharedOriginAndMuzzleSeparation()
{
    const Vec3 native{10, 20, 1.7f};
    Pose head{{}, {0, 1.6f, 0}};
    Pose grip{{}, {0.2f, 1.2f, -0.5f}};
    Pose aim = grip;
    aim.position.z -= 0.1f; // XR aim and grip are different spaces.
    const auto wrist = ControllerWorldFromHead(0, native, head, grip);
    const auto aimWorld = ControllerWorldFromHead(0, native, head, aim);
    Vec3 cache = aimWorld.position;
    RayOriginEdit edit{123, native, cache};
    Require(edit.Restore(123, cache) && NearVec(cache, native),
            "our ray origin is restored even if native unprojection later fails");
    const auto cleanWrist = ControllerWorldFromHead(0, cache, head, grip);
    Require(NearVec(cleanWrist.position, wrist.position), "aim origin does not feed back into wrist");
    const auto brokenWrist = ControllerWorldFromHead(0, aimWorld.position, head, grip);
    Require(!NearVec(brokenWrist.position, wrist.position), "positive control detects doubled hand offset");
    cache = {99, 88, 77};
    Require(!edit.Restore(123, cache) && NearVec(cache, {99, 88, 77}), "preserve another origin writer");
    cache = edit.written;
    Require(!edit.Restore(456, cache), "never restore an old player's origin into a new player");
    // A fixture-authored muzzle is 20 cm forward of the grip, not the aim point.
    const Pose muzzle = Compose(wrist, Pose{{}, {0, 0.2f, 0}});
    Require(!NearVec(muzzle.position, aimWorld.position), "controller origin cannot be labelled muzzle");
    const auto turnWrist = ControllerWorldFromHead(1.57079632679f, native, head, grip);
    const Pose turnedMuzzle = Compose(turnWrist, Pose{{}, {0, 0.2f, 0}});
    Require(NearVec({turnedMuzzle.position.x-turnWrist.position.x,
                     turnedMuzzle.position.y-turnWrist.position.y,
                     turnedMuzzle.position.z-turnWrist.position.z}, {-0.2f, 0, 0}),
            "authored muzzle offset rotates with the wrist basis");
    // Fixed physical controller stays fixed ONLY when native world anchor also
    // accounts for the head translation. Orientation-only view is a separate gate.
    head.position.x += 0.1f;
    Vec3 movedEye = native; movedEye.x += 0.1f;
    Require(NearVec(ControllerWorldFromHead(0, movedEye, head, grip).position, wrist.position),
            "matched native/XR head translation cancels with stationary controller");
}

void TestCalibrationLifecycle()
{
    CalibrationState state;
    state.Bind(1,0,1);
    state.Request(3);
    Require(state.Pending(0) && state.Pending(1), "both hands requested");
    state.Commit(0, AxisAngle({0,0,1}, 0.4f));
    Require(!state.Pending(0) && state.Pending(1) && state.calibrated == 1,
            "right hand must not consume left calibration");
    // A missing pose or failed native write makes no Commit call.
    Require(state.Pending(1), "failed hand leaves its request pending");
    state.Commit(1, AxisAngle({1,0,0}, 0.2f));
    Require(state.pending == 0 && state.calibrated == 3, "both complete individually");
    state.Request(3);
    Require(state.Bind(2,0,1) && state.calibrated == 0 && state.pending == 0,
            "re-equip invalidates rotations AND pending requests even when pointers are reused");
    state.Request(2); state.Commit(1, {});
    Require(state.pending == 0 && state.calibrated == 2, "left-only request completes");
    state.Commit(99, {});
    Require(state.calibrated == 2, "invalid hand cannot shift outside mask");
    Require(!state.Bind(2,0,1) && state.calibrated == 2, "same owner retains calibration");
    Require(state.Bind(2,2,1) && state.calibrated == 0, "recenter invalidates calibration");
    state.Request(1); state.Commit(0, {});
    Require(state.Bind(2,2,2) && state.calibrated == 0, "XR focus/session epoch invalidates calibration");
}

void TestNativeFiringPositionLayout()
{
    // Little-endian native result, poison padding: x=1, y=-2, z=0.5.
    std::array<std::uint8_t,16> bytes{0,0xCD,0xCD,0xCD, 0,0,0x80,0x3F,
                                    0,0,0,0xC0, 0,0,0,0x3F};
    auto value = DecodeFiringPosition(bytes);
    Require(value && value->cameraFallback == 0 && NearVec(value->origin,{1,-2,0.5f}),
            "firing position uses +4 and ignores three padding bytes");
    bytes[0] = 1; value = DecodeFiringPosition(bytes);
    Require(value && value->cameraFallback == 1, "native wall guard flag remains visible");
    Require(!DecodeFiringPosition(std::span(bytes).first(15)), "truncated native result refused");
    bytes[0] = 2; Require(!DecodeFiringPosition(bytes), "unknown fallback encoding refused");
    bytes[0] = 0; bytes[6] = 0xC0; bytes[7] = 0x7F;
    Require(!DecodeFiringPosition(bytes), "nonfinite native position refused");
}

void TestOwnerAndBounds()
{
    RigIdentity captured{11,22,33,44,5,66};
    auto current = captured;
    Require(SameRigBinding(captured, current, 66), "positive control current equipped rig");
    current.character = 45;
    Require(!SameRigBinding(captured, current, 66), "shared skeleton with different character is refused");
    current = captured; current.binding = 34;
    Require(!SameRigBinding(captured, current, 66), "rebound attachment is refused");
    Require(!SameRigBinding(captured, captured, 67), "weapon switch invalidates even with same skeleton");
    Require(!SameRigBinding(captured, captured, 0), "holster invalidates writes");
    Require(ValidJoint(100,101) && !ValidJoint(-1,101) && !ValidJoint(101,101), "joint bounds include upper bound");
    Location loc{};
    Require(ValidLocation(loc), "identity location is valid when actually read");
    loc.s = 0; Require(!ValidLocation(loc), "zero scale refused");
    loc.s = -1; Require(!ValidLocation(loc), "negative scale refused");
    loc.s = std::numeric_limits<float>::infinity(); Require(!ValidLocation(loc), "infinite scale refused");
    loc = {}; loc.q = {0,0,0,0}; Require(!ValidLocation(loc), "zero quaternion refused before normalize");
    loc = {}; loc.t.x = std::numeric_limits<float>::quiet_NaN(); Require(!ValidLocation(loc), "NaN location refused");
}

struct FrameFixture {
    std::uint64_t sequence = 0, head = 0, left = 0, right = 0;
};
struct BlockingCopy {
    std::promise<void>* entered = nullptr;
    std::shared_future<void> release{};
    bool block = false;
    BlockingCopy& operator=(const BlockingCopy& source) {
        if (source.block) { source.entered->set_value(); source.release.wait(); }
        return *this;
    }
};

void TestSnapshotPublication()
{
    LatestSnapshot<FrameFixture> slot;
    FrameFixture sample{};
    Require(!slot.TryRead(sample), "unpublished state refused");
    slot.Publish({1,1,1,1});
    Require(slot.TryRead(sample) && sample.head == 1, "initial snapshot positive control");
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (std::uint64_t i=2; i<=10000; ++i) { slot.Publish({i,i,i,i}); }
        done.store(true);
    });
    do {
        if (slot.TryRead(sample)) {
            Require(sample.sequence == sample.head && sample.head == sample.left && sample.left == sample.right,
                    "head and hands must come from the same publication under contention");
        }
    } while (!done.load());
    writer.join();
    Require(slot.TryRead(sample) && sample.sequence == 10000, "latest complete frame survives");
    slot.Clear();
    Require(!slot.TryRead(sample), "focus/session clear cannot replay a stale pose");
    LatestSnapshot<BlockingCopy> contended;
    std::promise<void> entered, release;
    auto began = entered.get_future();
    BlockingCopy blocked{&entered, release.get_future().share(), true};
    std::thread publisher([&] { contended.Publish(blocked); });
    Require(began.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "publisher reached controlled contention");
    BlockingCopy output;
    const bool readWhileLocked = contended.TryRead(output);
    release.set_value(); publisher.join();
    Require(!readWhileLocked, "engine reader refuses contention without waiting");
    Require(FreshSample(201,1,200) && !FreshSample(202,1,200), "freshness boundary");
    Require(!FreshSample(1,2) && !FreshSample(1,0), "future and absent timestamps refused");
}

} // namespace


// The 2026-09-07 defect, as a property: with the player facing one way, turning
// only the head must not move the hand.
//
// The engine's view camera carries head tracking, so its yaw is
// `body + (head - reference)`. Rotating a HEAD-RELATIVE offset by that angle
// applies the head twice and the hand swings with the headset -- what a wearer
// saw. Subtracting the head's own yaw leaves the body yaw, and the hand holds
// still. Both arms are asserted so the test documents the defect, not just the
// fix: a future "simplification" back to camera-minus-reference fails here.
void TestHandDoesNotSwingWithHeadYaw()
{
    const float bodyYaw = 0.4f;        // where the player's body faces
    const float referenceYaw = 0.15f;  // where the head faced at recenter
    Pose head;
    head.position = Vec3{0.3f, 1.6f, -0.2f};
    Pose controller;
    controller.position = Vec3{0.3f, 1.2f, -0.7f};
    const Vec3 eye{10.0f, 20.0f, 1.7f};
    const float headYaws[3] = {0.0f, 0.6f, -0.9f};

    Vec3 firstCorrect{}, firstNaive{};
    bool naiveMoved = false;
    for (int i = 0; i < 3; ++i) {
        const float cameraYaw = bodyYaw + (headYaws[i] - referenceYaw);
        const Vec3 correct =
            ControllerWorldFromHead(cameraYaw - headYaws[i], eye, head, controller).position;
        const Vec3 naive =
            ControllerWorldFromHead(cameraYaw - referenceYaw, eye, head, controller).position;
        if (i == 0) { firstCorrect = correct; firstNaive = naive; continue; }
        Require(NearVec(correct, firstCorrect, 1e-4f),
                "body yaw: turning the head must not move the hand");
        if (!NearVec(naive, firstNaive, 1e-3f)) { naiveMoved = true; }
    }
    Require(naiveMoved, "camera yaw must visibly swing the hand, or this test proves nothing");
}


// The same property, but driven through the ENGINE-FACING code rather than an
// assumed camera model.
//
// The previous test built `cameraYaw = body + (head - reference)` by hand, which
// is only my description of what the camera edit does. This one calls the real
// composition -- ComposeHeadOntoGameRotation, the function the camera seam
// applies -- turns the result into a matrix and reads it with CameraYawOf, which
// is literally what GameCameraYaw() does live. So if the camera seam's
// composition ever changes, this fails instead of quietly agreeing with a stale
// assumption.
//
// Head PITCH is included because that is the case that matters: a wearer looking
// up is exactly when the near-vertical path engages. The relationship survives
// it exactly, because both CameraYawOf and RecenterYawFromHeadPose project onto
// the same horizontal plane.
void TestPlaySpaceYawFromRealCameraComposition()
{
    const float bodyYaw = 0.4f;
    const float referenceYaw = 0.15f;
    const float headYaws[4] = {0.0f, 0.7f, -1.1f, 2.4f};
    const float headPitches[4] = {0.0f, 0.35f, -0.5f, 0.2f};

    for (int i = 0; i < 4; ++i) {
        Pose head;
        // OpenXR is Y-up: yaw about +Y, pitch about +X.
        head.orientation = Multiply(AxisAngle(Vec3{0.0f, 1.0f, 0.0f}, headYaws[i]),
                                    AxisAngle(Vec3{1.0f, 0.0f, 0.0f}, headPitches[i]));

        // What the camera seam actually writes, and what GameCameraYaw() reads.
        const Quaternion gameRotation = stereo::YawQuaternion(bodyYaw);
        const Quaternion composed =
            stereo::ComposeHeadOntoGameRotation(gameRotation, head, referenceYaw);
        const float cameraYaw = stereo::CameraYawOf(stereo::MatrixFromPose(Pose{composed, Vec3{}}));

        const auto measuredHeadYaw = stereo::RecenterYawFromHeadPose(head);
        Require(measuredHeadYaw.has_value(), "these head poses are not near-vertical");

        // The play-space yaw every lane must use, recovered from live readings.
        const float playSpaceYaw = cameraYaw - *measuredHeadYaw;
        Require(Near(playSpaceYaw, bodyYaw - referenceYaw, 1e-4f),
                "camera yaw minus head yaw must recover the play-space yaw");

        // And the naive value moves, which is the defect.
        if (i > 0) {
            Require(!Near(cameraYaw - referenceYaw, bodyYaw - referenceYaw, 1e-3f),
                    "camera minus reference must drift as the head turns");
        }
    }
}

int main()
{
    TestLocationRoundTrip();
    TestLocationRotation();
    TestControllerFromHead();
    TestHandDoesNotSwingWithHeadYaw();
    TestPlaySpaceYawFromRealCameraComposition();
    TestClampToReach();
    TestRotationCalibration();
    TestSharedOriginAndMuzzleSeparation();
    TestCalibrationLifecycle();
    TestOwnerAndBounds();
    TestNativeFiringPositionLayout();
    TestSnapshotPublication();
    std::cout << "PreyVR anim-ik tests passed\n";
    return 0;
}
