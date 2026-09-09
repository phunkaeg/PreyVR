#include "HudBridge.h"
#include "CameraEditHook.h"
#include "XrSessionHost.h"

#include "preyvr/WeaponAim.h"

#include "Logger.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <iomanip>
#include <locale>
#include <mutex>
#include <sstream>
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

// **Virtual slots, taken from the game's own call sites (R-119).**
//
// `+0x2D0` is `IUIElement::ScreenToFlash`. The FlowGraph node that converts a
// screen position into a flash position installs `0x1802F2E60` as its callback,
// and that function's entire body is `CALL qword ptr [R10 + 0x2D0]` after
// shuffling the arguments -- so the offset is the engine's, not a slot count.
//
// No counted slot is needed for the constraints at all. `SetConstraints` was
// found whole at a FIXED RVA by the log line inside it -- "%s (%i): UIElement
// set new constraints" at `0x181CABD38` has exactly one xref, and it lands in
// the middle of the implementation (R-120). Reading that function gave both the
// entry point and the storage:
//
//   MOVUPS XMM0, [RDI]         ; the caller's 32-byte SUIConstraints
//   MOVUPS [RBX + 0x84], XMM0
//   MOVUPS XMM1, [RDI + 0x10]
//   MOVUPS [RBX + 0x94], XMM1
//   CALL   qword ptr [RAX + 0x1E0]   ; UpdateViewPort
//
// So the live constraints ARE the 32 bytes at `element + 0x84`, and they can be
// read as a plain field rather than through a virtual. That removes the counted
// slot this file previously leaned on for reading them.
constexpr std::size_t kScreenToFlashSlot = 0x2D0;
constexpr std::size_t kConstraintsFieldOffset = 0x84;
constexpr std::size_t kConstraintsSize = 32;
constexpr std::uintptr_t kSetConstraintsRva = 0x2FFF30;

// Two SSE stores of the caller's struct, then UpdateViewPort. Excluding the
// RIP-relative displacement in the middle, per the standing rule: a signature
// that includes one is a signature that breaks for a reason unrelated to the
// code.
//
//   48 89 5C 24 08     mov  [rsp+8], rbx
//   57                 push rdi
//   48 83 EC 20        sub  rsp, 20h
//   83 3D <rel32> 00   cmp  dword ptr [rip+..], 0    <- the enable gate
//   48 8B FA           mov  rdi, rdx                 <- arg 2 is the struct
//   48 8B D9           mov  rbx, rcx                 <- arg 1 is the element
//   74 48              jz   (skip everything)
constexpr std::array<std::uint8_t, 12> kSetConstraintsHead{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0x3D};
constexpr std::array<std::uint8_t, 8> kSetConstraintsTail{
    0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x74, 0x48};

using ScreenToFlashFn =
    void(__fastcall*)(void*, const float*, const float*, float*, float*, bool);
using SetConstraintsFn = void(__fastcall*)(void*, const void*);

using GetHudElementFn = void*(__fastcall*)();
using CallTwoFloatFn = void(__fastcall*)(void*, const char*, float, float);
using CallOneFloatFn = void(__fastcall*)(void*, const char*, float);

std::atomic<unsigned long long> gElement{0}, gCalls{0}, gRefused{0};
struct PendingHudCall {
    std::string name;
    float x = 0, y = 0;
    bool twoArguments = false;
    // A probe converts and reports instead of dispatching. It rides the same
    // queue because it enters the same element on the same thread; the reason
    // `hud.call` is queued applies unchanged to reading through a vtable.
    bool probe = false;
    // 0 none, 1 clear bMax (fit inside), 2 set bMax (cover, the engine default)
    int fit = 0;
};
std::mutex gProbeMutex;
std::string gLastProbe = "none";
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

