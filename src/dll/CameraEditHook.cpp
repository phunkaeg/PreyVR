#include "CameraEditHook.h"

#include "Logger.h"
#include "preyvr/CameraEdit.h"
#include "preyvr/EngineMap.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>

namespace preyvr::dll {
namespace {

// CSystem::Render (R-058). Prologue stops before the jne's rel32 so the
// signature is the stable part: push r15 / sub rsp,0xD0 / cmp byte [rcx+0x9D7],0
// / mov r15,rcx. The +0x9D7 test is what makes it distinctive rather than a
// generic MSVC prologue.
constexpr std::uintptr_t kSystemRenderRva = 0xE0BA30;
constexpr std::array<std::uint8_t, 19> kSystemRenderPrologue{
    0x41, 0x57, 0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00, 0x80,
    0xB9, 0xD7, 0x09, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xF9,
};

// CCamera::UpdateFrustum. Rebuilds the corners, the six planes at +0x10C, the
// plane sign tables and the cached position at +0x230.
constexpr std::uintptr_t kUpdateFrustumRva = 0x121D70;
constexpr std::array<std::uint8_t, 20> kUpdateFrustumPrologue{
    0x48, 0x8B, 0xC4, 0x55, 0x53, 0x48, 0x8D, 0x68, 0xA1, 0x48,
    0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x59,
};

using SystemRenderFn = void(__fastcall*)(void* system);
using UpdateFrustumFn = void(__fastcall*)(void* camera);

std::mutex gMutex;
std::atomic<DWORD> gStatus{static_cast<DWORD>(CameraEditStatus::unavailable)};
std::atomic<SystemRenderFn> gOriginal{nullptr};
std::atomic<UpdateFrustumFn> gUpdateFrustum{nullptr};
std::atomic<float> gYawDegrees{0.0f};
std::atomic<bool> gArmed{false};
std::atomic<unsigned long long> gApplied{0};
std::atomic<unsigned long long> gRestoreFailures{0};
std::atomic<bool> gLoggedFirstApplication{false};
void* gTarget = nullptr;
bool gHookCreated = false;

bool PrologueMatches(std::uintptr_t address, const std::uint8_t* expected, std::size_t length)
{
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) !=
            sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY)) == 0) {
        return false;
    }
    return std::memcmp(reinterpret_cast<const void*>(address), expected, length) == 0;
}

