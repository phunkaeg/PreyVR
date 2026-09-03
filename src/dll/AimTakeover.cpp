#include "AimTakeover.h"

#include "HeadTrackingHook.h"
#include "Logger.h"
#include "XrInput.h"
#include "preyvr/EngineMap.h"
#include "preyvr/MotionController.h"
#include "preyvr/StereoCamera.h"

#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string_view>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_aim_takeover " + line);
}

// R-011 ArkPlayer::UpdateCachedReticleViewPosAndDir(ArkPlayer*).
using UpdateCachedRayFn = void(__fastcall*)(void* player);
using GetArkPlayerInstanceFn = void*(__fastcall*)();

void* gTarget = nullptr;
std::atomic<UpdateCachedRayFn> gOriginal{nullptr};
bool gInstalled = false;

std::atomic<bool> gEnabled{false};
std::atomic<unsigned long long> gApplied{0};
std::atomic<unsigned long long> gRejNoPose{0};
std::atomic<unsigned long long> gRejNoPlayer{0};
std::atomic<unsigned long long> gRejCompose{0};
std::atomic<unsigned int> gNativeMagnitude{0};

const engine::Landmark* FindLandmark(std::string_view id)
{
    for (const auto& landmark : engine::Landmarks()) {
        if (landmark.id == id) {
            return &landmark;
        }
    }
    return nullptr;
}

float Length(const Vec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

void __fastcall UpdateCachedRayWithTakeover(void* player)
{
    const UpdateCachedRayFn original = gOriginal.load(std::memory_order_acquire);
    if (original != nullptr) {
        original(player);   // let the engine build its own ray first
    }
    if (!gEnabled.load(std::memory_order_acquire) || player == nullptr) {
        return;
    }
    const auto playerAddress = reinterpret_cast<std::uintptr_t>(player);

    // Read the engine's own direction back before touching anything. It is
    // documented as a unit vector, so a magnitude near 1 confirms both the offset
    // and the convention -- the data naming itself rather than us trusting a
    // field name. A reading far from 1 means the layout is wrong, and writing
    // into it would be writing somewhere unknown.
    Vec3 nativeDirection{};
    std::memcpy(&nativeDirection,
                reinterpret_cast<const void*>(
                    playerAddress + engine::ArkPlayerLayout::cachedReticleDirection),
                sizeof(nativeDirection));
    const float nativeLength = Length(nativeDirection);
    gNativeMagnitude.store(static_cast<unsigned int>(nativeLength * 1000.0f + 0.5f),
                           std::memory_order_relaxed);
    if (!std::isfinite(nativeLength) || nativeLength < 0.9f || nativeLength > 1.1f) {
        gRejCompose.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    ControllerState controller{};
    if (!TryGetControllerState(Hand::right, controller)) {
        gRejNoPose.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // The same reference frame the view uses, so hand and eye cannot drift apart.
    // One recenter event feeds every lane -- CAM-003.
    Pose headPose{};
    unsigned long long age = 0;
    if (!TryReadHeadPose(headPose, age)) {
        gRejNoPose.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    stereo::ReferenceFrame reference{};
    reference.yawRadians = HeadTrackingReferenceYaw();
    // The ray's own origin is the engine's, so the hand's position is expressed
    // about the same point the engine is already firing from. Position tracking
    // is M3's business; this lane only replaces the direction and keeps the
    // engine's origin.
    Vec3 engineOrigin{};
    std::memcpy(&engineOrigin,
                reinterpret_cast<const void*>(
                    playerAddress + engine::ArkPlayerLayout::cachedReticleOrigin),
                sizeof(engineOrigin));
    reference.worldPosition = engineOrigin;

    const auto ray = controller::AimFromController(
        reference, controller.aimPose, controller.aimValidity,
        /*maxAgeNanoseconds=*/200ull * 1000ull * 1000ull);
    if (!ray) {
        // AimFromController refuses an untracked or stale pose rather than
        // producing a plausible ray from a dead controller -- the caller then
        // leaves Prey's own ray alone, which is what this does.
        gRejNoPose.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const float length = Length(ray->direction);
    if (!std::isfinite(length) || length < 0.99f || length > 1.01f) {
        gRejCompose.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Direction only. The engine's origin is left in place, so a shot still
    // starts where the engine expects and only its heading changes -- the
    // smallest edit that detaches aim.
    std::memcpy(reinterpret_cast<void*>(
                    playerAddress + engine::ArkPlayerLayout::cachedReticleDirection),
                &ray->direction, sizeof(ray->direction));
    gApplied.fetch_add(1, std::memory_order_relaxed);
}

bool Install()
{
    if (gInstalled) {
        return true;
    }
    const engine::Landmark* const landmark = FindLandmark("aim.update_cached_ray");
    if (landmark == nullptr) {
        Log("result=unavailable detail=landmark_missing");
        return false;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto target = reinterpret_cast<std::uintptr_t>(preyDll) + landmark->rva;
    if (std::memcmp(reinterpret_cast<const void*>(target),
                    landmark->expected.data(), landmark->expected.size()) != 0) {
        Log("result=unavailable detail=prologue_mismatch");
        return false;
    }
    gTarget = reinterpret_cast<void*>(target);
    UpdateCachedRayFn original = nullptr;
    if (MH_CreateHook(gTarget, reinterpret_cast<void*>(&UpdateCachedRayWithTakeover),
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
    Log("result=0 detail=hook_installed target=ArkPlayer::UpdateCachedReticleViewPosAndDir");
    return true;
}

} // namespace

DWORD SetAimTakeoverEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on) {
        if (!Install()) {
            return 1;
        }
        gApplied.store(0, std::memory_order_relaxed);
        gRejNoPose.store(0, std::memory_order_relaxed);
        gRejNoPlayer.store(0, std::memory_order_relaxed);
        gRejCompose.store(0, std::memory_order_relaxed);
    }
    gEnabled.store(on, std::memory_order_release);
    Log(std::string("result=0 detail=enabled value=") + (on ? "1" : "0"));
    return 0;
}

unsigned long long AimTakeoverAppliedCount() { return gApplied.load(std::memory_order_relaxed); }
unsigned long long AimTakeoverRejectedNoPose() { return gRejNoPose.load(std::memory_order_relaxed); }
unsigned long long AimTakeoverRejectedNoPlayer() { return gRejNoPlayer.load(std::memory_order_relaxed); }
unsigned long long AimTakeoverRejectedCompose() { return gRejCompose.load(std::memory_order_relaxed); }
DWORD AimTakeoverNativeDirectionMagnitude()
{
    return gNativeMagnitude.load(std::memory_order_relaxed);
}

} // namespace preyvr::dll
