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
// The 3D VIEW option: 2 both eyes every frame (DualSceneStereo), 1 alternate eyes, 0 mono (one picture from the
// centre of the head in both eyes). Chosen before VR starts, it is applied at activation; while VR is active, at once.
unsigned long SetVrStereoMode(unsigned mode);
unsigned VrStereoModeChosen();   // what was asked for
unsigned VrStereoModeActive();   // what is running (full stereo falls back to alternate eyes if its hooks fail)
unsigned NearFovFollowed();
}
