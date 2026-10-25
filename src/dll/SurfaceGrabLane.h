#pragma once
#include "preyvr/SurfaceGrab.h"
#include <windows.h>
#include <string>
namespace preyvr::dll {
struct FlightOwner;struct GameplayPoseFrame;struct TrackingFrame;
DWORD SetSurfaceGrabEnabled(unsigned);
unsigned SurfaceGrabEnabled();
DWORD SetSurfaceGrabReach(unsigned centimetres);
unsigned SurfaceGrabReach();
DWORD SetSurfacePushStrength(unsigned percent);
unsigned SurfacePushStrength();
DWORD SetSurfacePushLimit(unsigned centimetresPerSecond);
unsigned SurfacePushLimit();
void PrepareSurfaceGrab(const FlightOwner&,const GameplayPoseFrame&,const TrackingFrame&,bool allowed);
bool SurfaceGrabOwnsMotion();
bool SurfaceGrabOwnsLeftGrip();
std::uint64_t ClaimSurfaceGrabVelocity(std::uintptr_t animated,std::uintptr_t caller,unsigned type,Vec3 nativeVelocity,Vec3& desired);
void FinishSurfaceGrabVelocity(std::uint64_t token,bool success);
std::string SurfaceGrabReport();
}
