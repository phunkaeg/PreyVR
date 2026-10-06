#pragma once

#include "preyvr/RenderContract.h"
#include "preyvr/SlotFeedback.h"
#include "preyvr/TwoHandedAim.h"
#include "preyvr/WeaponAim.h"

#include <windows.h>

#include <array>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

// In-game debug overlay (`dbg.draw`): each hand's ray, the body slots, the
// wrist card test and the foregrip region, drawn into the submitted eye images.
//
// **Drawn into the eye image, with the contract that rendered it.** Every
// primitive is in the app's OpenXR space and is projected with the same eye
// pose and frustum the image is submitted with, at the moment that image is
// copied out of Prey's backbuffer. So in the headset each line lands on the
// physical point it describes, both eyes agree, and any capture of the
// submitted images (the mock, xr-tape) shows exactly what the wearer saw. It
// needs no engine debug renderer and changes no game state.
//
// Off by default. With the mask at 0 every producer returns after one atomic
// load and the renderer is never reached.
namespace preyvr::dll {
struct GameplayPoseFrame;
struct TrackingFrame;

// Settings and diagnostics (command channel).
DWORD SetDebugOverlay(unsigned mask);
unsigned DebugOverlayMask();
std::string DebugOverlayReport();   // counters and the status text
std::string DebugOverlayMarks();    // primitive positions, OpenXR space and last projected pixels

// Producers. All return immediately when the overlay is off.
// Game/input-drain thread, from the aim takeover, after its sample is final.
void DebugOverlayAim(const GameplayPoseFrame& frame, const aim::Sample& sample);
// Game/input-drain thread, once per gameplay frame (left ray).
void DebugOverlayGameFrame(const GameplayPoseFrame& frame, bool tracking);
// Game/input-drain thread, from the two-hand solver.
void DebugOverlayForegrip(const GameplayPoseFrame& frame, const twohand::Input& input,
                          const twohand::Output& output, bool regionReady);
// Holster input lane. `centres`/`valid` describe the gesture's torso estimate.
void DebugOverlayHolsters(bool live, const std::array<Vec3, 2>& centres, Vec3 hand, bool gripPressed,
                          bool armed, bool owned);
void DebugOverlayHolsterSlots(bool hipStored, bool chestStored);
// Medkit lane.
void DebugOverlayMedkit(bool live, Vec3 centre, Vec3 hand, bool gripPressed, bool armed, bool owned,
                        bool triggerArmed);
// Any slot notice, whether or not belt hints are shown.
void DebugOverlayNotice(equipment::SlotNotice notice);
// XR frame thread: the wrist layer's own decision this frame.
void DebugOverlayWristDecision(bool gate, bool visible);

// Render thread, inside the XR frame: draws into `eyeImage` (which must be a
// render target) using the contract that produced it. `shownTangents` (left,
// right, up, down; may be null) is the frustum the runtime displays, which
// keeps text inside what the wearer can see. Restores every piece of pipeline
// state it touches.
void DrawDebugOverlay(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* eyeImage,
                      const RenderContract& contract, const float* shownTangents);
// Render thread, at XR teardown.
void ReleaseDebugOverlay();
}  // namespace preyvr::dll
