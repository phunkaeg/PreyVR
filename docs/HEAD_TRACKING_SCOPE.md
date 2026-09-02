# Head tracking — scope

**Status: scoped, not started. Written 2026-09-02.**

Stereo works and is tuned. The world does not respond to head movement, because
the pose we submit is the runtime's while the image still comes from Prey's own
camera. Closing that is the next piece of work, and it is a different seam from
everything done so far — not a tuning problem.

## What already exists

More than half of this is built, which is why the scope is smaller than it looks.

| piece | where | state |
| --- | --- | --- |
| `ReferenceFrame{worldPosition, yawRadians}` | `StereoCamera.h:107` | **yaw-only by construction** |
| `EyePoseInWorld(reference, openXrEyePose)` | `StereoCamera.cpp:137` | the composition function |
| `CyclopsPose(left, right)` | `StereoCamera.cpp` | one eye point for gameplay consumers |
| `MatrixFromPose` / `WriteMatrix` | `StereoCamera.cpp` | writing into a live `CCamera` |
| `RotationIsSafeToWrite` | `CameraEdit.cpp` | the orthonormality gate |
| camera write, once per frame | `CameraEditHook.cpp` | already authoritative over the matrix |
| **the tracked pose itself** | `XrSessionHost.cpp:580` | **already located, then discarded** |
| cross-thread handoff pattern | `CameraEditHook.cpp` | the eye handoff, verified 2026-09-02 |

`xrLocateViews` already runs every frame and its result is used only to fill
`projViews`. The pose we need is being fetched and thrown away.

That `ReferenceFrame` has **no pitch or roll field** is not an accident to be
corrected later. The playbook's rule is that the stored reference is yaw-only
while the live head pose stays full: strip roll from both and head tilt stops
working, strip it from neither and a crooked capture tilts the world forever
(`FAIL-CAM-019`). The struct makes the wrong version unrepresentable.

## The architecture question, and why it is already answered

The playbook names two options. **Follower** leaves the engine owning the camera
and nudges it toward the headset — low risk, but everything lags the head and you
fight the engine's smoothing forever. **Authoritative** means owning the final
view matrix, with the engine's camera becoming a downstream value.

**PreyVR is already authoritative.** The camera edit hook overwrites Prey's view
matrix every frame; it simply writes an eye offset today rather than a head pose.
So this is not a choice to make, it is a position already held — and the playbook
is blunt about what that costs: authoritative mode is *diagnostic until every
dependent path is proven* — culling, recenter, turn, picking, viewmodel.

The work is therefore mostly **proving the dependents**, not building a camera.

## Staging

Ordered so each stage is independently testable and the risky parts come after
the parts that establish confidence.

### M1 — orientation only

Head rotation moves the world. No positional tracking.

Deliberately first because it needs **no `unitsPerMetre`** (that constant scales
position, not rotation) and creates **no collision problem** (the head cannot
lean through a wall if it cannot translate). It is the cheapest change that is
unmistakably head tracking, and it exercises the whole pose path.

- pose handoff, render thread to game thread — the eye handoff inverted
- recenter: capture current yaw into `ReferenceFrame.yawRadians`
- compose reference yaw with live head orientation, write the matrix
- `ReferenceFrame.worldPosition` stays the game's own eye point, so walking,
  collision and scripted movement are untouched

**Exit:** looking around moves the world, the horizon stays level, and a recenter
taken with the headset tilted does not tilt the world.

### M2 — positional

Adds the translation component, and with it the two things M1 avoided.

- **`unitsPerMetre` must be measured, not assumed.** It is currently assumed to be
  1 and never verified. Rotation is unaffected by it; position is scaled by it
  exactly. Wrong here and the world shears or swims as the player moves — which
  is precisely why a static view could never reveal it.
- **The head layer of collision** — the playbook's "second layer people forget":
  native movement gives capsule collision for the body, and does nothing to stop
  the residual head volume leaning through a nearby wall.

**Exit:** leaning produces correct parallax, and leaning into geometry degrades
gracefully rather than showing the inside of a wall.

### M3 — composition with the game's own motion

Stick turning and recentering composing correctly, and yielding to Prey's
authored cameras — cutscenes, ladders, seats, conversations — which the playbook
calls out as its own section.

