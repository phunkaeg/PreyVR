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
