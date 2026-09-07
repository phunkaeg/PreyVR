#include "AnimIkTakeover.h"

#include "HandRigTakeover.h"
#include "HeadTrackingHook.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "XrInput.h"
#include "preyvr/AnimIk.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/VrMath.h"

#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <span>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_anim_ik " + line); }

// R-102. CSkeletonAnim::ProcessAnimationDrivenIK(CCharInstance*, SAnimationPoseModifierParams*).
using ProcessAdikFn = void(__fastcall*)(void*, void*);
constexpr std::uintptr_t kProcessAdikRva = 0x877B50;
// Read from the static image; the gate is exact bytes, fail closed.
constexpr std::array<std::uint8_t, 32> kProcessAdikPrologue{
    0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x41,
    0x55, 0x48, 0x8D, 0xAC, 0x24, 0xE0, 0xFE, 0xFF,
    0xFF, 0x48, 0x81, 0xEC, 0x20, 0x02, 0x00, 0x00,
    0x48, 0x8B, 0x5A, 0x08, 0x4C, 0x8B, 0xE9, 0x48};

// Layout, all from decompiled reads (H-021 report sections 1, 2 and 7).
constexpr std::size_t kCharSkeleton = 0x10;     // CCharInstance -> CDefaultSkeleton*
// CCharInstance+0x610 aliases CSkeletonAnim+0x4D0 (the animation object is at
// character+0x140). Its writers (RE-H021-STATIC-VERIFICATION) make it a
// nonempty-command-buffer predicate -- CryEngine's m_IsAnimPlaying -- and the
// ADIK pass tests it. It says nothing about whether the rig has IK targets;
// that is the skeleton's table at +0x70, read separately below.
constexpr std::size_t kCharAnimPlaying = 0x610;
constexpr std::size_t kSkelJoints = 0x08;       // DynArray<joint>, stride 0xA8, name ptr at +0
constexpr std::size_t kJointStride = 0xA8;
constexpr std::size_t kSkelLimbs = 0x68;        // DynArray<IKLimb>, stride 0x30
constexpr std::size_t kLimbStride = 0x30;
constexpr std::size_t kLimbTag = 0x08;
constexpr std::size_t kLimbChain = 0x18;        // -> records of 0x10, int32 index at +0
constexpr std::size_t kSkelAdik = 0x70;         // DynArray<ADIKTarget>, stride 0x28
constexpr std::size_t kAdikStride = 0x28;
constexpr std::size_t kAdikTarget = 0x08;
constexpr std::size_t kAdikTargetName = 0x10;
constexpr std::size_t kAdikWeight = 0x18;
constexpr std::size_t kParamsPose = 0x08;
constexpr std::size_t kParamsLocQ = 0x14;
constexpr std::size_t kParamsLocT = 0x24;
constexpr std::size_t kParamsLocS = 0x30;
constexpr std::size_t kPoseRelative = 0x10;
constexpr std::size_t kPoseAbsolute = 0x18;
constexpr std::size_t kQuatTStride = 0x1C;
constexpr std::uintptr_t kUseAdikCvarRva = 0x225780C;   // DAT_18225780c, tested != 0 by the pass
constexpr std::uintptr_t kPlayerEyeOrigin = 0x17D4;     // ArkPlayer cached reticle origin (R-012)
constexpr std::uintptr_t kGetPlayerRva = 0x157C990;     // landmark player.get_instance
constexpr unsigned int kMaxJoints = 768;

std::atomic<bool> gInstalled{false};
std::atomic<ProcessAdikFn> gOriginal{nullptr};
void* gTarget = nullptr;

std::atomic<unsigned int> gMode{0};
std::atomic<int> gTestMm[3]{0, 0, 0};
std::atomic<bool> gDrive{false};
std::atomic<bool> gCalibrateRequest{false};
std::atomic<unsigned int> gSignatureJoints{101};
std::atomic<unsigned int> gHands{1};

