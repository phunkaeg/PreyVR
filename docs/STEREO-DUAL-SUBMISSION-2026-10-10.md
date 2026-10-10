# Rung 1b: both eyes in one frame by submitting the render view twice

Branch `stereoscopy`, 2026-10-10. Code: `src/dll/DualSceneStereo.{h,cpp}`, commands
`dual.observe`, `dual.mode`, `dual.ipd`, `dual.vr`, `dual.capture`, `dual.report`.
Measured in the real game on this machine (Steam `PreyDll.dll` `7D6E322F...`).

**Status: in the headset path and confirmed by a wearer.** VR mode submits both
eyes from one frame by default (mode 3, below). Steam Frame session on 2026-10-10:
"no noticeable doubling". Two open issues are listed at the end.

## Why this seam

Every rung-1 attempt (A3-A7, F-013..F-016) doubled the *traversal*: the prepare
step allocates once per traversal and is released once per frame, so a second
traversal leaks or wedges. CryEngine's own stereo renderer traverses once and
calls `RT_RenderScene` once per eye (CRYENGINE_SOURCE_FINDINGS.md). That function
only consumes the view, so doubling it cannot repeat the prepare.

## What the binary says (Ghidra 12.1.4, fresh analysis of the Steam build)

**`RT_RenderScene` is `0xF41CA0`** (R-128 promoted from external lead). Renderer
vtable `+0x928`, next to `RT_SetCameraInfo` `0xF42C00` at `+0x930`. Its only two
callers are `RC_RenderScene` `0xFEE280` (direct when already on the render thread,
passing `m_TI[slot]` from `renderer + 0x49A0 + slot*0x328`) and the render-command
processor `0xFE9A80` (passing its own copy of the queued `SThreadInfo`). Signature
`(CRenderView*, int nFlags, SThreadInfo&, void(*)())`; the function prints
`" +++ End HDR scene +++ "`.

**It takes its camera from the `SThreadInfo`, not the view.** On entry it saves
`m_TI[slot]` into `m_OldTI`, installs the passed `SThreadInfo`, renders, and on exit
restores the saved one. `CD3D9Renderer::SetCamera` `0xF7FE70` picks the slot by
thread (render thread: `renderThread(+0xD90)+0x48`) and writes, inside that slot:
the zero-translation camera matrix `+0x28`, `m_cam` `+0x68` (CCamera, 0x240), and
`m_rcam` `+0x2A8` (0x48); it loads the view and projection **matrix-stack tops**
the slot points at (`+0x18`, `+0x20`), then on the render thread calls
`RT_SetCameraInfo` directly (`0xFEE510`). The stacks are shared with every copy of
the `SThreadInfo`, because the copy carries the pointers.

**It tears the view down when it finishes.** Its tail switches the view
(`0xF42A46`) and each shadow-frustum view (`0xF42A8C`) to ReadingDone and calls
`CRenderView::Clear` `0xEE4030` (`0xF42A9B`), which empties every item list,
releases per-object resources and recursively clears the shadow views. A naive
second call therefore draws an empty view. `CRenderView::SwitchUsageMode`
`0xEE82F0` returns at once when asked for the current mode; switching to Reading
(1) otherwise waits on the view's jobs and finalises its lists.

**So:** the first submission's three teardown calls are skipped, the view stays in
Reading, the second submission's entry switch is a no-op and its exit does the one
real teardown. The skip is gated by a thread-local flag set only around the first
submission, the requested mode, and the exact return address of each call. If the
first teardown did not pass the gates exactly (one main switch, one Clear), the
held-back calls are replayed, the second submission is refused and the mode
disarms.

Five landmarks added (39 total, all matching the installed DLL): `renderer.render_scene`,
`view.switch_usage_mode`, `view.clear`, `renderer.set_camera`, `camera.update_frustum`.

## Live results

