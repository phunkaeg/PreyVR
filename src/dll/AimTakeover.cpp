#include "AimTakeover.h"
#include "WeaponAttachment.h"
#include "preyvr/AnimIk.h"
#include "MinHookInit.h"
#include "preyvr/LatestSnapshot.h"

#include <optional>

#include "HeadTrackingHook.h"
#include "Logger.h"
#include "ReticleFollow.h"
#include "XrInput.h"
#include "preyvr/EngineMap.h"
#include "preyvr/MotionController.h"
#include "preyvr/StereoCamera.h"

#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <sstream>
#include <span>
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
LatestSnapshot<GameplayPoseFrame> gGameplayFrame;
LatestSnapshot<preyvr::aim::Sample> gAimSample;
std::atomic<unsigned long long> gAimSamplePublished{0};
// The world direction actually published, and the raw controller orientation it
// came from. Reported so a head sweep says which one moves.
std::atomic<int> gAimDirMilli[3]{0, 0, 0};
std::atomic<int> gAimRawDirMilli[3]{0, 0, 0};
// The native producer and this edit run on the gameplay thread. A TLS record
// prevents a failed native unprojection from recycling our hand origin.
thread_local animik::RayOriginEdit gLastOriginEdit;

std::atomic<bool> gEnabled{false};
std::atomic<unsigned long long> gApplied{0};
std::atomic<unsigned long long> gRejNoPose{0};
std::atomic<unsigned long long> gRejNoPlayer{0};
std::atomic<unsigned long long> gRejCompose{0};
std::atomic<unsigned int> gNativeMagnitude{0};
// 0 native eye, 1 the tracked hand, 2 the calibrated muzzle.
std::atomic<unsigned int> gOriginMode{0};
std::atomic<unsigned long long> gMuzzleOriginApplied{0}, gMuzzleUnavailable{0};
std::atomic<bool> gBodyYaw{true};
std::atomic<int> gCamYawMilli{0}, gHeadYawMilli{0}, gPlaySpaceYawMilli{0};
std::atomic<int> gNativeEyeMilli[3]{0,0,0};
std::atomic<int> gTrackedHeadMilli[3]{0,0,0};
// The last head yaw that was defined. Near-vertical, the head's yaw is genuinely
// undefined but the BODY's has not changed, so holding this keeps the frame
// correct -- and camera yaw still carries body turning, so the player can look
// at the ceiling and keep walking in a circle without the hands drifting.
std::atomic<float> gLastHeadYaw{0.0f};
std::atomic<bool> gHaveHeadYaw{false};
std::atomic<unsigned long long> gHeadYawHeld{0}, gHeadYawUnavailable{0};
std::atomic<unsigned long long> gOriginApplied{0};

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

// The yaw the engine's own view camera is facing. Read live rather than cached,
// because the whole point is to follow the player's facing as it changes.
float GameCameraYaw()
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return 0.0f;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    auto* const systemPtr =
        *reinterpret_cast<std::uint8_t**>(base + engine::SystemLayout::pointerRva);
    if (systemPtr == nullptr) {
        return 0.0f;
    }
    const auto* const camera = reinterpret_cast<const std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(systemPtr) + engine::SystemLayout::viewCamera);
    const stereo::Matrix34 matrix =
        stereo::ReadMatrix(std::span<const std::uint8_t>(camera, engine::CameraLayout::size));
    return stereo::CameraYawOf(matrix);
}

