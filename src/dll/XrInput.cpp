#include "XrInput.h"
#include "preyvr/LatestSnapshot.h"

#include "InputPost.h"

#include "preyvr/MenuNavigator.h"

#include "Logger.h"

// Core OpenXR only. This file touches no graphics API, so it deliberately does
// not define XR_USE_GRAPHICS_API_D3D11 -- doing so makes openxr_platform.h
// reference D3D11 types and demand <d3d11.h> for no benefit here.
#include <openxr/openxr.h>

#include <array>
#include <atomic>
#include <cstring>
#include <sstream>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_xr_input " + line);
}

void LogResult(const char* step, XrResult result)
{
    std::ostringstream line;
    line << "result=failed step=" << step << " code=" << static_cast<int>(result);
    Log(line.str());
}

struct HandActions {
    XrPath subactionPath = XR_NULL_PATH;
    XrSpace gripSpace = XR_NULL_HANDLE;
    XrSpace aimSpace = XR_NULL_HANDLE;
};

XrActionSet gActionSet = XR_NULL_HANDLE;
XrAction gGripPose = XR_NULL_HANDLE;
XrAction gAimPose = XR_NULL_HANDLE;
XrAction gThumbstick = XR_NULL_HANDLE;
XrAction gMenuAccept = XR_NULL_HANDLE;
XrAction gMenuCancel = XR_NULL_HANDLE;
XrAction gMenuStart = XR_NULL_HANDLE;
XrAction gTrigger = XR_NULL_HANDLE;
XrAction gSqueeze = XR_NULL_HANDLE;
std::array<HandActions, 2> gHands{};

std::atomic<bool> gCreated{false};
std::atomic<bool> gMenuNavigation{false};
std::atomic<long long> gLastDisplayTime{0};
std::atomic<unsigned long long> gMenuActions{0};
// One navigator, touched only from the frame service that owns UpdateXrInput.
input::MenuNavigator gNavigator;
std::atomic<unsigned long long> gSyncs{0};
// Counted separately, because "the session is not focused" and "the controllers
// are not tracking" have different fixes -- put the headset on, versus pick the
// controllers up.
std::atomic<unsigned long long> gSyncsNotFocused{0};
std::array<std::atomic<unsigned long long>, 2> gLocated{};

LatestSnapshot<TrackingFrame> gTrackingFrame;
std::atomic<std::uint64_t> gTrackingSequence{0};
std::atomic<std::uint64_t> gTrackingEpoch{1};

Pose FromXrPose(const XrPosef& pose)
{
    return Pose{
        Quaternion{pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w},
        Vec3{pose.position.x, pose.position.y, pose.position.z}};
}