std::atomic<unsigned long long> gCalls{0};
std::atomic<unsigned long long> gMatched{0};
std::atomic<unsigned long long> gRigSkeleton{0};
std::atomic<unsigned int> gRigJoints{0};
std::atomic<unsigned int> gGate{0};
std::atomic<int> gCvar{-1};
std::atomic<int> gTargetJoint[2]{-1, -1};
std::atomic<int> gWeightJoint[2]{-1, -1};
std::atomic<int> gHandJoint[2]{-1, -1};
std::atomic<int> gLimbUpper[2]{-1, -1};
std::atomic<int> gLimbMid[2]{-1, -1};
std::atomic<int> gLimbEnd[2]{-1, -1};
std::atomic<unsigned int> gLimbTagValue[2]{0, 0};
std::atomic<unsigned long long> gWritten[2]{0, 0};
std::atomic<unsigned long long> gNoPose{0};
std::atomic<unsigned long long> gClamped{0};
std::atomic<bool> gCalibrated[2]{false, false};
std::atomic<int> gLocMm[3]{0, 0, 0};
std::atomic<int> gLocYawMilli{0};
std::atomic<int> gLastGoalMm[3]{0, 0, 0};

// Written from the job thread, read from it too; the calibration request is the
// only cross-thread flag and it is a plain atomic.
Quaternion gOffset[2]{};

// --- raw reads, SEH-guarded, POD only --------------------------------------

