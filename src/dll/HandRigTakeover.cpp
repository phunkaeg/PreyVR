#include "HandRigTakeover.h"

#include "Logger.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
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
        const int joint = gJoint.load(std::memory_order_relaxed);
        if (joint < 0 || static_cast<unsigned int>(joint) >= count) {
            gRefused.fetch_add(1, std::memory_order_relaxed);
            return forward();
        }
        // The subtree: every joint whose parent chain reaches the selected one.
        // Moving the wrist without its descendants tears the hand, so this is not
        // an optimisation -- it is the difference between a displaced hand and a
        // broken one.
        std::memset(tInSubtree.data(), 0, count);
        tInSubtree[static_cast<std::size_t>(joint)] = 1;
        for (unsigned int j = 0; j < count; ++j) {
            int walk = static_cast<int>(j);
            for (unsigned int guard = 0; guard < count && walk >= 0; ++guard) {
                if (walk == joint) {
                    tInSubtree[j] = 1;
                    break;
                }
                walk = gJointParents[static_cast<std::size_t>(walk)];
            }
        }
        const float dx = gOffsetX.load(std::memory_order_relaxed);
        const float dy = gOffsetY.load(std::memory_order_relaxed);
        const float dz = gOffsetZ.load(std::memory_order_relaxed);
        for (unsigned int j = 0; j < count; ++j) {
            if (!tInSubtree[j]) {
                continue;
            }
            float* const pos = reinterpret_cast<float*>(
                tScratch.data() + static_cast<std::size_t>(j) * kQuatTStride + kQuatTPosition);
            if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2])) {
                gRefused.fetch_add(1, std::memory_order_relaxed);
                return forward();
            }
            // A pure translation is a rigid delta whose rotation is identity, so
            // only positions change. Rotations are left exactly as the animator
            // produced them, which keeps this a displacement rather than a pose.
            pos[0] += dx;
            pos[1] += dy;
            pos[2] += dz;
            ++moved;
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
