#include "AnimIkTakeover.h"

#include "preyvr/FrameTiming.h"

#include "HandRigTakeover.h"
#include "AimTakeover.h"
#include "WeaponAttachment.h"
#include <mutex>
#include "HeadTrackingHook.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "XrInput.h"
#include "preyvr/AnimIk.h"
#include "preyvr/WeaponRigAlignment.h"
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
constexpr unsigned int kMaxJoints = 768;

std::atomic<bool> gInstalled{false};
std::atomic<ProcessAdikFn> gOriginal{nullptr};
void* gTarget = nullptr;

std::atomic<unsigned int> gMode{0};
std::atomic<int> gTestMm[3]{0, 0, 0};
std::atomic<bool> gDrive{false};
std::mutex gIkMutex;
// Opt-in until authored-basis alignment has been checked across live assets.
bool gAlignWeapon = false; // all alignment state is protected by gIkMutex
weaponrig::Status gAlignStatus = weaponrig::Status::disabled;
weaponrig::Basis gAlignBasis{};
std::uint64_t gAlignGeneration = 0, gAlignSequence = 0, gAlignApplied = 0, gAlignRefused = 0;
animik::CalibrationState gCalibration;
std::atomic<std::uint64_t> gOwnerGeneration{0}, gOwnerCharacter{0}, gUsedSequence{0};
std::atomic<unsigned long long> gNoOwner{0}, gBusy{0};
std::atomic<unsigned int> gSignatureJoints{101};
std::atomic<unsigned int> gHands{1};
std::atomic<int> gReachPercent{100};

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
// **One coherent per-hand record from inside the IK callback.**
//
// Separate latest-value counters cannot reconstruct a transformation chain: by
// the time three of them are read, they may describe three different frames, and
// the whole question here is which term in ONE frame's arithmetic moved. The
// static audit named two candidate paths -- a reticle-derived shared anchor, and
// reach compression retaining animated-shoulder motion -- and no existing counter
// can tell them apart, because `ikGoalMm` publishes only the right hand's final
// model-space goal.
//
// Captured under the lock that already serialises the write, published per hand.
struct IkTraceRecord {
    std::uint64_t stamp = 0;
    std::uint64_t trackingSequence = 0;
    std::uint64_t publishedNs = 0;
    std::uintptr_t owner = 0;
    // The shared anchor. Currently `GameplayPoseFrame::nativeEye`, which holds
    // the native cached RETICLE ray origin rather than a head position -- so a
    // reticle write that moves this moves both hands. Recorded so that claim is
    // measured rather than argued.
    Vec3 anchor{};
    float yaw = 0.0f;
    Pose head{}, grip{};
    Vec3 controllerWorld{};
    Vec3 characterLocation{};
    Vec3 shoulderModel{};
    // The three stages the audit asks to see separated. Raw is the controller's
    // goal before compression; scaled is after ScaleReach, which mixes in
    // (1-k) * shoulder; clamped is after the reach sphere.
    Vec3 goalRaw{}, goalScaled{}, goalClamped{};
    float reachLimit = 0.0f;
    unsigned int reachPercent = 0;
    bool clamped = false;
    bool calibrated = false;
    bool wroteRotation = false;
    bool valid = false;
};
IkTraceRecord gTrace[2]{};
std::atomic<bool> gTraceEnabled{false};
// **The crosstalk fix, switchable so it can be A/B'd by a wearer.**
//
// 1 (default) anchors both hands at the view camera's centre. 0 restores the
// old reticle-derived anchor, which is kept ONLY so the two can be compared in
// a headset -- it is the defect, not a fallback.
std::atomic<bool> gCameraAnchor{true};
std::atomic<unsigned long long> gAnchorCamera{0}, gAnchorReticle{0}, gAnchorMissing{0};

std::atomic<int> gLastGoalMm[3]{0, 0, 0};

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
// The signature identifies an asset, not a live owner. The caller first checks
// the current selected weapon's attachment-manager owner; validate indices anew
// on each matching callback, even when a skeleton pointer is reused.

