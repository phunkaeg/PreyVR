#include "ReticleFollow.h"

#include "HudBridge.h"
#include "CameraEditHook.h"
#include "Logger.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/StereoFrame.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <span>

namespace preyvr::dll {
namespace {

std::atomic<bool> gEnabled{false};
std::atomic<bool> gDispatch{true};
std::atomic<unsigned long long> gApplied{0};
std::atomic<unsigned long long> gOffScreen{0};
std::atomic<unsigned long long> gDispatched{0};
std::atomic<unsigned long long> gDispatchFailed{0};
std::atomic<unsigned int> gLastX{500};
std::atomic<unsigned int> gLastY{500};
std::atomic<unsigned long long> gOriginOffset{0};

// **The distance at which the crosshair is exact.** A crosshair marks one point,
// and a shot leaving the muzzle rather than the eye reaches a different screen
// position at every distance, so no single symbol can be right at all of them
// without a raycast. Ten metres is a typical engagement distance; the residual
// error grows toward the near end, which is where an eye-origin ray and a
// muzzle-origin shot disagree most and therefore where this is worth judging.
constexpr unsigned int kDefaultConvergenceMm = 10000;
std::atomic<unsigned int> gConvergenceMm{kDefaultConvergenceMm};

// The two names the engine's own reticle reset dispatches, read from that call
// site rather than guessed (R-109). Names not read from a native call site are
// not known to exist: Scaleform silently ignores an absent function and the
// dispatcher still returns success, so an invented name looks like it worked
// (F-011).
constexpr const char* kReticleXOffset = "reticleXOffset";
constexpr const char* kReticleYOffset = "reticleYOffset";

} // namespace

DWORD SetReticleFollowEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on) {
        if (EnsureRenderHookInstalled() != 0) { return 1; }
        gApplied.store(0, std::memory_order_relaxed);
        gOffScreen.store(0, std::memory_order_relaxed);
    }
    gEnabled.store(on, std::memory_order_release);
    lifecycle::Log(std::string("preyvr_reticle result=0 detail=enabled value=") +
                   (on ? "1" : "0"));
    return 0;
}

DWORD SetReticleConvergenceMillimetres(unsigned int millimetres)
{
    // Clamped rather than rejected: a zero would put the aim point at the muzzle
    // itself, which projects to a meaningless screen position, and an absurd
    // value is the same as infinity anyway.
    const unsigned int clamped = millimetres < 500u      ? 500u
                                 : (millimetres > 200000u ? 200000u : millimetres);
    gConvergenceMm.store(clamped, std::memory_order_release);
    lifecycle::Log("preyvr_reticle result=0 detail=convergence mm=" + std::to_string(clamped));
    return 0;
}

DWORD ReticleConvergenceMillimetres() { return gConvergenceMm.load(std::memory_order_relaxed); }