**Observe.** Exactly one top-level submission per frame (1.002 over 497 frames;
1.00002 over 40,000), always the render thread, never nested, never the recursive
view, flags `0xF`. Two views alternate (the `[2]` pool). The view's camera and the
`SThreadInfo` camera agree. `RT_RenderScene`'s slot (`+0x499C`) equals the render
thread's SetCamera slot on every frame, and the passed `SThreadInfo` shares that
slot's matrix stacks (`stacksShared=1`). The backbuffer right after
`RT_RenderScene` is the finished, post-processed scene **without the HUD**.

**Mode 1, the control (same camera twice).**

| | result |
| --- | --- |
| doubled frames | 2,000 + 16,019, **0 refused**, budget self-disarm clean |
| teardown deferred per frame | 1 main switch, 11 shadow switches, 1 Clear, every frame |
| memory | flat, ~1.89 GB, over 60 s / 16,000 doubled frames |
| frame rate | 152 fps single and doubled (headroom at 1440x900) |
| render-thread cost | single 3.1 ms; doubled 2.8 + 1.9 ms |
| eye0 vs eye1, `r_AntialiasingMode 3` | mean 0.40, 0.28% pixels: edges only (TAA history = previous submission) |
| eye0 vs eye1, `r_AntialiasingMode 1` | mean 0.13: **zero difference** at 16x gain except animated monitor shaders, which vary per draw |

**Mode 2, per-eye cameras (64 mm).** 600 frames, 0 refused. Each eye differs from
the centre camera by the same amount (mean 2.83 / 2.85). Left vs right: mean 4.11,
13% of pixels, best single shift 7 px explains only part of it (2.4x); the
difference and anaglyph views show wide disparity on near walls, floor strips and
pillars and thin disparity on the far wall and sign. Expected disparity at 120 deg
HFOV, 1440 px, 64 mm is about 27/Z px, i.e. ~7 px at 4 m.


## The headset path (mode 3)

VR mode arms it after stereo is up (`VrMode.cpp`), unless `PREYVR_DUAL_STEREO=0`;
`dual.vr 0|1` switches live, in the order that never publishes a both-eyes
contract without its second submission.

- **Game thread** (`CameraEditHook.cpp`, `SetDualStereoContract`): the stereo
  branch stops alternating. The frame keeps the head camera, so the engine culls
  and prepares it once from the head. The render contract is a both-eyes record
  (`kRenderContractBothEyes`): head pose plus both eye poses
  (`Compose(head, +-IPD/2)`), one display time, shared tangents.
- **Render thread** (`DualSceneStereo` mode 3): doubles the submission only while
  that contract is armed. Each eye is built by `BuildDualEyeCamera` -- the
  alternate-eye path's own construction at its own (runtime) IPD -- which also
  publishes the eye record, so the near pass's position lookup finds the exact bits
  the render view carries. Between the submissions `HoldDualFirstEye` copies the
  backbuffer (first eye, post-processed, pre-HUD) into its held image.
- **Submission** (`XrSessionHost.cpp`): at RT_EndFrame a both-eyes contract takes
  the held first eye and copies the backbuffer into the second, publishing both
  images and poses as one unit. The hold flag is taken at the top of the next
  `ServiceXrFrame`, so a hold can only pair with its own frame; a both-eyes frame
  without one is refused rather than paired with an older eye.
- **Fallback:** any mode-3 refusal returns the camera hook to alternate-eye
  contracts instead of leaving the headset dark.

## Headset results, 2026-10-10 (SteamVR, Steam Frame)

A developer-launcher session (1440x900, wrong aspect for the Frame) reached the
Frame because the runtime in Prey's process was SteamVR (see dev notes below):
5,192 doubled frames, 0 refused; 5,192 first-eye holds, 0 refused; 4,399 pairs
submitted, 0 refused (the rest were menu-panel frames); IPD 69.16 mm from the
runtime. xr-tape over 15,997 frames: `eye_order`, `ipd_plausible` (69.0-69.3 mm),
`no_vertical_disparity`, `eyes_parallel`, `eye_subimages_distinct`,
`no_submit_with_invalid_pose` pass. `submitted_fov_matches_located` and
`never_submits_zero_layers` fail as in upstream's own runs (the declared frustum is
Prey's by design, SUBMISSION_CONTRACT.md). `submitted_pose_matches_located` fails
only because the submitted pose is the render-time pose: p50 1.3 mm / 0.44 deg,
p99 11.6 mm / 5.0 deg, plus one 2-frame jump of 3.2 m / 135 deg (frames 8921-8922)
with no mod event near it -- the signature of the runtime's tracking space moving.
`eye_pair_shares_display_time` passes but also passes for alternate eyes; it does
not measure render-time skew.

