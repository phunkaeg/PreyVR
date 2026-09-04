#include "ReticleFollow.h"

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
std::atomic<unsigned long long> gApplied{0};
std::atomic<unsigned long long> gOffScreen{0};
std::atomic<unsigned int> gLastX{500};
std::atomic<unsigned int> gLastY{500};

} // namespace

DWORD SetReticleFollowEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on) {
        gApplied.store(0, std::memory_order_relaxed);
        gOffScreen.store(0, std::memory_order_relaxed);
    }
    gEnabled.store(on, std::memory_order_release);
    lifecycle::Log(std::string("preyvr_reticle result=0 detail=enabled value=") +
                   (on ? "1" : "0"));
    return 0;
}

bool WriteReticleScreenPosition(void* player, const Vec3& worldDirection)
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
    const float alongForward = worldDirection.x * forward.x + worldDirection.y * forward.y +
                               worldDirection.z * forward.z;
    if (!(alongForward > 0.001f)) {
        // Behind the camera. No honest screen position exists, so the crosshair is
        // left where the engine put it -- pinning it to an edge would claim the
        // target is over there when it is not.
        gOffScreen.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const float alongRight = worldDirection.x * right.x + worldDirection.y * right.y +
                             worldDirection.z * right.z;
    const float alongUp = worldDirection.x * up.x + worldDirection.y * up.y +
                          worldDirection.z * up.z;

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
    const float fractionX = (tanX - view->tanLeft) / spanX;
    // Screen Y runs downward while the up axis runs upward, so this inverts.
    const float fractionY = 1.0f - (tanY - view->tanDown) / spanY;

    if (!std::isfinite(fractionX) || !std::isfinite(fractionY) ||
        fractionX < 0.0f || fractionX > 1.0f || fractionY < 0.0f || fractionY > 1.0f) {
        gOffScreen.fetch_add(1, std::memory_order_relaxed);
        return false;
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
    return true;
}

unsigned long long ReticleFollowAppliedCount() { return gApplied.load(std::memory_order_relaxed); }
unsigned long long ReticleFollowOffScreenCount() { return gOffScreen.load(std::memory_order_relaxed); }
DWORD ReticleFollowLastX() { return gLastX.load(std::memory_order_relaxed); }
DWORD ReticleFollowLastY() { return gLastY.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
