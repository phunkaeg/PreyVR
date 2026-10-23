#pragma once

#include <windows.h>

#include <sstream>
#include <string>
#include <vector>

// The first-person arms when no weapon is drawn (holstered, carrying).
namespace preyvr::dll {

struct GameplayPoseFrame;

// Game thread (input drain), once per gameplay frame: the holster state, and a
// requested draw carried out with the native equip.
void UpdateArmsLane(const GameplayPoseFrame& frame, bool valid);
// No weapon drawn and one to draw (holstered), nothing carried, no cinematic.
bool ArmsHolstered();
// The input lane: the right grip asks for the last weapon again.
void RequestDrawWeapon();
std::string ArmsReport();

// arms.* and mem.* verbs; false if the verb is not one of them.
bool ExecuteArmsCommand(const std::vector<std::string>& args, std::ostringstream& out);

}  // namespace preyvr::dll
