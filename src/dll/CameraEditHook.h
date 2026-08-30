#pragma once

#include <Windows.h>

// The first write to Prey's render path, as a bounded and reversible experiment.
//
// **Why CSystem::Render and not CSystem::SetViewCamera.** The game rewrites
// m_ViewCamera every frame from ArkPlayerCamera, so a write issued at
// RT_EndFrame is simply overwritten before the next render reads it. The edit
// has to happen inside the window between the camera being set and the render
// consuming it. CSystem::Render (R-058) *is* that window: it reads m_ViewCamera
// and hands it to both consumers, so wrapping it gives
//
//     save -> modify -> render -> restore
//
// which is also the shape that generalises to stereo (call the original twice
// with two cameras). SetViewCamera itself is a 12-byte function ending in a tail
// jump -- a poor hook target for no benefit.
//
// **Why the frustum is rebuilt on our own copy.** CCamera caches derived state:
// eight frustum corners, six planes at +0x10C, plane sign tables, and the
// position at +0x230. Writing the matrix alone leaves all of it stale. So the
// edit is applied to a local copy, CCamera::UpdateFrustum (RVA 0x121D70) is
// called *on that copy*, and only then are the 0x240 bytes blitted into the
// game. The engine function therefore only ever touches our stack, and the sole
// write to game memory is one bounded memcpy with the original bytes held.
//
// **The hook stays installed and becomes a pass-through when disarmed**, rather
// than being removed. F-009 recorded that our observer's disable path did not
// restore the target prologue on a live host, so a design that depends on
// unhooking would be depending on something we have never observed working.
namespace preyvr::dll {

enum class CameraEditStatus : DWORD {
    unavailable = 0,  // signatures did not match, or MinHook refused
    ready = 1,        // hook installed, currently a pass-through
    armed = 2,        // an edit is being applied every frame
    failed = 3,
};

// Installs the hook if needed and arms a bounded yaw, in degrees. Passing 0
// disarms without removing the hook. Rejects anything outside the bound in
// preyvr::cameraedit.
DWORD SetCameraYawEdit(float degrees);

DWORD CameraEditStatusValue();

// Counts frames on which the edit was actually applied and the restore verified
// byte for byte. A frame that applied but failed to restore increments the
// failure counter instead, and disarms -- one unverified restore is enough to
// stop, because every later measurement in the session would be suspect.
unsigned long long CameraEditAppliedCount();
unsigned long long CameraEditRestoreFailureCount();

} // namespace preyvr::dll
