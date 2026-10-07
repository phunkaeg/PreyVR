#pragma once

#include <windows.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// World interaction, phase 2: what is carried is in the left hand (HandCarry.h).
// Native seams (Steam PreyDll.dll), all on ArkPlayerCarry (= ArkPlayer+0xAF0):
//   Update 0x125E400 (scope + releases), GetLerpTargetLocation 0x125A750 (where a
//   prop is pulled to), GetDragCorpseConstraintPos 0x125A670 (a body's drag point),
//   StartCarrying 0x125C3D0 (what was picked up), StopCarrying 0x125CF70 (the
//   release, and the velocity it leaves with).
namespace preyvr::dll {

struct GameplayPoseFrame;
struct TrackingFrame;

// Installs the hooks once. False if the binary does not match.
bool EnsureCarryLane();

// carry.hand 1: carried objects follow the left hand (default when VR starts);
// 0: the game's own carry, in front of the camera (phase 1, exactly).
DWORD SetCarryHand(unsigned enabled);
unsigned CarryHandEnabled();
// carry.hold 1: held while the grip is (VR); 0: press to pick up, press to drop.
DWORD SetCarryHoldToHold(unsigned enabled);
unsigned CarryHoldToHold();

// XR thread, once per located frame: the hands' motion history.
void RecordCarryMotion(const TrackingFrame& frame);
// Game thread, once per gameplay frame (after the interaction lane).
void UpdateCarryLane(const GameplayPoseFrame& frame, bool tracking);
// Input lane: the grip that holds something opened (or a press asked to let
// go). Executed inside the next native carry update.
void RequestCarryRelease();
// The hand holds something the lane placed (the pointer hides, the fingers close).
bool CarryHoldingInHand();

std::string CarryReport();
// carry.* verbs; false if the verb is not one of them.
bool ExecuteCarryCommand(const std::vector<std::string>& args, std::ostringstream& out);

}  // namespace preyvr::dll
