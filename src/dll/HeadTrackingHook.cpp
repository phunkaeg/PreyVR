#include "HeadTrackingHook.h"

#include "Logger.h"
#include "preyvr/CameraEdit.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoCamera.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstring>
#include <optional>
#include <span>
#include <sstream>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_head_tracking " + line);
}

// R-030 CRenderView::SetCamera(const CCamera&). RCX is the render view, RDX the
// camera. It copies by value into m_camera at +0x11A0, which is exactly why this
// seam is safe: what we pass cannot alias the global view camera.
using SetCameraFn = void(__fastcall*)(void* renderView, const std::uint8_t* camera);
using UpdateFrustumFn = void(__fastcall*)(std::uint8_t* camera);

constexpr std::uintptr_t kSetCameraRva = 0xEE7E80;

constexpr std::uintptr_t kUpdateFrustumRva = 0x121D70;
constexpr std::array<std::uint8_t, 20> kUpdateFrustumPrologue{
    0x48, 0x8B, 0xC4, 0x55, 0x53, 0x48, 0x8D, 0x68, 0xA1, 0x48,
    0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x59,
};


// The first 16 bytes of R-030's registry signature. Verified immediately before
// hooking, so a build that does not match cannot be instrumented at a
// plausible-looking address.
constexpr std::array<std::uint8_t, 16> kSetCameraPrologue = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48,
    0x89, 0x78, 0x10, 0x55, 0x48, 0x8D, 0x68, 0xD8,
};

void* gTarget = nullptr;
std::atomic<SetCameraFn> gOriginal{nullptr};
std::atomic<UpdateFrustumFn> gUpdateFrustum{nullptr};
bool gHookCreated = false;

std::atomic<bool> gArmed{false};
std::atomic<unsigned long long> gApplied{0};
std::atomic<unsigned long long> gRefused{0};
std::atomic<unsigned long long> gLastPoseAgeMicroseconds{0};
std::atomic<unsigned long long> gMaxPoseAgeMicroseconds{0};

// The published head pose.
//
// **A seqlock rather than a queue.** The eye handoff needed a queue because eye
// identity had to be exact and ordered. A pose is the opposite: an older one is
// simply a worse answer to the same question, so the newest is always the right
// one and a backlog would be pure latency. A writer bumps an odd sequence, writes,
// bumps it even; a reader retries while the sequence is odd or changed. No lock,
// and the render path never waits on the publisher.
struct PoseSlot {
    std::atomic<unsigned long long> sequence{0};
    Pose pose{};
    std::int64_t publishedQpc = 0;
    std::atomic<bool> everPublished{false};
};
PoseSlot gPoseSlot;

std::atomic<bool> gHaveReference{false};
std::atomic<float> gReferenceYaw{0.0f};

std::int64_t QpcNow()
{
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

std::int64_t QpcFrequency()
{
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value.QuadPart != 0 ? value.QuadPart : 1;
    }();
    return frequency;
}

bool ReadPose(Pose& out, std::int64_t& publishedQpc)
{
    if (!gPoseSlot.everPublished.load(std::memory_order_acquire)) {
        return false;
    }
    for (int attempt = 0; attempt < 8; ++attempt) {
        const unsigned long long before = gPoseSlot.sequence.load(std::memory_order_acquire);
        if ((before & 1ull) != 0ull) {
            continue;   // a write is in flight
        }
        const Pose pose = gPoseSlot.pose;
        const std::int64_t stamp = gPoseSlot.publishedQpc;
        const unsigned long long after = gPoseSlot.sequence.load(std::memory_order_acquire);
        if (before == after) {
            out = pose;
            publishedQpc = stamp;
            return true;
        }
    }
    return false;
}

bool PrologueMatches(std::uintptr_t address)
{
    return std::memcmp(reinterpret_cast<const void*>(address),
                       kSetCameraPrologue.data(), kSetCameraPrologue.size()) == 0;
}