Then a wearer session from a player package at 2688x2880: **no noticeable
doubling**, the defect this route exists to remove.

## Open issues

### Scene-transition fades are drawn in the right eye only

Reported by the wearer: during scene transitions the screen fades to black in the
right eye only. **Almost certainly specific to mode 3.** The first eye is copied
from the backbuffer right after its `RT_RenderScene`; the fade is drawn on top of
the finished frame later in the frame (after the scene, like the HUD), so only the
second eye -- the final backbuffer -- receives it. The alternate-eye path never
showed this because every eye image was a whole frame's backbuffer. The same
mechanism keeps the native HUD out of the first eye (HudCapturePair records it).
To find: which pass draws the fade (a 2D/Flash or post-effect overlay after the
scene) and its fade amount. Fix candidates: apply the fade to both eyes in the
compositor (a black quad or the HUD layer with the fade's alpha), redirect that
draw into each eye, or move the first-eye copy to after the overlay with the
second eye's overlay removed.

### The player body does not turn with the headset

Reported by the wearer: looking down shows the back of the player character,
because the body faces the stick/snap-turn yaw while the camera follows the head.
**Not caused by this branch:** body yaw is not rewritten by design
(COMFORT-CONTROLS-2026-09-19.md, "body yaw is not directly rewritten"), and
room-scale body following was declared out of scope (ARM-POSE-2026-10-06.md).
`armpose::TorsoYaw` already estimates a yaw-only torso from the headset with a
dead zone and relaxation, but only for the arm solver, never applied to the body.
Fix candidates: drive the player entity's yaw toward that torso yaw and subtract
the same rotation from the head-tracking reference so the view does not jump (aim,
head-relative movement and IK all read the body frame, so check each), or hide
the body mesh in VR.

### Smaller items

- One 2-frame pose jump at a runtime tracking-space change (above). The mod could
  handle `XrEventDataReferenceSpaceChangePending` explicitly.
- The near particles (`NearParticleStereo`) look the eye up from the game-thread
  camera, which is now the head, so draw-near sprites fall back to native parallax.
  Not reported by the wearer; not checked.

## Not established

- Frame rate and GPU time at 2688x2880 with both eyes per frame.
- The weapon and hands per eye (`NearViewStereo` identifies the eye from the
  render view's camera, which mode 3 sets per submission). The wearer reported no
  problem but they were not checked specifically.
- **Culling** is the head camera's; geometry at each eye's outer frustum edge may
  be culled.
- **Temporal effects.** TAA would blend the other eye (it stays off in VR). SSR and
  SSDO history and exposure adaptation now advance twice per frame; not measured.

## Developer notes from this session

- **Under the developer launcher the session went to SteamVR, not xr-sim.** The
  launcher started Prey with `XR_RUNTIME_JSON` pointing at xr-sim and the xr-tape
  layer loaded, but by the time the mod was injected the variable in Prey's
  process named SteamVR's manifest (`runtimeSource=environment`). Probably Steam's
  in-game overlay; not proven. Untested workaround: send `xr.runtime <xr-sim
  manifest>` after injection and before `vr.enable`.

## Next

1. The right-eye-only fade (above).
2. Measure frame time at headset resolution with both eyes; decide whether shadow
   maps and other per-frame work can be shared between the two submissions.
3. Body yaw following the headset, or hiding the body.
4. Per-eye history for SSR/SSDO, then re-enable TAA per eye.