bool WriteReticleScreenPosition(void* player, const Vec3& rayOrigin, const Vec3& worldDirection)
{
    if (!gEnabled.load(std::memory_order_acquire) || player == nullptr) {
        return false;
    }
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
    const auto cameraSpan =
        std::span<const std::uint8_t>(camera, engine::CameraLayout::size);

    // The camera's own frustum, so the projection matches the player's FOV slider
    // rather than a value measured once on one machine.
    const auto view = stereoframe::TangentsFromCamera(cameraSpan);
    if (!view) {
        return false;
    }

    // The ray into camera space. Engine basis: column 0 right, column 1 forward,
    // column 2 up, so a dot with each axis gives the component along it.
    const stereo::Matrix34 matrix = stereo::ReadMatrix(cameraSpan);
    const Vec3 right{matrix[0], matrix[4], matrix[8]};
    const Vec3 forward{matrix[1], matrix[5], matrix[9]};
    const Vec3 up{matrix[2], matrix[6], matrix[10]};

    // **The third side of the reconciliation, and the one that was missing.**
    // The weapon lane starts the shot at the calibrated muzzle; the aim lane
    // publishes that same origin. The crosshair was projecting a bare DIRECTION
    // from the camera, which is the screen position of an eye-origin ray -- so
    // the symbol and the shot were computed from different origins and agreed
    // only at infinity. That is the identical parallax the barrel calibration
    // was built to remove from firing, left in place for the crosshair.
    //
    // Projecting an actual point on the firing ray fixes it. Where the origin
    // IS the camera -- native origin mode, or an uncalibrated weapon -- the
    // offset is zero and this reduces exactly to the previous behaviour, so it
    // is a generalisation rather than a second code path.
    const Vec3 cameraPosition = stereo::PositionOf(matrix);
    const Vec3 offset{rayOrigin.x - cameraPosition.x, rayOrigin.y - cameraPosition.y,
                      rayOrigin.z - cameraPosition.z};
    const float offsetLength =
        std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
    if (!std::isfinite(offsetLength) || offsetLength > 5.0f) {
        // An origin metres from the camera is a bad sample, not a long weapon.
        // Refusing beats moving the crosshair somewhere arbitrary.
        gOffScreen.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    gOriginOffset.store(static_cast<unsigned long long>(offsetLength * 1000.0f + 0.5f),
                        std::memory_order_relaxed);

    const float distance =
        static_cast<float>(gConvergenceMm.load(std::memory_order_acquire)) * 0.001f;
    const Vec3 aimPoint{offset.x + worldDirection.x * distance,
                        offset.y + worldDirection.y * distance,
                        offset.z + worldDirection.z * distance};

    const float alongForward =
        aimPoint.x * forward.x + aimPoint.y * forward.y + aimPoint.z * forward.z;
    if (!(alongForward > 0.001f)) {
        // Behind the camera. No honest screen position exists, so the crosshair is
        // left where the engine put it -- pinning it to an edge would claim the
        // target is over there when it is not.
        gOffScreen.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const float alongRight =
        aimPoint.x * right.x + aimPoint.y * right.y + aimPoint.z * right.z;
    const float alongUp = aimPoint.x * up.x + aimPoint.y * up.y + aimPoint.z * up.z;

    // Tangents at the near plane, mapped into the frustum the camera actually has.
    // Using the asymmetric edges rather than assuming a symmetric field means this
    // stays correct if a per-eye projection is ever in play.
    const float tanX = alongRight / alongForward;
    const float tanY = alongUp / alongForward;
    const float spanX = view->tanRight - view->tanLeft;
    const float spanY = view->tanUp - view->tanDown;
    if (!(spanX > 0.0f) || !(spanY > 0.0f)) {
        return false;
    }
    float fractionX = (tanX - view->tanLeft) / spanX;
    // Screen Y runs downward while the up axis runs upward, so this inverts.
    float fractionY = 1.0f - (tanY - view->tanDown) / spanY;

    if (!std::isfinite(fractionX) || !std::isfinite(fractionY)) {
        gOffScreen.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (fractionX < 0.0f || fractionX > 1.0f || fractionY < 0.0f || fractionY > 1.0f) {
        // **Clamped to the edge, not left where it was.** Returning early here
        // leaves the previous position in place, so the crosshair stays sitting
        // over whatever it happened to be over when the aim left the screen --
        // an indicator that is confidently wrong, which is worse than one that
        // is obviously at a limit. The reticle report names this exactly:
        // "hide or deliberately replace an offscreen aiming symbol rather than
        // leaving a stale one over an unrelated object."
        //
        // Clamping is the honest half of that: the symbol pins to the edge the
        // aim went out of, so it reads as "off that way" rather than as a lock
        // on the wrong thing. Hiding it properly needs the native reticleDisplay
        // dispatch and is a separate, larger piece of work.
        gOffScreen.fetch_add(1, std::memory_order_relaxed);
        fractionX = fractionX < 0.0f ? 0.0f : (fractionX > 1.0f ? 1.0f : fractionX);
        fractionY = fractionY < 0.0f ? 0.0f : (fractionY > 1.0f ? 1.0f : fractionY);
    }

    float screen[2] = {fractionX, fractionY};
    std::memcpy(reinterpret_cast<void*>(
                    reinterpret_cast<std::uintptr_t>(player) +
                    engine::ArkPlayerLayout::reticleScreenPosition),
                screen, sizeof(screen));
    gLastX.store(static_cast<unsigned int>(fractionX * 1000.0f + 0.5f),
                 std::memory_order_relaxed);
    gLastY.store(static_cast<unsigned int>(fractionY * 1000.0f + 0.5f),
                 std::memory_order_relaxed);
    gApplied.fetch_add(1, std::memory_order_relaxed);

    // **The write alone does not move the crosshair.** The engine's own reticle
    // reset is one short function that writes these two fields and then
    // dispatches both names on the HUD element, in that order (R-109) -- the
    // field is where the value is kept, the dispatch is what the movie reads.
    // Writing one without the other is the defect the reticle report named:
    // proof of a memory write, and no visual movement.
    //
    // The units come from that same function: it stores 0x3F000000 -- 0.5f --
    // for centred X, so these are normalised screen fractions, which is exactly
    // what this lane already computes.
    //
    // Dispatch runs on the main game thread, after the render seam installs
    // this eye's camera. It is separately
    // switchable so that a crosshair which does not move can be attributed --
    // dispatch off isolates the write, dispatch on adds the movie call.
    if (gDispatch.load(std::memory_order_acquire)) {
        const DWORD x = CallHudOneFloat(kReticleXOffset, fractionX);
        const DWORD y = CallHudOneFloat(kReticleYOffset, fractionY);
        if (x == 0 && y == 0) {
            gDispatched.fetch_add(1, std::memory_order_relaxed);
        } else {
            gDispatchFailed.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return true;
}

DWORD SetReticleDispatchEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    gDispatched.store(0, std::memory_order_relaxed);
    gDispatchFailed.store(0, std::memory_order_relaxed);
    gDispatch.store(on, std::memory_order_release);
    lifecycle::Log(std::string("preyvr_reticle result=0 detail=dispatch value=") +
                   (on ? "1" : "0"));
    return 0;
}

unsigned long long ReticleOriginOffsetMillimetres()
{
    return gOriginOffset.load(std::memory_order_relaxed);
}
unsigned long long ReticleDispatchedCount() { return gDispatched.load(std::memory_order_relaxed); }
unsigned long long ReticleDispatchFailedCount()
{
    return gDispatchFailed.load(std::memory_order_relaxed);
}
unsigned long long ReticleFollowAppliedCount() { return gApplied.load(std::memory_order_relaxed); }
unsigned long long ReticleFollowOffScreenCount() { return gOffScreen.load(std::memory_order_relaxed); }
DWORD ReticleFollowLastX() { return gLastX.load(std::memory_order_relaxed); }
DWORD ReticleFollowLastY() { return gLastY.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
