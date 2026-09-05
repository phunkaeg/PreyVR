#pragma once

#include "preyvr/VrMath.h"

#include <windows.h>

// OpenXR controller input: the missing link between the motion-controller maths
// and anything that can be tested.
//
// **Why this is the piece worth building next.** `MotionController.cpp` already
// carries `ControllerPoseInWorld`, `AimFromController`, `WeaponPoseFromController`,
// `TwoHandedWeaponPose` and `SnapTurn`, with tests. None of it can run, because
// nothing ever asked OpenXR for a controller pose -- there is no action set, no
// suggested binding and no pose space anywhere in the session host. One piece
// unblocks aim, locomotion, and any later hand work at once.
//
// **Grip and aim are both requested, because they are not the same pose and the
// distinction matters here.** Grip sits in the palm and is what a held weapon and
// a hand model hang off; aim points along the controller's pointing axis and is
// what a ray should follow. Using grip for aiming makes a weapon shoot where the
// wrist is angled rather than where the player is pointing.
//
// The thumbstick is read at the same time because it costs one more action and
// locomotion needs it -- `SnapTurn` and `SmoothTurn` are already written and
// waiting for a stick value.
//
// Poses are published latest-wins, the same choice the head pose makes: an older
// controller pose is a worse answer to the same question, so a queue would only
// add latency.
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
void UpdateXrInput(void* sessionHandle, void* spaceHandle, long long predictedDisplayTime);

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
