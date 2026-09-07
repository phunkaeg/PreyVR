#include "AnimIkTakeover.h"
#include "HandRigTakeover.h"
#include "MinHookInit.h"

#include "HeadTrackingHook.h"
#include "Logger.h"
#include "WeaponAttachment.h"
#include "XrInput.h"
#include "preyvr/EngineMap.h"
#include "preyvr/MotionController.h"
#include "preyvr/StereoCamera.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <span>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_hand_rig " + line);
}

// CCharInstance::SkinningTransformationsComputation. Six arguments: the first four
// in registers, the pose data and movement accumulator on the stack -- which a
// six-parameter declaration reproduces exactly on this ABI.
using ComputeFn = void*(__fastcall*)(void* charInstance, void* skinningData,
                                     std::uint8_t* defaultSkeleton, unsigned int lod,
                                     std::uint8_t* poseData, float* movement);

constexpr std::uintptr_t kComputeRva = 0x82EE10;
constexpr std::array<std::uint8_t, 24> kComputePrologue{
    0x48, 0x8B, 0xC4, 0x53, 0x48, 0x81, 0xEC, 0x00,
    0x01, 0x00, 0x00, 0x48, 0x89, 0x68, 0x08, 0x49,
    0x8B, 0xC8, 0x48, 0x8B, 0x6A, 0x08, 0x49, 0x8B,
};

// Byte-verified layouts from the H-005 investigation.
constexpr std::size_t kPoseRelativeArray = 0x10;   // QuatT*, stride 0x1C
constexpr std::size_t kPoseAbsoluteArray = 0x18;   // QuatT*, stride 0x1C
constexpr std::size_t kPosePrefixBytes   = 0x20;   // enough for the one field read
constexpr std::size_t kSkeletonJointArray = 0x08;
constexpr std::size_t kJointRecordStride  = 0xA8;
constexpr std::size_t kJointParentOffset  = 0x18;  // signed 16-bit; -1 is root
constexpr std::size_t kQuatTStride        = 0x1C;
constexpr std::size_t kQuatTPosition      = 0x10;  // Vec3 after the quaternion

// The character CB is allocated for 768 dual quaternions, so a skeleton larger
// than that could not be uploaded anyway.
constexpr unsigned int kMaxJoints = 768;

void* gTarget = nullptr;
std::atomic<ComputeFn> gOriginal{nullptr};
bool gInstalled = false;

std::atomic<unsigned int> gMode{0};
std::atomic<int> gJoint{-1};
std::atomic<float> gOffsetX{0.0f}, gOffsetY{0.0f}, gOffsetZ{0.0f};
std::atomic<unsigned long long> gForwarded{0}, gApplied{0}, gRefused{0};
std::atomic<unsigned int> gLastSubtree{0};
std::atomic<unsigned long long> gSelectedCharacter{0};
std::atomic<unsigned long long> gLastCharacter{0};
std::atomic<unsigned long long> gMatched{0}, gSkipped{0};

// Controller drive.
std::atomic<int> gRightJoint{-1}, gLeftJoint{-1};
std::atomic<bool> gControllerDrive{false};
std::atomic<bool> gCalibrated{false};
// Wrist rotation. Separate from the positional drive because the two fail
// differently: a wrong position is a hand in the wrong place, a wrong rotation
// is a hand that looks broken, and they should be armable one at a time.
std::atomic<bool> gWristDrive{false};
Quaternion gZeroRightRot{0.0f, 0.0f, 0.0f, 1.0f};
Quaternion gZeroLeftRot{0.0f, 0.0f, 0.0f, 1.0f};
std::atomic<unsigned long long> gWristApplied{0};
// H-016 diagnostics: the operands behind `handRightMm`.
std::atomic<int> gLastWorldRight[3]{};
std::atomic<int> gCalibYawMilli{0};
std::atomic<int> gLastYawMilli{0};
std::atomic<float> gScale{1.0f};
std::atomic<unsigned long long> gNoPose{0};
std::atomic<int> gLastRightMm{0}, gLastLeftMm{0};
Vec3 gZeroRight{}, gZeroLeft{};