void __fastcall RenderWithCameraEdit(void* system)
{
    const SystemRenderFn original = gOriginal.load(std::memory_order_acquire);

    if (!gArmed.load(std::memory_order_acquire) || system == nullptr) {
        if (original != nullptr) {
            original(system);
        }
        return;
    }

    auto* camera = reinterpret_cast<std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(system) + engine::SystemLayout::viewCamera);
    const std::span<std::uint8_t> live(camera, cameraedit::kCameraSize);

    cameraedit::RestorePoint restore{};
    if (!cameraedit::Capture(live, restore)) {
        if (original != nullptr) {
            original(system);
        }
        return;
    }

    // Build the edited camera entirely in our own memory first. UpdateFrustum is
    // called on this copy, so the engine function never touches game state and
    // an aborted edit costs nothing.
    std::array<std::uint8_t, cameraedit::kCameraSize> edited{};
    std::memcpy(edited.data(), restore.bytes.data(), cameraedit::kCameraSize);

    const cameraedit::YawEdit edit{gYawDegrees.load(std::memory_order_acquire)};
    const UpdateFrustumFn updateFrustum = gUpdateFrustum.load(std::memory_order_acquire);
    if (!cameraedit::ApplyYaw(edited, edit) || updateFrustum == nullptr) {
        if (original != nullptr) {
            original(system);
        }
        return;
    }
    updateFrustum(edited.data());

    // Gated on both predicates: orthonormal keeps the engine from negating its
    // plane normals, and non-degenerate closes the hole where an all-zero matrix
    // satisfies the engine's own check.
    if (!cameraedit::RotationIsSafeToWrite(edited)) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=unsafe_rotation");
        gArmed.store(false, std::memory_order_release);
        if (original != nullptr) {
            original(system);
        }
        return;
    }

    std::memcpy(camera, edited.data(), cameraedit::kCameraSize);

    if (original != nullptr) {
        original(system);
    }

    std::memcpy(camera, restore.bytes.data(), cameraedit::kCameraSize);

    // Verified, not assumed. A restore that is merely performed is a hope.
    if (!cameraedit::MatchesRestorePoint(live, restore)) {
        gRestoreFailures.fetch_add(1, std::memory_order_relaxed);
        gArmed.store(false, std::memory_order_release);
        lifecycle::Log("preyvr_camera_edit result=restore_failed detail=disarmed");
        return;
    }

    const unsigned long long applied = gApplied.fetch_add(1, std::memory_order_relaxed) + 1;
    bool expected = false;
    if (gLoggedFirstApplication.compare_exchange_strong(expected, true)) {
        // Logged once rather than per frame: at 144 Hz a per-frame line would
        // bury everything else in the smoke log within seconds.
        const auto position = cameraedit::PositionOf(restore.bytes);
        std::ostringstream line;
        line << "preyvr_camera_edit result=0 detail=first_application"
             << " yawDegrees=" << edit.degrees
             << " originalPos=" << position[0] << ',' << position[1] << ',' << position[2]
             << " applied=" << applied;
        lifecycle::Log(line.str());
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

    const auto renderAddress = base + kSystemRenderRva;
    const auto frustumAddress = base + kUpdateFrustumRva;
    if (!PrologueMatches(renderAddress, kSystemRenderPrologue.data(), kSystemRenderPrologue.size())) {
        lifecycle::Log("preyvr_camera_edit result=unavailable detail=system_render_prologue");
        return false;
    }
    if (!PrologueMatches(frustumAddress, kUpdateFrustumPrologue.data(),
                         kUpdateFrustumPrologue.size())) {
        lifecycle::Log("preyvr_camera_edit result=unavailable detail=update_frustum_prologue");
        return false;
    }
    gUpdateFrustum.store(reinterpret_cast<UpdateFrustumFn>(frustumAddress),
                         std::memory_order_release);

    gTarget = reinterpret_cast<void*>(renderAddress);
    SystemRenderFn original = nullptr;
    MH_STATUS status = MH_CreateHook(
        gTarget, reinterpret_cast<void*>(&RenderWithCameraEdit),
        reinterpret_cast<void**>(&original));
    if (status != MH_OK) {
        std::ostringstream line;
        line << "preyvr_camera_edit result=failed detail=create_hook minhook="
             << MH_StatusToString(status);
        lifecycle::Log(line.str());
        return false;
    }
    gOriginal.store(original, std::memory_order_release);

    status = MH_EnableHook(gTarget);
    if (status != MH_OK) {
        std::ostringstream line;
        line << "preyvr_camera_edit result=failed detail=enable_hook minhook="
             << MH_StatusToString(status);
        lifecycle::Log(line.str());
        return false;
    }

    gHookCreated = true;
    gStatus.store(static_cast<DWORD>(CameraEditStatus::ready), std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera_edit result=ready targetRva=0x" << std::hex << std::uppercase
         << kSystemRenderRva << " target=0x" << renderAddress;
    lifecycle::Log(line.str());
    return true;
}

} // namespace

DWORD SetCameraYawEdit(float degrees)
{
    std::lock_guard lock(gMutex);

    if (degrees == 0.0f) {
        gArmed.store(false, std::memory_order_release);
        if (gHookCreated) {
            gStatus.store(static_cast<DWORD>(CameraEditStatus::ready), std::memory_order_release);
        }
        lifecycle::Log("preyvr_camera_edit result=0 detail=disarmed");
        return gStatus.load(std::memory_order_acquire);
    }

    if (!cameraedit::IsWithinBounds({degrees})) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=out_of_bounds");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }

    if (!EnsureHook()) {
        gStatus.store(static_cast<DWORD>(CameraEditStatus::unavailable), std::memory_order_release);
        return static_cast<DWORD>(CameraEditStatus::unavailable);
    }

    gYawDegrees.store(degrees, std::memory_order_release);
    gLoggedFirstApplication.store(false, std::memory_order_release);
    gArmed.store(true, std::memory_order_release);
    gStatus.store(static_cast<DWORD>(CameraEditStatus::armed), std::memory_order_release);

    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=armed yawDegrees=" << degrees;
    lifecycle::Log(line.str());
    return static_cast<DWORD>(CameraEditStatus::armed);
}

DWORD CameraEditStatusValue()
{
    return gStatus.load(std::memory_order_acquire);
}

unsigned long long CameraEditAppliedCount()
{
    return gApplied.load(std::memory_order_acquire);
}

unsigned long long CameraEditRestoreFailureCount()
{
    return gRestoreFailures.load(std::memory_order_acquire);
}

} // namespace preyvr::dll
