#pragma once
#include <string>
namespace preyvr::dll {
// Owned by the command worker; the worker polls hotkeys and advances startup.
void EnableVrMode();
void DisableVrMode();
void TickVrMode();
std::string VrModeReport();
}
