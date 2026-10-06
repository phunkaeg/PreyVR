#include "AnimIkTakeover.h"

#include "preyvr/FrameTiming.h"

#include "HandRigTakeover.h"
#include "AimTakeover.h"
#include "WeaponAttachment.h"
#include "PhysicalInteractions.h"
#include <mutex>
#include "HeadTrackingHook.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "XrInput.h"
#include "preyvr/AnimIk.h"
#include "preyvr/ArmPose.h"
#include "preyvr/HandPose.h"
#include "preyvr/WeaponRigAlignment.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/VrMath.h"

#include <MinHook.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>
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

// --- VR hand pose (pose.*) ------------------------------------------------------
//
// **The hand is where the player's hand is, shaped like a hand that holds
// nothing unless it holds the weapon.** Changes over the original lane, each
// switchable for A/B (pose.mode 0 = all of them off; docs/HAND-POSE-2026-10-06.md):
//
//  1. Orientation of the free (left) hand is ANATOMICAL: the game hand's own
//     frame (from its knuckles) is mapped onto the player's real hand frame,
//     which the OpenXR grip pose defines. The original captured "controller ->
//     animated wrist" at one instant and kept it, so the hand's angle to the
//     controller depended on what the weapon animation was doing then.
//  2. The PALM, not the wrist, goes to the controller: the IK target is the
//     wrist joint, and the grip pose's origin is in the middle of the fist, so
//     the wrist goal is the controller minus the palm offset (both hands).
//  3. The free hand's fingers take one relaxed open pose (handpose::Relaxed*),
//     and while the left grip holds a two-handed weapon the hand takes the
//     NATIVE two-handed hold instead -- the animation's own left hand on the
//     weapon, position and rotation carried rigidly with the right wrist.
//  4. A held weapon keeps its barrel on the aim ray but is ROLLED about it
//     until the hand holding it lies like the real hand (pose.roll): the
//     muzzle helper's authored roll held the Boltcaster upside down.
//  5. Reach: exact up to a knee, then a smooth approach (pose.reach), to an
//     arm the native solve may lengthen by pose.stretch -- instead of the
//     original 65% linear compression, which moved every hand off its
//     controller even at rest.
struct PoseSettings {
    unsigned mode = 1;          // 0 original, 1 hand pose
    float curl = 1.0f;          // relaxed flexion scale
    float tubeLean = 15.0f * 3.14159265f / 180.0f;
    handpose::GripPoint point{};
    unsigned reachMode = 1;     // 0 linear percent (original), 1 exact to the knee then soft
    float knee = 0.85f;
    bool weaponRoll = true;     // roll a held weapon about its barrel to fit the real hand
    float stretch = 1.20f;      // reach limit over the rig's arm length (native solve allows 1.25)
    // The arm (arm.*, docs/ARM-POSE-2026-10-06.md): from the player's shoulder.
    unsigned armMode = 1;       // 0: the native arm, hung from the animated shoulder
    armpose::Body body{};
    float torsoDeadzone = 35.0f * 3.14159265f / 180.0f;
    float torsoRelax = 0.6f;    // fraction per second towards the head's yaw
    float armKnee = 0.92f;      // exact reach up to this fraction of the stretched arm
    float poleTau = 0.06f;      // elbow pole smoothing, seconds
    armpose::WristLimits wrist{};
};
std::mutex gPoseSettingsMutex;
PoseSettings gPoseSettings;
PoseSettings ReadPoseSettings() { std::lock_guard lock(gPoseSettingsMutex); return gPoseSettings; }

// The hand's joints and frames on the identified rig. Rebuilt when the rig
// changes; read and written under gIkMutex.
struct HandRig {
    std::uintptr_t skeleton = 0;
    bool valid[2]{};
    int wrist[2]{-1, -1};
    int joint[2][handpose::kJointCount]{};
    handpose::Frame frame[2]{};
    float knuckle[2]{};
    // For pose.marks only: the arm and head the IK does not drive (-1 if absent).
    int clavicle[2]{-1, -1}, upperArm[2]{-1, -1}, lowerArm[2]{-1, -1};
    int neck = -1, head = -1, headEnd = -1;
    int handProp[2]{-1, -1};
    // Carried rigidly with the upper arm (top, twist, muscle) and the forearm (twist).
    int upperChild[2][3]{{-1, -1, -1}, {-1, -1, -1}};
    int lowerTwist[2]{-1, -1};
};
HandRig gHandRig;


// Per callback: what the drive decided for the fingers, consumed around the
// native pass.
struct FingerPlan {
    bool apply = false;
    float weight = 0.0f;        // 1 relaxed, 0 native
    float support = 0.0f;       // the native two-handed hold's blend
};
std::atomic<int> gWeaponRollMilliDeg{0};   // last roll correction, for the report

// What the last owned frame looked like, for pose.marks / pose.report. All
// positions in the app's OpenXR tracking space, so they compare directly with
// what the runtime reports and what the mock draws.
struct HandMarks {
    bool valid = false;
    Vec3 joint[2][handpose::kJointCount]{};
    Vec3 tip[2][handpose::kJointCount]{};   // only for the distal joints
    Vec3 wrist[2]{};
    Vec3 gripPoint[2]{};                    // the game palm's grip point
    Vec3 length[2]{}, palm[2]{};            // the game hand's frame
    Pose grip[2]{}, aim[2]{};               // what the runtime reported
    bool posed[2]{};                        // pose.mode applied to this hand
    // The arm as drawn, and what the drive asked of it: the wrist goal before
    // any reach shaping (palm on the controller) and the one written.
    Vec3 clavicle[2]{}, shoulder[2]{}, elbow[2]{};
    bool arm[2]{};
    Vec3 goalRaw[2]{}, goalWritten[2]{};
    bool goal[2]{};
    Vec3 neck{}, headJoint{}, headEnd{};
    bool headValid = false;
    Pose head{};                            // the HMD, same space
    float support = 0.0f;
    std::uint64_t stamp = 0;
};
std::mutex gMarksMutex;
HandMarks gMarks;
// DriveHand -> CaptureMarks, same callback and thread: the wrist goals in
// model space (before reach shaping, and as written).
struct MarkGoal { bool set = false; Vec3 raw{}, written{}; };
MarkGoal gMarkGoal[2];

int ParentOf(const std::uint8_t* skeleton, int index)
{
    void* table = nullptr;
    int field = -1;
    if (!ReadPointer(skeleton + kSkelJoints, &table) || table == nullptr ||
        !ReadInt(static_cast<std::uint8_t*>(table) + static_cast<std::size_t>(index) * kJointStride + 0x18, &field)) {
        return -2;
    }
    // Low 16 bits: the parent (0xFFFF = none); high 16 bits: the depth.
    const int parent = field & 0xFFFF;
    return parent == 0xFFFF ? -1 : parent;
}

// Under gIkMutex. Names identify the joints; the joint table's parent field
// must agree with the expected chain or the hand is left alone.
void BuildHandRig(std::uint8_t* skeleton, unsigned jointCount, std::uint8_t* relative, unsigned poseCount)
{
    HandRig rig{};
    rig.skeleton = reinterpret_cast<std::uintptr_t>(skeleton);
    for (int s = 0; s < 2; ++s) {
        const auto side = s == 1 ? handpose::Side::left : handpose::Side::right;
        rig.wrist[s] = JointIndexByName(skeleton, jointCount, handpose::WristName(side));
        bool ok = rig.wrist[s] >= 0;
        for (int j = 0; ok && j < handpose::kJointCount; ++j) {
            rig.joint[s][j] = JointIndexByName(skeleton, jointCount, handpose::JointName(side, j));
            const int role = handpose::ParentRole(j);
            const int expected = role < 0 ? rig.wrist[s] : rig.joint[s][role];
            ok = rig.joint[s][j] >= 0 && static_cast<unsigned>(rig.joint[s][j]) < poseCount &&
                 ParentOf(skeleton, rig.joint[s][j]) == expected;
        }
        if (!ok) { continue; }
        const int bases[4] = {handpose::kIndexBase, handpose::kMiddleBase, handpose::kRingBase, handpose::kPinkyBase};
        Vec3 baseOffset[4], firstOffset[4];
        Quaternion baseRotation[4];
        for (int f = 0; f < 4 && ok; ++f) {
            QuatT base{}, first{};
            ok = ReadQuatT(relative, poseCount, rig.joint[s][bases[f]], base) &&
                 ReadQuatT(relative, poseCount, rig.joint[s][bases[f] + 1], first);
            baseOffset[f] = {base.t[0], base.t[1], base.t[2]};
            firstOffset[f] = {first.t[0], first.t[1], first.t[2]};
            baseRotation[f] = handpose::RelaxedRelative(side, bases[f], 1.0f);
        }
        rig.valid[s] = ok && handpose::MeasureHandFrame(side, baseOffset, baseRotation, firstOffset,
                                                         rig.frame[s], rig.knuckle[s]);
    }
    const auto armJoint = [&](const char* name) {
        const int j = JointIndexByName(skeleton, jointCount, name);
        return j >= 0 && static_cast<unsigned>(j) < poseCount ? j : -1;
    };
    rig.clavicle[0] = armJoint("r_clavicle_jnt"); rig.clavicle[1] = armJoint("l_clavicle_jnt");
    rig.upperArm[0] = armJoint("r_upperArm_jnt"); rig.upperArm[1] = armJoint("l_upperArm_jnt");
    rig.lowerArm[0] = armJoint("r_lowerArm_jnt"); rig.lowerArm[1] = armJoint("l_lowerArm_jnt");
    rig.neck = armJoint("neck_jnt"); rig.head = armJoint("head_jnt"); rig.headEnd = armJoint("headEnd_jnt");
    rig.handProp[0] = armJoint("r_handProp_jnt"); rig.handProp[1] = armJoint("l_handProp_jnt");
    for (int s = 0; s < 2; ++s) {
        const std::string p = s == 0 ? "r_" : "l_";
        rig.upperChild[s][0] = armJoint((p + "upperArmTop_jnt").c_str());
        rig.upperChild[s][1] = armJoint((p + "upperArmTwist_jnt").c_str());
        rig.upperChild[s][2] = armJoint((p + "upperArmMuscle_jnt").c_str());
        rig.lowerTwist[s] = armJoint((p + "lowerArmTwist_jnt").c_str());
    }
    gHandRig = rig;
    char line[256];
    std::snprintf(line, sizeof(line),
        "result=0 detail=hand_rig right=%d left=%d knuckleR=%.4f knuckleL=%.4f "
        "lenL=%.3f,%.3f,%.3f palmL=%.3f,%.3f,%.3f",
        rig.valid[0] ? 1 : 0, rig.valid[1] ? 1 : 0, rig.knuckle[0], rig.knuckle[1],
        rig.frame[1].length.x, rig.frame[1].length.y, rig.frame[1].length.z,
        rig.frame[1].palm.x, rig.frame[1].palm.y, rig.frame[1].palm.z);
    Log(line);
}

