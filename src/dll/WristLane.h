#pragma once

#include "preyvr/VrMath.h"

#include <windows.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

// The status hologram on the left forearm (`wrist.*`; pure rules in
// preyvr/WristHolo.h). Three threads, one card:
//
//   animation  PublishWristArm: the left forearm and the right hand as skinned
//              for a frame, in OpenXR space, keyed by that frame's display time
//   game       UpdateWristLane: while the card is a candidate, scene segments
//              from the camera to the card -- the arm in a wall hides it
//   XR         DecideWrist: the arm of the frame being submitted, the gaze
//              test, the fade; what the session host composites
//
// The card is placed from the arm of the SAME frame as the eye images it is
// composited over, so it stays on the sleeve however fast the arm moves.
namespace preyvr::dll {
struct GameplayPoseFrame;
struct TrackingFrame;

struct WristArmFrame {
    long long displayTime = 0;
    std::uint64_t publishedNs = 0;
    bool left = false;          // the left forearm below is valid
    Vec3 wrist{}, elbow{}, palm{};
    bool right = false;         // the right hand below is valid
    Vec3 rightWrist{}, rightGrip{};
    Vec3 rightAim{};            // unit, the right controller's aim direction
    bool weapon = false;        // the right hand holds a weapon (not the free arms)
};
// Animation thread (AnimIkTakeover), once per owned frame.
void PublishWristArm(const WristArmFrame& frame);

// Game thread, once per gameplay frame.
void UpdateWristLane(const GameplayPoseFrame& frame, bool tracking);

struct WristDecision {
    bool active = false;     // composite a card this frame
    Pose pose{};             // OpenXR quad pose (app space)
    float width = 0, height = 0;
    float alpha = 0;         // fade, applied to the texture
    bool capture = false;    // ask the HUD for the native status this frame
};
// XR thread, once per submitted frame. `eligible` carries the host's gates
// (gameplay input, no menu/panel, not aiming two-handed, tracking usable);
// `submittedTime` is the display time the eye images were rendered for.
WristDecision DecideWrist(bool eligible, const TrackingFrame& latest, long long submittedTime,
                          long long frameTime, float aspect);
// Legacy grip-anchored card (Funk's prototype) instead of the hologram.
bool WristLegacyAnchor();
// XR thread: the host's gate as bits (1 option, 2 views, 4 rendered, 8 no menu
// panel, 16 no VR options, 32 gameplay input, 64 not two-handed, 128 no
// recentre in flight, 256 head and left hand tracked), for wrist.report.
void NoteWristGate(unsigned bits);
// The native status capture's presentation: scale of the movie's own (percent).
unsigned WristNativeScalePercent();
// Keep the native meters' backdrop and top line on the wrist (else the glass).
bool WristNativeBackground();
// The gameplay HUD keeps its status meters (0) or they move to the wrist (1).
bool WristMovesStatusOffFace();
void ResetWristLane();

std::string WristReport();
bool ExecuteWristCommand(const std::vector<std::string>& args, std::ostringstream& out);
}  // namespace preyvr::dll
