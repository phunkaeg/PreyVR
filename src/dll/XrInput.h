#pragma once

#include "preyvr/VrMath.h"
#include <array>

#include <windows.h>

// OpenXR head, grip and aim samples are published together at one predicted
// display time. Grip anchors the wrist; aim supplies the pointing ray. Neither
// is an authored weapon muzzle. Invalid/focus-lost publications are refused.
namespace preyvr::dll {

enum class Hand { left = 0, right = 1 };

struct ControllerState {
    Pose gripPose{};
    Pose aimPose{};
    PoseValidity gripValidity{};
    PoseValidity aimValidity{};
    float thumbstickX = 0.0f;
    float thumbstickY = 0.0f;
    bool triggerPressed = false;
    bool gripPressed = false;
    // Menu buttons. A controller that cannot confirm a choice leaves the mod
    // stopped at the main menu, which is why these are part of the product and
    // not a debug affordance.
    bool menuAccept = false;
    bool menuCancel = false;
    bool menuStart = false;
};

// Head and BOTH hands located at one predicted display time. This is the
// publication unit for gameplay; independently reading latest poses mixes times.
struct TrackingFrame {
    Pose head{};
    PoseValidity headValidity{};
    std::array<ControllerState, 2> hands{}; // indexed by Hand (left, right)
    long long displayTime = 0;
    std::uint64_t sequence = 0;
    std::uint64_t epoch = 0;
    std::uint64_t publishedNs = 0;
};
bool TryGetTrackingFrame(TrackingFrame& out);

// Builds the action set, suggests bindings and creates the pose spaces. Must run
// **before** the session is attached -- OpenXR permits xrAttachSessionActionSets
// exactly once per session, so this cannot be armed later from a console call.
//
// Returns false and logs rather than throwing; the session then runs without
// input, which is a mod with no hands rather than a mod that fails to start.
bool CreateXrInput(void* instanceHandle, void* sessionHandle);

// Syncs actions and locates both controllers. Called once per frame from the
// session's own frame service, after xrWaitFrame so the predicted display time is
// the one the poses are located against.
void UpdateXrInput(void* sessionHandle, void* spaceHandle, long long predictedDisplayTime,
                   const Pose& head, const PoseValidity& headValidity);

// Arms controller-driven menu navigation. Off by default: it posts synthesised
// input into the engine, so it is enabled deliberately like every other write
// this mod performs.
void SetMenuNavigation(unsigned int enabled);
unsigned long long MenuNavigationActionCount();

void DestroyXrInput();

// Latest state for one hand. Returns false before anything has been published.
bool TryGetControllerState(Hand hand, ControllerState& out);

// Diagnostics, so a session with no hands says which half is missing rather than
// silently doing nothing.
unsigned long long XrInputSyncCount();

// Syncs that returned XR_SESSION_NOT_FOCUSED -- a *success* code, so a plain
// XR_FAILED check counts them as working syncs while every action is inactive.
// A high count here means the session does not have focus, which in practice
// means the headset is not being worn; it is a different problem from controllers
// that are not tracking, and it needs a different fix.
unsigned long long XrInputNotFocusedCount();
unsigned long long XrInputLocatedCount(Hand hand);
DWORD XrInputCreated();

} // namespace preyvr::dll
