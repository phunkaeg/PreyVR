#pragma once
#include <windows.h>
#include <string>
namespace preyvr::dll {
struct TrackingFrame;
DWORD SetNativeFlightControls(unsigned enabled);
unsigned NativeFlightControlsEnabled();
void SetNativeFlightActive(bool active);
void PrepareNativeFlightInput();
void DrainNativeFlightInput();
// 0: existing ground/native path; 1: flight waiting/cleanup; 2: coherent flight.
unsigned NativeFlightMovementMode();
bool ReadNativeFlightTracking(TrackingFrame&);
bool NativeFlightOwnsFaces();
bool NativeFlightCleanupPending();
std::string NativeFlightReport();
}
