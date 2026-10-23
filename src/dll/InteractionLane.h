#pragma once

#include <windows.h>

#include "preyvr/VrMath.h"

#include <array>
#include <cstdint>
#include <string>

// `use.*`: world interaction from the LEFT hand (preyvr/InteractionUse.h has
// the why). This file owns the native side:
//
//   - the selector seam: ArkPlayerTargetSelector::UpdateCandidates (R-022,
//     0x159A660) runs with the player's cached reticle ray set to the left
//     hand's pointing ray, restored on return. It is the only interaction
//     reader of that ray: a getter probe on 2026-10-06 found exactly three
//     per-frame readers, this one, the HUD markers (0x15A7C10) and the HUD
//     reticle (0x1667D50). Those two keep the weapon's ray. Everything after
//     the selection -- prompt, highlight, Interact, carry, loot -- is native.
//   - suppression: while the right grip reloads, the selector runs looking
//     at an empty point of the world, so the shared use/reload button reloads.
//   - observation: Interact (R-023) and PerformInteraction (0x1593980) are
//     watched read-only, so a press is reported as the action the game took,
//     not as a button that was sent.
//
// Every write is scoped to one native call on the game thread and restored in
// a __finally; nothing is left edited between frames.
namespace preyvr::dll {
struct GameplayPoseFrame;

// Settings (command channel and VR options).
DWORD SetUseHand(unsigned left);          // 1: left hand points and uses (default); 0: right grip, eye ray
unsigned UseHandLeft();
DWORD SetUseHold(unsigned enabled);       // post the use button as held every frame while down (default 1)
unsigned UseHoldEnabled();
DWORD SetUseFilter(unsigned enabled, float minCutoff, float beta, float backlashDegrees);
DWORD SetUseBackMillimetres(unsigned mm); // ray origin behind the controller (default 100)
DWORD SetUseTrace(unsigned enabled);      // log every selection change and press

// Installs the hooks (idempotent). False if a prologue does not match.
bool EnsureInteractionLane();

// Game thread, once per gameplay frame from the aim hook, after the other lanes.
void UpdateInteractionLane(const GameplayPoseFrame& frame, bool tracking);

// For the use button (MoveLane, same thread).
bool UseTargetPresent();              // the game has a usable entity right now
std::uint32_t UseTargetId();          // its id (0 = none)
bool UseTargetTapOnly();              // a pickup whose hold is the hoover
bool UseCarrying();                   // the game is carrying something (the button drops it)
// Game thread: the seconds the native button must be held to CARRY `entity`
// (its carry-type interaction record), if it is the current selection; -1 if
// that is unknown. Leverage props need ~0.75 s, the generic hold mode ~0.33 s.
float UseCarryHoldSeconds(std::uint32_t entity);
void RequestUseTargetSuppression(bool suppress);
void NoteUseButton(bool down, unsigned owner, bool pressed, bool released, unsigned events);

// What the pointer shows (debug overlay). Engine world space.
struct UsePointer {
    bool valid = false;
    Vec3 hand{};           // the controller's aim point the line starts from
    Vec3 direction{};
    bool target = false;   // the game selected something
    Vec3 end{};            // where the line ends (on the target's bounds when target)
    bool onLine = false;   // the ray passes through the target's bounds
    bool suppressed = false;
    bool held = false;     // the use button is down
    std::uint32_t entity = 0;
    char name[64]{};
    char action[24]{};
    std::array<Vec3, 8> candidates{};
    unsigned candidateCount = 0;
    std::uint64_t ns = 0, reference = 0;
};
bool TryGetUsePointer(UsePointer& out);

std::string UseReport();
// Test harness: the entity's bounds centre and the controller yaw/pitch (the
// mock's lhandYaw/lhandPitch) that point the left hand straight at it.
std::string UseFind(std::uint32_t id);
std::string UseStatusLine();   // one line for the overlay's status panel
}  // namespace preyvr::dll