// A vtable slot is learned at runtime, so it cannot be prologue-matched the way
// the fixed-RVA entries above are. Bounding it into committed executable memory
// is the honest substitute: it will not tell us the callee is the right
// function, but it does catch a stale element, a freed object or a garbage
// vtable -- which are the ways this actually goes wrong.
bool PointerIsExecutable(std::uintptr_t address)
{
    if (address == 0) { return false; }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) != sizeof(info)) {
        return false;
    }
    if (info.State != MEM_COMMIT) { return false; }
    constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                  PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (info.Protect & kExecutable) != 0;
}

std::uintptr_t ReadVtableSlot(void* element, std::size_t byteOffset)
{
    __try {
        const std::uintptr_t vtable = *reinterpret_cast<const std::uintptr_t*>(element);
        if (vtable == 0) { return 0; }
        return *reinterpret_cast<const std::uintptr_t*>(vtable + byteOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool CallScreenToFlash(std::uintptr_t address, void* element, const float* x, const float* y,
                       float* outX, float* outY, bool stageScaleMode)
{
    __try {
        reinterpret_cast<ScreenToFlashFn>(address)(element, x, y, outX, outY, stageScaleMode);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CallSetConstraints(std::uintptr_t address, void* element, const void* constraints)
{
    __try {
        reinterpret_cast<SetConstraintsFn>(address)(element, constraints);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The live 32 bytes at `element + 0x84`. Raw bytes rather than a typed struct,
// because the bytes are what was observed; they are interpreted once, at the
// reporting edge.
bool ReadConstraintBytes(void* element, unsigned char* out)
{
    __try {
        const auto* const at = reinterpret_cast<const unsigned char*>(element) +
                               kConstraintsFieldOffset;
        for (std::size_t i = 0; i < kConstraintsSize; ++i) { out[i] = at[i]; }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void DecodeConstraints(const unsigned char* raw, HudConstraints* out)
{
    int words[7] = {};
    std::memcpy(words, raw, sizeof(words));
    out->positionType = words[0];
    out->left = words[1];
    out->top = words[2];
    out->width = words[3];
    out->height = words[4];
    out->hAlign = words[5];
    out->vAlign = words[6];
    out->scale = raw[28] != 0;
    out->max = raw[29] != 0;
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

DWORD QueueHudProbe(float screenX, float screenY)
{
    if (!std::isfinite(screenX) || !std::isfinite(screenY)) { return 2; }
    if (EnsureRenderHookInstalled() != 0) { return 9; }
    std::lock_guard lock(gQueueMutex);
    if (gQueue.size() >= 8) { return 10; }
    gQueue.push_back({std::string(), screenX, screenY, false, true});
    return 0;
}

DWORD QueueHudFit(bool maximise)
{
    if (EnsureRenderHookInstalled() != 0) { return 9; }
    std::lock_guard lock(gQueueMutex);
    if (gQueue.size() >= 8) { return 10; }
    gQueue.push_back({std::string(), 0.0f, 0.0f, false, false, maximise ? 2 : 1});
    return 0;
}

std::string HudLastProbe()
{
    std::lock_guard lock(gProbeMutex);
    return gLastProbe;
}

// Runs on the main thread, from the drain. Converts the same input BOTH ways --
// the engine's own conversion at each value of the stage-scale flag, and our
// R-118 cover-canvas arithmetic -- so the comparison is one record taken at one
// aspect on one frame, rather than two readings taken apart and matched later.
void RunHudProbe(float screenX, float screenY)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(6) << "inX=" << screenX << " inY=" << screenY;

    float nativeOffX = 0, nativeOffY = 0;
    const DWORD off = HudScreenToFlash(screenX, screenY, false, &nativeOffX, &nativeOffY);
    out << " stage0=" << off;
    if (off == 0) { out << " stage0X=" << nativeOffX << " stage0Y=" << nativeOffY; }

    float nativeOnX = 0, nativeOnY = 0;
    const DWORD on = HudScreenToFlash(screenX, screenY, true, &nativeOnX, &nativeOnY);
    out << " stage1=" << on;
    if (on == 0) { out << " stage1X=" << nativeOnX << " stage1Y=" << nativeOnY; }

    const float frameWidth = static_cast<float>(XrResolutionChain(4));
    const float frameHeight = static_cast<float>(XrResolutionChain(5));
    const auto ours = preyvr::aim::ViewportToHudCanvas(screenX, screenY, frameWidth, frameHeight);
    out << " frame=" << static_cast<int>(frameWidth) << "x" << static_cast<int>(frameHeight)
        << " oursX=" << ours.x << " oursY=" << ours.y;

    HudConstraints constraints{};
    const DWORD gotConstraints = HudReadConstraints(&constraints);
    out << " constraints=" << gotConstraints;
    if (gotConstraints == 0) {
        out << " cType=" << constraints.positionType
            << " cRect=" << constraints.left << "," << constraints.top << ","
            << constraints.width << "," << constraints.height
            << " cHAlign=" << constraints.hAlign << " cVAlign=" << constraints.vAlign
            << " cScale=" << (constraints.scale ? 1 : 0)
            << " cMax=" << (constraints.max ? 1 : 0);
    }

    const std::string line = out.str();
    {
        std::lock_guard lock(gProbeMutex);
        gLastProbe = line;
    }
    Log("result=0 detail=probe " + line);
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
    if (call.fit != 0) {
        HudSetConstraintMax(call.fit == 2);
        return;
    }
    if (call.probe) {
        RunHudProbe(call.x, call.y);
        return;
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

DWORD HudScreenToFlash(float screenX, float screenY, bool stageScaleMode,
                       float* outX, float* outY)
{
    if (outX == nullptr || outY == nullptr) { gRefused.fetch_add(1); return 1; }
    if (!std::isfinite(screenX) || !std::isfinite(screenY)) { gRefused.fetch_add(1); return 2; }

    std::uintptr_t base = 0;
    DWORD code = 0;
    void* const element = ResolveElement(base, code);
    if (element == nullptr) { gRefused.fetch_add(1); return code; }

    const std::uintptr_t slot = ReadVtableSlot(element, kScreenToFlashSlot);
    if (!PointerIsExecutable(slot)) {
        Log("result=unavailable detail=screen_to_flash_slot_not_executable");
        gRefused.fetch_add(1);
        return 11;
    }

    // In place, exactly as the engine's own forwarder calls it: the same two
    // addresses are passed as both the inputs and the outputs.
    float x = screenX;
    float y = screenY;
    if (!CallScreenToFlash(slot, element, &x, &y, &x, &y, stageScaleMode)) {
        Log("result=failed detail=exception_in_screen_to_flash");
        gRefused.fetch_add(1);
        return 8;
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        // A completed call that produced nonsense is a refusal, not a result.
        Log("result=failed detail=screen_to_flash_non_finite");
        gRefused.fetch_add(1);
        return 12;
    }
    *outX = x;
    *outY = y;
    gCalls.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

DWORD HudScreenToFlashFraction(float screenX, float screenY, bool stageScaleMode,
                               float* outX, float* outY)
{
    if (outX == nullptr || outY == nullptr) { gRefused.fetch_add(1); return 1; }

    // **ScreenToFlash answers in CANVAS PIXELS, not in a fraction.** Measured
    // live at 2688x2880: input 0.395 came back as 854.16, against a constraint
    // rect of 1920x1080 -- and 854.16 / 1920 is 0.444875, which is exactly what
    // the R-118 model computes. The reticle dispatch wants the fraction, so
    // handing it the raw native value would send 854.16 where 0.44 belongs
    // (R-122). The probe existed precisely to catch this before it shipped.
    HudConstraints constraints{};
    const DWORD read = HudReadConstraints(&constraints);
    if (read != 0) { return read; }
    if (constraints.width <= 0 || constraints.height <= 0) {
        Log("result=refused detail=constraint_rect_degenerate");
        gRefused.fetch_add(1);
        return 15;
    }

    float pixelX = 0, pixelY = 0;
    const DWORD converted = HudScreenToFlash(screenX, screenY, stageScaleMode, &pixelX, &pixelY);
    if (converted != 0) { return converted; }

    *outX = pixelX / static_cast<float>(constraints.width);
    *outY = pixelY / static_cast<float>(constraints.height);
    return 0;
}

DWORD HudReadConstraints(HudConstraints* out)
{
    if (out == nullptr) { gRefused.fetch_add(1); return 1; }

    std::uintptr_t base = 0;
    DWORD code = 0;
    void* const element = ResolveElement(base, code);
    if (element == nullptr) { gRefused.fetch_add(1); return code; }

    unsigned char raw[kConstraintsSize] = {};
    if (!ReadConstraintBytes(element, raw)) {
        Log("result=failed detail=exception_reading_constraints");
        gRefused.fetch_add(1);
        return 8;
    }
    DecodeConstraints(raw, out);
    gCalls.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

DWORD HudSetConstraintMax(bool maximise)
{
    std::uintptr_t base = 0;
    DWORD code = 0;
    void* const element = ResolveElement(base, code);
    if (element == nullptr) { gRefused.fetch_add(1); return code; }

    const auto* const setter = reinterpret_cast<const std::uint8_t*>(base + kSetConstraintsRva);
    if (!BytesMatch(setter, kSetConstraintsHead.data(), kSetConstraintsHead.size()) ||
        !BytesMatch(setter + 0x11, kSetConstraintsTail.data(), kSetConstraintsTail.size())) {
        Log("result=unavailable detail=set_constraints_prologue_mismatch");
        gRefused.fetch_add(1);
        return 6;
    }

    // **Edited, not fabricated.** The struct handed to the engine is the live
    // one read back from the element with a single byte changed. Nothing here
    // invents a layout or a value it did not first observe.
    unsigned char raw[kConstraintsSize] = {};
    if (!ReadConstraintBytes(element, raw)) {
        Log("result=failed detail=exception_reading_constraints");
        gRefused.fetch_add(1);
        return 8;
    }
    const unsigned char wanted = maximise ? 1 : 0;
    if (raw[29] == wanted) {
        Log("result=0 detail=constraint_max_already bMax=" + std::to_string(wanted));
        return 0;
    }
    unsigned char edited[kConstraintsSize] = {};
    std::memcpy(edited, raw, kConstraintsSize);
    edited[29] = wanted;

    if (!CallSetConstraints(base + kSetConstraintsRva, element, edited)) {
        Log("result=failed detail=exception_in_set_constraints");
        gRefused.fetch_add(1);
        return 8;
    }

    // **The readback is the whole point, and F-011 is why.** SetConstraints
    // opens with `cmp dword ptr [rip+..], 0` and returns without doing anything
    // when that global is zero -- a completed call that changed nothing, which
    // is exactly the shape of failure that made a nonexistent Scaleform function
    // return success. Reading the field back is the only thing that separates
    // "the engine accepted this" from "the call returned".
    unsigned char after[kConstraintsSize] = {};
    if (!ReadConstraintBytes(element, after)) {
        Log("result=failed detail=exception_reading_constraints_after");
        gRefused.fetch_add(1);
        return 8;
    }
    if (after[29] != wanted) {
        Log("result=refused detail=set_constraints_did_not_take"
            " note=gate_disabled_or_element_replaced bMax=" + std::to_string(after[29]));
        gRefused.fetch_add(1);
        return 14;
    }
    gCalls.fetch_add(1, std::memory_order_relaxed);
    Log("result=0 detail=constraint_max_set bMax=" + std::to_string(wanted));
    return 0;
}

unsigned long long HudElementPointer() { return gElement.load(std::memory_order_relaxed); }
unsigned long long HudCallCount() { return gCalls.load(std::memory_order_relaxed); }
unsigned long long HudRefusedCount() { return gRefused.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