## Named risks

**Pose latency, and it is already wrong.** The playbook: *sample the pose for the
frame you are about to render, not the one you just finished.* Our `xrLocateViews`
runs inside `EndRendererScene`, which is **after** rasterisation. The pose then
crosses to the game thread and is used on the following frame. That is one to two
frames of staleness before display latency is even counted — 11–33 ms at 90 Hz.
Latency is the main driver of discomfort, so this must be **measured in M1**, not
assumed acceptable. If it is too high, the fix is to locate the pose on the game
thread just before the camera write rather than reusing the render thread's.

**Aim couples to the head.** The playbook is explicit that camera view is not aim
ownership. If Prey derives aim from the camera, looking around will swing the
weapon; if it derives aim from player state, the weapon will not point where the
player looks. **Which one Prey does is not yet known** and should be established
before M1 rather than discovered in a headset.

**The engine fights the camera.** Headbob, recoil, and landing shake all write the
camera. We write after `UpdateRenderingCamera`, so we win the frame — but effects
the engine applies *downstream* of that will still land on the head. Expect to
disable some.

**Culling.** We write the matrix before `UpdateFrustum`, so the engine should
derive its cull frustum from our camera. Should — this is worth confirming
directly rather than reasoning about, because the failure mode is geometry
popping at the edges, which is easy to misread as an LOD problem.

## Open questions to settle before writing code

1. ~~Does Prey's aim come from the camera or from player state?~~ **Answered
   2026-09-02: from the camera, and it changes the seam decision.** See below.
2. Is `unitsPerMetre` 1? Not needed for M1, blocking for M2.
3. What is the actual end-to-end pose latency? Measurable in M1.
4. Which camera effects does Prey apply after `UpdateRenderingCamera`?

## What this does not include

Motion controllers, locomotion, and HUD reprojection. `MotionController.cpp`
already takes a `ReferenceFrame`, so it is waiting on M1 rather than on new
design, but it is separate work.


---

## The aim question, answered — and it moves the seam

`ArkPlayer::UpdateCachedReticleViewPosAndDir` (R-011) rebuilds the cached aim ray
by unprojecting through `ISystem::GetViewCamera()` — the **global** view camera,
not ArkPlayer's own — and its only caller is `CArkUIHUD::OnPreRender`, which runs
during render preparation.

**Our camera edit writes that exact camera.** It puts the eye camera into
`CSystem::m_ViewCamera`, calls the whole of `CSystem::Render`, and restores
afterwards, so an eye-specific camera is live for the entire render.

H-008 predicted this in advance and named the mitigation: inject at
`CRenderView::SetCamera` (R-030) instead, which is downstream of
`GetViewCamera()` and leaves R-011 untouched. R-049 makes that structural rather
than a matter of ordering — `CRenderView::SetCamera` copies the camera **by
value** into `CRenderView::m_camera` at `+0x11A0`, so a camera written there
provably cannot alias `CSystem::m_ViewCamera`.

### Why this matters more for head tracking than for stereo

Under stereo alone the contamination is one IPD — 64 mm of lateral jitter on the
aim ray. Measurable, probably not very visible.

**Head tracking makes it a different quantity.** Once the camera carries head
orientation, a ray unprojected from it swings through the player's entire look
range. Every consumer of that ray — weapon firing (R-014), target selection
(R-022), wrench hits (R-020) — would follow the head, which is precisely the
coupling the playbook warns about under "camera view is not aim ownership", and
precisely what this project's detached-aim lane (H-004) exists to avoid.

So the seam decision is no longer a stereo detail. **M1 should move the camera
write to `CRenderView::SetCamera` before adding head orientation**, or head
tracking will be built on top of a known aim defect.

### What is not yet known

Whether the contamination is actually occurring. `CArkUIHUD::OnPreRender` is
vtable-dispatched with no static callers, so whether it lands inside the window is
a live-timing question, not a structural one — H-008 said the same. The aim-ray
probe turns it into a number and is the next test.


---

## Aim-ray contamination: measured 2026-09-03, NOT occurring

H-008's feared failure does not happen on this path.

| run | samples | max origin gap | max angle gap |
| --- | --- | --- | --- |
| control — still, no stereo | 1454 | **0 um** | 0 |
| test — still, stereo armed | 1511 | **0 um** | 0 |
| ~~positive control — looking around~~ **VOID** | 1486 | 480 um | 216 mdeg |

