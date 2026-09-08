// Offline integration review: compile the REAL lane, replace only its external
// input, logger and hook-install boundaries. No game, injection or native calls.
// Exit 1 reports requirements violated by the current production implementation.
#include "../../../src/dll/MoveLane.cpp"
#include "preyvr/XrFrameContract.h"
#include <iostream>
#include <type_traits>
#include <vector>

struct Posted { int key; int value; };
std::vector<Posted> posted;
preyvr::dll::ControllerState controllerState;
bool focused = true;
int rejectedKey = 0;
int failures = 0;

namespace preyvr::lifecycle { void Log(std::string_view) {} }
namespace preyvr::dll {
bool EnsureMinHook() { return false; }
bool InputPostDrivingThisThread() { return true; }
DWORD SetInputPostEnabled(unsigned int) { return 0; }
DWORD PostRawInputImmediate(int key, unsigned int, int value) {
    if (key == rejectedKey) { return 4; }
    posted.push_back({key, value});
    return 0;
}
bool TryGetControllerState(Hand, ControllerState& out) {
    if (!focused) { return false; }
    out = controllerState;
    return true;
}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID, LPVOID*) { return MH_ERROR_UNSUPPORTED_FUNCTION; }
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) { return MH_ERROR_NOT_CREATED; }
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_ERROR_NOT_CREATED; }

void Check(bool ok, const char* name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    failures += !ok;
}
void Reset() {
    using namespace preyvr::dll;
    focused = true;
    rejectedKey = 0;
    controllerState = {};
    posted.clear();
    gInstalled = true; // Skip hook installation: this executable has no target.
    gStick = preyvr::locomotion::StickAxis{};
    gStickPolicyHundredths = 15;
    gLastTurnSent = gLastFireSent = 0;
    gTurnPrimed = gFirePrimed = gFireHeld = false;
    gMoveNeutralize = gTurnNeutralize = gFireNeutralize = false;
    gMoveMayBeHeld = false;
    SetMoveLaneMode(2);
    SetTurnLaneEnabled(1);
    SetFireLaneEnabled(1);
}
void Tick() {
    preyvr::dll::UpdateMoveLane();
    preyvr::dll::UpdateTurnAndFireLanes();
}
bool ZeroFor(int key) {
    for (const auto& e : posted) { if (e.key == key && e.value == 0) return true; }
    return false;
}
void Held() {
    controllerState.thumbstickX = 0.8f;
    controllerState.thumbstickY = 0.6f;
    controllerState.triggerValue = 0.8f;
    controllerState.triggerPressed = true;
    Tick();
}
bool forwarded = false;
bool __fastcall AnalogTarget(void* receiver, unsigned int entity, const void* action,
                             int mode, float value) {
    forwarded = entity == 0x1234 && action == &forwarded && mode == 8 && value == 0.625f;
    std::memcpy(static_cast<std::uint8_t*>(receiver) + 0x5C, &value, sizeof(value));
    return true;
}
int main() {
    using namespace preyvr;
    using namespace preyvr::dll;
    using NativeAnalog = bool(__fastcall*)(void*, unsigned int, const void*, int, float);
    Check((std::is_same_v<AnalogHandlerFn, NativeAnalog>),
          "movement hook preserves native five-argument ABI and return");
    std::array<std::uint8_t, 0xB0> inputReceiver{};
    gOriginalX = &AnalogTarget;
    const bool accepted = AnalogXObserved(inputReceiver.data(), 0x1234, &forwarded, 8, 0.625f);
    Check(accepted && forwarded && MoveLaneAxisMilli(0) == 625,
          "hook forwards all arguments and return; telemetry reads AFTER the native handler");

    Reset(); Held();
    Check(posted.size() == 4, "positive control: four held axes reach the posting boundary");
    posted.clear(); focused = false; Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX) && ZeroFor(locomotion::kKeyThumbLY) &&
          ZeroFor(locomotion::kKeyThumbRX) && ZeroFor(input::kTriggerR),
          "focus loss neutralizes all previously held axes");

    Reset(); Held(); posted.clear();
    SetMoveLaneMode(0); SetTurnLaneEnabled(0); SetFireLaneEnabled(0); Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX) && ZeroFor(locomotion::kKeyThumbLY) &&
          ZeroFor(locomotion::kKeyThumbRX) && ZeroFor(input::kTriggerR),
          "disabling lanes neutralizes all previously held axes");

    Reset();
    controllerState.thumbstickX = 0.151f;
    controllerState.triggerValue = 0.01f;
    Tick();
    posted.clear(); controllerState = {}; Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX) && ZeroFor(locomotion::kKeyThumbRX) &&
          ZeroFor(input::kTriggerR), "return to exact zero bypasses change epsilon");

    Reset(); rejectedKey = locomotion::kKeyThumbLY; Held();
    posted.clear(); rejectedKey = 0; focused = false; Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX) && ZeroFor(locomotion::kKeyThumbLY),
          "partially posted movement pair is still neutralized on focus loss");

    Reset(); Held(); SetMoveLaneMode(0); SetTurnLaneEnabled(0); SetFireLaneEnabled(0);
    rejectedKey = locomotion::kKeyThumbLX; Tick();
    posted.clear(); rejectedKey = 0; Tick();
    Check(ZeroFor(locomotion::kKeyThumbLX), "refused neutralization is retried on the frame thread");

    xrframe::FrameContract contract;
    contract.AllowSingleThreaded(true);
    contract.SetSessionRunning(true);
    contract.OnWaited(100, true, 1);
    contract.OnViewsLocated(100, true, true);
    contract.OnBegun();
    // The resize return at XrSessionHost.cpp:1023 skips OnSubmitted.
    Check(contract.OnWaited(200, true, 1) == xrframe::Result::outOfOrder,
          "positive control: abandoned resize frame rejects the next frame");
    contract.OnSubmitted(1);
    Check(contract.OnWaited(300, true, 1) == xrframe::Result::ok,
          "ending a skipped-copy frame restores frame progression");
    return failures ? 1 : 0;
}