// Builds the camera the engine should render with: the game's own position, and
// an orientation of the recentered play space composed with the live head.
//
// **Orientation only.** The head pose's translation is dropped rather than
// applied, which is what keeps M1 free of `unitsPerMetre` -- still unmeasured,
// and it scales position rather than rotation -- and free of the head-versus-wall
// collision problem, since a head that cannot translate cannot lean through
// geometry.
void __fastcall SetCameraWithHeadTracking(void* renderView, const std::uint8_t* camera)
{
    const SetCameraFn original = gOriginal.load(std::memory_order_acquire);
    if (!gArmed.load(std::memory_order_acquire) || camera == nullptr) {
        if (original != nullptr) {
            original(renderView, camera);
        }
        return;
    }

    // **Rotation deliberately does not happen here any more.** Measured
    // 2026-09-03: this seam is downstream of visibility, so a rotated camera
    // renders geometry the engine already culled against an unrotated one. That
    // is what makes it safe from contamination and what makes it wrong for
    // rotation. Rotation now happens upstream, on the camera culling reads.
    //
    // The hook stays installed and inert rather than removed, because the per-eye
    // offset belongs here -- downstream, where no restore is needed and nothing
    // can alias the global camera.
    gRefused.fetch_add(1, std::memory_order_relaxed);
    if (original != nullptr) {
        original(renderView, camera);
    }
}

bool EnsureHook()
{
    if (gHookCreated) {
        return true;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    const auto target = base + kSetCameraRva;
    if (!PrologueMatches(target)) {
        Log("result=unavailable detail=set_camera_prologue");
        return false;
    }

    const auto frustumAddress = base + kUpdateFrustumRva;
    if (std::memcmp(reinterpret_cast<const void*>(frustumAddress),
                    kUpdateFrustumPrologue.data(), kUpdateFrustumPrologue.size()) != 0) {
        Log("result=unavailable detail=update_frustum_prologue");
        return false;
    }
    gUpdateFrustum.store(reinterpret_cast<UpdateFrustumFn>(frustumAddress),
                         std::memory_order_release);

    gTarget = reinterpret_cast<void*>(target);
    SetCameraFn original = nullptr;
    MH_STATUS status = MH_CreateHook(
        gTarget, reinterpret_cast<void*>(&SetCameraWithHeadTracking),
        reinterpret_cast<void**>(&original));
    if (status != MH_OK) {
        std::ostringstream line;
        line << "result=failed detail=create_hook minhook=" << MH_StatusToString(status);
        Log(line.str());
        return false;
    }
    gOriginal.store(original, std::memory_order_release);

    status = MH_EnableHook(gTarget);
    if (status != MH_OK) {
        std::ostringstream line;
        line << "result=failed detail=enable_hook minhook=" << MH_StatusToString(status);
        Log(line.str());
        MH_RemoveHook(gTarget);
        return false;
    }
    gHookCreated = true;
    Log("result=0 detail=hook_installed target=CRenderView::SetCamera");
    return true;
}

} // namespace

bool TryReadHeadPose(Pose& out, unsigned long long& ageMicroseconds)
{
    Pose pose{};
    std::int64_t publishedQpc = 0;
    if (!ReadPose(pose, publishedQpc)) {
        return false;
    }
    const auto age = static_cast<unsigned long long>(
        ((QpcNow() - publishedQpc) * 1000000ll) / QpcFrequency());
    gLastPoseAgeMicroseconds.store(age, std::memory_order_relaxed);
    unsigned long long previousMax = gMaxPoseAgeMicroseconds.load(std::memory_order_relaxed);
    while (age > previousMax &&
           !gMaxPoseAgeMicroseconds.compare_exchange_weak(previousMax, age)) {
    }
    out = pose;
    ageMicroseconds = age;
    return true;
}

