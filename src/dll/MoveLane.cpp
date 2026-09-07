#include "MoveLane.h"

#include "InputPost.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "XrInput.h"
#include "preyvr/Locomotion.h"

#include <MinHook.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_move " + line); }

// R-089. The player-side analog handlers. RCX is the input object: the X
// handler stores to `+0x5C`, the Y handler to `+0x60`, and both discard the
// value when the cinematic gate at `+0x94` is set.
//
//   83 b9 94 00 00 00 00   cmp dword ptr [rcx+0x94], 0
//   f3 0f 10 44 24 28      movss xmm0, [rsp+0x28]
//   74 03                  je  +3
//   0f 57 c0               xorps xmm0, xmm0          <- gated: value becomes 0
//   f3 0f 11 41 5c         movss [rcx+0x5C], xmm0
//
// Hooking these rather than reading a guessed player->input pointer chain means
// the engine hands us the object, so nothing is inferred about where it lives.
using AnalogHandlerFn = void(__fastcall*)(void*, void*);
constexpr std::uintptr_t kAnalogXRva = 0x158FD20;
constexpr std::uintptr_t kAnalogYRva = 0x158FD80;
constexpr std::array<std::uint8_t, 23> kAnalogXPrologue{
    0x83, 0xB9, 0x94, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x44, 0x24,
    0x28, 0x74, 0x03, 0x0F, 0x57, 0xC0, 0xF3, 0x0F, 0x11, 0x41, 0x5C};
constexpr std::array<std::uint8_t, 23> kAnalogYPrologue{
    0x83, 0xB9, 0x94, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x4C, 0x24,
    0x28, 0x74, 0x03, 0x0F, 0x57, 0xC9, 0xF3, 0x0F, 0x10, 0x41, 0x5C};
constexpr std::size_t kMoveX = 0x5C;
constexpr std::size_t kMoveY = 0x60;
constexpr std::size_t kCinematicGate = 0x94;

std::atomic<bool> gInstalled{false};
std::atomic<AnalogHandlerFn> gOriginalX{nullptr}, gOriginalY{nullptr};
void* gTargetX = nullptr;
void* gTargetY = nullptr;

std::atomic<unsigned int> gMode{0};
std::atomic<unsigned int> gDeadzoneHundredths{15};
std::atomic<unsigned long long> gOursX{0}, gOursY{0}, gNative{0};
std::atomic<unsigned long long> gPosted{0}, gDropped{0};
std::atomic<unsigned long long> gInputObject{0};
std::atomic<int> gAxisMilli[2]{0, 0};
std::atomic<int> gCinematic{0};

// Owned by the frame thread that calls UpdateMoveLane. Rebuilt only when the
// policy changes: its state is what makes the release case correct, so
// reconstructing it per frame would forget the last value sent and the player
// would keep walking after letting go.
locomotion::StickAxis gStick;
unsigned int gStickPolicyHundredths = 15;

bool ReadFloat(const void* at, float* out)
{
    __try {
        *out = *reinterpret_cast<const float*>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadInt(const void* at, int* out)
{
    __try {
        *out = *reinterpret_cast<const int*>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Runs inside the engine's own input walk, on whichever thread posted. Reads
// only; the original does the work.
void ObserveHandler(void* inputObject, bool isX)
{
    if (inputObject == nullptr) { return; }
    // Attribution first, because it is the whole point of the hook. Our posts
    // run inside PostInputEvent on the drain thread with the flag set; anything
    // else is the player's physical hardware reaching the same handler.
    if (InputPostDrivingThisThread()) {
        (isX ? gOursX : gOursY).fetch_add(1, std::memory_order_relaxed);
    } else {
        gNative.fetch_add(1, std::memory_order_relaxed);
    }
    gInputObject.store(reinterpret_cast<unsigned long long>(inputObject),
                       std::memory_order_relaxed);
    auto* const bytes = static_cast<const std::uint8_t*>(inputObject);
    float value = 0.0f;
    // Read back what the engine actually stored, rather than trusting that a
    // posted value arrived. R-089 names these offsets; this is where that claim
    // gets checked every frame instead of once.
    if (ReadFloat(bytes + kMoveX, &value)) {
        gAxisMilli[0].store(static_cast<int>(value * 1000.0f), std::memory_order_relaxed);
    }
    if (ReadFloat(bytes + kMoveY, &value)) {
        gAxisMilli[1].store(static_cast<int>(value * 1000.0f), std::memory_order_relaxed);
    }
    int gate = 0;
    if (ReadInt(bytes + kCinematicGate, &gate)) {
        gCinematic.store(gate, std::memory_order_relaxed);
    }
}

void __fastcall AnalogXObserved(void* inputObject, void* event)
{
    ObserveHandler(inputObject, true);
    const AnalogHandlerFn original = gOriginalX.load(std::memory_order_acquire);
    if (original != nullptr) { original(inputObject, event); }
}

void __fastcall AnalogYObserved(void* inputObject, void* event)
{
    ObserveHandler(inputObject, false);
    const AnalogHandlerFn original = gOriginalY.load(std::memory_order_acquire);
    if (original != nullptr) { original(inputObject, event); }
}

bool InstallOne(std::uintptr_t rva, const std::uint8_t* prologue, std::size_t size,
                void* detour, std::atomic<AnalogHandlerFn>& original, void*& target,
                const char* name)
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) { return false; }
    const auto address = reinterpret_cast<std::uintptr_t>(preyDll) + rva;
    if (std::memcmp(reinterpret_cast<const void*>(address), prologue, size) != 0) {
        Log(std::string("result=unavailable detail=prologue_mismatch target=") + name);
        return false;
    }
    target = reinterpret_cast<void*>(address);
    AnalogHandlerFn originalFn = nullptr;
    EnsureMinHook();
    if (MH_CreateHook(target, detour, reinterpret_cast<void**>(&originalFn)) != MH_OK) {
        Log(std::string("result=failed detail=create_hook target=") + name);
        return false;
    }
    original.store(originalFn, std::memory_order_release);
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        Log(std::string("result=failed detail=enable_hook target=") + name);
        return false;
    }
    return true;
}

bool Install()
{
    if (gInstalled.load(std::memory_order_acquire)) { return true; }
    if (!InstallOne(kAnalogXRva, kAnalogXPrologue.data(), kAnalogXPrologue.size(),
                    reinterpret_cast<void*>(&AnalogXObserved), gOriginalX, gTargetX,
                    "analog_x")) {
        return false;
    }
    if (!InstallOne(kAnalogYRva, kAnalogYPrologue.data(), kAnalogYPrologue.size(),
                    reinterpret_cast<void*>(&AnalogYObserved), gOriginalY, gTargetY,
                    "analog_y")) {
        // Leave X installed rather than half-removing: it is passive, and a
        // partial install is visible in the counters.
        Log("result=partial detail=x_installed_y_failed");
        return false;
    }
    gInstalled.store(true, std::memory_order_release);
    Log("result=0 detail=hooks_installed x=0x158FD20 y=0x158FD80");
    return true;
}

} // namespace