// The engine's own camera basis. Used twice: to build the reference frame the
// controller conversion needs, and to turn a world displacement into the
// character's frame.
bool CameraBasis(Vec3& right, Vec3& forward, Vec3& up, Vec3& position)
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    auto* const systemPtr =
        *reinterpret_cast<std::uint8_t**>(base + engine::SystemLayout::pointerRva);
    if (systemPtr == nullptr) {
        return false;
    }
    const auto* const camera = reinterpret_cast<const std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(systemPtr) + engine::SystemLayout::viewCamera);
    const stereo::Matrix34 m =
        stereo::ReadMatrix(std::span<const std::uint8_t>(camera, engine::CameraLayout::size));
    right   = Vec3{m[0], m[4], m[8]};
    forward = Vec3{m[1], m[5], m[9]};
    up      = Vec3{m[2], m[6], m[10]};
    position = Vec3{m[3], m[7], m[11]};
    const float len = std::sqrt(right.x*right.x + right.y*right.y + right.z*right.z);
    return std::isfinite(len) && len > 0.9f && len < 1.1f;
}

// A controller's position in world space, through the same reference frame the
// aim lane uses -- one recenter resolved one way for every lane (CAM-003).
bool ControllerWorld(Hand hand, Vec3& out)
{
    ControllerState state{};
    if (!TryGetControllerState(hand, state)) {
        return false;
    }
    if (!state.gripValidity.positionTracked) {
        return false;   // untracked: keep the engine's animation rather than guess
    }
    Vec3 right{}, forward{}, up{}, position{};
    if (!CameraBasis(right, forward, up, position)) {
        return false;
    }
    stereo::ReferenceFrame reference{};
    reference.yawRadians = stereo::CameraYawOf(stereo::Matrix34{
        right.x, forward.x, up.x, position.x,
        right.y, forward.y, up.y, position.y,
        right.z, forward.z, up.z, position.z}) - HeadTrackingReferenceYaw();
    // **Origin, not the camera position.** The camera is offset +/-32mm PER EYE by
    // the synthetic stereo, so anchoring here would make the controller's computed
    // world position swing by the IPD every frame. Differencing that against a
    // single calibration zero bakes an alternating translation into a MODEL-SPACE
    // bone -- which is a constant screen offset at every depth, not parallax.
    //
    // A wearer diagnosed it exactly: "regardless of their distance from the camera
    // they still render with the same offset."
    //
    // The delta is a difference of two controller positions, so a shared reference
    // cancels and the origin is the right one. Yaw is safe to keep: native
    // projection builds each eye by translation alone, so both eyes share it.
    reference.worldPosition = Vec3{};
    out = controller::ControllerPoseInWorld(reference, state.gripPose).position;
    if (hand == Hand::right) {
        gLastWorldRight[0].store(static_cast<int>(out.x * 1000.0f), std::memory_order_relaxed);
        gLastWorldRight[1].store(static_cast<int>(out.y * 1000.0f), std::memory_order_relaxed);
        gLastWorldRight[2].store(static_cast<int>(out.z * 1000.0f), std::memory_order_relaxed);
        gLastYawMilli.store(static_cast<int>(reference.yawRadians * 57295.78f),
                            std::memory_order_relaxed);
    }
    return true;
}

// The same conversion as ControllerWorld, kept beside it, but returning the
// orientation. Split rather than merged so the positional lane -- which is
// proven exact to the millimetre -- is not disturbed by the rotation work.
bool ControllerWorldRotation(Hand hand, Quaternion& out)
{
    ControllerState state{};
    if (!TryGetControllerState(hand, state)) {
        return false;
    }
    if (!state.gripValidity.orientationTracked) {
        return false;
    }
    Vec3 right{}, forward{}, up{}, position{};
    if (!CameraBasis(right, forward, up, position)) {
        return false;
    }
    stereo::ReferenceFrame reference{};
    reference.yawRadians = stereo::CameraYawOf(stereo::Matrix34{
        right.x, forward.x, up.x, position.x,
        right.y, forward.y, up.y, position.y,
        right.z, forward.z, up.z, position.z}) - HeadTrackingReferenceYaw();
    reference.worldPosition = Vec3{};
    out = controller::ControllerPoseInWorld(reference, state.gripPose).orientation;
    return true;
}

