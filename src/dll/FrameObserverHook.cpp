#include "FrameObserverHook.h"

#include "Bootstrap.h"
#include "ConsoleBridgeWin32.h"
#include "FrameCaptureWin32.h"
#include "Logger.h"
#include "XrSessionHost.h"
#include "preyvr/FrameObserver.h"

#include <MinHook.h>

#include <atomic>
#include <mutex>
#include <sstream>

namespace preyvr::dll {
namespace {

using EndRendererSceneFn = void(__fastcall*)(void* renderer);

std::mutex gObserverMutex;
std::atomic<DWORD> gObserverStatus{
    static_cast<DWORD>(FrameObserverRuntimeStatus::unavailable)};
std::atomic<ULONGLONG> gFrameCount{0};
std::atomic<DWORD> gFirstThreadId{0};
std::atomic<LONGLONG> gFirstQpc{0};
std::atomic<void*> gFirstRenderer{nullptr};
std::atomic<DWORD> gMilestoneThreadId{0};
std::atomic<void*> gMilestoneRenderer{nullptr};
std::atomic<void*> gLastRenderer{nullptr};
void* gTarget = nullptr;
std::atomic<EndRendererSceneFn> gOriginal{nullptr};
bool gHookCreated = false;

void __fastcall ObserveEndRendererScene(void* renderer)
{
    const ULONGLONG count = gFrameCount.fetch_add(1, std::memory_order_relaxed) + 1;
    gLastRenderer.store(renderer, std::memory_order_relaxed);
    if (count == 1) {
        gFirstThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
        gFirstRenderer.store(renderer, std::memory_order_relaxed);
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        gFirstQpc.store(now.QuadPart, std::memory_order_relaxed);
    }
    else if (count == 120) {
        gMilestoneThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
        gMilestoneRenderer.store(renderer, std::memory_order_relaxed);
    }

    // Both return on a single atomic load when nothing is armed, so the
    // observer stays as close to free as it was. They run before the original
    // so a console command lands at a frame boundary, and so a capture reads the
    // backbuffer this callback was invoked for.
    ServiceConsoleQueue();
    ServiceFrameCapture(renderer, count);
    // The XR frame runs here because this is the render thread and the only
    // place Prey's backbuffer is valid. Returns on one atomic load when no
    // session is running.
    ServiceXrFrame(renderer);

    const EndRendererSceneFn original = gOriginal.load(std::memory_order_acquire);
    if (original != nullptr) {
        original(renderer);
    }
}

void SetFailed(const char* operation, MH_STATUS status)
{
    std::ostringstream line;
    line << "preyvr_frame_observer_result status=failed operation=" << operation
         << " minhook=" << MH_StatusToString(status);
    lifecycle::Log(line.str());
    gObserverStatus.store(
        static_cast<DWORD>(FrameObserverRuntimeStatus::failed),
        std::memory_order_release);
}

} // namespace

bool ConfigureFrameObserver(
    HMODULE preyDll,
    std::span<const std::uint8_t> mappedImage)
{
    std::lock_guard lock(gObserverMutex);
    const auto plan = PlanFrameObserver(
        mappedImage,
        reinterpret_cast<std::uintptr_t>(preyDll));
    if (!plan.has_value()) {
        lifecycle::Log("preyvr_frame_observer_plan status=unavailable reason=landmark_or_address");
        return false;
    }

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(
            reinterpret_cast<const void*>(plan->targetAddress),
            &memory,
            sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
        lifecycle::Log("preyvr_frame_observer_plan status=unavailable reason=target_not_executable");
        return false;
    }

    gTarget = reinterpret_cast<void*>(plan->targetAddress);
    gObserverStatus.store(
        static_cast<DWORD>(FrameObserverRuntimeStatus::ready),
        std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_frame_observer_plan status=ready targetRva=0x"
         << std::hex << std::uppercase << plan->targetRva
         << " target=0x" << plan->targetAddress
         << " enabled=0";
    lifecycle::Log(line.str());
    return true;
}

DWORD SetFrameObserverEnabled(bool enabled)
{
    std::lock_guard lock(gObserverMutex);
    const auto current = static_cast<FrameObserverRuntimeStatus>(
        gObserverStatus.load(std::memory_order_acquire));

    if (!enabled) {
        if (current != FrameObserverRuntimeStatus::enabled) {
            return static_cast<DWORD>(current);
        }
        MH_STATUS status = MH_DisableHook(gTarget);
        if (status != MH_OK) {
            SetFailed("disable", status);
            return static_cast<DWORD>(FrameObserverRuntimeStatus::failed);
        }

        // Keep the disabled hook entry and trampoline allocated until process exit.
        // MinHook can resume a thread that was already inside our detour when the
        // target patch is removed. Freeing the trampoline here would let that
        // in-flight callback call released executable memory. Supported hosts pin
        // this module, so retaining this small allocation is the safe lifecycle.
        gObserverStatus.store(
            static_cast<DWORD>(FrameObserverRuntimeStatus::ready),
            std::memory_order_release);
        std::ostringstream line;
        line << "preyvr_frame_observer_result status=disabled frames="
             << gFrameCount.load(std::memory_order_relaxed)
             << " firstThread=" << gFirstThreadId.load(std::memory_order_relaxed)
             << " firstRenderer=0x" << std::hex << std::uppercase
             << reinterpret_cast<std::uintptr_t>(
                    gFirstRenderer.load(std::memory_order_relaxed))
             << " milestone120=" << std::dec
             << (gFrameCount.load(std::memory_order_relaxed) >= 120 ? 1 : 0)
             << " milestoneThread="
             << gMilestoneThreadId.load(std::memory_order_relaxed)
             << " milestoneRenderer=0x" << std::hex << std::uppercase
             << reinterpret_cast<std::uintptr_t>(
                    gMilestoneRenderer.load(std::memory_order_relaxed));
        lifecycle::Log(line.str());
        return static_cast<DWORD>(FrameObserverRuntimeStatus::ready);
    }

    if (ModulePinStatus() == 0) {
        lifecycle::Log("preyvr_frame_observer_result status=not_pinned");
        return static_cast<DWORD>(current);
    }

    if (current == FrameObserverRuntimeStatus::enabled) {
        return static_cast<DWORD>(current);
    }
    if (current != FrameObserverRuntimeStatus::ready || gTarget == nullptr) {
        lifecycle::Log("preyvr_frame_observer_result status=not_ready");
        return static_cast<DWORD>(current);
    }

    MH_STATUS status = MH_OK;
    if (!gHookCreated) {
        status = MH_Initialize();
        if (status != MH_OK) {
            SetFailed("initialize", status);
            return static_cast<DWORD>(FrameObserverRuntimeStatus::failed);
        }
        EndRendererSceneFn original = nullptr;
        status = MH_CreateHook(
            gTarget,
            reinterpret_cast<void*>(&ObserveEndRendererScene),
            reinterpret_cast<void**>(&original));
        if (status != MH_OK) {
            SetFailed("create", status);
            MH_Uninitialize();
            return static_cast<DWORD>(FrameObserverRuntimeStatus::failed);
        }
        gOriginal.store(original, std::memory_order_release);
        gHookCreated = true;
    }

    // Initialize telemetry before publishing the hook to the render thread.
    // Once MH_EnableHook returns, ObserveEndRendererScene can run immediately.
    gFrameCount.store(0, std::memory_order_release);
    gFirstThreadId.store(0, std::memory_order_release);
    gFirstQpc.store(0, std::memory_order_release);
    gFirstRenderer.store(nullptr, std::memory_order_release);
    gMilestoneThreadId.store(0, std::memory_order_release);
    gMilestoneRenderer.store(nullptr, std::memory_order_release);
    gLastRenderer.store(nullptr, std::memory_order_release);

    status = MH_EnableHook(gTarget);
    if (status != MH_OK) {
        SetFailed("enable", status);
        MH_RemoveHook(gTarget);
        MH_Uninitialize();
        gOriginal.store(nullptr, std::memory_order_release);
        gHookCreated = false;
        return static_cast<DWORD>(FrameObserverRuntimeStatus::failed);
    }
    gObserverStatus.store(
        static_cast<DWORD>(FrameObserverRuntimeStatus::enabled),
        std::memory_order_release);
    lifecycle::Log("preyvr_frame_observer_result status=enabled mutation=none telemetry=bounded");
    return static_cast<DWORD>(FrameObserverRuntimeStatus::enabled);
}

DWORD FrameObserverStatus()
{
    return gObserverStatus.load(std::memory_order_acquire);
}

ULONGLONG ObservedFrameCount()
{
    return gFrameCount.load(std::memory_order_acquire);
}

} // namespace preyvr::dll