bool ApplyHeadRotation(std::uint8_t* camera, std::size_t size)
{
    if (camera == nullptr || size < cameraedit::kCameraSize) {
        return false;
    }
    if (!gHaveReference.load(std::memory_order_acquire)) {
        return false;
    }
    Pose headPose{};
    unsigned long long age = 0;
    if (!TryReadHeadPose(headPose, age)) {
        return false;
    }
    const auto span = std::span<std::uint8_t>(camera, cameraedit::kCameraSize);

    // The engine's own eye point stays the engine's business, so walking,
    // collision and scripted movement are untouched. Only the orientation is ours.
    const stereo::Matrix34 existing = stereo::ReadMatrix(span);
    stereo::ReferenceFrame reference{};
    reference.worldPosition = stereo::PoseFromMatrix(existing).position;
    reference.yawRadians = gReferenceYaw.load(std::memory_order_acquire);

    Pose orientationOnly = headPose;
    orientationOnly.position = Vec3{};
    const Pose world = stereo::EyePoseInWorld(reference, orientationOnly);

    if (!stereo::WriteMatrix(span, stereo::MatrixFromPose(world))) {
        return false;
    }
    // A non-orthonormal matrix makes the engine negate its own plane normals,
    // which is a confusing thing to debug from inside a headset.
    return cameraedit::RotationIsSafeToWrite(span);
}

void PublishHeadPose(const Pose& openXrHeadPose)
{
    const unsigned long long sequence = gPoseSlot.sequence.load(std::memory_order_relaxed);
    gPoseSlot.sequence.store(sequence + 1, std::memory_order_release);   // odd: writing
    gPoseSlot.pose = openXrHeadPose;
    gPoseSlot.publishedQpc = QpcNow();
    gPoseSlot.sequence.store(sequence + 2, std::memory_order_release);   // even: readable
    gPoseSlot.everPublished.store(true, std::memory_order_release);
}

DWORD RecenterHeadTracking()
{
    Pose headPose{};
    std::int64_t publishedQpc = 0;
    if (!ReadPose(headPose, publishedQpc)) {
        Log("result=refused detail=no_pose_published");
        return 1;
    }
    const auto yaw = stereo::RecenterYawFromHeadPose(headPose);
    if (!yaw) {
        // Near-vertical. Rejecting leaves the previous reference in place, which
        // is what a player looking at the ceiling expects; inventing a yaw would
        // face them somewhere arbitrary.
        Log("result=refused detail=near_vertical_no_usable_yaw");
        return 2;
    }
    gReferenceYaw.store(*yaw, std::memory_order_release);
    gHaveReference.store(true, std::memory_order_release);
    std::ostringstream line;
    line << "result=0 detail=recentered yawRadians=" << *yaw;
    Log(line.str());
    return 0;
}

DWORD SetHeadTrackingEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on) {
        if (!gHaveReference.load(std::memory_order_acquire)) {
            // Without a reference the composition has no idea which way the play
            // space faces, and would snap the world to engine north on frame one.
            Log("result=refused detail=no_reference_recenter_first");
            return 1;
        }
        if (!EnsureHook()) {
            return 2;
        }
        gApplied.store(0, std::memory_order_relaxed);
        gRefused.store(0, std::memory_order_relaxed);
        gMaxPoseAgeMicroseconds.store(0, std::memory_order_relaxed);
    }
    gArmed.store(on, std::memory_order_release);
    Log(std::string("result=0 detail=armed value=") + (on ? "1" : "0"));
    return 0;
}

unsigned long long HeadTrackingAppliedCount()
{
    return gApplied.load(std::memory_order_relaxed);
}

unsigned long long HeadTrackingRefusedCount()
{
    return gRefused.load(std::memory_order_relaxed);
}

unsigned long long HeadTrackingLastPoseAgeMicroseconds()
{
    return gLastPoseAgeMicroseconds.load(std::memory_order_relaxed);
}

unsigned long long HeadTrackingMaxPoseAgeMicroseconds()
{
    return gMaxPoseAgeMicroseconds.load(std::memory_order_relaxed);
}

DWORD HeadTrackingHasReference()
{
    return gHaveReference.load(std::memory_order_acquire) ? 1u : 0u;
}

} // namespace preyvr::dll
