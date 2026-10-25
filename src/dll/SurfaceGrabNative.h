#pragma once
#include "FlightNative.h"
namespace preyvr::dll {
struct SurfaceOwner {FlightOwner flight{};std::uintptr_t animated=0;};
DWORD EnsureSurfaceGrabHook();
DWORD ReadSurfaceGrabOwner(SurfaceOwner&);
bool IsFixedSurfaceCollider(std::uintptr_t collider);
}