PoseValidity ValidityFrom(XrSpaceLocationFlags flags)
{
    PoseValidity validity{};
    validity.orientationValid = (flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
    validity.positionValid = (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
    validity.orientationTracked = (flags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
    validity.positionTracked = (flags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    return validity;
}

XrPath StringToPath(XrInstance instance, const char* text)
{
    XrPath path = XR_NULL_PATH;
    if (XR_FAILED(xrStringToPath(instance, text, &path))) {
        return XR_NULL_PATH;
    }
    return path;
}

XrAction CreateAction(XrActionType type, const char* name, const char* localised,
                      const std::array<XrPath, 2>& subactions)
{
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = type;
    std::strncpy(info.actionName, name, sizeof(info.actionName) - 1);
    std::strncpy(info.localizedActionName, localised, sizeof(info.localizedActionName) - 1);
    info.countSubactionPaths = static_cast<std::uint32_t>(subactions.size());
    info.subactionPaths = subactions.data();
    XrAction action = XR_NULL_HANDLE;
    if (XR_FAILED(xrCreateAction(gActionSet, &info, &action))) {
        LogResult(name, XR_ERROR_RUNTIME_FAILURE);
        return XR_NULL_HANDLE;
    }
    return action;
}

} // namespace

bool CreateXrInput(void* instanceHandle, void* sessionHandle)
{
    const auto instance = static_cast<XrInstance>(instanceHandle);
    const auto session = static_cast<XrSession>(sessionHandle);
    if (instance == XR_NULL_HANDLE || session == XR_NULL_HANDLE) {
        return false;
    }

    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strncpy(setInfo.actionSetName, "preyvr", sizeof(setInfo.actionSetName) - 1);
    std::strncpy(setInfo.localizedActionSetName, "PreyVR",
                 sizeof(setInfo.localizedActionSetName) - 1);
    setInfo.priority = 0;
    XrResult result = xrCreateActionSet(instance, &setInfo, &gActionSet);
    if (XR_FAILED(result)) {
        LogResult("create_action_set", result);
        return false;
    }

    const std::array<XrPath, 2> subactions = {
        StringToPath(instance, "/user/hand/left"),
        StringToPath(instance, "/user/hand/right"),
    };
    gHands[0].subactionPath = subactions[0];
    gHands[1].subactionPath = subactions[1];

    // Grip and aim are separate actions on purpose. Grip is the palm, which a
    // held weapon hangs off; aim is the pointing axis, which a ray follows.
    gGripPose = CreateAction(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip Pose", subactions);
    gAimPose = CreateAction(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim Pose", subactions);
    gThumbstick = CreateAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", subactions);
    gTrigger = CreateAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", subactions);
    gSqueeze = CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "squeeze", "Squeeze", subactions);
    gMenuAccept = CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_accept", "Menu Accept",
                               subactions);
    gMenuCancel = CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_cancel", "Menu Cancel",
                               subactions);
    gMenuStart = CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu_start", "Menu Start",
                              subactions);
    if (gGripPose == XR_NULL_HANDLE || gAimPose == XR_NULL_HANDLE) {
        Log("result=failed step=create_pose_actions");
        return false;
    }

    // Oculus Touch: the Quest 3's profile. Other profiles can be suggested
    // alongside later; a runtime simply ignores bindings for hardware it does
    // not have, so adding them costs nothing but is not needed to test here.
    // A/B are right-hand only on Touch and X/Y are left-hand only, so accept and
    // cancel bind to one controller rather than both. The menu button is the
    // left one; its right-hand counterpart is reserved by the runtime.
    const std::array<XrActionSuggestedBinding, 13> bindings = {{
        {gGripPose, StringToPath(instance, "/user/hand/left/input/grip/pose")},
        {gGripPose, StringToPath(instance, "/user/hand/right/input/grip/pose")},
        {gAimPose, StringToPath(instance, "/user/hand/left/input/aim/pose")},
        {gAimPose, StringToPath(instance, "/user/hand/right/input/aim/pose")},
        {gThumbstick, StringToPath(instance, "/user/hand/left/input/thumbstick")},
        {gThumbstick, StringToPath(instance, "/user/hand/right/input/thumbstick")},
        {gTrigger, StringToPath(instance, "/user/hand/left/input/trigger/value")},
        {gTrigger, StringToPath(instance, "/user/hand/right/input/trigger/value")},
        {gSqueeze, StringToPath(instance, "/user/hand/left/input/squeeze/value")},
        {gSqueeze, StringToPath(instance, "/user/hand/right/input/squeeze/value")},
        {gMenuAccept, StringToPath(instance, "/user/hand/right/input/a/click")},
        {gMenuCancel, StringToPath(instance, "/user/hand/right/input/b/click")},
        {gMenuStart, StringToPath(instance, "/user/hand/left/input/menu/click")},
    }};
    XrInteractionProfileSuggestedBinding suggested{
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile =
        StringToPath(instance, "/interaction_profiles/oculus/touch_controller");
    suggested.suggestedBindings = bindings.data();
    suggested.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
    result = xrSuggestInteractionProfileBindings(instance, &suggested);
    if (XR_FAILED(result)) {
        LogResult("suggest_bindings", result);
        return false;
    }

    for (std::size_t hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;
        spaceInfo.subactionPath = gHands[hand].subactionPath;

        spaceInfo.action = gGripPose;
        if (XR_FAILED(xrCreateActionSpace(session, &spaceInfo, &gHands[hand].gripSpace))) {
            Log("result=failed step=create_grip_space");
            return false;
        }
        spaceInfo.action = gAimPose;
        if (XR_FAILED(xrCreateActionSpace(session, &spaceInfo, &gHands[hand].aimSpace))) {
            Log("result=failed step=create_aim_space");
            return false;
        }
    }

    // **Once per session, and only here.** OpenXR permits this exactly once, so
    // input cannot be armed later from a console call -- it is created with the
    // session or not at all.
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &gActionSet;
    result = xrAttachSessionActionSets(session, &attach);
    if (XR_FAILED(result)) {
        LogResult("attach_action_sets", result);
        return false;
    }

    gCreated.store(true, std::memory_order_release);
    Log("result=0 detail=input_created profile=oculus/touch_controller");
    return true;
}

void UpdateXrInput(void* sessionHandle, void* spaceHandle, long long predictedDisplayTime,
                   const Pose& headPose, const PoseValidity& headValidity)
{
    TrackingFrame frame{};
    frame.head = headPose;
    frame.headValidity = headValidity;
    frame.displayTime = predictedDisplayTime;
    ControllerState rightState{};
    bool leftStart = false;
    if (!gCreated.load(std::memory_order_acquire)) {
        gTrackingEpoch.fetch_add(1);
        gTrackingFrame.Clear();
        return;
    }
    const auto session = static_cast<XrSession>(sessionHandle);
    const auto space = static_cast<XrSpace>(spaceHandle);

    XrActiveActionSet active{gActionSet, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    // **XR_SESSION_NOT_FOCUSED is a SUCCESS code, and that matters here.**
    // Checking only XR_FAILED counts an unfocused sync as an effective one, so
    // the counter reads healthy while every action is inactive and no pose can
    // locate. That is exactly what happened on 2026-09-04: 277 syncs, zero hands
    // located, and the cause was the headset sitting on a desk rather than being
    // worn. The counter said the input layer was working; it was reporting that
    // the call returned, not that it did anything.
    const XrResult syncResult = xrSyncActions(session, &sync);
    if (XR_FAILED(syncResult)) {
        gTrackingEpoch.fetch_add(1);
        gTrackingFrame.Clear();
        return;
    }
    if (syncResult == XR_SESSION_NOT_FOCUSED) {
        gSyncsNotFocused.fetch_add(1, std::memory_order_relaxed);
        gTrackingEpoch.fetch_add(1);
        gTrackingFrame.Clear();
        return;   // never retain a tracked snapshot after focus loss
    }
    gSyncs.fetch_add(1, std::memory_order_relaxed);

    for (std::size_t hand = 0; hand < 2; ++hand) {
        ControllerState state{};
        const XrPath subaction = gHands[hand].subactionPath;

        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(xrLocateSpace(gHands[hand].gripSpace, space,
                                       static_cast<XrTime>(predictedDisplayTime), &location))) {
            state.gripPose = FromXrPose(location.pose);
            state.gripValidity = ValidityFrom(location.locationFlags);
        }
        XrSpaceLocation aimLocation{XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(xrLocateSpace(gHands[hand].aimSpace, space,
                                       static_cast<XrTime>(predictedDisplayTime), &aimLocation))) {
            state.aimPose = FromXrPose(aimLocation.pose);
            state.aimValidity = ValidityFrom(aimLocation.locationFlags);
        }

        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.subactionPath = subaction;

        get.action = gThumbstick;
        XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (XR_SUCCEEDED(xrGetActionStateVector2f(session, &get, &stick)) && stick.isActive) {
            state.thumbstickX = stick.currentState.x;
            state.thumbstickY = stick.currentState.y;
        }
        get.action = gTrigger;
        XrActionStateFloat analog{XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &analog)) && analog.isActive) {
            state.triggerValue = analog.currentState;
            // Half travel is the conventional trip point, and it is applied here
            // rather than relied on from the runtime so the threshold is one
            // known number instead of a per-runtime behaviour.
            state.triggerPressed = analog.currentState >= 0.5f;
        }
        XrActionStateBoolean button{XR_TYPE_ACTION_STATE_BOOLEAN};
        get.action = gSqueeze;
        if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &button)) && button.isActive) {
            state.gripPressed = button.currentState == XR_TRUE;
        }
        get.action = gMenuAccept;
        if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &button)) && button.isActive) {
            state.menuAccept = button.currentState == XR_TRUE;
        }
        get.action = gMenuCancel;
        if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &button)) && button.isActive) {
            state.menuCancel = button.currentState == XR_TRUE;
        }
        get.action = gMenuStart;
        if (XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &button)) && button.isActive) {
            state.menuStart = button.currentState == XR_TRUE;
        }

        if (state.gripValidity.orientationValid || state.aimValidity.orientationValid) {
            gLocated[hand].fetch_add(1, std::memory_order_relaxed);
        }
        frame.hands[hand] = state;
        if (hand == static_cast<int>(Hand::right)) {
            rightState = state;
        } else {
            leftStart = state.menuStart;
        }
    }

    frame.sequence = gTrackingSequence.fetch_add(1, std::memory_order_relaxed) + 1;
    frame.epoch = gTrackingEpoch.load();
    frame.publishedNs = MonotonicNanoseconds();
    gTrackingFrame.Publish(frame);

    // --- controller-driven menus ---------------------------------------------
    //
    // The delta comes from the runtime's own predicted display times rather than
    // a wall clock, so the repeat behaviour follows the frames the player is
    // actually seeing. A first frame, or a backwards or absurd step, contributes
    // nothing rather than a guess.
    if (gMenuNavigation.load(std::memory_order_acquire)) {
        float delta = 0.0f;
        const long long previous = gLastDisplayTime.exchange(predictedDisplayTime,
                                                             std::memory_order_acq_rel);
        if (previous != 0 && predictedDisplayTime > previous) {
            const long long elapsed = predictedDisplayTime - previous;
            if (elapsed < 1000000000LL) {   // a second is not a frame
                delta = static_cast<float>(elapsed) / 1.0e9f;
            }
        }

        input::ControllerMenuState menu;
        menu.stickX = rightState.thumbstickX;
        menu.stickY = rightState.thumbstickY;
        menu.accept = rightState.menuAccept;
        menu.cancel = rightState.menuCancel;
        menu.start = rightState.menuStart || leftStart;

        input::MenuAction actions[4];
        const unsigned int count = gNavigator.Update(menu, delta, actions, 4);
        for (unsigned int i = 0; i < count; ++i) {
            // Enqueued, not posted: the engine's input walk belongs to the main
            // thread and this runs on the frame service.
            PostMenuAction(static_cast<unsigned int>(actions[i]));
            gMenuActions.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void SetMenuNavigation(unsigned int enabled)
{
    const bool on = enabled != 0u;
    gMenuNavigation.store(on, std::memory_order_release);
    if (!on) {
        gNavigator.Reset();
    }
    Log(std::string("result=0 detail=menu_navigation value=") + (on ? "1" : "0"));
}

unsigned long long MenuNavigationActionCount()
{
    return gMenuActions.load(std::memory_order_relaxed);
}

void DestroyXrInput()
{
    for (auto& hand : gHands) {
        if (hand.gripSpace != XR_NULL_HANDLE) xrDestroySpace(hand.gripSpace);
        if (hand.aimSpace != XR_NULL_HANDLE) xrDestroySpace(hand.aimSpace);
        hand = HandActions{};
    }
    if (gActionSet != XR_NULL_HANDLE) {
        xrDestroyActionSet(gActionSet);
        gActionSet = XR_NULL_HANDLE;
    }
    gGripPose = gAimPose = gThumbstick = gTrigger = gSqueeze = XR_NULL_HANDLE;
    gMenuAccept = gMenuCancel = gMenuStart = XR_NULL_HANDLE;
    gCreated.store(false, std::memory_order_release);
    gSyncs.store(0, std::memory_order_relaxed);
    gSyncsNotFocused.store(0, std::memory_order_relaxed);
    for (auto& count : gLocated) {
        count.store(0, std::memory_order_relaxed);
    }
    gTrackingEpoch.fetch_add(1);
    gTrackingFrame.Clear();
}

bool TryGetTrackingFrame(TrackingFrame& out)
{
    if (!gTrackingFrame.TryRead(out)) { return false; }
    const auto now = MonotonicNanoseconds();
    if (!FreshSample(now, out.publishedNs)) { return false; }
    const auto age = now - out.publishedNs;
    out.headValidity.ageNanoseconds = age;
    for (auto& state : out.hands) {
        state.gripValidity.ageNanoseconds = age;
        state.aimValidity.ageNanoseconds = age;
    }
    return true;
}

bool TryGetControllerState(Hand hand, ControllerState& out)
{
    TrackingFrame frame{};
    const auto index = static_cast<std::size_t>(hand);
    if (index >= frame.hands.size() || !TryGetTrackingFrame(frame)) { return false; }
    out = frame.hands[index];
    return true;
}

unsigned long long XrInputSyncCount() { return gSyncs.load(std::memory_order_relaxed); }

unsigned long long XrInputNotFocusedCount()
{
    return gSyncsNotFocused.load(std::memory_order_relaxed);
}

unsigned long long XrInputLocatedCount(Hand hand)
{
    return gLocated[static_cast<std::size_t>(hand)].load(std::memory_order_relaxed);
}

DWORD XrInputCreated() { return gCreated.load(std::memory_order_acquire) ? 1u : 0u; }

} // namespace preyvr::dll
