#pragma once
#include <string>
namespace preyvr::dll {
// Owned by the command worker; the worker polls hotkeys and advances startup.
void EnableVrMode();
void DisableVrMode();
void TickVrMode();
std::string VrModeReport();
// While active, r_DrawNearFoV follows the vertical FOV the world camera rendered
// (default on). 0 keeps the value set at activation (120-degree assumption).
void FollowNearFov();
unsigned long SetNearFovFollow(unsigned enabled);
unsigned NearFovFollowed();
}
