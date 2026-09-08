#include "HudBridge.h"
#include "CameraEditHook.h"

#include "Logger.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_hud " + line); }

// R-108/R-109, all read from this build.
constexpr std::uintptr_t kGetHudElementRva = 0x1665780;
constexpr std::uintptr_t kCallTwoFloatRva = 0x11797C0;
constexpr std::uintptr_t kCallOneFloatRva = 0x118C970;

// The two-float entry prologue, and it is the ABI evidence rather than just a gate:
//
//   48 89 5C 24 08        mov  [rsp+8], rbx
//   F3 0F 11 5C 24 20     movss [rsp+20h], xmm3    <- 4th argument is a FLOAT
//   F3 0F 11 54 24 18     movss [rsp+18h], xmm2    <- 3rd argument is a FLOAT
//   55 56 57              push rbp, rsi, rdi
//   48 83 EC 60           sub  rsp, 60h
constexpr std::array<std::uint8_t, 24> kCallTwoFloatPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0xF3, 0x0F, 0x11, 0x5C, 0x24, 0x20, 0xF3,
    0x0F, 0x11, 0x54, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x60};

// The one-float entry prologue. The discriminating detail is that it saves XMM2
// and NOT XMM3, so it takes exactly one float:
//
//   48 89 5C 24 08        mov  [rsp+8], rbx
//   48 89 6C 24 10        mov  [rsp+10h], rbp
//   48 89 74 24 20        mov  [rsp+20h], rsi
//   F3 0F 11 54 24 18     movss [rsp+18h], xmm2    <- 3rd argument is a FLOAT
//   57                    push rdi
//   48 83 EC 60           sub  rsp, 60h
//
// Ghidra renders some call sites of this as two-argument, because it has not
// recovered the XMM parameter there. The prologue is the authority, not the
// decompiler arity.
constexpr std::array<std::uint8_t, 26> kCallOneFloatPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x20, 0xF3, 0x0F, 0x11, 0x54, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x60};

// The getter is a three-instruction tail-call:
//
//   48 8B 0D <rel32>   mov rcx, [rip+..]    ; the UI singleton
//   48 8D 15 <rel32>   lea rdx, [rip+..]    ; "DanielleHUD"
//   48 8B 01           mov rax, [rcx]
//   48 FF 60 60        jmp qword ptr [rax+60h]
//
// Two RIP-relative displacements sit in the middle, and the standing rule is to
// keep those out of a signature: they encode a link-time layout rather than the
// instruction, and a signature that includes one is a signature that will be
// wrong for a reason unrelated to the code. So the gate checks the opcodes
// either side and steps over the displacements.
constexpr std::array<std::uint8_t, 3> kGetHudHead{0x48, 0x8B, 0x0D};
constexpr std::array<std::uint8_t, 3> kGetHudLea{0x48, 0x8D, 0x15};
constexpr std::array<std::uint8_t, 7> kGetHudTail{0x48, 0x8B, 0x01, 0x48, 0xFF, 0x60, 0x60};

using GetHudElementFn = void*(__fastcall*)();
using CallTwoFloatFn = void(__fastcall*)(void*, const char*, float, float);
using CallOneFloatFn = void(__fastcall*)(void*, const char*, float);

std::atomic<unsigned long long> gElement{0}, gCalls{0}, gRefused{0};
struct PendingHudCall {
    std::string name;
    float x = 0, y = 0;
    bool twoArguments = false;
};
std::mutex gQueueMutex;
std::deque<PendingHudCall> gQueue;

