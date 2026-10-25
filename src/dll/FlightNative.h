#pragma once
#include "preyvr/NativeFlight.h"
#include <windows.h>
#include <cstdint>
namespace preyvr::dll {
struct FlightOwner {std::uintptr_t player=0,entity=0,input=0;int state=0,cinematic=0;};
DWORD EnsureNativeFlightHook();
DWORD ReadNativeFlightOwner(FlightOwner&);
DWORD SetNativeFlightBoost(std::uintptr_t player,bool pressed);
DWORD SetNativeFlightBrake(std::uintptr_t player,bool pressed);
// Called only after the real getter fills the caller's quaternion buffer.
bool ApplyNativeFlightRotation(std::uintptr_t entity,std::uintptr_t caller,flight::NativeQuat&);
}
