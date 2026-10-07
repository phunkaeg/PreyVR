#pragma once

#include <windows.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// Grenades thrown with the right hand (GrenadeThrow.h). Native seams (Steam
// PreyDll.dll), all on CArkWeaponGrenade:
//   ThrowGrenade 0x16A6300 (vtable +0x118; the throw animation's "Throw" event
//   calls it from 0x169DDA9): the spawn, the speed, the ammo.
//   Its projectile creator 0x168F2B0 (called at 0x16A6497): where it spawns and
//   which way it faces.
//   The release 0x16A6040 (vtable +0xE8; the fire action's release): where the
//   wind-up animation starts, and where the VR throw leaves the hand instead.
namespace preyvr::dll {

struct GameplayPoseFrame;

// Installs the hooks once. False if the binary does not match.
bool EnsureGrenadeLane();

// grenade.hand 1: the grenade leaves the hand at the trigger's release with the
// hand's velocity (default when VR starts); 0: the game's own throw, exactly.
DWORD SetGrenadeHand(unsigned enabled);
unsigned GrenadeHandEnabled();

// The IK drives the hand holding this weapon like a free hand (no barrel to
// align): true for the player's grenade while grenade.hand is on. Any thread.
bool GrenadeHeldByHand(std::uintptr_t weapon);

// Input lane: the right trigger's press/release edges as posted to the game.
void NoteGrenadeTrigger(bool pressed);
// Game thread, once per gameplay frame: queued gives, the flight watch.
void UpdateGrenadeLane(const GameplayPoseFrame& frame, bool tracking);

std::string GrenadeReport();
// grenade.* verbs; false if the verb is not one of them.
bool ExecuteGrenadeCommand(const std::vector<std::string>& args, std::ostringstream& out);

}  // namespace preyvr::dll
