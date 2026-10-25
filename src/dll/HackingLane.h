#pragma once
#include <windows.h>
#include <string>
namespace preyvr::dll {
DWORD SetHackingControls(unsigned enabled);
unsigned HackingControlsEnabled();
bool HackingInputOwnsControls();
// Main engine input-drain thread only; uses the existing native input pipeline.
void UpdateHackingInput();
void InvalidateHackingInput();
void SetHackingInputActive(bool active);
std::string HackingInputReport();
}