unsigned int DynArrayCount(const void* data)
{
    __try {
        if (data == nullptr) { return 0; }
        return *reinterpret_cast<const unsigned int*>(
                   reinterpret_cast<const std::uint8_t*>(data) - 4) & 0x7FFFFFFFu;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool ReadPointer(const void* at, void** out)
{
    __try {
        *out = *reinterpret_cast<void* const*>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadInt(const void* at, int* out)
{
    __try {
        *out = *reinterpret_cast<const int*>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadFloats(const void* at, float* out, unsigned int count)
{
    __try {
        std::memcpy(out, at, count * sizeof(float));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteFloats(void* at, const float* in, unsigned int count)
{
    __try {
        std::memcpy(at, in, count * sizeof(float));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Bounded string compare against engine memory.
bool NameEquals(const char* name, const char* expected)
{
    __try {
        if (name == nullptr) { return false; }
        for (unsigned int i = 0; i < 64; ++i) {
            if (name[i] != expected[i]) { return false; }
            if (expected[i] == '\0') { return true; }
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// POD-only, so the SEH guard is legal: the names are engine memory and may be
// anything at all.
void FormatAdikLine(char* line, std::size_t capacity, unsigned int index, int target,
                    const char* targetName, int weight, const char* weightName)
{
    __try {
        std::snprintf(line, capacity, "adik[%u] target=%d(%s) weight=%d(%s)", index, target,
                      targetName ? targetName : "?", weight, weightName ? weightName : "?");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(line, capacity, "adik[%u] unreadable", index);
    }
}

const char* JointName(const std::uint8_t* skeleton, int index)
{
    void* table = nullptr;
    if (!ReadPointer(skeleton + kSkelJoints, &table) || table == nullptr) { return nullptr; }
    void* name = nullptr;
    if (!ReadPointer(reinterpret_cast<std::uint8_t*>(table) + static_cast<std::size_t>(index) * kJointStride,
                     &name)) {
        return nullptr;
    }
    return static_cast<const char*>(name);
}

int JointIndexByName(const std::uint8_t* skeleton, unsigned int count, const char* expected)
{
    for (unsigned int i = 0; i < count; ++i) {
        if (NameEquals(JointName(skeleton, static_cast<int>(i)), expected)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// --- rig identification -----------------------------------------------------
//
// Keyed on the CDefaultSkeleton, which is the model asset shared by every
// instance of the rig. F-009: instances are recreated on every weapon change,
// so an instance pointer is exactly the wrong key.

bool IdentifyRig(std::uint8_t* character)
{
    void* skeletonPtr = nullptr;
    if (!ReadPointer(character + kCharSkeleton, &skeletonPtr) || skeletonPtr == nullptr) {
        return false;
    }
    auto* const skeleton = static_cast<std::uint8_t*>(skeletonPtr);
    if (reinterpret_cast<std::uintptr_t>(skeleton) == gRigSkeleton.load(std::memory_order_acquire)) {
        return true;
    }
    void* joints = nullptr;
    if (!ReadPointer(skeleton + kSkelJoints, &joints)) { return false; }
    const unsigned int jointCount = DynArrayCount(joints);
    if (jointCount != gSignatureJoints.load(std::memory_order_relaxed) || jointCount > kMaxJoints) {
        return false;
    }
    void* adik = nullptr;
    if (!ReadPointer(skeleton + kSkelAdik, &adik) || adik == nullptr) { return false; }
    const unsigned int adikCount = DynArrayCount(adik);
    if (adikCount == 0 || adikCount > 64) { return false; }

    int target[2] = {-1, -1}, weight[2] = {-1, -1};
    for (unsigned int i = 0; i < adikCount; ++i) {
        auto* const entry = static_cast<std::uint8_t*>(adik) + i * kAdikStride;
        void* name = nullptr;
        if (!ReadPointer(entry + kAdikTargetName, &name)) { continue; }
        int t = -1, w = -1;
        if (!ReadInt(entry + kAdikTarget, &t) || !ReadInt(entry + kAdikWeight, &w)) { continue; }
        if (NameEquals(static_cast<const char*>(name), "r_hand_spine_target")) { target[0] = t; weight[0] = w; }
        if (NameEquals(static_cast<const char*>(name), "l_hand_spine_target")) { target[1] = t; weight[1] = w; }
    }
    if (target[0] < 0 && target[1] < 0) {
        return false;   // the signature is the ADIK target, not the joint count alone
    }
    const int hand[2] = {JointIndexByName(skeleton, jointCount, "r_hand_jnt"),
                         JointIndexByName(skeleton, jointCount, "l_hand_jnt")};

    // The limb the engine will solve: matched by its chain's END joint, which is
    // the fabrication-free way to know the arm is defined at all.
    int upper[2] = {-1, -1}, mid[2] = {-1, -1}, end[2] = {-1, -1};
    unsigned int tag[2] = {0, 0};
    void* limbs = nullptr;
    if (ReadPointer(skeleton + kSkelLimbs, &limbs) && limbs != nullptr) {
        const unsigned int limbCount = DynArrayCount(limbs);
        for (unsigned int i = 0; i < limbCount && i < 32; ++i) {
            auto* const limb = static_cast<std::uint8_t*>(limbs) + i * kLimbStride;
            void* chain = nullptr;
            if (!ReadPointer(limb + kLimbChain, &chain) || chain == nullptr) { continue; }
            int c1 = -1, c2 = -1, c3 = -1, t = 0;
            if (!ReadInt(static_cast<std::uint8_t*>(chain) + 0x10, &c1) ||
                !ReadInt(static_cast<std::uint8_t*>(chain) + 0x20, &c2) ||
                !ReadInt(static_cast<std::uint8_t*>(chain) + 0x30, &c3) ||
                !ReadInt(limb + kLimbTag, &t)) {
                continue;
            }
            for (unsigned int h = 0; h < 2; ++h) {
                if (hand[h] >= 0 && c3 == hand[h]) {
                    upper[h] = c1; mid[h] = c2; end[h] = c3; tag[h] = static_cast<unsigned int>(t);
                }
            }
        }
    }
    for (unsigned int h = 0; h < 2; ++h) {
        gTargetJoint[h].store(target[h], std::memory_order_relaxed);
        gWeightJoint[h].store(weight[h], std::memory_order_relaxed);
        gHandJoint[h].store(hand[h], std::memory_order_relaxed);
        gLimbUpper[h].store(upper[h], std::memory_order_relaxed);
        gLimbMid[h].store(mid[h], std::memory_order_relaxed);
        gLimbEnd[h].store(end[h], std::memory_order_relaxed);
        gLimbTagValue[h].store(tag[h], std::memory_order_relaxed);
    }
    gRigJoints.store(jointCount, std::memory_order_relaxed);
    gRigSkeleton.store(reinterpret_cast<std::uintptr_t>(skeleton), std::memory_order_release);
    Log("result=0 detail=rig_identified skeleton=0x" +
        [&] { char b[32]; std::snprintf(b, sizeof(b), "%llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(skeleton))); return std::string(b); }() +
        " joints=" + std::to_string(jointCount) + " adik=" + std::to_string(adikCount) +
        " rTarget=" + std::to_string(target[0]) + " rWeight=" + std::to_string(weight[0]) +
        " rHand=" + std::to_string(hand[0]) + " rLimb=" + std::to_string(upper[0]) + "/" +
        std::to_string(mid[0]) + "/" + std::to_string(end[0]) +
        " lTarget=" + std::to_string(target[1]) + " lWeight=" + std::to_string(weight[1]) +
        " lHand=" + std::to_string(hand[1]) + " lLimb=" + std::to_string(upper[1]) + "/" +
        std::to_string(mid[1]) + "/" + std::to_string(end[1]));
    return true;
}

// --- the engine's eye point ---------------------------------------------------
//
// FAIL-HAND-037: never the view camera, which alternates by half an IPD per eye.
// The player's cached reticle origin is the single eye point gameplay uses.

bool ReadEyeOrigin(Vec3& out)
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) { return false; }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    using GetPlayerFn = void*(*)();
    void* player = nullptr;
    __try {
        player = reinterpret_cast<GetPlayerFn>(base + kGetPlayerRva)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (player == nullptr) { return false; }
    float v[3] = {0.0f, 0.0f, 0.0f};
    if (!ReadFloats(static_cast<std::uint8_t*>(player) + kPlayerEyeOrigin, v, 3)) { return false; }
    out = Vec3{v[0], v[1], v[2]};
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

float GameCameraYaw()
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) { return 0.0f; }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    auto* const systemPtr = *reinterpret_cast<std::uint8_t**>(base + engine::SystemLayout::pointerRva);
    if (systemPtr == nullptr) { return 0.0f; }
    const auto* const camera = reinterpret_cast<const std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(systemPtr) + engine::SystemLayout::viewCamera);
    const stereo::Matrix34 matrix =
        stereo::ReadMatrix(std::span<const std::uint8_t>(camera, engine::CameraLayout::size));
    return stereo::CameraYawOf(matrix);
}

// --- the write ------------------------------------------------------------------

struct QuatT {
    float q[4];
    float t[3];
};

bool ReadQuatT(std::uint8_t* array, int index, QuatT& out)
{
    return ReadFloats(array + static_cast<std::size_t>(index) * kQuatTStride, &out.q[0], 7);
}

void DriveHand(unsigned int hand, std::uint8_t* relative, std::uint8_t* absolute,
               const animik::Location& location, bool haveEye, const Vec3& eye,
               float yaw, bool haveHead, const Pose& head)
{
    const int target = gTargetJoint[hand].load(std::memory_order_relaxed);
    const int weight = gWeightJoint[hand].load(std::memory_order_relaxed);
    const int handJoint = gHandJoint[hand].load(std::memory_order_relaxed);
    if (target < 0 || weight < 0 || handJoint < 0) { return; }

    QuatT wrist{};
    if (!ReadQuatT(absolute, handJoint, wrist)) { return; }

    Vec3 goal{wrist.t[0], wrist.t[1], wrist.t[2]};
    Quaternion rotation{wrist.q[0], wrist.q[1], wrist.q[2], wrist.q[3]};
    bool writeRotation = false;

    if (gDrive.load(std::memory_order_acquire)) {
        ControllerState state{};
        if (!haveEye || !haveHead || !TryGetControllerState(hand == 0 ? Hand::right : Hand::left, state) ||
            !state.gripValidity.positionTracked || !state.gripValidity.orientationTracked) {
            gNoPose.fetch_add(1, std::memory_order_relaxed);
            return;   // keep the animation rather than guess
        }
        const Pose world = animik::ControllerWorldFromHead(yaw, eye, head, state.gripPose);
        goal = animik::WorldToModel(location, world.position);
        const Quaternion controllerModel = animik::WorldToModel(location, world.orientation);
        if (gCalibrateRequest.load(std::memory_order_acquire)) {
            gOffset[hand] = animik::CalibrateRotationOffset(controllerModel, rotation);
            gCalibrated[hand].store(true, std::memory_order_release);
            if (hand == 0) { gCalibrateRequest.store(false, std::memory_order_release); }
            Log("result=0 detail=calibrated hand=" + std::to_string(hand));
        }
        if (gCalibrated[hand].load(std::memory_order_acquire)) {
            rotation = animik::ApplyRotationOffset(controllerModel, gOffset[hand]);
            writeRotation = true;
        }
    } else {
        const Vec3 test{gTestMm[0].load(std::memory_order_relaxed) * 0.001f,
                        gTestMm[1].load(std::memory_order_relaxed) * 0.001f,
                        gTestMm[2].load(std::memory_order_relaxed) * 0.001f};
        if (test.x == 0.0f && test.y == 0.0f && test.z == 0.0f) { return; }
        goal = Vec3{goal.x + test.x, goal.y + test.y, goal.z + test.z};
    }

    // Reach: the authored bone lengths are the relative translations of the mid
    // and end joints, rebuilt from animation every frame before this runs.
    const int upper = gLimbUpper[hand].load(std::memory_order_relaxed);
    const int mid = gLimbMid[hand].load(std::memory_order_relaxed);
    const int end = gLimbEnd[hand].load(std::memory_order_relaxed);
    if (upper >= 0 && mid >= 0 && end >= 0) {
        QuatT upperAbs{}, midRel{}, endRel{};
        if (ReadQuatT(absolute, upper, upperAbs) && ReadQuatT(relative, mid, midRel) &&
            ReadQuatT(relative, end, endRel)) {
            const float reach = 0.995f * (std::sqrt(midRel.t[0]*midRel.t[0] + midRel.t[1]*midRel.t[1] + midRel.t[2]*midRel.t[2]) +
                                          std::sqrt(endRel.t[0]*endRel.t[0] + endRel.t[1]*endRel.t[1] + endRel.t[2]*endRel.t[2]));
            const Vec3 clamped = animik::ClampToReach(Vec3{upperAbs.t[0], upperAbs.t[1], upperAbs.t[2]}, goal, reach);
            if (clamped.x != goal.x || clamped.y != goal.y || clamped.z != goal.z) {
                gClamped.fetch_add(1, std::memory_order_relaxed);
                goal = clamped;
            }
        }
    }
    if (!std::isfinite(goal.x) || !std::isfinite(goal.y) || !std::isfinite(goal.z)) { return; }

    // Target: absolute position (and rotation once calibrated); weight: relative X = 1.
    float t[3] = {goal.x, goal.y, goal.z};
    auto* const targetAt = absolute + static_cast<std::size_t>(target) * kQuatTStride;
    if (!WriteFloats(targetAt + 0x10, t, 3)) { return; }
    if (writeRotation) {
        float q[4] = {rotation.x, rotation.y, rotation.z, rotation.w};
        WriteFloats(targetAt, q, 4);
    }
    float one = 1.0f;
    WriteFloats(relative + static_cast<std::size_t>(weight) * kQuatTStride + 0x10, &one, 1);
    gWritten[hand].fetch_add(1, std::memory_order_relaxed);
    if (hand == 0) {
        gLastGoalMm[0].store(static_cast<int>(goal.x * 1000.0f), std::memory_order_relaxed);
        gLastGoalMm[1].store(static_cast<int>(goal.y * 1000.0f), std::memory_order_relaxed);
        gLastGoalMm[2].store(static_cast<int>(goal.z * 1000.0f), std::memory_order_relaxed);
    }
}

void __fastcall ProcessAdikWithTakeover(void* character, void* params)
{
    gCalls.fetch_add(1, std::memory_order_relaxed);
    const unsigned int mode = gMode.load(std::memory_order_acquire);
    if (mode != 0 && character != nullptr && params != nullptr) {
        auto* const ch = static_cast<std::uint8_t*>(character);
        if (IdentifyRig(ch)) {
            gMatched.fetch_add(1, std::memory_order_relaxed);
            int gate = 0;
            if (ReadInt(ch + kCharAnimPlaying, &gate)) {
                gGate.store(static_cast<unsigned int>(gate), std::memory_order_relaxed);
            }
            const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
            int cvar = -1;
            if (preyDll != nullptr &&
                ReadInt(reinterpret_cast<std::uint8_t*>(preyDll) + kUseAdikCvarRva, &cvar)) {
                gCvar.store(cvar, std::memory_order_relaxed);
            }
            auto* const p = static_cast<std::uint8_t*>(params);
            float loc[8] = {0};
            animik::Location location{};
            if (ReadFloats(p + kParamsLocQ, &loc[0], 4) && ReadFloats(p + kParamsLocT, &loc[4], 3) &&
                ReadFloats(p + kParamsLocS, &loc[7], 1)) {
                location.q = Quaternion{loc[0], loc[1], loc[2], loc[3]};
                location.t = Vec3{loc[4], loc[5], loc[6]};
                location.s = loc[7] > 0.0f ? loc[7] : 1.0f;
                gLocMm[0].store(static_cast<int>(loc[4] * 1000.0f), std::memory_order_relaxed);
                gLocMm[1].store(static_cast<int>(loc[5] * 1000.0f), std::memory_order_relaxed);
                gLocMm[2].store(static_cast<int>(loc[6] * 1000.0f), std::memory_order_relaxed);
                gLocYawMilli.store(static_cast<int>(animik::YawOf(location.q) * 57295.78f),
                                   std::memory_order_relaxed);
            }
            if (mode == 2) {
                void* pose = nullptr;
                void* relative = nullptr;
                void* absolute = nullptr;
                if (ReadPointer(p + kParamsPose, &pose) && pose != nullptr &&
                    ReadPointer(static_cast<std::uint8_t*>(pose) + kPoseRelative, &relative) &&
                    ReadPointer(static_cast<std::uint8_t*>(pose) + kPoseAbsolute, &absolute) &&
                    relative != nullptr && absolute != nullptr) {
                    Vec3 eye{};
                    const bool haveEye = ReadEyeOrigin(eye);
                    Pose head{};
                    unsigned long long age = 0;
                    const bool haveHead = TryReadHeadPose(head, age);
                    const float yaw = GameCameraYaw() - HeadTrackingReferenceYaw();
                    const unsigned int hands = gHands.load(std::memory_order_relaxed);
                    if (hands & 1u) {
                        DriveHand(0, static_cast<std::uint8_t*>(relative), static_cast<std::uint8_t*>(absolute),
                                  location, haveEye, eye, yaw, haveHead, head);
                    }
                    if (hands & 2u) {
                        DriveHand(1, static_cast<std::uint8_t*>(relative), static_cast<std::uint8_t*>(absolute),
                                  location, haveEye, eye, yaw, haveHead, head);
                    }
                }
            }
        }
    }
    const ProcessAdikFn original = gOriginal.load(std::memory_order_acquire);
    if (original != nullptr) {
        original(character, params);
    }
}

bool Install()
{
    if (gInstalled.load(std::memory_order_acquire)) { return true; }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        Log("result=unavailable detail=no_preydll");
        return false;
    }
    const auto target = reinterpret_cast<std::uintptr_t>(preyDll) + kProcessAdikRva;
    if (std::memcmp(reinterpret_cast<const void*>(target),
                    kProcessAdikPrologue.data(), kProcessAdikPrologue.size()) != 0) {
        Log("result=unavailable detail=adik_prologue_mismatch");
        return false;
    }
    gTarget = reinterpret_cast<void*>(target);
    ProcessAdikFn original = nullptr;
    EnsureMinHook();
    if (MH_CreateHook(gTarget, reinterpret_cast<void*>(&ProcessAdikWithTakeover),
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
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hook_installed target=ProcessAnimationDrivenIK rva=0x877B50");
    return true;
}

} // namespace

DWORD SetAnimIkMode(unsigned int mode)
{
    if (mode > 2u) { return 1; }
    if (mode != 0u && !Install()) { return 2; }
    if (mode == 2u && HandRigMode() == 2u) {
        Log("warn=hand_rig_mode_2_also_active detail=controller_applied_twice");
    }
    gMode.store(mode, std::memory_order_release);
    Log("result=0 detail=mode value=" + std::to_string(mode));
    return 0;
}

DWORD SetAnimIkTestOffsetMillimetres(int x, int y, int z)
{
    if (x < -2000 || x > 2000 || y < -2000 || y > 2000 || z < -2000 || z > 2000) { return 1; }
    gTestMm[0].store(x, std::memory_order_relaxed);
    gTestMm[1].store(y, std::memory_order_relaxed);
    gTestMm[2].store(z, std::memory_order_relaxed);
    Log("result=0 detail=test_offset x=" + std::to_string(x) + " y=" + std::to_string(y) + " z=" + std::to_string(z));
    return 0;
}

DWORD SetAnimIkControllerDrive(unsigned int enabled)
{
    gDrive.store(enabled != 0u, std::memory_order_release);
    Log(std::string("result=0 detail=controller_drive value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD CalibrateAnimIk()
{
    if (!gDrive.load(std::memory_order_acquire)) {
        Log("result=refused detail=drive_off");
        return 1;
    }
    gCalibrateRequest.store(true, std::memory_order_release);
    Log("result=0 detail=calibration_requested");
    return 0;
}

DWORD SetAnimIkJointSignature(unsigned int joints)
{
    if (joints == 0 || joints > kMaxJoints) { return 1; }
    gSignatureJoints.store(joints, std::memory_order_relaxed);
    gRigSkeleton.store(0, std::memory_order_release);   // re-identify
    Log("result=0 detail=signature joints=" + std::to_string(joints));
    return 0;
}

DWORD SetAnimIkHands(unsigned int mask)
{
    if (mask == 0 || mask > 3) { return 1; }
    gHands.store(mask, std::memory_order_relaxed);
    Log("result=0 detail=hands mask=" + std::to_string(mask));
    return 0;
}

DWORD DumpAnimIk()
{
    const auto skeleton = reinterpret_cast<std::uint8_t*>(gRigSkeleton.load(std::memory_order_acquire));
    if (skeleton == nullptr) {
        Log("result=refused detail=no_rig_identified");
        return 1;
    }
    void* adik = nullptr;
    if (ReadPointer(skeleton + kSkelAdik, &adik) && adik != nullptr) {
        const unsigned int count = DynArrayCount(adik);
        for (unsigned int i = 0; i < count && i < 64; ++i) {
            auto* const entry = static_cast<std::uint8_t*>(adik) + i * kAdikStride;
            int t = -1, w = -1;
            void* tn = nullptr;
            void* wn = nullptr;
            ReadInt(entry + kAdikTarget, &t);
            ReadInt(entry + kAdikWeight, &w);
            ReadPointer(entry + kAdikTargetName, &tn);
            ReadPointer(entry + 0x20, &wn);
            char line[256];
            FormatAdikLine(line, sizeof(line), i, t, static_cast<const char*>(tn), w,
                           static_cast<const char*>(wn));
            Log(line);
        }
    }
    void* limbs = nullptr;
    if (ReadPointer(skeleton + kSkelLimbs, &limbs) && limbs != nullptr) {
        const unsigned int count = DynArrayCount(limbs);
        for (unsigned int i = 0; i < count && i < 32; ++i) {
            auto* const limb = static_cast<std::uint8_t*>(limbs) + i * kLimbStride;
            void* chain = nullptr;
            int c0 = -1, c1 = -1, c2 = -1, c3 = -1, tag = 0;
            ReadInt(limb + kLimbTag, &tag);
            if (ReadPointer(limb + kLimbChain, &chain) && chain != nullptr) {
                ReadInt(static_cast<std::uint8_t*>(chain) + 0x00, &c0);
                ReadInt(static_cast<std::uint8_t*>(chain) + 0x10, &c1);
                ReadInt(static_cast<std::uint8_t*>(chain) + 0x20, &c2);
                ReadInt(static_cast<std::uint8_t*>(chain) + 0x30, &c3);
            }
            char line[160];
            std::snprintf(line, sizeof(line), "limb[%u] tag=0x%08x chain=%d/%d/%d/%d", i,
                          static_cast<unsigned int>(tag), c0, c1, c2, c3);
            Log(line);
        }
    }
    Log("result=0 detail=dumped");
    return 0;
}

unsigned int AnimIkMode() { return gMode.load(std::memory_order_relaxed); }
unsigned int AnimIkHooked() { return gInstalled.load(std::memory_order_relaxed) ? 1u : 0u; }
unsigned long long AnimIkCalls() { return gCalls.load(std::memory_order_relaxed); }
unsigned long long AnimIkMatched() { return gMatched.load(std::memory_order_relaxed); }
unsigned long long AnimIkRigSkeleton() { return gRigSkeleton.load(std::memory_order_relaxed); }
unsigned int AnimIkRigJoints() { return gRigJoints.load(std::memory_order_relaxed); }
unsigned int AnimIkGate() { return gGate.load(std::memory_order_relaxed); }
int AnimIkCvar() { return gCvar.load(std::memory_order_relaxed); }
int AnimIkTargetJoint(unsigned int hand) { return hand < 2 ? gTargetJoint[hand].load(std::memory_order_relaxed) : -1; }
int AnimIkWeightJoint(unsigned int hand) { return hand < 2 ? gWeightJoint[hand].load(std::memory_order_relaxed) : -1; }
int AnimIkLimbEnd(unsigned int hand) { return hand < 2 ? gLimbEnd[hand].load(std::memory_order_relaxed) : -1; }
unsigned int AnimIkLimbTag(unsigned int hand) { return hand < 2 ? gLimbTagValue[hand].load(std::memory_order_relaxed) : 0; }
unsigned long long AnimIkWritten(unsigned int hand) { return hand < 2 ? gWritten[hand].load(std::memory_order_relaxed) : 0; }
unsigned long long AnimIkNoPose() { return gNoPose.load(std::memory_order_relaxed); }
unsigned long long AnimIkClamped() { return gClamped.load(std::memory_order_relaxed); }
unsigned int AnimIkCalibrated(unsigned int hand) { return hand < 2 && gCalibrated[hand].load(std::memory_order_relaxed) ? 1u : 0u; }
int AnimIkLocationMillimetres(unsigned int axis) { return axis < 3 ? gLocMm[axis].load(std::memory_order_relaxed) : 0; }
int AnimIkLocationYawMilliDegrees() { return gLocYawMilli.load(std::memory_order_relaxed); }
int AnimIkLastGoalMillimetres(unsigned int axis) { return axis < 3 ? gLastGoalMm[axis].load(std::memory_order_relaxed) : 0; }

} // namespace preyvr::dll