// World displacement into the character's frame.
//
// **Yaw-only, and anchored to the BODY rather than the head.** The obvious
// implementation projects onto the live camera basis, and the fleet playbook says
// plainly why that is wrong: "parenting the shoulders to the HMD is the obvious
// implementation and it is wrong... every head movement then drags the shoulders."
// Our camera *carries head tracking*, so a camera-basis projection rotates the
// hand frame every time the wearer looks around, and the hands swim.
//
// The body yaw is the camera yaw minus the head's own contribution -- the same
// composition the view seam performs, run backwards. Pitch and roll are dropped
// entirely: a shoulder girdle does not pitch when you look at your feet.
//
// Still an approximation of the true model frame, which is the character's render
// matrix (`RenderCHR` receives it in R8 and this hook does not see it). Good
// enough to tell whether the hand follows the controller; not good enough to ship.
bool BodyYaw(float& out)
{
    Vec3 right{}, forward{}, up{}, position{};
    if (!CameraBasis(right, forward, up, position)) {
        return false;
    }
    const float cameraYaw = stereo::CameraYawOf(stereo::Matrix34{
        right.x, forward.x, up.x, position.x,
        right.y, forward.y, up.y, position.y,
        right.z, forward.z, up.z, position.z});
    Pose head{};
    unsigned long long age = 0;
    if (!TryReadHeadPose(head, age)) {
        // No head pose: the camera has no head contribution to subtract, so its
        // own yaw IS the body yaw.
        out = cameraYaw;
        return true;
    }
    const auto headYaw = stereo::RecenterYawFromHeadPose(head);
    if (!headYaw) {
        out = cameraYaw;
        return true;
    }
    out = cameraYaw - (*headYaw - HeadTrackingReferenceYaw());
    return true;
}

// A constant yaw between the body frame the position lane computes and the frame
// the bone rotations actually live in.
//
// **Exposed rather than assumed.** The body-yaw conversion above is required for
// correctness on its own, but whether a *further* fixed offset remains is a
// property of the rig's authored convention, and the honest way to settle it is
// one command in a headset rather than a guess compiled in. 180 degrees is the
// leading candidate: it is what the first wearer report describes exactly, and
// it is what a rig authored facing -Y against a world forward of +Y would give.
std::atomic<int> gModelTurnYawDeciDegrees{0};

float ModelTurnYawOffsetRadians()
{
    return static_cast<float>(gModelTurnYawDeciDegrees.load(std::memory_order_relaxed)) *
           0.1f * 3.14159265358979f / 180.0f;
}

Vec3 WorldDeltaToModel(const Vec3& delta, float bodyYaw)
{
    // CryEngine convention: forward is +Y, right is +X, up is +Z, yaw about Z.
    const float s = std::sin(bodyYaw), c = std::cos(bodyYaw);
    const Vec3 right{c, s, 0.0f};
    const Vec3 forward{-s, c, 0.0f};
    return Vec3{
        delta.x*right.x   + delta.y*right.y,
        delta.x*forward.x + delta.y*forward.y,
        delta.z};   // up is world up; a body does not roll
}

// Topology of the most recent conversion, for the passive dump.
std::atomic<unsigned int> gTopologyCount{0};
std::array<const char*, kMaxJoints> gJointNames{};
std::array<int, kMaxJoints> gJointParents{};

// **Thread-local, because this runs as a job.** Concurrent conversions must not
// share a scratch buffer; the investigation is explicit that each invocation needs
// its own, and the storage has to outlive the forwarded call.
thread_local std::array<std::uint8_t, kMaxJoints * kQuatTStride> tScratch{};
// **The relative array is cloned too, which is what unblocks the arm chain.**
// R-088 found the native two-bone solver at `0x871CA0` reads relative at
// `pose+0x10` and absolute at `pose+0x18` and **writes both**. Redirecting only
// `+0x18` left `+0x10` pointing at engine memory, so calling that solver through
// our view would have had it write into the engine's own relative pose while we
// believed we were working on a private copy. Cloning both is the precondition
// for using the native solver at all.
thread_local std::array<std::uint8_t, kMaxJoints * kQuatTStride> tRelativeScratch{};
thread_local std::array<std::uint8_t, kPosePrefixBytes> tPoseView{};
thread_local std::array<unsigned char, kMaxJoints> tInSubtree{};

const std::uint8_t* JointArray(const std::uint8_t* skeleton)
{
    return *reinterpret_cast<const std::uint8_t* const*>(skeleton + kSkeletonJointArray);
}

// Read directly rather than through the vtable: these accessors are four
// instructions each, and calling into the engine from a hook on its own job thread
// buys nothing but risk.
unsigned int JointCount(const std::uint8_t* skeleton)
{
    const std::uint8_t* const joints = JointArray(skeleton);
    if (joints == nullptr) {
        return 0;
    }
    const std::uint32_t raw = *reinterpret_cast<const std::uint32_t*>(joints - 4);
    return raw & 0x7FFFFFFFu;
}