The camera edit was demonstrably live during the test: 1464 applications, eyes
alternating, zero restore failures. Contamination would have shown as roughly one
IPD, **64000 um**.

**CORRECTION 2026-09-03 — the third run was not a positive control.** The player
did not see the request in time and did not look around, so that run was the
*same condition as the test*: still, stereo armed. It is void as a control, and
the claim built on it — "the instrument resolves real motion" — was not
established by it.

Two things follow. The 480 um has an **unknown cause**, most likely an idle
animation moving the eye point, and it is not evidence of probe sensitivity. And
the same condition produced 0 um in one window and 480 um in another, so the
test's 0 is not a stable property of the condition.

What does survive: the probe is **not** hard-stuck at zero, since it reported a
non-zero value. And across roughly 22 seconds of stereo-armed sampling the
largest gap seen was 480 um. Real contamination would put ~64000 um on
essentially **every** frame pair, not as a rare maximum, so its absence over 2965
samples is still strong. The conclusion is probably right; the evidence
originally cited for it was not.

480 um and 216 millidegrees are the right magnitudes for a *per-frame* gap: at 90
fps even a brisk turn moves the ray a fraction of a degree between consecutive
frames.

**So `CArkUIHUD::OnPreRender` does not run inside the window where our eye camera
is live.** H-008 could not settle this statically because `OnPreRender` is
vtable-dispatched; it is now settled empirically, in the direction that costs us
nothing.

### This does not make the current seam safe for head tracking

The measurement covers the aim ray only. H-008's other finding stands untouched:
**84+ sites read the global view camera per frame**, spanning rendering,
gameplay, UI and 3D-engine code. The aim ray was the one consumer we could name
and instrument; the rest were never enumerated.

Under stereo the exposure is one IPD. Under head tracking the eye camera carries
the player's whole look rotation, so anything reading it mid-render sees a
different world orientation. **The seam should still move**, on the strength of
the unenumerated readers rather than on the aim ray.

## R-030 observed executing, 2026-09-03 — the seam is unblocked

The registry recorded `CRenderView::SetCamera` as *never observed executing*,
with an explicit instruction that it must not be hooked until a live entry was
captured. Captured: **230 entries in 5 seconds (46/sec) on thread 21092**, with
the 16-byte prologue verified against the registry signature before attaching.

The observation used a counting-only Frida `Interceptor` attached and detached
within one script, so nothing in the shipped DLL was modified to obtain it.

**M1 can now be built on R-030.** It is the better seam for a reason independent
of the aim result: R-049 shows it copies the camera by value into
`CRenderView::m_camera` at `+0x11A0`, so a camera written there provably cannot
alias `CSystem::m_ViewCamera` — structurally immune rather than correctly
ordered, and it never touches the global those 84+ sites read.


---

## M1 first run, 2026-09-03: the pose path works, the seam is wrong

**What worked.** Every mechanism built for M1 did its job. Recenter succeeded, so
the whole pose path is live: `xrLocateViews` to `CyclopsPose` to the seqlock to
yaw extraction. The hook then ran **9521 times with zero refusals** — never a
missing pose, never a failed orthonormality gate — and **the view followed the
headset**.

**Pose latency, measured.** Typical **~4 ms**, max 4.7 ms in steady state.

That corrects a concern raised in this document. Publishing after rasterisation
was flagged as costing one to two frames before display latency was counted; it
costs about **4 ms**, comfortably inside a single 11 ms frame, because
`ServiceXrFrame` and the next frame's `SetCamera` land close together. The
outlier of 104907 us was a hitch during teardown, not steady-state. **This is no
longer a reason to move the sampling point.**

**What broke: massive level culling.** Geometry missing and popping across the
view.

### The seam is wrong for rotation, and the playbook says so directly

`09`'s cull-camera section is unambiguous:

> the visibility pass runs **before** your render-view rewrite, from the engine's
> camera, not yours. Widening your render frustum or moving your projection
> changes *how* submitted geometry is drawn — it cannot make the engine submit
> geometry it already culled.

And SS2VR's rule from the same section: *keep the RenderView override for
stereo/projection, **never for CPU culling**.*