DWORD SetMoveLaneMode(unsigned int mode)
{
    if (mode > 2u) { return 1; }
    if (mode != 0u && !Install()) { return 2; }
    if (mode == 2u) {
        // Posting requires the input lane to be armed; without it every event
        // would be queued and silently dropped.
        if (SetInputPostEnabled(1) != 0) {
            Log("result=refused detail=input_post_unavailable");
            return 3;
        }
    }
    if (mode != 2u) { gStick.Reset(); }
    gMode.store(mode, std::memory_order_release);
    Log("result=0 detail=mode value=" + std::to_string(mode));
    return 0;
}

DWORD SetMoveLaneDeadzone(unsigned int hundredths)
{
    if (hundredths > 60u) { return 1; }
    gDeadzoneHundredths.store(hundredths, std::memory_order_relaxed);
    Log("result=0 detail=deadzone value=" + std::to_string(hundredths));
    return 0;
}

void UpdateMoveLane()
{
    if (gMode.load(std::memory_order_acquire) != 2u) { return; }
    // The LEFT stick moves, matching `xi_thumblx`/`xi_thumbly` and every mod in
    // the fleet survey.
    ControllerState state{};
    if (!TryGetControllerState(Hand::left, state)) {
        return;
    }
    const unsigned int hundredths = gDeadzoneHundredths.load(std::memory_order_relaxed);
    if (hundredths != gStickPolicyHundredths) {
        locomotion::StickPolicy policy{};
        policy.deadzone = hundredths / 100.0f;
        gStick = locomotion::StickAxis(policy);
        gStickPolicyHundredths = hundredths;
    }
    locomotion::AxisEvent events[2]{};
    const unsigned int count = gStick.Update(state.thumbstickX, state.thumbstickY, events);
    for (unsigned int i = 0; i < count; ++i) {
        const int valueMilli = static_cast<int>(events[i].value * 1000.0f);
        if (PostRawInput(events[i].keyId, static_cast<unsigned int>(events[i].state),
                         valueMilli) == 0) {
            gPosted.fetch_add(1, std::memory_order_relaxed);
        } else {
            gDropped.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

unsigned int MoveLaneMode() { return gMode.load(std::memory_order_relaxed); }
unsigned int MoveLaneHooked() { return gInstalled.load(std::memory_order_relaxed) ? 1u : 0u; }
unsigned long long MoveLaneOursX() { return gOursX.load(std::memory_order_relaxed); }
unsigned long long MoveLaneOursY() { return gOursY.load(std::memory_order_relaxed); }
unsigned long long MoveLaneNative() { return gNative.load(std::memory_order_relaxed); }
unsigned long long MoveLanePosted() { return gPosted.load(std::memory_order_relaxed); }
unsigned long long MoveLaneDropped() { return gDropped.load(std::memory_order_relaxed); }
unsigned long long MoveLaneInputObject() { return gInputObject.load(std::memory_order_relaxed); }
int MoveLaneAxisMilli(unsigned int axis) { return axis < 2 ? gAxisMilli[axis].load(std::memory_order_relaxed) : 0; }
int MoveLaneCinematicGate() { return gCinematic.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