void __fastcall UpdateCachedRayWithTakeover(void* player)
{
    // Every refusal below invalidates the previous publication immediately.
    gAimSample.Clear();
    const UpdateCachedRayFn original = gOriginal.load(std::memory_order_acquire);
    if (player && gLastOriginEdit.owner == reinterpret_cast<std::uintptr_t>(player)) {
        auto* originAt = static_cast<std::uint8_t*>(player) + engine::ArkPlayerLayout::cachedReticleOrigin;
        Vec3 current{};
        std::memcpy(&current, originAt, sizeof(current));
        if (gLastOriginEdit.Restore(reinterpret_cast<std::uintptr_t>(player), current)) {
            std::memcpy(originAt, &current, sizeof(current));
        }
        gLastOriginEdit = {};
    }
    if (original != nullptr) {
        original(player);   // let the engine build its own ray first
    }
    if (player == nullptr || original == nullptr) {
        gGameplayFrame.Clear();
        gRejNoPlayer.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto playerAddress = reinterpret_cast<std::uintptr_t>(player);
    GameplayPoseFrame frame{};
    frame.player = playerAddress;
    std::memcpy(&frame.nativeEye, reinterpret_cast<const void*>(
        playerAddress + engine::ArkPlayerLayout::cachedReticleOrigin), sizeof(Vec3));
    // The view camera's own translation -- Matrix34, translation in column 3,
    // so elements 3, 7 and 11. This is the gameplay camera on the game thread,
    // before the render seam installs a per-eye offset, so it carries no half
    // IPD (the trap H-005C / FAIL-HAND-037 records for the live render camera).
    frame.cameraCentreValid = false;
    if (const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll")) {
        const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
        if (auto* const systemPtr =
                *reinterpret_cast<std::uint8_t**>(base + engine::SystemLayout::pointerRva)) {
            const auto* const camera = reinterpret_cast<const float*>(
                reinterpret_cast<std::uintptr_t>(systemPtr) + engine::SystemLayout::viewCamera +
                engine::CameraLayout::matrix);
            const Vec3 centre{camera[3], camera[7], camera[11]};
            if (std::isfinite(centre.x) && std::isfinite(centre.y) && std::isfinite(centre.z)) {
                frame.cameraCentre = centre;
                frame.cameraCentreValid = true;
            }
        }
    }
    Vec3 nativeDirection{};
    std::memcpy(&nativeDirection, reinterpret_cast<const void*>(
        playerAddress + engine::ArkPlayerLayout::cachedReticleDirection), sizeof(Vec3));
    const float nativeLength = Length(nativeDirection);
    if (!std::isfinite(nativeLength) || nativeLength < 0.9f || nativeLength > 1.1f ||
        !std::isfinite(frame.nativeEye.x) || !std::isfinite(frame.nativeEye.y) ||
        !std::isfinite(frame.nativeEye.z)) {
        gGameplayFrame.Clear();
        gRejCompose.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    gNativeMagnitude.store(static_cast<unsigned int>(nativeLength * 1000.0f + 0.5f));
    const bool haveTracking = TryGetTrackingFrame(frame.tracking) &&
        IsPoseUsable(frame.tracking.head, frame.tracking.headValidity, 200000000ull);
    frame.referenceGeneration = HeadTrackingReferenceGeneration();
    frame.referenceYaw = HeadTrackingReferenceYaw();
    frame.cameraYaw = GameCameraYaw();
    // Camera yaw is body + (head - reference) because this mod writes head
    // tracking into that camera. Subtracting the head's own yaw leaves the
    // body, which is what a head-relative offset must be rotated by; using the
    // camera directly applies the head twice and the hand follows the headset.
    frame.yaw = frame.cameraYaw - frame.referenceYaw;   // aim.bodyyaw 0 behaviour
    // Measured unconditionally. Recording it only on the body-yaw path made the
    // instrument unable to describe the mode it was meant to be compared
    // against: an A/B on 2026-09-08 reported a 0-degree head sweep for
    // `aim.bodyyaw 0` purely because nothing sampled the head there.
    const auto measuredHeadYaw = haveTracking
        ? stereo::RecenterYawFromHeadPose(frame.tracking.head)
        : std::optional<float>{};
    if (measuredHeadYaw) {
        gHeadYawMilli.store(static_cast<int>(*measuredHeadYaw * 57295.78f),
                            std::memory_order_relaxed);
    }
    if (gBodyYaw.load(std::memory_order_acquire)) {
        const auto headYaw = measuredHeadYaw;
        if (headYaw) {
            gLastHeadYaw.store(*headYaw, std::memory_order_release);
            gHaveHeadYaw.store(true, std::memory_order_release);
            frame.headYaw = *headYaw;
            frame.headYawUsable = true;
            frame.yaw = frame.cameraYaw - *headYaw;
        } else if (gHaveHeadYaw.load(std::memory_order_acquire)) {
            // Looking near-vertical. **Never fall through to the camera yaw
            // here**: that is precisely the defect this lane exists to fix, and
            // silently restoring it would make the hands swing again whenever a
            // player looked up. Hold the last defined head yaw instead -- the
            // body has not turned just because the head tilted, and body turning
            // still arrives through the camera yaw.
            const float held = gLastHeadYaw.load(std::memory_order_acquire);
            frame.headYaw = held;
            frame.headYawUsable = true;
            frame.yaw = frame.cameraYaw - held;
            gHeadYawHeld.fetch_add(1, std::memory_order_relaxed);
        } else {
            // No head yaw has ever been defined. Reject rather than invent:
            // consumers below refuse the sample instead of driving from a frame
            // whose orientation is unknown.
            frame.headYawUsable = false;
            gHeadYawUnavailable.fetch_add(1, std::memory_order_relaxed);
        }
    }
    gCamYawMilli.store(static_cast<int>(frame.cameraYaw * 57295.78f), std::memory_order_relaxed);
    gPlaySpaceYawMilli.store(static_cast<int>(frame.yaw * 57295.78f), std::memory_order_relaxed);
    // The anchor the hand is placed against, and the tracked head it is measured
    // from. A 2026-09-08 measurement found the hand still moving 0.74 mm per
    // degree of head yaw at r = -0.92 with the controller resting motionless,
    // *after* the play-space yaw was proved decoupled. The remaining suspect is
    // these two disagreeing: the composition assumes the native eye translates
    // by the same amount as the tracked head, and if it translates by less, the
    // difference lands in the hand. Reporting both makes that one measurement.
    for (unsigned int i = 0; i < 3; ++i) {
        const float eye = (&frame.nativeEye.x)[i];
        const float head = (&frame.tracking.head.position.x)[i];
        gNativeEyeMilli[i].store(static_cast<int>(eye * 1000.0f), std::memory_order_relaxed);
        gTrackedHeadMilli[i].store(static_cast<int>(head * 1000.0f), std::memory_order_relaxed);
    }
    if ((frame.referenceGeneration & 1) ||
        frame.referenceGeneration != HeadTrackingReferenceGeneration() || !std::isfinite(frame.yaw)) { gGameplayFrame.Clear(); return; }
    // IK must never reread the mutable cached origin below.
    frame.publishedNs = MonotonicNanoseconds();
    gGameplayFrame.Publish(frame);
    if (!haveTracking) { gRejNoPose.fetch_add(1); return; }
    // A frame whose play-space yaw is unknown cannot aim, and must not silently
    // aim with the camera-relative yaw the body-yaw mode exists to replace.
    if (gBodyYaw.load(std::memory_order_acquire) && !frame.headYawUsable) {
        gRejNoPose.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!gEnabled.load(std::memory_order_acquire)) { return; }
    const auto& controller = frame.tracking.hands[static_cast<unsigned int>(Hand::right)];
    const auto& headPose = frame.tracking.head;
    const Vec3 engineOrigin = frame.nativeEye;
    stereo::ReferenceFrame reference{};
    reference.yawRadians = frame.yaw;
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

    // Describe the ray actually installed below, not the best origin available
    // regardless of aim.origin. A positional muzzle calibration does not prove
    // a barrel rotation: this direction is still the runtime's aiming axis.
    preyvr::aim::Sample sample{};
    sample.origin = engineOrigin;
    sample.direction = ray->direction;
    const Pose hand = animik::ControllerWorldFromHead(
        reference.yawRadians, engineOrigin, headPose, controller.aimPose);
    sample.orientation = hand.orientation; // world space, like the direction
    sample.confidence = preyvr::aim::Confidence::origin;
    sample.equipGeneration = WeaponEquipGeneration();
    sample.referenceGeneration = frame.referenceGeneration;
    sample.trackingSequence = frame.tracking.sequence;

    // This selects a pointing direction. Native firing may converge from its
    // authored muzzle toward this ray; a controller point is not that muzzle.
    std::memcpy(reinterpret_cast<void*>(
                    playerAddress + engine::ArkPlayerLayout::cachedReticleDirection),
                &ray->direction, sizeof(ray->direction));
    gApplied.fetch_add(1, std::memory_order_relaxed);

    const unsigned int originMode = gOriginMode.load(std::memory_order_acquire);
    if (originMode != 0) {
        Vec3 chosen = hand.position;
        bool haveMuzzle = false;
        if (originMode == 2) {
            // **The reconciliation.** Prey's projectile already leaves the
            // weapon's authored muzzle helper while the reticle ray starts at
            // the eye, so a shot flies from the muzzle toward wherever an eye
            // ray landed and the two agree at exactly one distance. Starting the
            // ray at that same muzzle removes the convergence entirely.
            //
            // The GRIP pose is used, not the aim pose, because the calibration
            // was measured against the grip: an offset is only meaningful in
            // the frame it was captured in.
            const Pose grip = animik::ControllerWorldFromHead(
                reference.yawRadians, engineOrigin, headPose, controller.gripPose);
            Vec3 muzzle{};
            if (IsPoseUsable(controller.gripPose, controller.gripValidity, 200000000ull) &&
                TryGetMuzzleFromGrip(grip, sample.equipGeneration, muzzle)) {
                chosen = muzzle;
                haveMuzzle = true;
            } else {
                // Uncalibrated, or a different weapon since. Fall back to the
                // hand and COUNT it: silently reverting to a less accurate
                // origin is how a lane looks fine and aims wrong.
                gMuzzleUnavailable.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (std::isfinite(chosen.x) && std::isfinite(chosen.y) && std::isfinite(chosen.z)) {
            std::memcpy(reinterpret_cast<void*>(
                            playerAddress + engine::ArkPlayerLayout::cachedReticleOrigin),
                        &chosen, sizeof(chosen));
            gLastOriginEdit = {playerAddress, engineOrigin, chosen};
            sample.origin = chosen;
            gOriginApplied.fetch_add(1, std::memory_order_relaxed);
            if (haveMuzzle) { gMuzzleOriginApplied.fetch_add(1, std::memory_order_relaxed); }
        }
    }

    // Legacy latest-value telemetry; use reticleProjection's coherent render
    // record to compare a ray with its actual projection camera and raw pose.
    for (unsigned int i = 0; i < 3; ++i) {
        gAimDirMilli[i].store(static_cast<int>((&sample.direction.x)[i] * 1000.0f),
                              std::memory_order_relaxed);
        gAimRawDirMilli[i].store(
            static_cast<int>((&controller.aimPose.orientation.x)[i] * 1000.0f),
            std::memory_order_relaxed);
    }
    sample.publishedNs = MonotonicNanoseconds();
    gAimSample.Publish(sample);
    gAimSamplePublished.fetch_add(1, std::memory_order_relaxed);

    // Screen projection is deferred until CSystem::Render has installed the
    // actual eye camera. Here the global camera still has gameplay projection;
    // the per-eye projection is transient and restored after each render.
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
    // Shared and idempotent: without it MH_CreateHook returns
    // MH_ERROR_NOT_INITIALIZED and the feature reports "unavailable" for a
    // reason unrelated to itself. See MinHookInit.h.
    EnsureMinHook();
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

bool TryGetAimSample(preyvr::aim::Sample& out) { return gAimSample.TryRead(out); }

void UpdateAimReticleForRender()
{
    if (!gEnabled.load(std::memory_order_acquire)) { return; }
    preyvr::aim::Sample sample{};
    GameplayPoseFrame frame{};
    if (!TryGetAimSample(sample) || !TryGetGameplayPoseFrame(frame) ||
        sample.trackingSequence != frame.tracking.sequence ||
        !preyvr::aim::Usable(sample, WeaponEquipGeneration(),
                            HeadTrackingReferenceGeneration(), MonotonicNanoseconds())) {
        return;
    }
    const ReticleAimContext context{frame.tracking.sequence, frame.tracking.epoch,
        frame.referenceGeneration, frame.tracking.displayTime, frame.yaw,
        frame.tracking.head, frame.tracking.hands[static_cast<int>(Hand::right)].aimPose};
    WriteReticleScreenPosition(reinterpret_cast<void*>(frame.player),
                               sample.origin, sample.direction, &context);
}
int AimDirectionMilli(unsigned int axis)
{
    return axis < 3 ? gAimDirMilli[axis].load(std::memory_order_relaxed) : 0;
}
int AimRawOrientationMilli(unsigned int axis)
{
    return axis < 3 ? gAimRawDirMilli[axis].load(std::memory_order_relaxed) : 0;
}
unsigned long long AimSamplePublishedCount()
{
    return gAimSamplePublished.load(std::memory_order_relaxed);
}

bool EnsureGameplayPoseObservation() { return Install(); }

bool TryGetGameplayPoseFrame(GameplayPoseFrame& out, bool requireTracking)
{
    if (!gGameplayFrame.TryRead(out)) { return false; }
    if (!FreshSample(MonotonicNanoseconds(), out.publishedNs)) { return false; }
    if (!requireTracking) { return true; }
    TrackingFrame current{};
    // Also require a currently focused publication. A cleared XR slot must
    // invalidate an otherwise young cached gameplay frame immediately.
    if (!TryGetTrackingFrame(current) || out.tracking.epoch != current.epoch ||
        out.referenceGeneration != HeadTrackingReferenceGeneration()) { return false; }
    const auto now = MonotonicNanoseconds();
    if (!FreshSample(now, out.tracking.publishedNs)) { return false; }
    const auto age = now - out.tracking.publishedNs;
    out.tracking.headValidity.ageNanoseconds = age;
    for (auto& hand : out.tracking.hands) {
        hand.gripValidity.ageNanoseconds = age;
        hand.aimValidity.ageNanoseconds = age;
    }
    return IsPoseUsable(out.tracking.head, out.tracking.headValidity, 200000000ull);
}

DWORD SetAimBodyYaw(unsigned int enabled)
{
    if (enabled > 1) { return 1; }
    gBodyYaw.store(enabled != 0u, std::memory_order_release);
    Log(std::string("result=0 detail=body_yaw value=") + (enabled ? "1" : "0"));
    return 0;
}

unsigned int AimBodyYawEnabled() { return gBodyYaw.load(std::memory_order_relaxed) ? 1u : 0u; }
unsigned long long AimHeadYawHeldCount() { return gHeadYawHeld.load(std::memory_order_relaxed); }
unsigned long long AimHeadYawUnavailableCount() { return gHeadYawUnavailable.load(std::memory_order_relaxed); }
int AimCameraYawMilliDegrees() { return gCamYawMilli.load(std::memory_order_relaxed); }
int AimHeadYawMilliDegrees() { return gHeadYawMilli.load(std::memory_order_relaxed); }
int AimPlaySpaceYawMilliDegrees() { return gPlaySpaceYawMilli.load(std::memory_order_relaxed); }
int AimNativeEyeMillimetres(unsigned int axis) { return axis < 3 ? gNativeEyeMilli[axis].load(std::memory_order_relaxed) : 0; }
int AimTrackedHeadMillimetres(unsigned int axis) { return axis < 3 ? gTrackedHeadMilli[axis].load(std::memory_order_relaxed) : 0; }

DWORD SetAimOriginFromHand(unsigned int mode)
{
    if (mode > 2u) { return 1; }
    gOriginMode.store(mode, std::memory_order_release);
    Log("result=0 detail=origin_mode value=" + std::to_string(mode));
    return 0;
}

unsigned int AimOriginMode() { return gOriginMode.load(std::memory_order_relaxed); }
unsigned long long AimMuzzleOriginAppliedCount()
{
    return gMuzzleOriginApplied.load(std::memory_order_relaxed);
}
unsigned long long AimMuzzleUnavailableCount()
{
    return gMuzzleUnavailable.load(std::memory_order_relaxed);
}

unsigned long long AimOriginAppliedCount() { return gOriginApplied.load(std::memory_order_relaxed); }

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
    if (!on) { gAimSample.Clear(); }
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