`CRenderView::SetCamera` **is** the RenderView override. It sits downstream of
visibility — which is exactly why it is contamination-safe, and exactly why it
cannot drive head rotation. The engine culled against its own unrotated camera
and then rendered through our rotated one, so everything outside the original
frustum was already gone.

**This was in the scope's own risk list and I checked the wrong half of it.** The
entry read "we write the matrix before `UpdateFrustum`, so the engine should
derive its cull frustum from our camera" — true of the *old* seam, and carried
over unexamined when the seam moved. Moving to R-030 moved us downstream of the
cull, which is the thing that makes it safe and the thing that breaks it.

### The corrected architecture: two seams, split by what each is for

- **Head rotation goes upstream, on the global view camera**, because that is
  where culling reads from.
- **Per-eye offset and projection stay on R-030**, downstream and
  contamination-free.

**And the contamination worry does not transfer to rotation.** The concern about
84+ readers of the global camera was about a *per-eye offset* — a transient,
alternating camera no engine expects. A **rotated** view camera is what the
engine sees every time the player turns their head with a mouse. Those readers
are built for it. Rotation on the global camera is an ordinary operation;
per-eye offset is not.

That distinction was not made when the seam was chosen, and making it is what
resolves the apparent conflict between "move off the global camera" and "culling
needs the global camera".

### Also

`r_MotionBlur 0` was not set for this run — an oversight, since head rotation
moves the camera between frames just as alternate-eye does. `e_CameraFrustumSize`
was probed as a possible cull-widening lever and **refused by the console
allowlist**, which is the fail-closed design working; it is also the wrong fix,
per the quote above.


## Upstream rotation, 2026-09-03: the view tracks, the cull frustum does not

Rotation moved to the upstream camera edit, on `CSystem::m_ViewCamera`. Applied
8500 times at 90/sec, zero refusals, zero restore failures, pose age ~4.2 ms.

**The view tracks the headset. The cull frustum follows the mouse.** Reported
directly: geometry appears and disappears according to where the *player* is
aiming, not where the headset is looking.

So there is a **third camera**. Our edit moves the camera the image is rendered
through, and something else decides what is submitted in the first place.

### Why this is surprising, and what it rules out

CryEngine's own source makes the pass camera the camera it is handed:

```cpp
const CCamera& rCameraToSet = (pCameraFreeze && pCameraFreeze->GetIVal() != 0)
    ? gEnv->p3DEngine->GetRenderingCamera() : rCamera;
passInfo.SetCamera(rCameraToSet);
```

`e_CameraFreeze` is off, so the pass camera should be `rCamera` -- and R-061
records that `CSystem::Render` hands `m_ViewCamera` to
`CreateGeneralPassRenderingInfo`. On that reading our edit should reach culling.

It does not, so one of these is true and they are distinguishable by experiment:

1. `CreateGeneralPassRenderingInfo` is called with a camera that is **not**
   `m_ViewCamera`.
2. Something rewrites `m_ViewCamera` between our write and that call -- our hook
   wraps `CSystem::Render`, so anything inside it that calls `SetViewCamera` wins.
3. Culling does not use the pass camera at all, and Prey's visibility runs from a
   separate camera the way SS2VR's did.

**This is the playbook's pattern, not a Prey oddity.** SS2VR: *cell/portal
visibility computed from the body-anchored cull camera*, with a single dedicated
writer. SOMAVR hooks frustum ownership so every non-player camera returns its
native frustum. The general rule from the same section is that the visibility pass
runs from the engine's camera, not yours, and cannot be fixed downstream.

### The next test distinguishes all three, and needs no headset

Hook `CreateGeneralPassRenderingInfo` (R-071, landmark `pass.create_general`) and
read the forward vector of the camera it is actually handed, alongside the forward
vector we wrote. Same, and the problem is downstream of the pass camera. Different,
and our write is being lost or bypassed -- and the value tells us which.

That is a read-only observation of an existing landmark, so it costs one launch
and no arming.

### Also fixed

Head rotation was not an arming condition in the camera edit's guard, so the first
upstream run armed successfully and never executed -- applied and refused both
zero. It cost a test cycle to spot, because "armed" and "applied" were only
distinguishable by reading a counter. The guard now includes it.
