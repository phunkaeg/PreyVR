#pragma once
#include "preyvr/WeaponAim.h"
#include <string>
namespace preyvr::dll {
struct GameplayPoseFrame;
void QueryAimScene(const GameplayPoseFrame&,aim::Sample&);
void SetSceneReticle(unsigned enabled);
unsigned SceneReticleEnabled();
std::string SceneQueryReport();
}