Vec3 Sub3(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Add3(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Scale3(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Quaternion Conj(Quaternion q) { return {-q.x, -q.y, -q.z, q.w}; }

// Engine world -> the app's OpenXR space: the inverse of ControllerWorldFromHead.
Vec3 WorldToOpenXr(const GameplayPoseFrame& frame, const Vec3& anchor, const Vec3& world)
{
    const Vec3 engine = Rotate(Conj(stereo::YawQuaternion(frame.yaw)), Sub3(world, anchor));
    return Add3(frame.tracking.head.position, Rotate(Conj(stereo::kOpenXrToEngine), engine));
}

// The wrist rotation (engine world) that puts the game hand's anatomical frame
// on the player's, given the controller's grip orientation in engine world.
Quaternion AnatomicalWrist(unsigned hand, const Quaternion& gripWorld, const PoseSettings& settings)
{
    const auto side = hand == 1 ? handpose::Side::left : handpose::Side::right;
    const Quaternion handInGrip = handpose::FrameToFrame(gHandRig.frame[hand],
                                                         handpose::GripHandFrame(side, settings.tubeLean));
    return Normalize(Multiply(Multiply(gripWorld, stereo::kOpenXrToEngine), handInGrip));
}

// The grip point (middle of the fist) in the wrist's local frame, model units.
Vec3 GripPointLocal(unsigned hand, const PoseSettings& settings)
{
    return handpose::GripPointInWrist(gHandRig.frame[hand], gHandRig.knuckle[hand], settings.point);
}

// --- VR arm (arm.*) ---------------------------------------------------------------
//
// **The arm hangs from the player's shoulder, not the weapon animation's.**
// The native two-bone pass solves from the animated upper-arm joint, which the
// weapon's idle puts 4-16 cm from where the player's shoulder is (the GLOO
// protracts one clavicle and retracts the other; the pistol pulls the right
// shoulder back and in). Measured on the mock with an anatomical reference arm:
// hands short of the controller at full extension, elbows 10-30 cm off.
//
// Before the pass, the player's shoulder is placed (armpose: torso yaw from the
// HMD, an adult's glenohumeral centre, reached by rotating the rig's clavicle)
// and the hand's reach is measured from there. After the pass -- which keeps
// recomputing the animated shoulder from the clavicle, so it cannot be moved
// before -- the arm is rewritten in the absolute pose: clavicle, upper arm,
// elbow (the most likely one for the player's REAL hand, from the controller's
// grip pose: armpose::ChooseElbowPole), forearm, and the hand on its goal with
// everything it carries. Measured: the skinned mesh and the held weapon follow
// absolute writes made after the pass. On the mock, against arms whose elbow is
// known: shoulders on target, palms 0 cm, elbows 0.2-6.7 cm with every weapon
// (the native arm: shoulders 4-16 cm, elbows 15-21 cm with the pistol).
struct ArmPlan {
    bool valid = false;
    armpose::TorsoFrame torso{};
    Vec3 clavicleXr{}, targetXr{}, shoulderXr{};
    Vec3 shoulderModel{};
    float reachModel = 0.0f;    // the stretched arm's reach, model units
    bool goalSet = false;       // DriveHand wrote this wrist goal (model)
    Vec3 goal{};
};

// What the last solve did, for arm.report and pose.marks.
struct ArmStats {
    bool solved[2]{};
    Vec3 target[2]{}, shoulder[2]{}, elbow[2]{};   // OpenXR
    float handMoveCm[2]{};      // how far the native pass left the hand from its goal
    float scale[2]{};           // segment stretch applied
    bool straight[2]{};
    float torsoYawDeg = 0.0f, headYawDeg = 0.0f;
};
std::mutex gArmMutex;           // the torso filter, pole smoothing and stats
armpose::TorsoYaw gTorsoYaw;
std::uint64_t gTorsoNs = 0, gPoleNs[2]{};
Vec3 gPole[2]{};
ArmStats gArmStats;

// Model <-> the app's OpenXR space, through the same anchor the hands use.
struct XrMap {
    const GameplayPoseFrame* frame = nullptr;
    Vec3 anchor{};
    animik::Location location{};
    Vec3 ToXr(const Vec3& model) const
    {
        return WorldToOpenXr(*frame, anchor, animik::ModelToWorld(location, model));
    }
    Vec3 ToModel(const Vec3& xr) const
    {
        const Vec3 engine = Rotate(stereo::kOpenXrToEngine, Sub3(xr, frame->tracking.head.position));
        return animik::WorldToModel(location, Add3(anchor, Rotate(stereo::YawQuaternion(frame->yaw), engine)));
    }
};

bool ArmAnchor(const GameplayPoseFrame& frame, Vec3& anchor)
{
    if (gCameraAnchor.load(std::memory_order_acquire)) {
        if (!frame.cameraCentreValid) { return false; }
        anchor = frame.cameraCentre;
        return true;
    }
    anchor = frame.nativeEye;
    return true;
}

float Dist3(Vec3 a, Vec3 b)
{
    const Vec3 d = Sub3(a, b);
    return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}

// Before the native pass, under gIkMutex: the torso and each shoulder.
void PrepareArms(const GameplayPoseFrame& frame, const animik::Location& location, std::uint8_t* absolute,
                 unsigned count, const PoseSettings& pose, ArmPlan plan[2])
{
    if (pose.mode != 1 || pose.armMode == 0 || gHandRig.skeleton != gRigSkeleton.load()) { return; }
    XrMap map{&frame, {}, location};
    if (!ArmAnchor(frame, map.anchor) || !(location.s > 1e-4f)) { return; }
    const Pose& head = frame.tracking.head;
    const float headYaw = armpose::HeadYaw(head.orientation);
    armpose::TorsoFrame torso;
    {
        std::lock_guard lock(gArmMutex);
        const std::uint64_t now = preyvr::timing::MonotonicNanoseconds();
        const float dt = gTorsoNs != 0 ? static_cast<float>(now - gTorsoNs) * 1e-9f : 0.0f;
        gTorsoNs = now;
        const float yaw = gTorsoYaw.Update(headYaw, dt, pose.torsoDeadzone, pose.torsoRelax);
        torso = armpose::TorsoFromYaw(yaw);
        gArmStats.torsoYawDeg = yaw * 57.29578f;
        gArmStats.headYawDeg = headYaw * 57.29578f;
    }
    const armpose::Body& body = pose.body;
    for (int s = 0; s < 2; ++s) {
        QuatT clavicle{}, upper{};
        if (!ReadQuatT(absolute, count, gHandRig.clavicle[s], clavicle) ||
            !ReadQuatT(absolute, count, gHandRig.upperArm[s], upper)) { continue; }
        ArmPlan& p = plan[s];
        p.torso = torso;
        p.clavicleXr = map.ToXr(Vec3{clavicle.t[0], clavicle.t[1], clavicle.t[2]});
        const Vec3 animated = map.ToXr(Vec3{upper.t[0], upper.t[1], upper.t[2]});
        p.targetXr = armpose::ShoulderTarget(head.position, torso, s == 0 ? 1 : -1, body);
        p.shoulderXr = armpose::PlaceShoulder(p.clavicleXr, Dist3(animated, p.clavicleXr), p.targetXr,
                                              body.clavicleStretch);
        p.shoulderModel = map.ToModel(p.shoulderXr);
        p.reachModel = 0.995f * (body.upper + body.fore) * body.armStretch / location.s;
        p.valid = std::isfinite(p.shoulderModel.x) && std::isfinite(p.shoulderModel.y) &&
                  std::isfinite(p.shoulderModel.z);
    }
}

// --- skeleton dump (ik.skel) ------------------------------------------------------
//
// One-shot research record of the owning rig: every joint's name, the joint
// table's candidate parent fields, the parent DERIVED from the pose itself
// (the p whose abs * rel[j] lands on abs[j]), and the relative and absolute
// pose before and after the engine's ADIK pass. It is what the hand-pose work
// is designed from; the derived parent does not trust any table offset.
std::atomic<int> gSkelDump{0};   // 1 = requested, consumed by the next matched frame

void CopyName(const char* name, char* out, std::size_t capacity)
{
    __try {
        std::size_t i = 0;
        for (; name != nullptr && i + 1 < capacity && name[i] != '\0'; ++i) { out[i] = name[i]; }
        out[i] = '\0';
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(out, capacity, "?");
    }
}

void DumpSkeletonPose(std::uint8_t* character, std::uint8_t* relative, const std::uint8_t* before,
                      std::uint8_t* absolute, unsigned count)
{
    void* skeletonPtr = nullptr;
    if (!ReadPointer(character + kCharSkeleton, &skeletonPtr) || skeletonPtr == nullptr) { return; }
    auto* const skeleton = static_cast<std::uint8_t*>(skeletonPtr);
    void* table = nullptr;
    ReadPointer(skeleton + kSkelJoints, &table);
    Log("skel begin joints=" + std::to_string(count));
    for (unsigned j = 0; j < count && j < kMaxJoints; ++j) {
        QuatT rel{}, abs{}, pre{};
        ReadFloats(relative + j * kQuatTStride, &rel.q[0], 7);
        ReadFloats(absolute + j * kQuatTStride, &abs.q[0], 7);
        std::memcpy(&pre, before + j * kQuatTStride, sizeof(pre));
        int fields[6]{-9, -9, -9, -9, -9, -9};
        if (table != nullptr) {
            for (int k = 0; k < 6; ++k) {
                ReadInt(static_cast<std::uint8_t*>(table) + j * kJointStride + 8 + 4 * k, &fields[k]);
            }
        }
        // abs[p] * rel[j] == abs[j], judged on the pre-ADIK pose (consistent by construction).
        int parent = -1;
        float best = 1e9f;
        for (unsigned p = 0; p < count; ++p) {
            if (p == j) { continue; }
            QuatT a{};
            std::memcpy(&a, before + p * kQuatTStride, sizeof(a));
            const Vec3 r = Rotate(Quaternion{a.q[0], a.q[1], a.q[2], a.q[3]}, Vec3{rel.t[0], rel.t[1], rel.t[2]});
            const float dx = a.t[0] + r.x - pre.t[0], dy = a.t[1] + r.y - pre.t[1], dz = a.t[2] + r.z - pre.t[2];
            const Quaternion q = Multiply(Quaternion{a.q[0], a.q[1], a.q[2], a.q[3]},
                                          Quaternion{rel.q[0], rel.q[1], rel.q[2], rel.q[3]});
            const float dq = std::fabs(q.x * pre.q[0] + q.y * pre.q[1] + q.z * pre.q[2] + q.w * pre.q[3]);
            const float err = std::sqrt(dx * dx + dy * dy + dz * dz) + (1.0f - dq);
            if (err < best) { best = err; parent = static_cast<int>(p); }
        }
        char name[64];
        CopyName(JointName(skeleton, static_cast<int>(j)), name, sizeof(name));
        char line[512];
        std::snprintf(line, sizeof(line),
            "skel j=%u name=%s f=%d,%d,%d,%d,%d,%d parent=%d perr=%.5f "
            "rel=%.4f,%.4f,%.4f,%.4f|%.4f,%.4f,%.4f pre=%.4f,%.4f,%.4f,%.4f|%.4f,%.4f,%.4f "
            "abs=%.4f,%.4f,%.4f,%.4f|%.4f,%.4f,%.4f",
            j, name, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], parent, best,
            rel.q[0], rel.q[1], rel.q[2], rel.q[3], rel.t[0], rel.t[1], rel.t[2],
            pre.q[0], pre.q[1], pre.q[2], pre.q[3], pre.t[0], pre.t[1], pre.t[2],
            abs.q[0], abs.q[1], abs.q[2], abs.q[3], abs.t[0], abs.t[1], abs.t[2]);
        Log(line);
    }
    Log("skel end");
}

void DriveHand(unsigned int hand, std::uint8_t* relative, std::uint8_t* absolute,
               unsigned poseCount, const animik::Location& location, const GameplayPoseFrame& frame,
               const EquippedRig& owner, const PoseSettings& pose, Vec3* writtenPrimary = nullptr,
               const Vec3* supportPrimary = nullptr, bool* primaryWritten = nullptr,
               Quaternion* writtenRotation = nullptr, const Quaternion* supportPrimaryRotation = nullptr,
               FingerPlan* fingers = nullptr, ArmPlan* arm = nullptr)
{
    const int target = gTargetJoint[hand].load(std::memory_order_relaxed);
    const int weight = gWeightJoint[hand].load(std::memory_order_relaxed);
    const int handJoint = gHandJoint[hand].load(std::memory_order_relaxed);
    if (hand < 2) { gMarkGoal[hand].set = false; }
    Vec3 markRaw{};
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
    const bool supportLocked=hand==1&&twoHand&&frame.twoHand.snapSupport&&supportPrimary;
    // pose.mode 1 on a rig whose hand was identified.
    const bool posed = pose.mode == 1 && gHandRig.valid[hand] && gHandRig.skeleton == gRigSkeleton.load();
    // The native two-handed hold, carried with the written right wrist.
    bool carry = false;
    Quaternion carryRotation{};
    Vec3 carryGoal{};
    if (posed && supportLocked && supportPrimaryRotation) {
        QuatT nativeRight{};
        if (ReadQuatT(absolute, poseCount, gHandJoint[0].load(std::memory_order_relaxed), nativeRight)) {
            const Quaternion turn = Normalize(Multiply(*supportPrimaryRotation,
                Conj(Quaternion{nativeRight.q[0], nativeRight.q[1], nativeRight.q[2], nativeRight.q[3]})));
            carryRotation = Normalize(Multiply(turn, Quaternion{wrist.q[0], wrist.q[1], wrist.q[2], wrist.q[3]}));
            carryGoal = Add3(*supportPrimary, Rotate(turn, Vec3{wrist.t[0] - nativeRight.t[0],
                                                                wrist.t[1] - nativeRight.t[1],
                                                                wrist.t[2] - nativeRight.t[2]}));
            carry = true;
        }
    }
    bool posedFree = false;   // the free hand's anatomical goal was computed

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
                // With pose.mode 1 a controller stands for the PALM, so the
                // support region is where the native left palm is, not its wrist.
                Vec3 nativeLeftPoint{nativeLeft.t[0], nativeLeft.t[1], nativeLeft.t[2]};
                if (posed && gHandRig.valid[1]) {
                    nativeLeftPoint = Add3(nativeLeftPoint, Rotate(
                        Quaternion{nativeLeft.q[0], nativeLeft.q[1], nativeLeft.q[2], nativeLeft.q[3]},
                        GripPointLocal(1, pose)));
                }
                const Vec3 delta{(nativeLeftPoint.x-wrist.t[0])*location.s,
                                 (nativeLeftPoint.y-wrist.t[1])*location.s,
                                 (nativeLeftPoint.z-wrist.t[2])*location.s};
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
            if (posed) {
                // The weapon's grip sets the wrist's rotation; the palm goes to
                // the controller's grip point (the IK target is the wrist).
                Quaternion wristWorld = Normalize(Multiply(location.q, rotation));
                if (pose.weaponRoll) {
                    // **The barrel stays on the aim ray; its ROLL is the hand's.**
                    // The roll above comes from the muzzle helper's authored
                    // frame, which is the weapon artist's, not the hand's: with
                    // the Huntress Boltcaster it put the right palm facing out
                    // (measured 166 deg from the real palm) although the game's
                    // own idle holds it palm-in. Roll the weapon and hand about
                    // the barrel until the drawn hand lies like the player's.
                    const Vec3 axis = Rotate(aimWorld.orientation, Vec3{0.0f, 1.0f, 0.0f});
                    const Quaternion real = AnatomicalWrist(hand, world.orientation, pose);
                    const Vec3 from[2] = {Rotate(wristWorld, gHandRig.frame[hand].palm),
                                          Rotate(wristWorld, gHandRig.frame[hand].thumb)};
                    const Vec3 to[2] = {Rotate(real, gHandRig.frame[hand].palm),
                                        Rotate(real, gHandRig.frame[hand].thumb)};
                    const float roll = handpose::RollToMatch(axis, from, to, 2);
                    wristWorld = Normalize(Multiply(handpose::AxisAngle(axis, roll), wristWorld));
                    rotation = animik::WorldToModel(location, wristWorld);
                    gWeaponRollMilliDeg.store(static_cast<int>(roll * 57295.78f), std::memory_order_relaxed);
                }
                goal = animik::WorldToModel(location, Sub3(world.position,
                    Rotate(wristWorld, Scale3(GripPointLocal(hand, pose), location.s))));
            }
        } else if (posed) {
            // The free hand: the game hand's anatomical frame on the player's.
            // The raw controller, not a two-handed support orientation: the
            // support hold is the native one, blended in below.
            const Pose raw = animik::ControllerWorldFromHead(frame.yaw, anchor, frame.tracking.head, state.gripPose);
            const Quaternion wristWorld = AnatomicalWrist(hand, raw.orientation, pose);
            rotation = animik::WorldToModel(location, wristWorld);
            goal = animik::WorldToModel(location, Sub3(raw.position,
                Rotate(wristWorld, Scale3(GripPointLocal(hand, pose), location.s))));
            writeRotation = true;
            posedFree = true;
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
            markRaw = goalRaw;
            // The character's arm (0.516 m shoulder -> wrist) is shorter than a
            // player's; pose.stretch lets the native two-bone solve lengthen it
            // (it stretches the forearm, up to 1.25x natively) instead of
            // leaving the hand short of the controller at full extension.
            // With the arm on (arm.mode 1) the reach is the player's arm,
            // measured from the player's shoulder: the arm is rewritten from
            // there after the pass, so the animated shoulder no longer limits it.
            const bool anatomical = posed && arm != nullptr && arm->valid;
            const Vec3 reachFrom = anatomical ? arm->shoulderModel : shoulder;
            const float limit = anatomical ? arm->reachModel : (posed ? reach * pose.stretch : reach);
            // Compress the player's reach into the character's BEFORE clamping,
            // so a longer-armed player keeps continuous motion instead of dead
            // travel at full extension.
            //
            // **This mixes the shoulder into the goal**, which is a dependency
            // the trace exists to expose: with k = 0.65, a stationary controller
            // still moves the goal by 35% of any shoulder movement. Zero clamping
            // does not mean zero shoulder contribution.
            const unsigned int reachPercent = gReachPercent.load(std::memory_order_relaxed);
            if (posed && pose.reachMode == 1) {
                // Exact to the knee, then a soft approach to the arm's reach.
                goal = handpose::SoftReach(reachFrom, goal, limit, anatomical ? pose.armKnee : pose.knee);
            } else {
                goal = animik::ScaleReach(shoulder, goal, reachPercent / 100.0f);
            }
            if (posed && hand == 1) {
                // Free hand -> the native two-handed hold, by the support blend.
                const float blend = carry ? frame.twoHand.blend : 0.0f;
                if (carry) {
                    goal = {goal.x + (carryGoal.x - goal.x) * blend, goal.y + (carryGoal.y - goal.y) * blend,
                            goal.z + (carryGoal.z - goal.z) * blend};
                    rotation = posedFree ? handpose::Slerp(rotation, carryRotation, blend) : carryRotation;
                    writeRotation = true;
                }
                if (fingers) { fingers->apply = posedFree; fingers->weight = 1.0f - blend; fingers->support = blend; }
            } else if(supportLocked) {
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
            const Vec3 clamped = animik::ClampToReach(reachFrom, goal, limit);
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
    weaponrig::ContactGeometry contactGeometry{};
    const bool contactReady=hand==0&&aligned&&PhysicalInteractionsEnabled()&&
        weaponrig::ReadContactGeometry({nullptr,ReadAlignmentMemory},
            reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll")),owner,
            reinterpret_cast<std::uintptr_t>(absolute),poseCount,handJoint,contactGeometry);
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
    if (writtenRotation) { *writtenRotation = rotation; }
    if(hand==0&&aligned) {
        if(contactReady)PublishPhysicalWeapon(frame,owner,location,Pose{rotation,goal},contactGeometry);
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
    if (hand < 2) { gMarkGoal[hand] = {true, markRaw, goal}; }
    if (arm != nullptr) { arm->goal = goal; arm->goalSet = true; }
    if (hand == 0) {
        gLastGoalMm[0].store(static_cast<int>(goal.x * 1000.0f), std::memory_order_relaxed);
        gLastGoalMm[1].store(static_cast<int>(goal.y * 1000.0f), std::memory_order_relaxed);
        gLastGoalMm[2].store(static_cast<int>(goal.z * 1000.0f), std::memory_order_relaxed);
    }
}

bool WriteFloatsGuarded(std::uint8_t* at, const float* values, unsigned count)
{
    __try { std::memcpy(at, values, count * sizeof(float)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Before the native pass: the left hand's finger joints take the relaxed pose,
// blended with the animation's by `weight` (1 = relaxed). Relative rotations
// only; their offsets (bone lengths) are the rig's.
bool ApplyRelaxedFingers(std::uint8_t* relative, unsigned count, float weight, float curl)
{
    if (weight <= 0.0f) { return false; }
    for (int j = 0; j < handpose::kJointCount; ++j) {
        const int index = gHandRig.joint[1][j];
        QuatT rel{};
        if (!ReadQuatT(relative, count, index, rel)) { return false; }
        const Quaternion native{rel.q[0], rel.q[1], rel.q[2], rel.q[3]};
        const Quaternion target = handpose::Slerp(native,
            handpose::RelaxedRelative(handpose::Side::left, j, curl), weight);
        const float q[4] = {target.x, target.y, target.z, target.w};
        if (!WriteFloatsGuarded(relative + static_cast<std::size_t>(index) * kQuatTStride, q, 4)) { return false; }
    }
    return true;
}

// After the native pass, which placed the wrist: the finger joints' absolute
// poses from their (rewritten) relatives, parents first. Idempotent with the
// pass's own re-propagation; makes the result independent of whether it did.
void PropagateFingers(std::uint8_t* relative, std::uint8_t* absolute, unsigned count, const HandRig& rig)
{
    for (int j = 0; j < handpose::kJointCount; ++j) {
        const int role = handpose::ParentRole(j);
        const int parent = role < 0 ? rig.wrist[1] : rig.joint[1][role];
        QuatT p{}, r{};
        if (!ReadQuatT(absolute, count, parent, p) || !ReadQuatT(relative, count, rig.joint[1][j], r)) { return; }
        const Quaternion pq{p.q[0], p.q[1], p.q[2], p.q[3]};
        const Quaternion q = Normalize(Multiply(pq, Quaternion{r.q[0], r.q[1], r.q[2], r.q[3]}));
        const Vec3 t = Add3(Vec3{p.t[0], p.t[1], p.t[2]}, Rotate(pq, Vec3{r.t[0], r.t[1], r.t[2]}));
        const float out[7] = {q.x, q.y, q.z, q.w, t.x, t.y, t.z};
        if (!WriteFloatsGuarded(absolute + static_cast<std::size_t>(rig.joint[1][j]) * kQuatTStride, out, 7)) { return; }
    }
}

// After the native pass: the arm from the player's shoulder (see ArmPlan).
// Every write is to the absolute pose, which is what the engine skins, and
// each joint the arm carries keeps its offset from its parent as the pass left
// it -- lengthened along the bone with the segment.
bool WriteQuatT(std::uint8_t* absolute, unsigned count, int index, const Quaternion& q, const Vec3& t)
{
    if (index < 0 || static_cast<unsigned>(index) >= count) { return false; }
    const Quaternion n = Normalize(q);
    const float v[7] = {n.x, n.y, n.z, n.w, t.x, t.y, t.z};
    return WriteFloatsGuarded(absolute + static_cast<std::size_t>(index) * kQuatTStride, v, 7);
}

// A child of `parent` re-placed under the parent's new pose; its offset along
// the bone (local X) scaled with the segment.
void CarryChild(std::uint8_t* absolute, unsigned count, int child, const QuatT& parentOld,
                const Quaternion& parentNew, const Vec3& parentNewT, float alongScale)
{
    QuatT c{};
    if (!ReadQuatT(absolute, count, child, c)) { return; }
    const Quaternion po{parentOld.q[0], parentOld.q[1], parentOld.q[2], parentOld.q[3]};
    Vec3 local = Rotate(Conj(po), Vec3{c.t[0] - parentOld.t[0], c.t[1] - parentOld.t[1], c.t[2] - parentOld.t[2]});
    local.x *= alongScale;
    const Quaternion localQ = Multiply(Conj(po), Quaternion{c.q[0], c.q[1], c.q[2], c.q[3]});
    WriteQuatT(absolute, count, child, Multiply(parentNew, localQ), Add3(parentNewT, Rotate(parentNew, local)));
}

void SolveArms(const GameplayPoseFrame& frame, std::uint8_t* absolute, unsigned count,
               const animik::Location& location, const HandRig& rig, const ArmPlan plan[2],
               const PoseSettings& pose)
{
    XrMap map{&frame, {}, location};
    if (!ArmAnchor(frame, map.anchor)) { return; }
    const armpose::Body& body = pose.body;
    ArmStats stats{};
    const std::uint64_t now = preyvr::timing::MonotonicNanoseconds();
    for (int s = 0; s < 2; ++s) {
        const ArmPlan& p = plan[s];
        if (!p.valid || !p.goalSet) { continue; }
        QuatT clav{}, upper{}, lower{}, hand{};
        if (!ReadQuatT(absolute, count, rig.clavicle[s], clav) || !ReadQuatT(absolute, count, rig.upperArm[s], upper) ||
            !ReadQuatT(absolute, count, rig.lowerArm[s], lower) || !ReadQuatT(absolute, count, rig.wrist[s], hand)) {
            continue;
        }
        const Vec3 c0{clav.t[0], clav.t[1], clav.t[2]};
        const Vec3 s0{upper.t[0], upper.t[1], upper.t[2]};
        const Vec3 e0{lower.t[0], lower.t[1], lower.t[2]};
        const Vec3 w0{hand.t[0], hand.t[1], hand.t[2]};
        const Quaternion handQ{hand.q[0], hand.q[1], hand.q[2], hand.q[3]};
        // The solve, in OpenXR metres.
        const Vec3 shoulder = p.shoulderXr;
        const Vec3 wrist = map.ToXr(p.goal);
        const armpose::Side side = s == 0 ? 1 : -1;
        const Vec3 bodyPole = armpose::BodyPole(p.torso, side);
        // The elbow the PLAYER's hand allows: the real hand's frame from the
        // controller's grip pose (OpenXR defines it), not the drawn hand's --
        // a held weapon turns the drawn hand to its barrel, the real one stays.
        const auto& state = frame.tracking.hands[s == 0 ? static_cast<unsigned>(Hand::right)
                                                        : static_cast<unsigned>(Hand::left)];
        const handpose::Frame real = handpose::GripHandFrame(s == 1 ? handpose::Side::left : handpose::Side::right,
                                                             pose.tubeLean);
        const Vec3 handLength = Rotate(state.gripPose.orientation, real.length);
        const Vec3 handPalm = Rotate(state.gripPose.orientation, real.palm);
        Vec3 pole{};
        {
            std::lock_guard lock(gArmMutex);
            const float dt = gPoleNs[s] != 0 ? static_cast<float>(now - gPoleNs[s]) * 1e-9f : 0.0f;
            const bool fresh = gPoleNs[s] != 0 && dt < 0.5f;
            gPoleNs[s] = now;
            pole = armpose::ChooseElbowPole(shoulder, wrist, body.upper, body.fore, body.armStretch, handLength,
                                            handPalm, side, bodyPole, pose.wrist, fresh ? &gPole[s] : nullptr);
            pole = fresh ? armpose::SmoothDirection(gPole[s], pole, dt, pose.poleTau) : pole;
            gPole[s] = pole;
        }
        const armpose::ArmSolution arm = armpose::SolveElbow(shoulder, wrist, body.upper, body.fore, pole,
                                                             body.armStretch);
        // Back to the model, and the joints.
        const Vec3 sm = p.shoulderModel, em = map.ToModel(arm.elbow), wm = p.goal;
        if (!std::isfinite(em.x) || !std::isfinite(em.y) || !std::isfinite(em.z)) { continue; }
        const auto unit = [](Vec3 v) {
            const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            return l > 1e-6f ? Scale3(v, 1.0f / l) : Vec3{1, 0, 0};
        };
        const auto len = [](Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
        // Clavicle: turned about its root towards the new shoulder.
        const Quaternion clavQ{clav.q[0], clav.q[1], clav.q[2], clav.q[3]};
        const Quaternion clavNew = Multiply(armpose::FromTo(unit(Sub3(s0, c0)), unit(Sub3(sm, c0))), clavQ);
        WriteQuatT(absolute, count, rig.clavicle[s], clavNew, c0);
        // Upper arm: at the shoulder, along shoulder -> elbow.
        const Quaternion upperQ{upper.q[0], upper.q[1], upper.q[2], upper.q[3]};
        const Quaternion upperNew = Multiply(armpose::FromTo(unit(Sub3(e0, s0)), unit(Sub3(em, sm))), upperQ);
        const float upperScale = len(Sub3(e0, s0)) > 1e-4f ? len(Sub3(em, sm)) / len(Sub3(e0, s0)) : 1.0f;
        for (int k = 0; k < 3; ++k) {
            CarryChild(absolute, count, rig.upperChild[s][k], upper, upperNew, sm, upperScale);
        }
        WriteQuatT(absolute, count, rig.upperArm[s], upperNew, sm);
        // Forearm: at the elbow, along elbow -> wrist.
        const Quaternion lowerQ{lower.q[0], lower.q[1], lower.q[2], lower.q[3]};
        const Quaternion lowerNew = Multiply(armpose::FromTo(unit(Sub3(w0, e0)), unit(Sub3(wm, em))), lowerQ);
        const float lowerScale = len(Sub3(w0, e0)) > 1e-4f ? len(Sub3(wm, em)) / len(Sub3(w0, e0)) : 1.0f;
        CarryChild(absolute, count, rig.lowerTwist[s], lower, lowerNew, em, lowerScale);
        WriteQuatT(absolute, count, rig.lowerArm[s], lowerNew, em);
        // The hand on its goal, its rotation as the pass left it, and what it
        // carries (fingers, the weapon's prop joint) moved with it.
        const Vec3 move = Sub3(wm, w0);
        if (len(move) > 1e-6f) {
            const auto shift = [&](int j) {
                QuatT a{};
                if (ReadQuatT(absolute, count, j, a)) {
                    WriteQuatT(absolute, count, j, Quaternion{a.q[0], a.q[1], a.q[2], a.q[3]},
                               Add3(Vec3{a.t[0], a.t[1], a.t[2]}, move));
                }
            };
            shift(rig.wrist[s]);
            shift(rig.handProp[s]);
            if (rig.valid[s]) {
                for (int j = 0; j < handpose::kJointCount; ++j) { shift(rig.joint[s][j]); }
            }
        }
        stats.solved[s] = true;
        stats.target[s] = p.targetXr;
        stats.shoulder[s] = shoulder;
        stats.elbow[s] = arm.elbow;
        stats.handMoveCm[s] = len(move) * location.s * 100.0f;
        stats.scale[s] = arm.scale;
        stats.straight[s] = arm.straight;
    }
    std::lock_guard lock(gArmMutex);
    stats.torsoYawDeg = gArmStats.torsoYawDeg;
    stats.headYawDeg = gArmStats.headYawDeg;
    gArmStats = stats;
}

// After the native pass: the hands as the engine will skin them, in the app's
// OpenXR space, for pose.marks and pose.report.
void CaptureMarks(const GameplayPoseFrame& frame, std::uint8_t* absolute, unsigned count,
                  const animik::Location& location, const HandRig& rig, const FingerPlan& fingers)
{
    if (!frame.cameraCentreValid && gCameraAnchor.load()) { return; }
    const Vec3 anchor = gCameraAnchor.load() ? frame.cameraCentre : frame.nativeEye;
    const PoseSettings settings = ReadPoseSettings();
    HandMarks marks{};
    const auto toXr = [&](const Vec3& model) {
        return WorldToOpenXr(frame, anchor, animik::ModelToWorld(location, model));
    };
    for (int s = 0; s < 2; ++s) {
        if (!rig.valid[s]) { continue; }
        QuatT w{};
        if (!ReadQuatT(absolute, count, rig.wrist[s], w)) { return; }
        const Quaternion wq{w.q[0], w.q[1], w.q[2], w.q[3]};
        const Vec3 wt{w.t[0], w.t[1], w.t[2]};
        marks.wrist[s] = toXr(wt);
        for (int j = 0; j < handpose::kJointCount; ++j) {
            QuatT a{};
            if (!ReadQuatT(absolute, count, rig.joint[s][j], a)) { return; }
            const Vec3 at{a.t[0], a.t[1], a.t[2]};
            marks.joint[s][j] = toXr(at);
            const bool distal = j == handpose::kThumb3 || j == handpose::kIndex3 || j == handpose::kMiddle3 ||
                                j == handpose::kRing3 || j == handpose::kPinky3;
            if (distal) {
                // No tip joint in the rig: the distal phalanx, ~2.2 cm along its bone.
                marks.tip[s][j] = toXr(Add3(at, Rotate(Quaternion{a.q[0], a.q[1], a.q[2], a.q[3]},
                                                       Vec3{0.022f, 0.0f, 0.0f})));
            }
        }
        const Vec3 point = handpose::GripPointInWrist(rig.frame[s], rig.knuckle[s], settings.point);
        marks.gripPoint[s] = toXr(Add3(wt, Rotate(wq, point)));
        // Axes as directions in OpenXR space: difference of two mapped points.
        const auto dir = [&](const Vec3& local) {
            return Sub3(toXr(Add3(wt, Rotate(wq, local))), marks.wrist[s]);
        };
        marks.length[s] = dir(rig.frame[s].length);
        marks.palm[s] = dir(rig.frame[s].palm);
        const auto& state = frame.tracking.hands[s == 0 ? static_cast<unsigned>(Hand::right)
                                                        : static_cast<unsigned>(Hand::left)];
        marks.grip[s] = state.gripPose;
        marks.aim[s] = state.aimPose;
        marks.posed[s] = settings.mode == 1;
        QuatT c{}, u{}, l{};
        if (rig.clavicle[s] >= 0 && rig.upperArm[s] >= 0 && rig.lowerArm[s] >= 0 &&
            ReadQuatT(absolute, count, rig.clavicle[s], c) && ReadQuatT(absolute, count, rig.upperArm[s], u) &&
            ReadQuatT(absolute, count, rig.lowerArm[s], l)) {
            marks.clavicle[s] = toXr(Vec3{c.t[0], c.t[1], c.t[2]});
            marks.shoulder[s] = toXr(Vec3{u.t[0], u.t[1], u.t[2]});
            marks.elbow[s] = toXr(Vec3{l.t[0], l.t[1], l.t[2]});
            marks.arm[s] = true;
        }
        if (gMarkGoal[s].set) {
            marks.goalRaw[s] = toXr(gMarkGoal[s].raw);
            marks.goalWritten[s] = toXr(gMarkGoal[s].written);
            marks.goal[s] = true;
            gMarkGoal[s].set = false;
        }
    }
    {
        QuatT n{}, h{}, e{};
        if (rig.neck >= 0 && rig.head >= 0 && rig.headEnd >= 0 && ReadQuatT(absolute, count, rig.neck, n) &&
            ReadQuatT(absolute, count, rig.head, h) && ReadQuatT(absolute, count, rig.headEnd, e)) {
            marks.neck = toXr(Vec3{n.t[0], n.t[1], n.t[2]});
            marks.headJoint = toXr(Vec3{h.t[0], h.t[1], h.t[2]});
            marks.headEnd = toXr(Vec3{e.t[0], e.t[1], e.t[2]});
            marks.headValid = true;
        }
    }
    marks.head = frame.tracking.head;
    marks.support = fingers.support;
    marks.stamp = frame.tracking.sequence;
    marks.valid = true;
    {
        std::unique_lock lock(gMarksMutex, std::try_to_lock);
        if (lock.owns_lock()) { gMarks = marks; }
    }
    // Every 30 s into the log: on a headset this records what the mock can only
    // estimate -- the runtime's grip pose relative to its aim pose -- and how
    // far each drawn palm is from the controller, with no command to type.
    static std::atomic<std::uint64_t> lastLog{0};
    const std::uint64_t now = preyvr::timing::MonotonicNanoseconds();
    std::uint64_t last = lastLog.load();
    if ((last == 0 || now - last > 30000000000ull) && lastLog.compare_exchange_strong(last, now)) {
        Log("pose_report" + HandPoseReport());
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
        gHandRig.skeleton = 0;   // re-identify the hand's joints with the rig
        gOwnerGeneration.store(owner.generation);
        gOwnerCharacter.store(owner.character);
    }
    // The owning rig's pose arrays when this callback drove it, for the work
    // that has to follow the native pass (finger propagation, measurement).
    FingerPlan fingerPlan{};
    ArmPlan armPlan[2]{};
    PoseSettings ownedSettings{};
    std::uint8_t* ownedRelative = nullptr;
    std::uint8_t* ownedAbsolute = nullptr;
    unsigned ownedCount = 0;
    animik::Location ownedLocation{};
    HandRig ownedHandRig{};
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
                    const PoseSettings poseSettings = ReadPoseSettings();
                    const std::uintptr_t skeleton = gRigSkeleton.load();
                    if (skeleton != 0 && gHandRig.skeleton != skeleton) {
                        BuildHandRig(reinterpret_cast<std::uint8_t*>(skeleton), gRigJoints.load(),
                                     static_cast<std::uint8_t*>(relative), static_cast<unsigned>(poseCount));
                    }
                    PrepareArms(frame, location, static_cast<std::uint8_t*>(absolute),
                                static_cast<unsigned>(poseCount), poseSettings, armPlan);
                    Vec3 primaryGoal{};
                    Quaternion primaryRotation{};
                    bool primaryWritten=false;
                    if (hands & 1u) {
                        DriveHand(0, static_cast<std::uint8_t*>(relative), static_cast<std::uint8_t*>(absolute),
                                  static_cast<unsigned>(poseCount), location, frame, owner, poseSettings,
                                  &primaryGoal, nullptr, &primaryWritten, &primaryRotation, nullptr, nullptr,
                                  &armPlan[0]);
                    }
                    if ((hands & 2u) && gMode.load() == 2) {
                        DriveHand(1, static_cast<std::uint8_t*>(relative), static_cast<std::uint8_t*>(absolute),
                                  static_cast<unsigned>(poseCount), location, frame, owner, poseSettings,
                                  nullptr, primaryWritten ? &primaryGoal : nullptr, nullptr, nullptr,
                                  primaryWritten ? &primaryRotation : nullptr, &fingerPlan, &armPlan[1]);
                    }
                    if (fingerPlan.apply && gHandRig.valid[1]) {
                        fingerPlan.apply = ApplyRelaxedFingers(static_cast<std::uint8_t*>(relative),
                            static_cast<unsigned>(poseCount), fingerPlan.weight, poseSettings.curl);
                    }
                    ownedRelative = static_cast<std::uint8_t*>(relative);
                    ownedAbsolute = static_cast<std::uint8_t*>(absolute);
                    ownedCount = static_cast<unsigned>(poseCount);
                    ownedLocation = location;
                    ownedHandRig = gHandRig;
                    ownedSettings = poseSettings;
                }
            }
        }
    }
    // ik.skel: the pose arrays of the owning rig, captured around the native pass.
    std::uint8_t* dumpRelative = nullptr;
    std::uint8_t* dumpAbsolute = nullptr;
    unsigned dumpCount = 0;
    static std::uint8_t dumpBefore[kMaxJoints * kQuatTStride];
    if (gSkelDump.load() == 1 && haveOwner && owner.character == reinterpret_cast<std::uintptr_t>(character) &&
        params != nullptr) {
        void* pose = nullptr;
        void* rel = nullptr;
        void* abs = nullptr;
        int n = 0;
        auto* const p = static_cast<std::uint8_t*>(params);
        if (ReadPointer(p + kParamsPose, &pose) && pose != nullptr &&
            ReadInt(static_cast<std::uint8_t*>(pose) + 8, &n) && n > 0 && n <= static_cast<int>(kMaxJoints) &&
            ReadPointer(static_cast<std::uint8_t*>(pose) + kPoseRelative, &rel) &&
            ReadPointer(static_cast<std::uint8_t*>(pose) + kPoseAbsolute, &abs) && rel && abs &&
            ReadFloats(abs, reinterpret_cast<float*>(dumpBefore), static_cast<unsigned>(n) * 7)) {
            dumpRelative = static_cast<std::uint8_t*>(rel);
            dumpAbsolute = static_cast<std::uint8_t*>(abs);
            dumpCount = static_cast<unsigned>(n);
        }
    }
    if (stateLock.owns_lock()) { stateLock.unlock(); }
    const ProcessAdikFn original = gOriginal.load(std::memory_order_acquire);
    if (original != nullptr) {
        original(character, params);
    }
    if (ownedAbsolute != nullptr) {
        if (fingerPlan.apply) { PropagateFingers(ownedRelative, ownedAbsolute, ownedCount, ownedHandRig); }
        SolveArms(frame, ownedAbsolute, ownedCount, ownedLocation, ownedHandRig, armPlan, ownedSettings);
        CaptureMarks(frame, ownedAbsolute, ownedCount, ownedLocation, ownedHandRig, fingerPlan);
    }
    int expected = 1;
    if (dumpCount != 0 && gSkelDump.compare_exchange_strong(expected, 0)) {
        DumpSkeletonPose(static_cast<std::uint8_t*>(character), dumpRelative, dumpBefore, dumpAbsolute, dumpCount);
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

DWORD SetHandPoseMode(unsigned int mode)
{
    if (mode > 1) { return 1; }
    { std::lock_guard lock(gPoseSettingsMutex); gPoseSettings.mode = mode; }
    Log("result=0 detail=pose_mode value=" + std::to_string(mode));
    return 0;
}

DWORD SetHandPoseCurlPercent(int percent)
{
    if (percent < 0 || percent > 300) { return 1; }
    { std::lock_guard lock(gPoseSettingsMutex); gPoseSettings.curl = percent / 100.0f; }
    return 0;
}

DWORD SetHandPoseGrip(int alongPermille, int outOfPalmMm, int towardThumbMm, int leanDeciDegrees)
{
    if (alongPermille < 0 || alongPermille > 1500 || outOfPalmMm < -100 || outOfPalmMm > 150 ||
        towardThumbMm < -100 || towardThumbMm > 100 || leanDeciDegrees < -900 || leanDeciDegrees > 900) { return 1; }
    std::lock_guard lock(gPoseSettingsMutex);
    gPoseSettings.point.alongHand = alongPermille / 1000.0f;
    gPoseSettings.point.outOfPalm = outOfPalmMm / 1000.0f;
    gPoseSettings.point.towardThumb = towardThumbMm / 1000.0f;
    gPoseSettings.tubeLean = leanDeciDegrees / 10.0f * 3.14159265f / 180.0f;
    return 0;
}

// arm.* -- see the declaration. Values are integers: mm, permille, degrees.
DWORD SetArmSetting(const std::string& what, const int* v, unsigned n)
{
    std::lock_guard lock(gPoseSettingsMutex);
    PoseSettings& p = gPoseSettings;
    armpose::Body& b = p.body;
    const auto mm = [](int x) { return x * 0.001f; };
    if (what == "mode" && n >= 1 && v[0] >= 0 && v[0] <= 1) {
        p.armMode = static_cast<unsigned>(v[0]);
    } else if (what == "shoulder" && n >= 3 && std::abs(v[0]) <= 300 && v[1] >= 50 && v[1] <= 350 &&
               v[2] <= -50 && v[2] >= -450) {
        b.shoulderForward = mm(v[0]); b.shoulderOut = mm(v[1]); b.shoulderUp = mm(v[2]);
    } else if (what == "len" && n >= 2 && v[0] >= 150 && v[0] <= 450 && v[1] >= 150 && v[1] <= 450) {
        b.upper = mm(v[0]); b.fore = mm(v[1]);
    } else if (what == "stretch" && n >= 2 && v[0] >= 0 && v[0] <= 500 && v[1] >= 1000 && v[1] <= 1300) {
        b.clavicleStretch = v[0] / 1000.0f; b.armStretch = v[1] / 1000.0f;
    } else if (what == "prior" && n >= 4 && v[0] >= 5 && v[0] <= 180 && v[1] >= -40 && v[1] <= 30 &&
               v[2] >= 3 && v[2] <= 90 && v[3] >= 3 && v[3] <= 180) {
        p.wrist.flexionSigmaDeg = static_cast<float>(v[0]); p.wrist.deviationRestDeg = static_cast<float>(v[1]);
        p.wrist.deviationSigmaDeg = static_cast<float>(v[2]); p.wrist.swivelSigmaDeg = static_cast<float>(v[3]);
    } else if (what == "rom" && n >= 4 && v[0] >= 10 && v[0] <= 120 && v[1] >= 10 && v[1] <= 120 &&
               v[2] >= 5 && v[2] <= 90 && v[3] >= 5 && v[3] <= 90) {
        p.wrist.flexionMaxDeg = static_cast<float>(v[0]); p.wrist.extensionMaxDeg = static_cast<float>(v[1]);
        p.wrist.radialMaxDeg = static_cast<float>(v[2]); p.wrist.ulnarMaxDeg = static_cast<float>(v[3]);
    } else if (what == "hold" && n >= 2 && v[0] >= 0 && v[0] <= 360 && v[1] >= 0 && v[1] <= 1000) {
        p.wrist.holdSigmaDeg = static_cast<float>(v[0]); p.poleTau = v[1] / 1000.0f;
    } else if (what == "torso" && n >= 2 && v[0] >= 0 && v[0] <= 180 && v[1] >= 0 && v[1] <= 10000) {
        p.torsoDeadzone = v[0] * 3.14159265f / 180.0f; p.torsoRelax = v[1] / 1000.0f;
    } else if (what == "knee" && n >= 1 && v[0] >= 500 && v[0] <= 1000) {
        p.armKnee = v[0] / 1000.0f;
    } else {
        return 1;
    }
    return 0;
}

std::string ArmReport()
{
    const PoseSettings p = ReadPoseSettings();
    ArmStats st;
    {
        std::lock_guard lock(gArmMutex);
        st = gArmStats;
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed;
    out.precision(1);
    const armpose::Body& b = p.body;
    out << " armMode=" << p.armMode << " shoulderMm=" << b.shoulderForward * 1000 << ',' << b.shoulderOut * 1000
        << ',' << b.shoulderUp * 1000 << " lenMm=" << b.upper * 1000 << ',' << b.fore * 1000
        << " clavStretch=" << b.clavicleStretch << " armStretch=" << b.armStretch << " knee=" << p.armKnee
        << " priorDeg=" << p.wrist.flexionSigmaDeg << ',' << p.wrist.deviationRestDeg << ','
        << p.wrist.deviationSigmaDeg << ',' << p.wrist.swivelSigmaDeg
        << " romDeg=" << p.wrist.flexionMaxDeg << ',' << p.wrist.extensionMaxDeg << ','
        << p.wrist.radialMaxDeg << ',' << p.wrist.ulnarMaxDeg
        << " holdDeg=" << p.wrist.holdSigmaDeg << " poleTauMs=" << p.poleTau * 1000
        << " torsoDeadzoneDeg=" << p.torsoDeadzone * 57.29578f << " torsoRelax=" << p.torsoRelax
        << " headYawDeg=" << st.headYawDeg << " torsoYawDeg=" << st.torsoYawDeg;
    for (int s = 0; s < 2; ++s) {
        const char* t = s == 0 ? "R" : "L";
        out << " solved" << t << '=' << st.solved[s];
        if (!st.solved[s]) { continue; }
        const Vec3 d = Sub3(st.shoulder[s], st.target[s]);
        out << " shoulderToTargetCm" << t << '=' << std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) * 100.0f
            << " handMoveCm" << t << '=' << st.handMoveCm[s];
        out.precision(3);
        out << " scale" << t << '=' << st.scale[s];
        out.precision(1);
        out << " straight" << t << '=' << st.straight[s];
    }
    return out.str();
}

DWORD SetHandPoseStretchPercent(int percent)
{
    if (percent < 90 || percent > 125) { return 1; }
    std::lock_guard lock(gPoseSettingsMutex);
    gPoseSettings.stretch = percent / 100.0f;
    return 0;
}

DWORD SetHandPoseWeaponRoll(unsigned int enabled)
{
    if (enabled > 1) { return 1; }
    std::lock_guard lock(gPoseSettingsMutex);
    gPoseSettings.weaponRoll = enabled != 0;
    return 0;
}

DWORD SetHandPoseReach(unsigned int mode, int kneePercent)
{
    if (mode > 1 || kneePercent < 50 || kneePercent > 99) { return 1; }
    std::lock_guard lock(gPoseSettingsMutex);
    gPoseSettings.reachMode = mode;
    gPoseSettings.knee = kneePercent / 100.0f;
    return 0;
}

namespace {
float Len(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
float AngleDeg(Vec3 a, Vec3 b)
{
    const float la = Len(a), lb = Len(b);
    if (la < 1e-6f || lb < 1e-6f) { return -1.0f; }
    const float c = std::clamp((a.x * b.x + a.y * b.y + a.z * b.z) / (la * lb), -1.0f, 1.0f);
    return std::acos(c) * 57.29578f;
}
HandMarks LatestMarks() { std::lock_guard lock(gMarksMutex); return gMarks; }
}

std::string HandPoseReport()
{
    const PoseSettings settings = ReadPoseSettings();
    const HandMarks marks = LatestMarks();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed;
    out.precision(1);
    out << " poseMode=" << settings.mode << " curl=" << settings.curl * 100.0f
        << " along=" << settings.point.alongHand * 1000.0f << " outOfPalmMm=" << settings.point.outOfPalm * 1000.0f
        << " thumbMm=" << settings.point.towardThumb * 1000.0f
        << " leanDeg=" << settings.tubeLean * 57.29578f
        << " reachMode=" << settings.reachMode << " knee=" << settings.knee * 100.0f
        << " weaponRoll=" << (settings.weaponRoll ? 1 : 0) << " stretch=" << settings.stretch * 100.0f
        << " rollDeg=" << gWeaponRollMilliDeg.load(std::memory_order_relaxed) / 1000.0f;
    {
        std::lock_guard lock(gIkMutex);
        out << " rigR=" << gHandRig.valid[0] << " rigL=" << gHandRig.valid[1];
    }
    out << " marks=" << (marks.valid ? "fresh" : "none");
    if (!marks.valid) { return out.str(); }
    out << " support=" << marks.support;
    for (int s = 0; s < 2; ++s) {
        const char* tag = s == 0 ? "R" : "L";
        const auto side = s == 1 ? handpose::Side::left : handpose::Side::right;
        const Pose& grip = marks.grip[s];
        const handpose::Frame expected = handpose::GripHandFrame(side, settings.tubeLean);
        const Vec3 lenExp = Rotate(grip.orientation, expected.length);
        const Vec3 palmExp = Rotate(grip.orientation, expected.palm);
        // The game palm's grip point against the controller's grip origin,
        // and the game hand's axes against the real hand's.
        out << " palmErrCm" << tag << '=' << Len(Sub3(marks.gripPoint[s], grip.position)) * 100.0f
            << " lenErrDeg" << tag << '=' << AngleDeg(marks.length[s], lenExp)
            << " palmNormErrDeg" << tag << '=' << AngleDeg(marks.palm[s], palmExp)
            << " wristToGripCm" << tag << '=' << Len(Sub3(marks.wrist[s], grip.position)) * 100.0f;
        // The game hand's axes in the controller's grip frame (unit), to read
        // WHICH way a hand is off, not only by how much.
        const auto inGrip = [&](Vec3 v) {
            const float l = Len(v);
            return l > 1e-6f ? Rotate(Conj(grip.orientation), Scale3(v, 1.0f / l)) : Vec3{};
        };
        const Vec3 lg = inGrip(marks.length[s]), pg = inGrip(marks.palm[s]);
        out.precision(2);
        out << " lenInGrip" << tag << '=' << lg.x << ',' << lg.y << ',' << lg.z
            << " palmInGrip" << tag << '=' << pg.x << ',' << pg.y << ',' << pg.z;
        out.precision(1);
        // grip in aim: what a real runtime says about its controller; the mock
        // uses an estimate (DVR_MockGrip.txt) until a headset has reported it.
        const Pose& aim = marks.aim[s];
        const Quaternion rel = Normalize(Multiply(Conj(aim.orientation), grip.orientation));
        const Vec3 off = Rotate(Conj(aim.orientation), Sub3(grip.position, aim.position));
        const float angle = handpose::AngleBetween(aim.orientation, grip.orientation) * 57.29578f;
        out.precision(3);
        out << " gripInAim" << tag << "=q" << rel.x << ',' << rel.y << ',' << rel.z << ',' << rel.w;
        out.precision(1);
        out << "|deg" << angle << "|cm" << off.x * 100.0f << ',' << off.y * 100.0f << ',' << off.z * 100.0f;
    }
    return out.str();
}

std::string HandPoseMarks()
{
    const PoseSettings settings = ReadPoseSettings();
    const HandMarks marks = LatestMarks();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed;
    out.precision(4);
    if (!marks.valid) { return out.str(); }
    const auto seg = [&](Vec3 a, Vec3 b, int r, int g, int bl) {
        out << a.x << ' ' << a.y << ' ' << a.z << ' ' << b.x << ' ' << b.y << ' ' << b.z << ' '
            << r << ' ' << g << ' ' << bl << '\n';
    };
    const auto unit = [](Vec3 v, float length) {
        const float l = Len(v);
        return l > 1e-6f ? Scale3(v, length / l) : Vec3{};
    };
    for (int s = 0; s < 2; ++s) {
        const int r = s == 1 ? 255 : 255, g = s == 1 ? 255 : 200, b = s == 1 ? 255 : 120;
        // The skeleton: wrist -> metacarpal -> three phalanges -> tip, and the thumb.
        const int chains[5][4] = {
            {handpose::kThumb1, handpose::kThumb2, handpose::kThumb3, -1},
            {handpose::kIndexBase, handpose::kIndex1, handpose::kIndex2, handpose::kIndex3},
            {handpose::kMiddleBase, handpose::kMiddle1, handpose::kMiddle2, handpose::kMiddle3},
            {handpose::kRingBase, handpose::kRing1, handpose::kRing2, handpose::kRing3},
            {handpose::kPinkyBase, handpose::kPinky1, handpose::kPinky2, handpose::kPinky3},
        };
        for (const auto& chain : chains) {
            Vec3 previous = marks.wrist[s];
            int last = -1;
            for (int k = 0; k < 4 && chain[k] >= 0; ++k) {
                seg(previous, marks.joint[s][chain[k]], r, g, b);
                previous = marks.joint[s][chain[k]];
                last = chain[k];
            }
            if (last >= 0) { seg(previous, marks.tip[s][last], r, g, b); }
        }
        // The game hand's frame at its grip point: length cyan, palm normal magenta.
        const Vec3 p = marks.gripPoint[s];
        seg(p, Add3(p, unit(marks.length[s], 0.08f)), 0, 255, 255);
        seg(p, Add3(p, unit(marks.palm[s], 0.06f)), 255, 0, 255);
        // The real hand's frame at the controller's grip origin, darker.
        const auto side = s == 1 ? handpose::Side::left : handpose::Side::right;
        const handpose::Frame expected = handpose::GripHandFrame(side, settings.tubeLean);
        const Pose& grip = marks.grip[s];
        seg(grip.position, Add3(grip.position, Scale3(Rotate(grip.orientation, expected.length), 0.08f)), 0, 110, 110);
        seg(grip.position, Add3(grip.position, Scale3(Rotate(grip.orientation, expected.palm), 0.06f)), 110, 0, 110);
        // The drawn arm, orange: clavicle -> shoulder -> elbow -> wrist.
        if (marks.arm[s]) {
            seg(marks.clavicle[s], marks.shoulder[s], 255, 140, 0);
            seg(marks.shoulder[s], marks.elbow[s], 255, 140, 0);
            seg(marks.elbow[s], marks.wrist[s], 255, 140, 0);
        }
        // The wrist goals as small crosses: white before reach shaping, red as written.
        const auto cross = [&](Vec3 c, int r, int g, int bl) {
            const float h = 0.015f;
            seg(Vec3{c.x - h, c.y, c.z}, Vec3{c.x + h, c.y, c.z}, r, g, bl);
            seg(Vec3{c.x, c.y - h, c.z}, Vec3{c.x, c.y + h, c.z}, r, g, bl);
            seg(Vec3{c.x, c.y, c.z - h}, Vec3{c.x, c.y, c.z + h}, r, g, bl);
        };
        if (marks.goal[s]) {
            cross(marks.goalRaw[s], 255, 255, 255);
            cross(marks.goalWritten[s], 255, 40, 40);
        }
    }
    if (marks.headValid) {
        seg(marks.neck, marks.headJoint, 255, 140, 0);
        seg(marks.headJoint, marks.headEnd, 255, 140, 0);
    }
    // The same as data (not drawn: the mock reads numeric lines only).
    const auto point = [&](const char* name, Vec3 v) {
        out << "J " << name << ' ' << v.x << ' ' << v.y << ' ' << v.z << '\n';
    };
    const auto pose = [&](const char* name, const Pose& p) {
        out << "P " << name << ' ' << p.position.x << ' ' << p.position.y << ' ' << p.position.z << ' '
            << p.orientation.x << ' ' << p.orientation.y << ' ' << p.orientation.z << ' ' << p.orientation.w << '\n';
    };
    pose("head", marks.head);
    {
        ArmStats st;
        {
            std::lock_guard lock(gArmMutex);
            st = gArmStats;
        }
        for (int s = 0; s < 2; ++s) {
            if (!st.solved[s]) { continue; }
            point(s == 0 ? "shoulderTargetR" : "shoulderTargetL", st.target[s]);
        }
        out << "A torsoYawDeg " << st.torsoYawDeg << " headYawDeg " << st.headYawDeg << '\n';
    }
    if (marks.headValid) { point("neck", marks.neck); point("headJnt", marks.headJoint); point("headEnd", marks.headEnd); }
    for (int s = 0; s < 2; ++s) {
        const std::string t = s == 0 ? "R" : "L";
        pose(("grip" + t).c_str(), marks.grip[s]);
        pose(("aim" + t).c_str(), marks.aim[s]);
        point(("wrist" + t).c_str(), marks.wrist[s]);
        point(("palm" + t).c_str(), marks.gripPoint[s]);
        point(("handLen" + t).c_str(), marks.length[s]);     // directions, not points
        point(("handPalm" + t).c_str(), marks.palm[s]);
        if (marks.arm[s]) {
            point(("clavicle" + t).c_str(), marks.clavicle[s]);
            point(("shoulder" + t).c_str(), marks.shoulder[s]);
            point(("elbow" + t).c_str(), marks.elbow[s]);
        }
        if (marks.goal[s]) {
            point(("goalRaw" + t).c_str(), marks.goalRaw[s]);
            point(("goalWritten" + t).c_str(), marks.goalWritten[s]);
        }
    }
    return out.str();
}

DWORD RequestAnimIkSkeletonDump()
{
    if (!gInstalled.load(std::memory_order_acquire)) { return 1; }
    gSkelDump.store(1);
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
