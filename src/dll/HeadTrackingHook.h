#pragma once

#include "preyvr/VrMath.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

// M1 head tracking: the world responds to head rotation.
//
// **On a different seam from the stereo camera edit, deliberately.** The stereo
// path writes `CSystem::m_ViewCamera`, the global view camera. H-008 found 84+
// sites that read that global per frame, spanning rendering, gameplay, UI and
// 3D-engine code, and only one of them -- the cached aim ray -- was ever named
// and instrumented. Under stereo the exposure is one IPD, which measured as
// harmless on 2026-09-03. Under head tracking the camera carries the player's
// entire look rotation, so any of those unenumerated readers would see a
// different world orientation mid-render.
//
// This hooks `CRenderView::SetCamera` (R-030) instead, which is **structurally**
// immune rather than correctly ordered: R-049 shows it copies the camera by
// value into `CRenderView::m_camera` at `+0x11A0`, so a camera passed here
// provably cannot alias the global, and nothing that reads the global can see
// it. There is also no restore to perform, because nothing global was written --
// the detour edits a copy of its own argument and hands that to the original.
//
// R-030 was blocked from hooking until 2026-09-03 on the grounds that it had
// never been observed executing. It has now: 230 entries in 5 seconds on a
// single thread, prologue verified immediately before attaching.
//
// **Orientation only.** No positional tracking, which is what keeps M1 free of
// both `unitsPerMetre` -- that constant scales position, not rotation, and is
// still unmeasured -- and of the head-versus-world collision problem, since a
// head that cannot translate cannot lean through a wall.
namespace preyvr::dll {

// Publishes the head pose for the camera hook to consume. Called from wherever
// the runtime pose is located; safe from any thread.
//
// **Latest-wins rather than queued**, unlike the eye handoff. Eye identity had to
// be exact, so it needed a queue that preserved order. A pose does not: an older
// pose is simply a worse answer to the same question, so the newest available is
// always the right one to use and a backlog would be pure latency.
void PublishHeadPose(const Pose& openXrHeadPose);

// Reads the latest published head pose, and how old it is in microseconds.
//
// Exposed because **rotation belongs upstream**, on the camera the engine culls
// from, while this file owns the pose slot. Measured 2026-09-03: driving rotation
// from CRenderView::SetCamera renders through a rotated camera that the engine
// already culled against an unrotated one, so geometry outside the original
// frustum is gone before the rewrite happens. The playbook's rule is to keep the
// RenderView override for stereo and projection and never for CPU culling.
bool TryReadHeadPose(Pose& out, unsigned long long& ageMicroseconds);

// Composes the recentered reference with a live head pose, orientation only,
// keeping whatever position the camera already has.
//
// Shared rather than duplicated, so the upstream rotation and anything later
// cannot drift apart in how they build the same camera.
bool ApplyHeadRotation(std::uint8_t* camera, std::size_t size);

// ---------------------------------------------------------------------------
// The view seam: ArkPlayerCamera::UpdateView (R-009)
// ---------------------------------------------------------------------------
//
// **The camera is set here, at the source, so everything downstream inherits it.**
// Writing further down the frame reaches some readers and not others: an edit at
// CRenderView::SetCamera missed culling entirely, and an edit at CSystem::Render
// reached the pass camera exactly -- 903 samples, zero disagreements -- while
// culling still followed the aim, because the occlusion job is spawned earlier
// from GetViewCamera(). Setting the view where the game computes it puts us
// upstream of every one of those reads at once.
//
// **Orientation is yaw plus head, not the game's rotation plus a head delta.**
// The target architecture has no mouse pitch at all: the headset owns the whole
// view in 6DoF, a stick moves the player capsule, and a motion controller owns
// aim. So the only thing the view needs from outside the headset is a *yaw* for
// the play space -- taken from the game's camera today, so mouse turning still
// works while building, and replaced by an owned turn value later.
//
// Off by default, and observe-first: the SViewParams layout below is from the
// CryGame CE3 tree and Prey is an Arkane fork, so it is checked against a value
// measured independently -- the live camera's 1.5447 rad FOV -- before anything
// is written.

// Frames the view hook saw, and frames it actually rewrote.
unsigned long long ViewHookObservedCount();
unsigned long long ViewHookAppliedCount();

// The last SViewParams the hook saw, for confirming the struct layout before
// trusting it. FOV in radians: a reading near 1.5447 confirms the offsets, since
// that value was measured from the live camera by a different route.
float ViewHookLastFov();
float ViewHookLastNearPlane();

// Installs the hook in observe-only mode. Nothing is written; the counters and
// the readings above start moving.
DWORD SetViewHookObserving(unsigned int enabled);

// Starts rewriting the view orientation. Requires observe mode to have confirmed
// the layout, and a recenter reference.
DWORD SetViewHookApplying(unsigned int enabled);

// Arms the camera edit. Off by default.
//
// Refused unless a recenter reference has been captured, because without one the
// composition has no idea which way the play space faces and would snap the
// world to engine north on the first frame.
DWORD SetHeadTrackingEnabled(unsigned int enabled);

// Captures the current head pose as the yaw-only reference.
//
// Refused when the head is near-vertical, per RecenterYawFromHeadPose: looking
// straight up or down carries no usable yaw, and inventing one would face the
// player somewhere arbitrary. A refusal leaves the previous reference in place.
DWORD RecenterHeadTracking();

// Frames the detour ran with the edit armed and applied.
unsigned long long HeadTrackingAppliedCount();

// Frames the detour refused to edit -- no pose yet, an unusable pose, or a
// matrix that failed the orthonormality gate. A high count against a high
// applied count means something is intermittently wrong.
unsigned long long HeadTrackingRefusedCount();

// Microseconds between a pose being published and the camera edit that consumed
// it. **This is the latency number the scope calls for measuring in M1.** It is
// only the handoff segment -- it excludes the runtime's own prediction and the
// display pipeline -- so it is a floor on end-to-end latency, not the whole of it.
unsigned long long HeadTrackingLastPoseAgeMicroseconds();

unsigned long long HeadTrackingMaxPoseAgeMicroseconds();

// 1 once a recenter reference exists.
DWORD HeadTrackingHasReference();

} // namespace preyvr::dll