bool IdentifyRig(std::uint8_t* character)
{
    void* skeletonPtr = nullptr;
    if (!ReadPointer(character + kCharSkeleton, &skeletonPtr) || skeletonPtr == nullptr) {
        return false;
    }
    auto* const skeleton = static_cast<std::uint8_t*>(skeletonPtr);
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
            if (!ReadPointer(limb + kLimbChain, &chain) || chain == nullptr || DynArrayCount(chain) < 4 || DynArrayCount(chain) > kMaxJoints) { continue; }
            int c1 = -1, c2 = -1, c3 = -1, t = 0;
            if (!ReadInt(static_cast<std::uint8_t*>(chain) + 0x10, &c1) ||
                !ReadInt(static_cast<std::uint8_t*>(chain) + 0x20, &c2) ||
                !ReadInt(static_cast<std::uint8_t*>(chain) + 0x30, &c3) ||
                !ReadInt(limb + kLimbTag, &t)) {
                continue;
            }
            for (unsigned int h = 0; h < 2; ++h) {
                if (hand[h] >= 0 && c3 == hand[h] && animik::ValidJoint(c1, jointCount) &&
                    animik::ValidJoint(c2, jointCount) && animik::ValidJoint(c3, jointCount)) {
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
    const bool changed = gRigSkeleton.load() != reinterpret_cast<std::uintptr_t>(skeleton);
    gRigJoints.store(jointCount, std::memory_order_relaxed);
    gRigSkeleton.store(reinterpret_cast<std::uintptr_t>(skeleton), std::memory_order_release);
    if (changed) Log("result=0 detail=rig_identified skeleton=0x" +
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

// --- the write ------------------------------------------------------------------

struct QuatT {
    float q[4];
    float t[3];
};

bool ReadQuatT(std::uint8_t* array, unsigned count, int index, QuatT& out)
{
    if (count>kMaxJoints || !animik::ValidJoint(index, count) ||
        !ReadFloats(array + static_cast<std::size_t>(index) * kQuatTStride, &out.q[0], 7)) { return false; }
    return animik::ValidLocation({{out.q[0], out.q[1], out.q[2], out.q[3]},
                                 {out.t[0], out.t[1], out.t[2]}, 1.0f});
}

// The animation job owns these arrays. Restore a partially attempted edit on
// an access fault, then disarm; do not report a partial target as a solved hand.
bool WriteGoal(std::uint8_t* target, float* weight, const Vec3& position,
               const Quaternion& rotation, bool writeRotation)
{
    QuatT backup{};
    float oldWeight = 0;
    bool haveBackup = false;
    __try {
        std::memcpy(&backup, target, sizeof(backup));
        oldWeight = *weight;
        haveBackup = true;
        std::memcpy(target + 0x10, &position, sizeof(position));
        if (writeRotation) { std::memcpy(target, &rotation, sizeof(rotation)); }
        *weight = 1.0f;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (haveBackup) {
            __try { std::memcpy(target, &backup, sizeof(backup)); *weight = oldWeight; }
            __except (EXCEPTION_EXECUTE_HANDLER) { /* destroyed storage: cannot restore */ }
        }
        return false;
    }
}

bool ReadAlignmentMemory(void*, std::uintptr_t address, void* output, std::size_t size)
{
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void DriveHand(unsigned int hand, std::uint8_t* relative, std::uint8_t* absolute,
               unsigned poseCount, const animik::Location& location, const GameplayPoseFrame& frame,
               const EquippedRig& owner, Vec3* writtenPrimary = nullptr,
               const Vec3* supportPrimary = nullptr, bool* primaryWritten = nullptr)
{
    const int target = gTargetJoint[hand].load(std::memory_order_relaxed);
    const int weight = gWeightJoint[hand].load(std::memory_order_relaxed);
    const int handJoint = gHandJoint[hand].load(std::memory_order_relaxed);
    if (poseCount>kMaxJoints || !animik::ValidJoint(target, poseCount) ||
        !animik::ValidJoint(weight, poseCount) ||
        !animik::ValidJoint(handJoint, poseCount)) { return; }

    QuatT wrist{};
    if (!ReadQuatT(absolute, poseCount, handJoint, wrist)) { return; }

    Vec3 goal{wrist.t[0], wrist.t[1], wrist.t[2]};
    Quaternion rotation{wrist.q[0], wrist.q[1], wrist.q[2], wrist.q[3]};
    bool writeRotation = false;
    bool calibrating = false;
    bool aligned = false;
    Quaternion candidateOffset{};
    SupportGripGeometry supportGeometry{};
    const bool twoHand=frame.weaponGeneration==owner.generation&&frame.twoHand.blend>0&&TwoHandedAimEnabled();
    const bool supportLocked=hand==1&&twoHand&&supportPrimary;

    // Hoisted purely so the trace can see them: the controller values are built
    // inside the drive branch, while the reach maths that consumes the goal sits
    // outside it. Zero when the branch did not run, which the trace's own
    // validity flag already distinguishes from a real zero.
    Vec3 traceGrip{}, traceControllerWorld{};
    if (gDrive.load(std::memory_order_acquire)) {
        const auto& state = frame.tracking.hands[static_cast<unsigned int>(hand == 0 ? Hand::right : Hand::left)];
        // Without a play-space yaw the world placement is unknown, and the
        // camera-relative fallback is the defect body yaw exists to remove.
        if (AimBodyYawEnabled() && !frame.headYawUsable) {
            gNoPose.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!IsPoseUsable(state.gripPose, state.gripValidity, 200000000ull)) {
            gNoPose.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        // **Anchor selection, and the whole point of the fix.** `nativeEye` is the
        // native cached reticle ray origin: our reticle lane moves the reticle,
        // the engine unprojects it, and the result lands here -- so the right
        // controller's aim reaches BOTH hands through this one term. The camera
        // centre does not move with the reticle.
        Vec3 anchor = frame.nativeEye;
        if (gCameraAnchor.load(std::memory_order_acquire)) {
            if (!frame.cameraCentreValid) {
                // Refuse rather than fall back: a silent fall back to nativeEye
                // would reintroduce the defect on exactly the frames where the
                // camera is unreadable, which is the worst place to hide it.
                gAnchorMissing.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            anchor = frame.cameraCentre;
            gAnchorCamera.fetch_add(1, std::memory_order_relaxed);
        } else {
            gAnchorReticle.fetch_add(1, std::memory_order_relaxed);
        }
        Pose world = animik::ControllerWorldFromHead(frame.yaw, anchor,
                                                          frame.tracking.head, state.gripPose);
        if(supportLocked) world.orientation=frame.twoHand.supportOrientation;
        traceGrip = state.gripPose.position;
        traceControllerWorld = world.position;
        goal = animik::WorldToModel(location, world.position);
        if (hand == 0 && gAlignWeapon) {
            gAlignGeneration = owner.generation;
            gAlignSequence = frame.tracking.sequence;
            gAlignBasis = {};
            if (!IsPoseUsable(state.aimPose, state.aimValidity, 200000000ull)) {
                gAlignStatus = weaponrig::Status::staleAim;
                ++gAlignRefused;
                return;
            }
            gAlignStatus = weaponrig::ReadBasis({nullptr, ReadAlignmentMemory},
                reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll")), owner,
                reinterpret_cast<std::uintptr_t>(absolute), poseCount, handJoint, gAlignBasis);
            if (gAlignStatus != weaponrig::Status::ready) { ++gAlignRefused; return; }
            // Both wrist poses are still native here. Convert their separation
            // into the actual barrel basis, never into a controller-at-equip basis.
            QuatT nativeLeft{};
            if(gAlignBasis.source!=weaponrig::Source::wrenchModel&&
               ReadQuatT(absolute,poseCount,gHandJoint[1].load(),nativeLeft)) {
                const Quaternion nativeBarrel=Normalize(Multiply(Multiply(
                    Quaternion{wrist.q[0],wrist.q[1],wrist.q[2],wrist.q[3]},
                    gAlignBasis.weaponInWrist),gAlignBasis.barrelInWeapon));
                const Vec3 delta{(nativeLeft.t[0]-wrist.t[0])*location.s,
                                 (nativeLeft.t[1]-wrist.t[1])*location.s,
                                 (nativeLeft.t[2]-wrist.t[2])*location.s};
                const Vec3 socket=Rotate(Quaternion{-nativeBarrel.x,-nativeBarrel.y,-nativeBarrel.z,nativeBarrel.w},delta);
                if(socket.y>=.16f&&socket.y<=.7f&&std::fabs(socket.x)<.25f&&std::fabs(socket.z)<.25f) {
                    supportGeometry.region.start={socket.x,socket.y-.04f,socket.z};
                    supportGeometry.region.end={socket.x,socket.y+.04f,socket.z};
                    supportGeometry.owner=owner.generation;
                    supportGeometry.reference=frame.referenceGeneration;
                    supportGeometry.epoch=frame.tracking.epoch;
                }
            }
            const Vec3 aimAnchor = (gCameraAnchor.load(std::memory_order_acquire) &&
                                    frame.cameraCentreValid)
                                       ? frame.cameraCentre : frame.nativeEye;
            Pose aimWorld = animik::ControllerWorldFromHead(frame.yaw, aimAnchor,
                frame.tracking.head, state.aimPose);
            if(twoHand) aimWorld.orientation=frame.twoHand.orientation;
            if (!weaponrig::SolveWrist(location.q, aimWorld.orientation, gAlignBasis, rotation)) {
                gAlignStatus = weaponrig::Status::invalidBasis;
                ++gAlignRefused;
                return;
            }
            aligned = true;
            writeRotation = true;
        } else {
            const Quaternion controllerModel = animik::WorldToModel(location, world.orientation);
            calibrating = gCalibration.Pending(hand);
            candidateOffset = calibrating ? animik::CalibrateRotationOffset(controllerModel, rotation)
                                          : gCalibration.offsets[hand];
            if (calibrating || (gCalibration.calibrated & (1u << hand))) {
                rotation = animik::ApplyRotationOffset(controllerModel, candidateOffset);
                writeRotation = true;
            }
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
    if (upper < 0 || mid < 0 || end < 0) { return; }
    {
        QuatT upperAbs{}, midRel{}, endRel{};
        if (ReadQuatT(absolute, poseCount, upper, upperAbs) && ReadQuatT(relative, poseCount, mid, midRel) &&
            ReadQuatT(relative, poseCount, end, endRel)) {
            const float reach = 0.995f * (std::sqrt(midRel.t[0]*midRel.t[0] + midRel.t[1]*midRel.t[1] + midRel.t[2]*midRel.t[2]) +
                                          std::sqrt(endRel.t[0]*endRel.t[0] + endRel.t[1]*endRel.t[1] + endRel.t[2]*endRel.t[2]));
            if (!std::isfinite(reach) || reach <= 1e-6f) { return; }
            const Vec3 shoulder{upperAbs.t[0], upperAbs.t[1], upperAbs.t[2]};
            const Vec3 goalRaw = goal;
            // Compress the player's reach into the character's BEFORE clamping,
            // so a longer-armed player keeps continuous motion instead of dead
            // travel at full extension.
            //
            // **This mixes the shoulder into the goal**, which is a dependency
            // the trace exists to expose: with k = 0.65, a stationary controller
            // still moves the goal by 35% of any shoulder movement. Zero clamping
            // does not mean zero shoulder contribution.
            const unsigned int reachPercent = gReachPercent.load(std::memory_order_relaxed);
            goal = animik::ScaleReach(shoulder, goal, reachPercent / 100.0f);
            if(supportLocked) {
                // The weapon follows the already compressed primary wrist.
                // Compressing the support arm independently would pull it off the gun.
                const Vec3 worldOffset=Rotate(frame.twoHand.orientation,frame.twoHand.socket);
                const Quaternion inverseLocation{-location.q.x,-location.q.y,-location.q.z,location.q.w};
                const Vec3 modelOffset=Rotate(inverseLocation,worldOffset);
                const Vec3 attached{supportPrimary->x+modelOffset.x/location.s,
                                    supportPrimary->y+modelOffset.y/location.s,
                                    supportPrimary->z+modelOffset.z/location.s};
                const float blend=frame.twoHand.blend;
                goal={goal.x+(attached.x-goal.x)*blend,goal.y+(attached.y-goal.y)*blend,goal.z+(attached.z-goal.z)*blend};
            }
            const Vec3 goalScaled = goal;
            const Vec3 clamped = animik::ClampToReach(shoulder, goal, reach);
            const bool didClamp =
                clamped.x != goal.x || clamped.y != goal.y || clamped.z != goal.z;
            if (didClamp) {
                gClamped.fetch_add(1, std::memory_order_relaxed);
                goal = clamped;
            }
            if (gTraceEnabled.load(std::memory_order_acquire) && hand < 2) {
                auto& t = gTrace[hand];
                t.stamp = preyvr::timing::MonotonicNanoseconds();
                t.trackingSequence = frame.tracking.sequence;
                t.publishedNs = frame.publishedNs;
                t.owner = frame.player;
                t.anchor = gCameraAnchor.load(std::memory_order_acquire) && frame.cameraCentreValid
                              ? frame.cameraCentre : frame.nativeEye;
                t.yaw = frame.yaw;
                t.head = frame.tracking.head;
                t.grip.position = traceGrip;
                t.controllerWorld = traceControllerWorld;
                t.characterLocation = location.t;
                t.shoulderModel = shoulder;
                t.goalRaw = goalRaw;
                t.goalScaled = goalScaled;
                t.goalClamped = goal;
                t.reachLimit = reach;
                t.reachPercent = reachPercent;
                t.clamped = didClamp;
                t.calibrated = (gCalibration.calibrated & (1u << hand)) != 0;
                t.wroteRotation = writeRotation;
                t.valid = true;
            }
        } else { return; }
    }
    if (!std::isfinite(goal.x) || !std::isfinite(goal.y) || !std::isfinite(goal.z)) { return; }

    if (aligned) {
        EquippedRig current{};
        // Basis reads can straddle an equip/recenter/focus transition. Refuse
        // the old goal if its ownership/reference sample is no longer current.
        GameplayPoseFrame latest{};
        if (!TryGetEquippedRig(frame.player, current) || current.generation != owner.generation ||
            !SameRigBinding(owner, current, owner.itemId) || !TryGetGameplayPoseFrame(latest, true) ||
            latest.player != frame.player || latest.referenceGeneration != frame.referenceGeneration ||
            latest.tracking.epoch != frame.tracking.epoch) {
            gAlignStatus = weaponrig::Status::invalidOwner;
            ++gAlignRefused;
            return;
        }
    }

    // Target: absolute position (and rotation once calibrated); weight: relative X = 1.
    auto* const targetAt = absolute + static_cast<std::size_t>(target) * kQuatTStride;
    auto* const weightAt = reinterpret_cast<float*>(relative + static_cast<std::size_t>(weight) * kQuatTStride + 0x10);
    if (!WriteGoal(targetAt, weightAt, goal, rotation, writeRotation)) {
        if (aligned) { gAlignStatus = weaponrig::Status::writeFailed; ++gAlignRefused; }
        gMode.store(0);
        gCalibration = {};
        for (auto& flag : gCalibrated) { flag.store(false); }
        Log("result=fault detail=target_write_disarmed");
        return;
    }
    if (aligned) { gAlignStatus = weaponrig::Status::applied; ++gAlignApplied; }
    if(hand==0&&aligned) {
        if(writtenPrimary) *writtenPrimary=goal;
        if(primaryWritten) *primaryWritten=true;
        const Vec3 actual=animik::ModelToWorld(location,goal);
        supportGeometry.primaryOffsetWorld={actual.x-traceControllerWorld.x,
                                          actual.y-traceControllerWorld.y,
                                          actual.z-traceControllerWorld.z};
        supportGeometry.publishedNs=preyvr::timing::MonotonicNanoseconds();
        PublishSupportGripGeometry(supportGeometry); // owner=0 actively withdraws an unsupported region
    }
    if (calibrating) {
        gCalibration.Commit(hand, candidateOffset);
        gCalibrated[hand].store(true);
        Log("result=0 detail=calibrated hand=" + std::to_string(hand));
    }
    gUsedSequence.store(frame.tracking.sequence);
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
    if (mode == 0) {
        const auto original = gOriginal.load(std::memory_order_acquire);
        if (original) { original(character, params); }
        return;
    }
    // Only the owning character needs the IK state. Every other character's
    // animation job used to take the same try-lock and win it against the
    // owner -- measured live 2026-09-07: ikBusy ~105/s while the owner matched
    // ~93/s on a ~132 fps game, a third of its frames skipped, which a wearer
    // would see as the hand flickering between goal and animation. Non-owners
    // pass straight through, except while the owner is unknown or the equipped
    // weapon has changed since it was bound.
    {
        const auto knownOwner = gOwnerCharacter.load(std::memory_order_acquire);
        if (knownOwner != 0 && reinterpret_cast<std::uintptr_t>(character) != knownOwner &&
            WeaponEquipGeneration() == gOwnerGeneration.load(std::memory_order_acquire)) {
            const auto original = gOriginal.load(std::memory_order_acquire);
            if (original) { original(character, params); }
            return;
        }
    }
    std::unique_lock stateLock(gIkMutex, std::try_to_lock);
    GameplayPoseFrame frame{};
    EquippedRig owner{};
    const bool haveOwner = stateLock.owns_lock() && TryGetGameplayPoseFrame(frame, gDrive.load()) &&
        TryGetEquippedRig(frame.player, owner);
    if (stateLock.owns_lock() && gAlignWeapon) {
        gAlignStatus = weaponrig::Status::noSample;
        gAlignBasis = {};
        gAlignGeneration = haveOwner ? owner.generation : 0;
        gAlignSequence = haveOwner ? frame.tracking.sequence : 0;
    }
    if (mode != 0 && !stateLock.owns_lock()) { gBusy.fetch_add(1); }
    if (mode != 0 && stateLock.owns_lock() && !haveOwner) { gNoOwner.fetch_add(1); }
    if (haveOwner && gCalibration.Bind(owner.generation, frame.referenceGeneration, frame.tracking.epoch)) {
        for (auto& flag : gCalibrated) { flag.store(false); }
        gRigSkeleton.store(0);
        gOwnerGeneration.store(owner.generation);
        gOwnerCharacter.store(owner.character);
    }
    if (mode != 0 && haveOwner && owner.character == reinterpret_cast<std::uintptr_t>(character) && params != nullptr) {
        auto* const ch = static_cast<std::uint8_t*>(character);
        if (IdentifyRig(ch)) {
            gMatched.fetch_add(1, std::memory_order_relaxed);
            gCalibration.Tick();   // settles an auto re-take past the equip animation
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
            bool validLocation = false;
            if (ReadFloats(p + kParamsLocQ, &loc[0], 4) && ReadFloats(p + kParamsLocT, &loc[4], 3) &&
                ReadFloats(p + kParamsLocS, &loc[7], 1)) {
                location.q = Quaternion{loc[0], loc[1], loc[2], loc[3]};
                location.t = Vec3{loc[4], loc[5], loc[6]};
                location.s = loc[7];
                validLocation = animik::ValidLocation(location);
            }
            if (validLocation) {
                gLocMm[0].store(static_cast<int>(loc[4] * 1000.0f), std::memory_order_relaxed);
                gLocMm[1].store(static_cast<int>(loc[5] * 1000.0f), std::memory_order_relaxed);
                gLocMm[2].store(static_cast<int>(loc[6] * 1000.0f), std::memory_order_relaxed);
                gLocYawMilli.store(static_cast<int>(animik::YawOf(location.q) * 57295.78f),
                                   std::memory_order_relaxed);
            }
            if (mode == 2 && validLocation && gate != 0 && cvar != 0 && cvar != -1 &&
                HandRigMode() != 2 && !WeaponRotationDriveArmed() && !WeaponOffsetArmed()) {
                void* pose = nullptr;
                void* relative = nullptr;
                void* absolute = nullptr;
                int poseCount=0;
                if (ReadPointer(p + kParamsPose, &pose) && pose != nullptr &&
                    ReadInt(static_cast<std::uint8_t*>(pose)+8,&poseCount) && poseCount>0 && poseCount<=kMaxJoints &&
                    ReadPointer(static_cast<std::uint8_t*>(pose) + kPoseRelative, &relative) &&
                    ReadPointer(static_cast<std::uint8_t*>(pose) + kPoseAbsolute, &absolute) &&
                    relative != nullptr && absolute != nullptr) {
                    const unsigned int hands = gHands.load(std::memory_order_relaxed);
                    Vec3 primaryGoal{};
                    bool primaryWritten=false;
                    if (hands & 1u) {
                        DriveHand(0, static_cast<std::uint8_t*>(relative), static_cast<std::uint8_t*>(absolute),
                                  static_cast<unsigned>(poseCount), location, frame, owner,&primaryGoal,nullptr,&primaryWritten);
                    }
                    if ((hands & 2u) && gMode.load() == 2) {
                        DriveHand(1, static_cast<std::uint8_t*>(relative), static_cast<std::uint8_t*>(absolute),
                                  static_cast<unsigned>(poseCount), location, frame, owner,nullptr,primaryWritten?&primaryGoal:nullptr);
                    }
                }
            }
        }
    }
    if (stateLock.owns_lock()) { stateLock.unlock(); }
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
    if (mode != 0u && (!EnsureGameplayPoseObservation() ||
        SetWeaponAttachmentObserving(1) != 0 || !Install())) { return 2; }
    if (mode == 2u && HandRigMode() == 2u) {
        Log("result=refused detail=hand_rig_mode_2_active");
        return 3;
    }
    if (mode == 2u && (WeaponRotationDriveArmed() || WeaponOffsetArmed())) { return 3; }
    std::lock_guard stateLock(gIkMutex);
    if (mode == 0) {
        gCalibration = {};
        for (auto& flag : gCalibrated) { flag.store(false); }
        gAlignStatus = gAlignWeapon ? weaponrig::Status::noSample : weaponrig::Status::disabled;
        gAlignBasis = {};
        gAlignGeneration = gAlignSequence = 0;
    }
    gMode.store(mode, std::memory_order_release);
    Log("result=0 detail=mode value=" + std::to_string(mode));
    return 0;
}

DWORD SetAnimIkTestOffsetMillimetres(int x, int y, int z)
{
    if (x < -2000 || x > 2000 || y < -2000 || y > 2000 || z < -2000 || z > 2000) { return 1; }
    std::lock_guard stateLock(gIkMutex);
    gTestMm[0].store(x, std::memory_order_relaxed);
    gTestMm[1].store(y, std::memory_order_relaxed);
    gTestMm[2].store(z, std::memory_order_relaxed);
    Log("result=0 detail=test_offset x=" + std::to_string(x) + " y=" + std::to_string(y) + " z=" + std::to_string(z));
    return 0;
}

DWORD SetAnimIkControllerDrive(unsigned int enabled)
{
    std::lock_guard stateLock(gIkMutex);
    if (!enabled) {
        gCalibration = {};
        for (auto& flag : gCalibrated) { flag.store(false); }
        gAlignStatus = gAlignWeapon ? weaponrig::Status::noSample : weaponrig::Status::disabled;
        gAlignBasis = {};
        gAlignGeneration = gAlignSequence = 0;
    }
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
    std::lock_guard stateLock(gIkMutex);
    // The authored right-hand path has no equip-angle zero to capture.
    gCalibration.Request(gHands.load() & (gAlignWeapon ? 2u : 3u));
    Log("result=0 detail=calibration_requested");
    return 0;
}

DWORD SetAnimIkWeaponAlignment(unsigned int enabled)
{
    if (enabled > 1) { return 1; }
    std::lock_guard stateLock(gIkMutex);
    if (gAlignWeapon != (enabled != 0)) {
        // A baseline comparison needs a fresh explicit calibration; an old
        // grip offset must not survive a different orientation ownership mode.
        gCalibration.pending &= ~1u;
        gCalibration.autoPending &= ~1u;
        gCalibration.calibrated &= ~1u;
        gCalibrated[0].store(false);
    }
    gAlignWeapon = enabled != 0;
    gAlignStatus = gAlignWeapon ? weaponrig::Status::noSample : weaponrig::Status::disabled;
    gAlignBasis = {};
    gAlignGeneration = gAlignSequence = 0;
    return 0;
}

std::string AnimIkWeaponAlignmentReport()
{
    std::lock_guard stateLock(gIkMutex);
    return " enabled=" + std::to_string(gAlignWeapon) +
        " status=" + weaponrig::StatusName(gAlignStatus) +
        " basis=" + weaponrig::SourceName(gAlignBasis.source) +
        " helper=" + (gAlignBasis.helper[0] ? std::string(gAlignBasis.helper) : "none") +
        " socket=" + std::to_string(gAlignBasis.socketJoint) +
        " generation=" + std::to_string(gAlignGeneration) +
        " sequence=" + std::to_string(gAlignSequence) +
        " applied=" + std::to_string(gAlignApplied) +
        " refused=" + std::to_string(gAlignRefused);
}

DWORD SetAnimIkJointSignature(unsigned int joints)
{
    if (joints == 0 || joints > kMaxJoints) { return 1; }
    std::lock_guard stateLock(gIkMutex);
    gCalibration.Invalidate();
    for (auto& flag : gCalibrated) { flag.store(false); }
    gSignatureJoints.store(joints, std::memory_order_relaxed);
    gRigSkeleton.store(0, std::memory_order_release);   // re-identify
    Log("result=0 detail=signature joints=" + std::to_string(joints));
    return 0;
}

DWORD SetAnimIkReachPercent(int percent)
{
    if (percent < 50 || percent > 150) { return 1; }
    gReachPercent.store(percent, std::memory_order_relaxed);
    Log("result=0 detail=reach_percent value=" + std::to_string(percent));
    return 0;
}

int AnimIkReachPercent() { return gReachPercent.load(std::memory_order_relaxed); }

DWORD SetAnimIkCameraAnchor(unsigned int enabled)
{
    const bool on = enabled != 0;
    gCameraAnchor.store(on, std::memory_order_release);
    lifecycle::Log(std::string("preyvr_anim_ik result=0 detail=camera_anchor enabled=") +
                   (on ? "1" : "0"));
    return 0;
}
unsigned long long AnimIkAnchorCameraCount() { return gAnchorCamera.load(std::memory_order_relaxed); }
unsigned long long AnimIkAnchorReticleCount() { return gAnchorReticle.load(std::memory_order_relaxed); }
unsigned long long AnimIkAnchorMissingCount() { return gAnchorMissing.load(std::memory_order_relaxed); }

DWORD SetAnimIkTrace(unsigned int enabled)
{
    const bool on = enabled != 0;
    if (!on) {
        std::lock_guard stateLock(gIkMutex);
        gTrace[0] = {};
        gTrace[1] = {};
    }
    gTraceEnabled.store(on, std::memory_order_release);
    lifecycle::Log(std::string("preyvr_ik result=0 detail=trace enabled=") + (on ? "1" : "0"));
    return 0;
}

std::string AnimIkTraceReport()
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (!gTraceEnabled.load(std::memory_order_acquire)) { return " ikTrace=off"; }
    std::lock_guard stateLock(gIkMutex);
    const auto mm = [](float v) { return static_cast<int>(v * 1000.0f); };
    const auto vec = [&out, &mm](const char* name, const Vec3& v) {
        out << ' ' << name << '=' << mm(v.x) << ',' << mm(v.y) << ',' << mm(v.z);
    };
    for (unsigned int hand = 0; hand < 2; ++hand) {
        const auto& t = gTrace[hand];
        const char* const tag = hand == 0 ? "R" : "L";
        out << " ikTrace" << tag << '=' << (t.valid ? "fresh" : "none");
        if (!t.valid) { continue; }
        out << " ikSeq" << tag << '=' << t.trackingSequence
            << " ikOwner" << tag << "=0x" << std::hex << t.owner << std::dec;
        // Millimetres throughout, so a term that moves is visible against terms
        // that do not without reading floats out of a log.
        vec((std::string("ikAnchor") + tag).c_str(), t.anchor);
        vec((std::string("ikHeadPos") + tag).c_str(), t.head.position);
        vec((std::string("ikGrip") + tag).c_str(), t.grip.position);
        vec((std::string("ikCtrlWorld") + tag).c_str(), t.controllerWorld);
        vec((std::string("ikCharLoc") + tag).c_str(), t.characterLocation);
        vec((std::string("ikShoulder") + tag).c_str(), t.shoulderModel);
        vec((std::string("ikGoalRaw") + tag).c_str(), t.goalRaw);
        vec((std::string("ikGoalScaled") + tag).c_str(), t.goalScaled);
        vec((std::string("ikGoalClamped") + tag).c_str(), t.goalClamped);
        out << " ikYawMdeg" << tag << '='
            << static_cast<int>(t.yaw * 57295.779513f)
            << " ikReachLimit" << tag << '=' << mm(t.reachLimit)
            << " ikReachPct" << tag << '=' << t.reachPercent
            << " ikDidClamp" << tag << '=' << (t.clamped ? 1 : 0)
            << " ikCal" << tag << '=' << (t.calibrated ? 1 : 0)
            << " ikRot" << tag << '=' << (t.wroteRotation ? 1 : 0);
    }
    return out.str();
}

DWORD SetAnimIkHands(unsigned int mask)
{
    if (mask == 0 || mask > 3) { return 1; }
    std::lock_guard stateLock(gIkMutex);
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

unsigned long long AnimIkOwnerCharacter() { return gOwnerCharacter.load(); }
unsigned long long AnimIkOwnerGeneration() { return gOwnerGeneration.load(); }
unsigned long long AnimIkPoseSequence() { return gUsedSequence.load(); }
unsigned long long AnimIkNoOwner() { return gNoOwner.load(); }
unsigned long long AnimIkBusy() { return gBusy.load(); }
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