int JointParent(const std::uint8_t* skeleton, unsigned int index)
{
    const std::uint8_t* const joints = JointArray(skeleton);
    if (joints == nullptr) {
        return -1;
    }
    return *reinterpret_cast<const std::int16_t*>(
        joints + index * kJointRecordStride + kJointParentOffset);
}

const char* JointName(const std::uint8_t* skeleton, unsigned int index)
{
    const std::uint8_t* const joints = JointArray(skeleton);
    if (joints == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<const char* const*>(joints + index * kJointRecordStride);
}

void CaptureTopology(const std::uint8_t* skeleton, unsigned int count)
{
    if (count == 0 || count > kMaxJoints) {
        return;
    }
    for (unsigned int j = 0; j < count; ++j) {
        gJointNames[j] = JointName(skeleton, j);
        gJointParents[j] = JointParent(skeleton, j);
    }
    gTopologyCount.store(count, std::memory_order_release);
}

void* __fastcall ComputeWithHandTakeover(void* charInstance, void* skinningData,
                                         std::uint8_t* defaultSkeleton, unsigned int lod,
                                         std::uint8_t* poseData, float* movement)
{
    const ComputeFn original = gOriginal.load(std::memory_order_acquire);
    const unsigned int mode = gMode.load(std::memory_order_acquire);
    const auto forward = [&] {
        return original != nullptr
                   ? original(charInstance, skinningData, defaultSkeleton, lod, poseData, movement)
                   : nullptr;
    };
    if (mode == 0 || original == nullptr || defaultSkeleton == nullptr || poseData == nullptr) {
        return forward();
    }

    const unsigned int count = JointCount(defaultSkeleton);
    if (count == 0 || count > kMaxJoints) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        return forward();
    }
    CaptureTopology(defaultSkeleton, count);
    gLastCharacter.store(reinterpret_cast<unsigned long long>(charInstance),
                         std::memory_order_relaxed);

    // Topology is captured for every character; edits are not. A zero selection
    // means "any", which is only right while dumping.
    const unsigned long long selected = gSelectedCharacter.load(std::memory_order_acquire);
    const bool matches = selected == 0ull ||
                         selected == reinterpret_cast<unsigned long long>(charInstance);
    if (!matches) {
        gSkipped.fetch_add(1, std::memory_order_relaxed);
        return forward();
    }
    gMatched.fetch_add(1, std::memory_order_relaxed);

    auto* const absolute =
        *reinterpret_cast<std::uint8_t* const*>(poseData + kPoseAbsoluteArray);
    if (absolute == nullptr) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        return forward();
    }

    // Private copy of the absolute joints. The original reads this array and never
    // writes it, so a copy is a complete substitute for the one field it consumes.
    const std::size_t bytes = static_cast<std::size_t>(count) * kQuatTStride;
    std::memcpy(tScratch.data(), absolute, bytes);

    unsigned int moved = 0;
    if (mode == 2) {
        // Displace one joint's subtree by a model-space delta. Factored out
        // because both hands need it and they must not share the mask.
        const auto displace = [&](int joint, const Vec3& delta,
                                  const Quaternion& turn) -> unsigned int {
            if (joint < 0 || static_cast<unsigned int>(joint) >= count) {
                return 0;
            }
            std::memset(tInSubtree.data(), 0, count);
            for (unsigned int j = 0; j < count; ++j) {
                int walk = static_cast<int>(j);
                for (unsigned int guard = 0; guard < count && walk >= 0; ++guard) {
                    if (walk == joint) { tInSubtree[j] = 1; break; }
                    walk = gJointParents[static_cast<std::size_t>(walk)];
                }
            }
            // **The pivot is read before anything moves.** It is the driven
            // joint's own position, so that joint is a fixed point and its
            // descendants orbit it. Reading it inside the loop would use a
            // position this same loop had already rewritten.
            const float* const pivotSrc = reinterpret_cast<const float*>(
                tScratch.data() + static_cast<std::size_t>(joint) * kQuatTStride + kQuatTPosition);
            if (!std::isfinite(pivotSrc[0]) || !std::isfinite(pivotSrc[1]) ||
                !std::isfinite(pivotSrc[2])) {
                return 0;
            }
            const Vec3 pivot{pivotSrc[0], pivotSrc[1], pivotSrc[2]};
            const bool turning = std::fabs(turn.w) < 0.99999f;   // not identity

            unsigned int n = 0;
            for (unsigned int j = 0; j < count; ++j) {
                if (!tInSubtree[j]) { continue; }
                std::uint8_t* const entry =
                    tScratch.data() + static_cast<std::size_t>(j) * kQuatTStride;
                float* const pos = reinterpret_cast<float*>(entry + kQuatTPosition);
                float* const rot = reinterpret_cast<float*>(entry);
                if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2])) {
                    return 0;
                }
                if (turning) {
                    // **Rigid, about the pivot.** Rotating a wrist in an
                    // *absolute* pose array does not carry its children -- each
                    // holds its own model-space transform and would be left
                    // behind. That is the tearing failure this header warns
                    // about, seen from the rotation side.
                    controller::JointPose in;
                    in.rotation = Quaternion{rot[0], rot[1], rot[2], rot[3]};
                    in.position = Vec3{pos[0], pos[1], pos[2]};
                    const controller::JointPose out =
                        controller::RotateJointAboutPivot(in, pivot, turn);
                    if (!std::isfinite(out.position.x) || !std::isfinite(out.rotation.w)) {
                        return 0;
                    }
                    rot[0] = out.rotation.x; rot[1] = out.rotation.y;
                    rot[2] = out.rotation.z; rot[3] = out.rotation.w;
                    pos[0] = out.position.x; pos[1] = out.position.y; pos[2] = out.position.z;
                }
                pos[0] += delta.x; pos[1] += delta.y; pos[2] += delta.z;
                ++n;
            }
            if (turning) {
                gWristApplied.fetch_add(1, std::memory_order_relaxed);
            }
            return n;
        };

        if (!gControllerDrive.load(std::memory_order_acquire)) {
            // The fixed offset, which is what the first discriminator used.
            const Vec3 fixed{gOffsetX.load(std::memory_order_relaxed),
                             gOffsetY.load(std::memory_order_relaxed),
                             gOffsetZ.load(std::memory_order_relaxed)};
            moved = displace(gJoint.load(std::memory_order_relaxed), fixed, Quaternion{0.0f, 0.0f, 0.0f, 1.0f});
        } else if (!gCalibrated.load(std::memory_order_acquire)) {
            // Refusing before calibration is deliberate: the first frame would
            // otherwise snap the hands to wherever the controllers sit relative
            // to an arbitrary origin.
            gNoPose.fetch_add(1, std::memory_order_relaxed);
        } else {
            float bodyYaw = 0.0f;
            if (!BodyYaw(bodyYaw)) {
                gNoPose.fetch_add(1, std::memory_order_relaxed);
            } else {
                const float scale = gScale.load(std::memory_order_relaxed);
                Vec3 world{};
                // The wrist turn since calibration, or identity when the
                // rotation lane is not armed. Identity is checked for inside
                // `displace`, so an unarmed rotation costs nothing.
                Quaternion rightTurn = Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
                Quaternion leftTurn = Quaternion{0.0f, 0.0f, 0.0f, 1.0f};
                if (gWristDrive.load(std::memory_order_acquire)) {
                    Quaternion now{};
                    // The turn is taken in world terms and must be carried into
                    // the model frame by the SAME yaw the position lane uses, or
                    // the hand's travel and its twist are read against different
                    // bases. A missing yaw conversion shows up as correct yaw
                    // with inverted pitch and roll -- the 2026-09-07 wearer
                    // report -- because conjugating by a yaw preserves the
                    // rotation's own Z term.
                    const float turnYaw = bodyYaw + ModelTurnYawOffsetRadians();
                    if (ControllerWorldRotation(Hand::right, now)) {
                        rightTurn = controller::WorldTurnToModel(
                            Normalize(Multiply(now, stereo::Conjugate(gZeroRightRot))), turnYaw);
                    }
                    if (ControllerWorldRotation(Hand::left, now)) {
                        leftTurn = controller::WorldTurnToModel(
                            Normalize(Multiply(now, stereo::Conjugate(gZeroLeftRot))), turnYaw);
                    }
                }
                if (ControllerWorld(Hand::right, world)) {
                    const Vec3 d = WorldDeltaToModel(
                        Vec3{world.x - gZeroRight.x, world.y - gZeroRight.y, world.z - gZeroRight.z},
                        bodyYaw);
                    const Vec3 scaled{d.x * scale, d.y * scale, d.z * scale};
                    moved += displace(gRightJoint.load(std::memory_order_relaxed),
                                      scaled, rightTurn);
                    gLastRightMm.store(static_cast<int>(
                        std::sqrt(scaled.x*scaled.x + scaled.y*scaled.y + scaled.z*scaled.z) * 1000.0f),
                        std::memory_order_relaxed);
                } else {
                    gNoPose.fetch_add(1, std::memory_order_relaxed);
                }
                if (ControllerWorld(Hand::left, world)) {
                    const Vec3 d = WorldDeltaToModel(
                        Vec3{world.x - gZeroLeft.x, world.y - gZeroLeft.y, world.z - gZeroLeft.z},
                        bodyYaw);
                    const Vec3 scaled{d.x * scale, d.y * scale, d.z * scale};
                    moved += displace(gLeftJoint.load(std::memory_order_relaxed),
                                      scaled, leftTurn);
                    gLastLeftMm.store(static_cast<int>(
                        std::sqrt(scaled.x*scaled.x + scaled.y*scaled.y + scaled.z*scaled.z) * 1000.0f),
                        std::memory_order_relaxed);
                } else {
                    gNoPose.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        gLastSubtree.store(moved, std::memory_order_relaxed);

        // The weapon rides the same batch. The playbook's rule is that a solve
        // clocked differently from the animation it corrects reads as jitter in
        // the hands specifically -- so the weapon mount is updated here rather
        // than from a timer, and shares the controller basis as well as the
        // cadence.
        UpdateWeaponMountFromController();
    }

    // A private pose-data view: the prefix copied, with the absolute-array pointer
    // aimed at our scratch. The original reads exactly this one field, so a prefix
    // is enough **for these bytes** -- it is not a general CPoseData clone and must
    // not be treated as one.
    std::memcpy(tPoseView.data(), poseData, kPosePrefixBytes);
    *reinterpret_cast<std::uint8_t**>(tPoseView.data() + kPoseAbsoluteArray) = tScratch.data();

    // The relative array, cloned on the same terms. Absent this, any native
    // solver invoked through this view writes the engine's relative pose
    // directly -- silently, and outside the substitution this hook is built on.
    // Cloned only when the source is readable; a null or unreadable pointer
    // leaves the view's field as the engine set it, which is the fail-closed
    // choice because the alternative is aiming the solver at our empty buffer.
    {
        auto* const relative =
            *reinterpret_cast<std::uint8_t* const*>(poseData + kPoseRelativeArray);
        if (relative != nullptr && bytes <= tRelativeScratch.size()) {
            __try {
                std::memcpy(tRelativeScratch.data(), relative, bytes);
                *reinterpret_cast<std::uint8_t**>(tPoseView.data() + kPoseRelativeArray) =
                    tRelativeScratch.data();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                // Unreadable: leave the original pointer in place rather than
                // hand the engine a buffer we never filled.
            }
        }
    }

    // Exactly once. Calling the original a second time on the same skinning packet
    // would repeat its publication protocol after that state has changed.
    void* const result = original(charInstance, skinningData, defaultSkeleton, lod,
                                  tPoseView.data(), movement);
    if (mode == 2 && moved > 0) {
        gApplied.fetch_add(1, std::memory_order_relaxed);
    } else {
        gForwarded.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

bool Install()
{
    if (gInstalled) {
        return true;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        Log("result=unavailable detail=no_preydll");
        return false;
    }
    const auto target = reinterpret_cast<std::uintptr_t>(preyDll) + kComputeRva;
    if (std::memcmp(reinterpret_cast<const void*>(target),
                    kComputePrologue.data(), kComputePrologue.size()) != 0) {
        Log("result=unavailable detail=compute_prologue_mismatch");
        return false;
    }
    gTarget = reinterpret_cast<void*>(target);
    ComputeFn original = nullptr;
    // Shared and idempotent: without it MH_CreateHook returns
    // MH_ERROR_NOT_INITIALIZED and the feature reports "unavailable" for a
    // reason unrelated to itself. See MinHookInit.h.
    EnsureMinHook();
    if (MH_CreateHook(gTarget, reinterpret_cast<void*>(&ComputeWithHandTakeover),
                      reinterpret_cast<void**>(&original)) != MH_OK) {
        Log("result=failed detail=create_hook");
        return false;
    }
    gOriginal.store(original, std::memory_order_release);
    if (MH_EnableHook(gTarget) != MH_OK) {
        Log("result=failed detail=enable_hook");
        MH_RemoveHook(gTarget);
        return false;
    }
    gInstalled = true;
    Log("result=0 detail=hook_installed target=SkinningTransformationsComputation rva=0x82EE10");
    return true;
}

struct NameRequest {
    unsigned int index;
    unsigned int capacity;
    char* buffer;
};

} // namespace

DWORD SetHandRigTakeoverMode(unsigned int mode)
{
    if (mode == 2u && AnimIkMode() == 2u) { return 3; }
    if (mode > 2u) {
        return 1;
    }
    if (mode != 0u && !Install()) {
        return 2;
    }
    if (mode != 0u) {
        gForwarded.store(0, std::memory_order_relaxed);
        gApplied.store(0, std::memory_order_relaxed);
        gRefused.store(0, std::memory_order_relaxed);
    }
    gMode.store(mode, std::memory_order_release);
    Log("result=0 detail=mode value=" + std::to_string(mode));
    return 0;
}

DWORD SetHandRigJoint(unsigned int jointIndex)
{
    if (jointIndex >= kMaxJoints) {
        return 1;
    }
    gJoint.store(static_cast<int>(jointIndex), std::memory_order_relaxed);
    Log("result=0 detail=joint value=" + std::to_string(jointIndex));
    return 0;
}

DWORD SetHandRigOffsetMillimetres(int x, int y, int z)
{
    // A hand does not move a metre from its own skeleton. Larger is a caller
    // error, and this runs on an engine job thread.
    const auto tooBig = [](int v) { return v < -1000 || v > 1000; };
    if (tooBig(x) || tooBig(y) || tooBig(z)) {
        return 1;
    }
    gOffsetX.store(static_cast<float>(x) / 1000.0f, std::memory_order_relaxed);
    gOffsetY.store(static_cast<float>(y) / 1000.0f, std::memory_order_relaxed);
    gOffsetZ.store(static_cast<float>(z) / 1000.0f, std::memory_order_relaxed);
    Log("result=0 detail=offset_mm x=" + std::to_string(x) +
        " y=" + std::to_string(y) + " z=" + std::to_string(z));
    return 0;
}

DWORD SetHandRigRightJoint(unsigned int jointIndex)
{
    if (jointIndex >= kMaxJoints) { return 1; }
    gRightJoint.store(static_cast<int>(jointIndex), std::memory_order_relaxed);
    Log("result=0 detail=right_joint value=" + std::to_string(jointIndex));
    return 0;
}

DWORD SetHandRigLeftJoint(unsigned int jointIndex)
{
    if (jointIndex >= kMaxJoints) { return 1; }
    gLeftJoint.store(static_cast<int>(jointIndex), std::memory_order_relaxed);
    Log("result=0 detail=left_joint value=" + std::to_string(jointIndex));
    return 0;
}

unsigned int HandRigMode() { return gMode.load(std::memory_order_acquire); }

unsigned int HandRigControllerDriveArmed()
{
    return gControllerDrive.load(std::memory_order_acquire) ? 1u : 0u;
}

unsigned int HandRigCalibrationDone()
{
    return gCalibrated.load(std::memory_order_acquire) ? 1u : 0u;
}

int HandRigZeroRightMm(unsigned int axis)
{
    const float v = axis == 0 ? gZeroRight.x : axis == 1 ? gZeroRight.y : gZeroRight.z;
    return axis > 2 ? 0 : static_cast<int>(v * 1000.0f);
}

int HandRigWorldRightMm(unsigned int axis)
{
    return axis > 2 ? 0 : gLastWorldRight[axis].load(std::memory_order_relaxed);
}

int HandRigCalibrationYawMilli() { return gCalibYawMilli.load(std::memory_order_relaxed); }
int HandRigLastYawMilli() { return gLastYawMilli.load(std::memory_order_relaxed); }

int HandRigSelectedRightJoint() { return gRightJoint.load(std::memory_order_relaxed); }
int HandRigSelectedLeftJoint() { return gLeftJoint.load(std::memory_order_relaxed); }

DWORD SetHandRigWristDrive(unsigned int enabled)
{
    gWristDrive.store(enabled != 0u, std::memory_order_release);
    Log(std::string("result=0 detail=wrist_drive value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD SetHandRigTurnYaw(int deciDegrees)
{
    if (deciDegrees < -3600 || deciDegrees > 3600) {
        Log("result=1 detail=turn_yaw_out_of_range");
        return 1;
    }
    gModelTurnYawDeciDegrees.store(deciDegrees, std::memory_order_release);
    Log("result=0 detail=turn_yaw deciDegrees=" + std::to_string(deciDegrees));
    return 0;
}

int HandRigTurnYawDeciDegrees()
{
    return gModelTurnYawDeciDegrees.load(std::memory_order_relaxed);
}

unsigned long long HandRigWristAppliedCount()
{
    return gWristApplied.load(std::memory_order_relaxed);
}

unsigned int HandRigWristDriveArmed()
{
    return gWristDrive.load(std::memory_order_acquire) ? 1u : 0u;
}

DWORD SetHandRigControllerDrive(unsigned int enabled)
{
    gControllerDrive.store(enabled != 0u, std::memory_order_release);
    gNoPose.store(0, std::memory_order_relaxed);
    Log(std::string("result=0 detail=controller_drive value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD CalibrateHandRig()
{
    Vec3 r{}, l{};
    const bool haveRight = ControllerWorld(Hand::right, r);
    const bool haveLeft = ControllerWorld(Hand::left, l);
    if (!haveRight || !haveLeft) {
        // Both or neither: a half-calibrated pair would move one hand from a real
        // zero and the other from a stale one, which looks like a broken rig
        // rather than an untracked controller.
        Log("result=refused detail=controller_untracked right=" + std::to_string(haveRight) +
            " left=" + std::to_string(haveLeft));
        return 1;
    }
    // Orientations are captured in the same call, so a wrist turn is measured
    // from the same instant as the displacement and the two cannot disagree.
    Quaternion rr{}, lr{};
    if (ControllerWorldRotation(Hand::right, rr) && ControllerWorldRotation(Hand::left, lr)) {
        gZeroRightRot = rr;
        gZeroLeftRot = lr;
    }
    gZeroRight = r;
    gZeroLeft = l;
    // The yaw the zero was taken at. If this and `handLastYawMilli` differ, the
    // delta is a difference of two differently-oriented frames and not a pure
    // controller movement -- which is H-016's leading suspect.
    gCalibYawMilli.store(gLastYawMilli.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
    gCalibrated.store(true, std::memory_order_release);
    Log("result=0 detail=calibrated");
    return 0;
}

DWORD SetHandRigScalePercent(unsigned int percent)
{
    if (percent < 10u || percent > 400u) { return 1; }
    gScale.store(static_cast<float>(percent) / 100.0f, std::memory_order_relaxed);
    Log("result=0 detail=scale_percent value=" + std::to_string(percent));
    return 0;
}

int HandRigLastRightMillimetres() { return gLastRightMm.load(std::memory_order_relaxed); }
int HandRigLastLeftMillimetres() { return gLastLeftMm.load(std::memory_order_relaxed); }
unsigned long long HandRigNoPoseCount() { return gNoPose.load(std::memory_order_relaxed); }

DWORD SetHandRigCharacterPtr(void* character)
{
    gSelectedCharacter.store(reinterpret_cast<unsigned long long>(character),
                             std::memory_order_release);
    gMatched.store(0, std::memory_order_relaxed);
    gSkipped.store(0, std::memory_order_relaxed);
    Log("result=0 detail=selected_character value=" +
        std::to_string(reinterpret_cast<unsigned long long>(character)));
    return 0;
}

unsigned long long HandRigLastCharacter() { return gLastCharacter.load(std::memory_order_relaxed); }
unsigned long long HandRigMatchedCount() { return gMatched.load(std::memory_order_relaxed); }
unsigned long long HandRigSkippedCount() { return gSkipped.load(std::memory_order_relaxed); }

DWORD HandRigJointCount() { return gTopologyCount.load(std::memory_order_acquire); }

DWORD HandRigJointNamePtr(void* request)
{
    auto* const r = static_cast<NameRequest*>(request);
    if (r == nullptr || r->buffer == nullptr || r->capacity == 0) {
        return 0;
    }
    if (r->index >= gTopologyCount.load(std::memory_order_acquire)) {
        return 0;
    }
    const char* const name = gJointNames[r->index];
    if (name == nullptr) {
        return 0;
    }
    unsigned int written = 0;
    __try {
        while (written + 1 < r->capacity && name[written] != '\0') {
            r->buffer[written] = name[written];
            ++written;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    r->buffer[written] = '\0';
    return written;
}

int HandRigJointParent(unsigned int index)
{
    if (index >= gTopologyCount.load(std::memory_order_acquire)) {
        return -2;
    }
    return gJointParents[index];
}

unsigned long long HandRigForwardedCount() { return gForwarded.load(std::memory_order_relaxed); }
unsigned long long HandRigAppliedCount() { return gApplied.load(std::memory_order_relaxed); }
unsigned long long HandRigRefusedCount() { return gRefused.load(std::memory_order_relaxed); }
DWORD HandRigLastSubtreeSize() { return gLastSubtree.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