// Every function below that contains __try is POD-only on purpose: SEH cannot
// live in a function that requires object unwinding, and this project has hit
// that three times.
bool BytesMatch(const std::uint8_t* at, const std::uint8_t* expected, std::size_t size)
{
    __try {
        return std::memcmp(at, expected, size) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* CallGetHudElement(std::uintptr_t address)
{
    __try {
        return reinterpret_cast<GetHudElementFn>(address)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool CallTwoFloat(std::uintptr_t address, void* element, const char* name, float x, float y)
{
    __try {
        reinterpret_cast<CallTwoFloatFn>(address)(element, name, x, y);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallOneFloat(std::uintptr_t address, void* element, const char* name, float value)
{
    __try {
        reinterpret_cast<CallOneFloatFn>(address)(element, name, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Shared front half: gate the getter, resolve the element. Returns the module
// base through `base` and the element, or nullptr with `code` set.
void* ResolveElement(std::uintptr_t& base, DWORD& code)
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) { code = 4; return nullptr; }
    base = reinterpret_cast<std::uintptr_t>(preyDll);

    const auto* const getter = reinterpret_cast<const std::uint8_t*>(base + kGetHudElementRva);
    if (!BytesMatch(getter, kGetHudHead.data(), kGetHudHead.size()) ||
        !BytesMatch(getter + 7, kGetHudLea.data(), kGetHudLea.size()) ||
        !BytesMatch(getter + 14, kGetHudTail.data(), kGetHudTail.size())) {
        Log("result=unavailable detail=get_hud_element_prologue_mismatch");
        code = 5;
        return nullptr;
    }

    void* const element = CallGetHudElement(base + kGetHudElementRva);
    gElement.store(reinterpret_cast<unsigned long long>(element), std::memory_order_relaxed);
    if (element == nullptr) {
        // Real before the HUD exists -- during a load, or with no level. Note it
        // is NOT null at the main menu: the element resolves there (R-109).
        code = 7;
        return nullptr;
    }
    code = 0;
    return element;
}

bool ValidName(const char* function) { return function != nullptr && function[0] != 0; }

} // namespace

DWORD CallHudTwoFloat(const char* function, float x, float y)
{
    if (!ValidName(function)) { gRefused.fetch_add(1); return 1; }
    if (!std::isfinite(x) || !std::isfinite(y)) { gRefused.fetch_add(1); return 2; }

    std::uintptr_t base = 0;
    DWORD code = 0;
    void* const element = ResolveElement(base, code);
    if (element == nullptr) { gRefused.fetch_add(1); return code; }

    const auto* const caller = reinterpret_cast<const std::uint8_t*>(base + kCallTwoFloatRva);
    if (!BytesMatch(caller, kCallTwoFloatPrologue.data(), kCallTwoFloatPrologue.size())) {
        Log("result=unavailable detail=call_two_float_prologue_mismatch");
        gRefused.fetch_add(1);
        return 6;
    }
    if (!CallTwoFloat(base + kCallTwoFloatRva, element, function, x, y)) {
        Log("result=failed detail=exception_in_dispatch fn=" + std::string(function));
        gRefused.fetch_add(1);
        return 8;
    }
    gCalls.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

DWORD CallHudOneFloat(const char* function, float value)
{
    if (!ValidName(function)) { gRefused.fetch_add(1); return 1; }
    if (!std::isfinite(value)) { gRefused.fetch_add(1); return 2; }

    std::uintptr_t base = 0;
    DWORD code = 0;
    void* const element = ResolveElement(base, code);
    if (element == nullptr) { gRefused.fetch_add(1); return code; }

    const auto* const caller = reinterpret_cast<const std::uint8_t*>(base + kCallOneFloatRva);
    if (!BytesMatch(caller, kCallOneFloatPrologue.data(), kCallOneFloatPrologue.size())) {
        Log("result=unavailable detail=call_one_float_prologue_mismatch");
        gRefused.fetch_add(1);
        return 6;
    }
    if (!CallOneFloat(base + kCallOneFloatRva, element, function, value)) {
        Log("result=failed detail=exception_in_dispatch fn=" + std::string(function));
        gRefused.fetch_add(1);
        return 8;
    }
    gCalls.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

DWORD CallHudFunction(const char* function, float x, float y, bool twoArguments)
{
    // The file-poll worker must not enter Scaleform concurrently with the
    // game's update/render. Copy arguments into an owned queue; the existing
    // main-thread render seam drains it, including while a menu is open.
    if (!ValidName(function)) { return 1; }
    if (!std::isfinite(x) || (twoArguments && !std::isfinite(y))) { return 2; }
    if (EnsureRenderHookInstalled() != 0) { return 9; }
    std::lock_guard lock(gQueueMutex);
    if (gQueue.size() >= 8) { return 10; }
    gQueue.push_back({std::string(function), x, y, twoArguments});
    return 0; // queued, not dispatched
}

void DrainQueuedHudCalls()
{
    PendingHudCall call;
    {
        std::unique_lock lock(gQueueMutex, std::try_to_lock);
        if (!lock.owns_lock() || gQueue.empty()) { return; }
        call = std::move(gQueue.front());
        gQueue.pop_front();
    }
    const DWORD result =
        call.twoArguments ? CallHudTwoFloat(call.name.c_str(), call.x, call.y)
                          : CallHudOneFloat(call.name.c_str(), call.x);
    // A completed dispatch is not visual acceptance: a zero says the ABI and the
    // element were right, not that the movie has a function by that name.
    Log("result=" + std::to_string(result) +
        " fn=" + call.name + " x=" + std::to_string(call.x) +
        (call.twoArguments ? " y=" + std::to_string(call.y) : std::string()));
}

unsigned long long HudElementPointer() { return gElement.load(std::memory_order_relaxed); }
unsigned long long HudCallCount() { return gCalls.load(std::memory_order_relaxed); }
unsigned long long HudRefusedCount() { return gRefused.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
