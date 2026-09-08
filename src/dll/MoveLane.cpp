#include "MoveLane.h"

#include "InputPost.h"
#include "HeadTrackingHook.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "XrInput.h"
#include "preyvr/InputEvent.h"
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
// The action callback has FIVE arguments including this. The target loads
// value from [rsp+28h] before changing rsp, and returns acceptance in AL.
// Forwarding only this/event would make its value come from unrelated stack data.
using AnalogHandlerFn = bool(__fastcall*)(void*, unsigned int, const void*, int, float);
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
std::atomic<bool> gMoveNeutralize{false};
bool gMoveMayBeHeld = false;

// --- turn and fire, same thread and frame as the move lane ------------------
std::atomic<bool> gTurnEnabled{false};
std::atomic<unsigned int> gTurnDeadzoneHundredths{15};
std::atomic<unsigned int> gTurnScalePercent{100};
std::atomic<unsigned long long> gTurnPosted{0}, gTurnRefused{0};
float gLastTurnSent = 0.0f;
bool gTurnPrimed = false;
std::atomic<bool> gTurnNeutralize{false};

std::atomic<bool> gFireEnabled{false};
std::atomic<unsigned long long> gFirePressed{0}, gFireReleased{0}, gFireRefused{0};
bool gFireHeld = false;
float gLastFireSent = 0.0f;
bool gFirePrimed = false;
std::atomic<bool> gFireNeutralize{false};

// Recentre-on-both-grips, edge triggered so a held pair fires once.
bool gRecenterHeld = false;
std::atomic<unsigned long long> gRecenters{0};

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

bool __fastcall AnalogXObserved(void* inputObject, unsigned int entity,
                               const void* action, int mode, float value)
{
    const AnalogHandlerFn original = gOriginalX.load(std::memory_order_acquire);
    const bool accepted = original != nullptr && original(inputObject, entity, action, mode, value);
    ObserveHandler(inputObject, true);
    return accepted;
}

