#pragma once

#include "preyvr/VrMath.h"

#include <windows.h>

#include <sstream>
#include <string>
#include <vector>

// The character follows the player (`body.*`; pure rules in preyvr/BodyFollow.h).
//
// The view is `game yaw - reference yaw + head` (HeadTrackingHook). A body that
// does not turn with the head is the game yaw standing still while the head
// turns: the arms twist, and looking down shows the torso facing elsewhere. A
// body that does not walk with the player is the head offset growing while the
// capsule stays: the neck is seen from outside.
//
// Both are closed here, on the game thread, in ArkPlayerCamera::PrePhysicsUpdate
// AFTER the game has integrated its own rotation input for the frame:
//
//   yaw   the player's look rotation (ArkPlayerCamera+0x150, the quaternion
//         ArkPlayer::GetViewRotation returns) is turned by the head's heading
//         error, limited by the game's own LimitRotation, and EXACTLY the turn
//         that was written is taken out of the reference yaw. The view does not
//         move; the body does.
//   room  the character is moved under the player's neck with CEntity::SetPos,
//         only to places a capsule of rays finds free, and EXACTLY the move that
//         was made is taken out of the tracking origin. The view does not move;
//         the body does. A wall stops the body and leaves the head where it is.
//
// Both are deterministic: what is absorbed is what was written, read back the
// next frame. A write the game did not keep is given back (`yawRejected`).
namespace preyvr::dll {

// Installs the PrePhysicsUpdate hook (idempotent). 0 = installed.
DWORD EnsureBodyFollowHook();

// Called by the view hook with the game's own view, before head tracking edits it.
// Takes the body's verified turn and this frame's move out of the reference.
void NoteGameView(const Vec3& eye, const Quaternion& rotation);
// ... and with the camera it composed, for the trace's world-invariance check.
void NoteComposedView(const Vec3& position, const Quaternion& rotation, float playYaw, const Vec3& gameEye,
                      const Vec3& origin, const Pose& head);

// How dark the view should be (0..1): the head has gone into geometry the body
// could not follow it through. Read by the XR thread, which eases toward it.
float BodyFadeAlpha();

// body.yaw / body.room. Both on by default in VR (VrMode).
DWORD SetBodyYawFollow(unsigned enabled);
DWORD SetBodyRoomScale(unsigned enabled);
unsigned BodyYawFollowEnabled();
unsigned BodyRoomScaleEnabled();

// The per-frame composed camera since body.tracestart (the mock tests' measurements).
std::string BodyTrace();
std::string BodyFollowReport();

// The `body.*` verbs of the command channel. False when the verb is not ours.
bool ExecuteBodyCommand(const std::vector<std::string>& args, std::ostringstream& out);

} // namespace preyvr::dll
