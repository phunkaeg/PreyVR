#pragma once
#include "preyvr/WeaponAim.h"
#include "preyvr/SceneQuery.h"
#include <string>
namespace preyvr::dll {
struct GameplayPoseFrame;
void QueryAimScene(const GameplayPoseFrame&,aim::Sample&);
void SetSceneReticle(unsigned enabled);
unsigned SceneReticleEnabled();
std::string SceneQueryReport();
// Input-drain thread only, synchronous caller-owned hit. Miss: distance=-1.
bool QueryPhysicalSegment(const GameplayPoseFrame&,Vec3 origin,Vec3 delta,scene::Hit&);
// Input-drain thread only: one full-range ray for the debug overlay. Shares the
// query's gates and its process-wide fault latch; never changes the reticle.
bool QueryDebugRay(const GameplayPoseFrame&,Vec3 origin,Vec3 direction,scene::Hit&);
bool SceneQueryFaulted();
}