bool __fastcall AnalogYObserved(void* inputObject, unsigned int entity,
                               const void* action, int mode, float value)
{
    const AnalogHandlerFn original = gOriginalY.load(std::memory_order_acquire);
    const bool accepted = original != nullptr && original(inputObject, entity, action, mode, value);
    ObserveHandler(inputObject, false);
    return accepted;
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
    if (mode != 2u) { gMoveNeutralize.store(true, std::memory_order_release); }
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
    // The LEFT stick moves, matching `xi_thumblx`/`xi_thumbly` and every mod in
    // the fleet survey.
    ControllerState state{};
    const bool active = gMode.load(std::memory_order_acquire) == 2u;
    const bool neutralize = gMoveNeutralize.exchange(false, std::memory_order_acq_rel);
    const bool haveInput = active && !neutralize && TryGetControllerState(Hand::left, state);
    if (!haveInput) {
        // A partially delivered X/Y pair can leave one axis held even though
        // the candidate shaper was not committed. Release both owned axes and
        // retry refusals; never infer successful delivery from shaper state.
        bool released = true;
        if (gMoveMayBeHeld) {
            for (const int key : {locomotion::kKeyThumbLX, locomotion::kKeyThumbLY}) {
                if (PostRawInputImmediate(key, locomotion::kStateChanged, 0) == 0) {
                    gPosted.fetch_add(1, std::memory_order_relaxed);
                } else {
                    released = false;
                    gDropped.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        if (released) { gMoveMayBeHeld = false; gStick.Reset(); }
        else { gMoveNeutralize.store(true, std::memory_order_release); }
        return;
    }
    const unsigned int hundredths = gDeadzoneHundredths.load(std::memory_order_relaxed);
    if (hundredths != gStickPolicyHundredths) {
        locomotion::StickPolicy policy{};
        policy.deadzone = hundredths / 100.0f;
        // Keep the last successful values when changing the deadzone.
        gStick.SetPolicy(policy);
        gStickPolicyHundredths = hundredths;
    }
    locomotion::AxisEvent events[2]{};
    auto candidate = gStick;
    const unsigned int count = candidate.Update(state.thumbstickX, state.thumbstickY, events);
    bool delivered = true;
    for (unsigned int i = 0; i < count; ++i) {
        const int valueMilli = static_cast<int>(events[i].value * 1000.0f);
        // Immediate, not queued: this already runs on the drain thread, and the
        // queue drains one event per frame while a two-axis stick produces two.
        if (PostRawInputImmediate(events[i].keyId, static_cast<unsigned int>(events[i].state),
                                  valueMilli) == 0) {
            if (valueMilli != 0) { gMoveMayBeHeld = true; }
            gPosted.fetch_add(1, std::memory_order_relaxed);
        } else {
            delivered = false;
            gDropped.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (delivered) {
        gStick = candidate;
        if (gStick.LastSentX() == 0 && gStick.LastSentY() == 0) { gMoveMayBeHeld = false; }
    }
}

void UpdateTurnAndFireLanes()
{
    ControllerState right{};
    const bool haveInput = TryGetControllerState(Hand::right, right);
    if (!haveInput) { right = {}; }

    // **Recentre, on both grips at once.** A wearer whose view has drifted
    // behind the character's head cannot reach a console, and asking someone in
    // a headset to alt-tab is not a recentre button. Both grips because no
    // single control is free -- menu buttons drive menus, sticks move and turn,
    // triggers fire -- and because squeezing both at once is not something a
    // hand does by accident while playing.
    //
    // Edge-triggered: held grips must recentre ONCE, not every frame, or the
    // reference would be rebuilt continuously and the view would never settle.
    {
        ControllerState leftGrip{};
        const bool haveLeft = TryGetControllerState(Hand::left, leftGrip);
        const bool both = haveInput && haveLeft && leftGrip.gripPressed && right.gripPressed;
        if (both && !gRecenterHeld) {
            // This bumps the head-tracking reference generation, which
            // deliberately invalidates the IK calibration -- the hands were
            // calibrated against the old reference and would be wrong against
            // the new one. Recalibrating after a recentre is expected rather
            // than a fault, and saying so here saves rediscovering it while
            // wearing a headset.
            RecenterHeadTracking();
            gRecenters.fetch_add(1, std::memory_order_relaxed);
        }
        gRecenterHeld = both;
    }

    const bool turnOn = gTurnEnabled.load(std::memory_order_acquire);
    const bool releaseTurn = gTurnNeutralize.exchange(false, std::memory_order_acq_rel);
    if (turnOn || (gTurnPrimed && gLastTurnSent != 0)) {
        const float dead = gTurnDeadzoneHundredths.load(std::memory_order_relaxed) / 100.0f;
        const float scale = gTurnScalePercent.load(std::memory_order_relaxed) / 100.0f;
        float value = (turnOn && !releaseTurn) ? right.thumbstickX : 0.0f;
        if (!std::isfinite(value)) { value = 0.0f; }
        // Rescaled past the deadzone rather than clipped, so leaving the dead
        // area is gentle instead of a step to full rate.
        const float magnitude = std::fabs(value);
        value = (magnitude <= dead || dead >= 1.0f)
                    ? 0.0f
                    : ((value > 0.0f) ? 1.0f : -1.0f) * ((magnitude - dead) / (1.0f - dead));
        value *= scale;
        // Emit only on change, and ALWAYS emit the return to zero: the engine
        // keeps turning on the last value it was told until contradicted, which
        // is the same failure the movement lane's release case exists to avoid.
        if (!gTurnPrimed || (value == 0 && gLastTurnSent != 0) ||
            std::fabs(value - gLastTurnSent) > 0.002f) {
            const int milli = static_cast<int>(value * 1000.0f);
            if (PostRawInputImmediate(locomotion::kKeyThumbRX,
                                      locomotion::kStateChanged, milli) == 0) {
                gLastTurnSent = value;
                gTurnPrimed = true;
                gTurnPosted.fetch_add(1, std::memory_order_relaxed);
            } else {
                if (releaseTurn) { gTurnNeutralize.store(true, std::memory_order_release); }
                gTurnRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    const bool fireOn = gFireEnabled.load(std::memory_order_acquire);
    const bool releaseFire = gFireNeutralize.exchange(false, std::memory_order_acq_rel);
    if (fireOn || (gFirePrimed && gLastFireSent != 0)) {
        // **The real travel is posted, not a quantised press.** `xi_triggerr` is
        // an analog axis, and sending only 0 or 1000 threw away everything
        // between -- which matters for a weapon whose native binding may ramp.
        float value = (fireOn && !releaseFire) ? right.triggerValue : 0.0f;
        if (!std::isfinite(value)) { value = 0.0f; }
        value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        const bool pressed = fireOn && !releaseFire && right.triggerPressed;
        // Emit on a meaningful change OR on a press-state crossing, so the
        // counters still name a discrete pull while the value stays continuous.
        if (!gFirePrimed ||
            (value == 0 && gLastFireSent != 0) || std::fabs(value - gLastFireSent) > 0.02f) {
            const int milli = static_cast<int>(value * 1000.0f);
            if (PostRawInputImmediate(input::kTriggerR,
                                      locomotion::kStateChanged, milli) == 0) {
                gLastFireSent = value;
                gFirePrimed = true;
            } else {
                if (releaseFire) { gFireNeutralize.store(true, std::memory_order_release); }
                gFireRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // **The axis alone fires nothing, and that cost a headset session.**
        // The trigger is TWO keys: 0x20F is the analog axis and 0x21D is the
        // digital button, and firing is bound to the button (R-115). Posting
        // only the axis was accepted -- 36 presses, zero refusals -- and did
        // nothing at all. A valid key that nothing is bound to is
        // indistinguishable in the counters from one that works, which is
        // exactly what the fail-closed refusal cannot catch.
        //
        // Sent as a press/release EDGE rather than a changed value, because a
        // button posted as "changed" is not a press and the action map wants
        // the transition.
        if (pressed != gFireHeld) {
            const unsigned int state = pressed ? static_cast<unsigned int>(input::kStatePressed)
                                               : static_cast<unsigned int>(input::kStateReleased);
            if (PostRawInputImmediate(input::kTriggerRButton, state, pressed ? 1000 : 0) == 0) {
                gFireHeld = pressed;
                (pressed ? gFirePressed : gFireReleased)
                    .fetch_add(1, std::memory_order_relaxed);
            } else {
                // A refused RELEASE must be retried, or the weapon keeps firing.
                if (!pressed) { gFireNeutralize.store(true, std::memory_order_release); }
                gFireRefused.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

DWORD SetTurnLaneEnabled(unsigned int enabled)
{
    if (enabled > 1) { return 1; }
    if (enabled && SetInputPostEnabled(1) != 0) { return 2; }
    gTurnEnabled.store(enabled != 0u, std::memory_order_release);
    if (!enabled) { gTurnNeutralize.store(true, std::memory_order_release); }
    Log(std::string("result=0 detail=turn value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD SetTurnLaneDeadzone(unsigned int hundredths)
{
    if (hundredths > 60u) { return 1; }
    gTurnDeadzoneHundredths.store(hundredths, std::memory_order_relaxed);
    return 0;
}

DWORD SetTurnLaneScale(unsigned int percent)
{
    if (percent < 10u || percent > 200u) { return 1; }
    gTurnScalePercent.store(percent, std::memory_order_relaxed);
    return 0;
}

DWORD SetFireLaneEnabled(unsigned int enabled)
{
    if (enabled > 1) { return 1; }
    if (enabled && SetInputPostEnabled(1) != 0) { return 2; }
    gFireEnabled.store(enabled != 0u, std::memory_order_release);
    if (!enabled) { gFireNeutralize.store(true, std::memory_order_release); }
    Log(std::string("result=0 detail=fire value=") + (enabled ? "1" : "0"));
    return 0;
}

DWORD SetFireLaneThreshold(unsigned int hundredths)
{
    if (hundredths < 5u || hundredths > 95u) { return 1; }
    return 0;   // XrInput already thresholds the trigger; kept for the contract
}

unsigned int TurnLaneEnabled() { return gTurnEnabled.load(std::memory_order_relaxed) ? 1u : 0u; }
unsigned long long TurnLanePosted() { return gTurnPosted.load(std::memory_order_relaxed); }
unsigned long long TurnLaneRefused() { return gTurnRefused.load(std::memory_order_relaxed); }
unsigned int FireLaneEnabled() { return gFireEnabled.load(std::memory_order_relaxed) ? 1u : 0u; }
unsigned long long FireLanePressed() { return gFirePressed.load(std::memory_order_relaxed); }
unsigned long long FireLaneReleased() { return gFireReleased.load(std::memory_order_relaxed); }
unsigned long long FireLaneRefused() { return gFireRefused.load(std::memory_order_relaxed); }

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
unsigned long long MoveLaneRecenterCount() { return gRecenters.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
