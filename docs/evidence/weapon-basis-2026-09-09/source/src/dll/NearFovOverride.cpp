#include "NearFovOverride.h"
#include "Bootstrap.h"
#include "Logger.h"
#include "MinHookInit.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace preyvr::dll {
namespace {
// Steam SHA256 7d6e322f...05311a7, R-002/R-069. Evidence and scope:
// docs/RE-BUILD-TAKEOVER-2026-09-09.md. ABI: void RT_BeginFrame(renderer*).
using BeginFrameFn = void(__fastcall*)(void*);
constexpr std::uintptr_t kBeginFrameRva = 0xF7D710;
constexpr std::uintptr_t kLatchRva = 0xF7DC85;
constexpr std::uintptr_t kBeginFrameSlot = 0x8C8;
constexpr std::uintptr_t kDrawNearFovLatched = 0x95B4;
constexpr std::array<std::uint8_t, 23> kBeginFramePrologue{
    0x48,0x8B,0xC4,0x55,0x53,0x48,0x8D,0x68,0xA1,0x48,0x81,0xEC,
    0xB8,0,0,0,0x4C,0x89,0x78,0xE8,0x48,0x8B,0xD9};
// Exact build gate, including RIP displacement to cvar RVA 0x2B1C64C.
// Not a signature for discovering other builds.
constexpr std::array<std::uint8_t, 23> kLatchInstructions{
    0xF3,0x0F,0x10,0x05,0xBF,0xE9,0xB9,0x01,0x48,0x8D,0x8B,0x80,
    0xAF,0,0,0xF3,0x0F,0x11,0x83,0xB4,0x95,0,0};
std::atomic<unsigned int> gDeciDegrees{0};
std::atomic<unsigned long long> gApplied{0}, gRefused{0};
std::atomic<int> gObserved{0};
std::atomic<bool> gObservedValid{false}, gFaulted{false};
std::atomic<DWORD> gLastThread{0};
std::atomic<BeginFrameFn> gOriginal{nullptr};
std::mutex gInstallMutex;
void* gTarget = nullptr;
bool gHookCreated = false, gInstalled = false;

bool MatchesCode(const void* entry, const void* latch)
{
    __try {
        return std::memcmp(entry, kBeginFramePrologue.data(), kBeginFramePrologue.size()) == 0 &&
               std::memcmp(latch, kLatchInstructions.data(), kLatchInstructions.size()) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Guard only mod memory access, never exceptions from the engine's original.
bool ApplyToRenderer(void* renderer, unsigned int wanted, int* observed)
{
    __try {
        if (renderer == nullptr) { return false; }
        const auto* table = *reinterpret_cast<const std::uint8_t* const*>(renderer);
        if (table == nullptr ||
            *reinterpret_cast<void* const*>(table + kBeginFrameSlot) != gTarget) {
            return false;
        }
        auto* field = reinterpret_cast<float*>(
            static_cast<std::uint8_t*>(renderer) + kDrawNearFovLatched);
        const float native = *field;
        // Engine fallback permits zero/negative sentinels. Plausibility alone
        // never establishes identity; the code and concrete slot are gated too.
        if (!std::isfinite(native) || native < -180.0f || native > 180.0f) { return false; }
        *observed = static_cast<int>(std::lround(native * 10.0f));
        *field = static_cast<float>(wanted) / 10.0f;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void __fastcall BeginFrameWithNearFov(void* renderer)
{
    const auto original = gOriginal.load(std::memory_order_acquire);
    if (original == nullptr) { return; }
    original(renderer);
    // The native latch completed on this callback's thread. A game-thread
    // CSystem::Render entry cannot establish this ordering for a queued renderer.
    const auto wanted = gDeciDegrees.load(std::memory_order_acquire);
    if (wanted == 0 || gFaulted.load(std::memory_order_acquire)) { return; }
    int observed = 0;
    if (!ApplyToRenderer(renderer, wanted, &observed)) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        gObservedValid.store(false, std::memory_order_relaxed);
        gFaulted.store(true, std::memory_order_release);
        return; // no repeated writes after a fault; explicit command rearms
    }
    gObserved.store(observed, std::memory_order_relaxed);
    gObservedValid.store(true, std::memory_order_relaxed);
    gLastThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    gApplied.fetch_add(1, std::memory_order_relaxed);
}

bool Install()
{
    // Caller holds gInstallMutex. Keep the hook/trampoline until process exit;
    // disabling only changes the atomic setting, so in-flight calls remain safe.
    if (gInstalled) { return true; }
    if (ModulePinStatus() == 0) { return false; }
    const auto module = GetModuleHandleW(L"PreyDll.dll");
    if (module == nullptr) { return false; }
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    if (!gHookCreated) {
        if (!MatchesCode(reinterpret_cast<void*>(base + kBeginFrameRva),
                         reinterpret_cast<void*>(base + kLatchRva))) {
            lifecycle::Log("preyvr_near_fov result=refused detail=code_mismatch");
            return false;
        }
        if (!EnsureMinHook()) { return false; }
        gTarget = reinterpret_cast<void*>(base + kBeginFrameRva);
        BeginFrameFn original = nullptr;
        if (MH_CreateHook(gTarget, reinterpret_cast<void*>(&BeginFrameWithNearFov),
                          reinterpret_cast<void**>(&original)) != MH_OK) { return false; }
        gOriginal.store(original, std::memory_order_release);
        gHookCreated = true;
    }
    if (MH_EnableHook(gTarget) != MH_OK) { return false; }
    gInstalled = true;
    lifecycle::Log("preyvr_near_fov result=0 detail=hook_installed seam=RT_BeginFrame_return");
    return true;
}
} // namespace

DWORD SetNearFovDeciDegrees(unsigned int deciDegrees)
{
    // The native consumers substitute only for 1 < degrees < 179.
    if (deciDegrees != 0 && (deciDegrees <= 10u || deciDegrees >= 1790u)) {
        lifecycle::Log("preyvr_near_fov result=refused detail=out_of_range");
        return 1;
    }
    std::lock_guard lock(gInstallMutex);
    if (deciDegrees != 0 && !Install()) {
        lifecycle::Log("preyvr_near_fov result=refused detail=hook_unavailable");
        return 2;
    }
    gFaulted.store(false, std::memory_order_release);
    gDeciDegrees.store(deciDegrees, std::memory_order_release);
    lifecycle::Log("preyvr_near_fov result=0 detail=setting_accepted");
    return 0;
}

DWORD NearFovDeciDegrees() { return gDeciDegrees.load(std::memory_order_acquire); }
unsigned long long NearFovAppliedCount() { return gApplied.load(std::memory_order_relaxed); }
unsigned long long NearFovRefusedCount() { return gRefused.load(std::memory_order_relaxed); }
int NearFovObservedDeciDegrees() { return gObserved.load(std::memory_order_relaxed); }
bool NearFovObservedValid() { return gObservedValid.load(std::memory_order_relaxed); }
bool NearFovFaulted() { return gFaulted.load(std::memory_order_acquire); }
DWORD NearFovThreadId() { return gLastThread.load(std::memory_order_relaxed); }
} // namespace preyvr::dll
