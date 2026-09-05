#include "HandRigTakeover.h"

#include "HeadTrackingHook.h"
#include "Logger.h"
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
    reference.worldPosition = position;
    out = controller::ControllerPoseInWorld(reference, state.gripPose).position;
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
        const auto displace = [&](int joint, const Vec3& delta) -> unsigned int {
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
            unsigned int n = 0;
            for (unsigned int j = 0; j < count; ++j) {
                if (!tInSubtree[j]) { continue; }
                float* const pos = reinterpret_cast<float*>(
                    tScratch.data() + static_cast<std::size_t>(j) * kQuatTStride + kQuatTPosition);
                if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2])) {
                    return 0;
                }
                // Translation only. Rotations stay exactly as the animator
                // produced them, so this is a displacement rather than a pose --
                // wrist orientation is the next piece, not this one.
                pos[0] += delta.x; pos[1] += delta.y; pos[2] += delta.z;
                ++n;
            }
            return n;
        };

        if (!gControllerDrive.load(std::memory_order_acquire)) {
            // The fixed offset, which is what the first discriminator used.
            const Vec3 fixed{gOffsetX.load(std::memory_order_relaxed),
                             gOffsetY.load(std::memory_order_relaxed),
                             gOffsetZ.load(std::memory_order_relaxed)};
            moved = displace(gJoint.load(std::memory_order_relaxed), fixed);
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
                if (ControllerWorld(Hand::right, world)) {
                    const Vec3 d = WorldDeltaToModel(
                        Vec3{world.x - gZeroRight.x, world.y - gZeroRight.y, world.z - gZeroRight.z},
                        bodyYaw);
                    const Vec3 scaled{d.x * scale, d.y * scale, d.z * scale};
                    moved += displace(gRightJoint.load(std::memory_order_relaxed), scaled);
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
                    moved += displace(gLeftJoint.load(std::memory_order_relaxed), scaled);
                    gLastLeftMm.store(static_cast<int>(
                        std::sqrt(scaled.x*scaled.x + scaled.y*scaled.y + scaled.z*scaled.z) * 1000.0f),
                        std::memory_order_relaxed);
                } else {
                    gNoPose.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        gLastSubtree.store(moved, std::memory_order_relaxed);
    }

    // A private pose-data view: the prefix copied, with the absolute-array pointer
    // aimed at our scratch. The original reads exactly this one field, so a prefix
    // is enough **for these bytes** -- it is not a general CPoseData clone and must
    // not be treated as one.
    std::memcpy(tPoseView.data(), poseData, kPosePrefixBytes);
    *reinterpret_cast<std::uint8_t**>(tPoseView.data() + kPoseAbsoluteArray) = tScratch.data();

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
    gZeroRight = r;
    gZeroLeft = l;
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
