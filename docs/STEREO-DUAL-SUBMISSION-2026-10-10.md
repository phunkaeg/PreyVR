# Rung 1b: both eyes in one frame by submitting the render view twice

Branch `stereoscopy`, 2026-10-10. Code: `src/dll/DualSceneStereo.{h,cpp}`, commands
`dual.observe`, `dual.mode`, `dual.ipd`, `dual.vr`, `dual.capture`, `dual.report`.
Measured in the real game on this machine (Steam `PreyDll.dll` `7D6E322F...`).
2026-10-11 (branch `stereo-both-eyes`): `src/dll/DualOverlay.{h,cpp}`, the text panel
and VR reticle in `HudLayer`/`DebugOverlay`, commands `dual.overlay`, `aim.vrreticle`.

**Status: in the headset path and confirmed by a wearer.** VR mode submits both
eyes from one frame by default (mode 3, below). Steam Frame session on 2026-10-10:
"no noticeable doubling". Since 2026-10-11 everything Prey draws after the world --
HUD, reticle, text popups, objective markers, fades -- reaches both eyes
([below](#after-world-overlays-in-both-eyes-2026-10-11)). Open issues are listed at
the end.

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
contract without its second submission. The player launcher sets the variable
from `DualStereo` in `PreyVR.json` (or `-DualStereo 0|1`) and prints the mode
VrMode logged once VR is active; `Invoke-PreyVRLaunch.ps1 -DualStereo 0|1` sets
it for developer runs (omitted, the shell's value is inherited).
`tests/LauncherStereo.Tests.ps1` covers the setting.

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

## After-world overlays in both eyes (2026-10-11)

The 2026-10-10 wearer reported scene-transition fades in the right eye only; a
re-test found the reticle, objective markers and book/note popups the same. One
cause: the first eye is held right after its `RT_RenderScene`, so anything drawn
after the second submission reached the second (right) eye only.

### What draws after the world (static, Steam build)

- **Screen fades are 2D images.** The flow-graph fader (`Image:ScreenFader` ->
  `CMasterFader`, singleton `0x2C216B0`, a framework listener named
  `HUD_Master_Fader` at priority 2; `CHUDFader::Draw` `0x17AC1D0`) and the TrackView
  `ScreenFader` node (draw loop `0xA45540`) both call `SetState(0x265)` then
  `Draw2dImage(0, 0, 800, 600, ...)` (renderer vtable `+0x1D0`, `0xF78A00`). That
  queues render command `0x3B`, run as `RT_Draw2dImage` (`+0x990`, `0xF34B80`); it
  and the batched `RT_Draw2dImageList` (`+0x9A8`, `0xF353A0`) both draw through
  `RT_Draw2dImageInternal` `0xF34C70` (R-135). The load fade seen in the headset is
  an untextured, opaque black 800x600 image on every frame of the fade.
- **Flash movies other than the gameplay HUD:** `DanielleMarkers`,
  `DanielleTranscribeNote` (books and notes), `DanielleSubtitles`,
  `DanielleTutorialCard`, `DanielleDeadScreen` and others. `DanielleHUD.HUDFadeOn`
  (called by `ArkLevelTransitionDoor`) is not a screen fade: in `code.HUD.Visibility`
  it tweens the `safe` container's `autoAlpha`.
- **The gameplay HUD itself.** The floating HUD layer had never come up on SteamVR
  (F-017), so the HUD -- reticle and status popups included -- stayed in the scene.

### What was changed

- **HUD panel on SteamVR (F-017).** The HUD quad's swapchain is created by
  `InventorySwapchain` in the main swapchain's format, like every other layer, with
  a bounded image wait. Log: `hud_swapchain format=29 captureFormat=28`.
- **Text panel.** `DanielleTranscribeNote`, `DanielleSubtitles` and
  `DanielleTutorialCard` are captured (with the HUD layer on and gameplay input
  allowed) into one shared texture -- the first movie of a frame clears it -- and
  shown as a quad on the HUD panel's fit, above the HUD. Log: `text_swapchain`.
- **Per-eye overlay redraw (`DualOverlay`).** DualSceneStereo opens a window after
  the second submission of a frame whose first eye was held; the top of
  `ServiceXrFrame` closes it. While it is open, a draw into the frame's backbuffer
  (identity checked against the held backbuffer) is repeated into the held first eye
  through a view with the game's own format (`HeldFirstEyeTarget`):
  - Flash: inside the movie's one native callback, the root's Display (`0x18AB710`)
    runs twice -- natively, then into the held eye -- the second starting from a
    copy of the native depth-stencil, which Flash clips with. The stereo
    inventory's pattern: no extra outer callback, no extra Advance.
  - 2D images: `RT_Draw2dImageInternal` (landmark `renderer.draw2d_images`, 40 in
    all) is called a second time with the backbuffer redirected to the held eye
    (HudLayer's `OMSetRenderTargets` substitution).
  - `dual.overlay 0|1` switches it live; `PREYVR_DUAL_OVERLAY=0` at launch. The log
    names the first redraw or refusal per movie and the first 12 2D images.
- **VR reticle.** `_root.safe.reticle_mc` leaves the HUD panel through the wrist's
  sprite filter, and a ring-and-dot reticle (1.1 deg across at any distance) is
  drawn in each eye, with that eye's submitted pose, where the right hand's aim ray
  ends: the aim's own scene hit (`aim.scene`), else a scene query of the same ray,
  else the 10 m convergence (never nearer than 0.25 m). It is drawn only on frames
  whose panel left the native crosshair out, and while `reticle_mc._visible` and
  `safe._visible` are true. `aim.vrreticle 0|1`; `PREYVR_VR_RETICLE=0` at launch.

### Headset results, 2026-10-11 (Steam Frame, SteamVR, player packages at 2688x2880)

Wearer: the reticle sits on the aimed surface in both eyes; the book popup is on
its panel in both eyes; objective markers and the load fade are in both eyes. From
the once-a-minute status lines of the ~10-minute reticle run: 41,855 doubled frames
and 41,404 pairs, 0 refused; 3,635 2D images and 41,605 `DanielleMarkers` draws
redrawn, 0 refused; the panel left the crosshair out on 37,643 frames, 0 refused;
41,614 reticle samples, 35,045 (84%) with a scene hit, drawn into 74,894 eye images;
the native crosshair never read as hidden. No movie was refused in any run.

### Limits and what is not established

- **Markers have no depth.** Both eyes get them at the same pixels, so they read as
  infinitely far: close to right for distant objectives (about 0.2 deg per eye at
  10 m), wrong for nearby points of interest. Per-marker depth needs the objective
  positions, which nothing reads yet.
- **The reticle is generic.** Prey's weapon-specific crosshairs, spread and hit
  feedback are not shown. Keeping the native art at depth (a crosshair-only replay
  into each eye with a per-eye offset) is possible and not attempted.
- **Reticle visibility mirrors `_visible` only.** A crosshair Prey hides some other
  way (alpha, an empty frame) would not hide ours; none was seen.
- **Cost is unmeasured on the GPU.** Each redrawn Flash movie also copies the full
  depth-stencil once per frame, and the subtitles movie renders every gameplay frame,
  so the text panel uploads every frame even with no subtitle on screen.
- **Menus waste work:** the second submission and the overlay redraws still run
  while the menu panel shows only the backbuffer.

## Open issues

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

- Frame rate and GPU time at 2688x2880 with both eyes per frame. Rough only: the
  status lines of one 2026-10-11 run average 3.0 ms and 9.1 ms of render-thread time
  in the two submissions, and frames counted against wall time suggest about
  50 fps rather than 90 (menus and loading included). Not a GPU measurement.
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
- **Counters without the tuner (2026-10-11).** While VR is active, VR mode logs
  `preyvr_vr status dual={ ... } hud={ ... }` once a minute: the text of
  `dual.report` and `hud.layer` plus the reticle counters. Running totals, so the
  last line of a session is the one to read.
- **Recenter refusals are rate-limited (2026-10-11).** After a tracking-space change
  the session host retries the recenter every frame until the head is tracked; with
  the headset off that logged one line per frame (998 in 86 s). Now one line per
  reason per 5 s, with `heldBack=N` on the next line written.
- **A level-load crash (2026-10-11)**, loading a save after quitting to the main
  menu: an access violation in a job worker inside PreyDll's `OctreeNodeRender` job
  (`PreyDll+0x278EAE`, a per-object record whose owner pointer was garbage). The
  whole faulting stack is PreyDll, the C runtime and Windows -- no PreyVR frame --
  and nothing changed that day touches scene objects. Not attributed to the mod; if
  it recurs on the same path, retry with `dual.vr 0`.

## Next

1. Measure frame time and GPU time at headset resolution with both eyes; skip the
   second submission and the overlay redraws while the menu panel is up; decide
   whether shadow maps and other per-frame work can be shared between submissions.
2. Body yaw following the headset, or hiding the body.
3. Marker depth, if markers look wrong in play (see the limits above).
4. Prey's own crosshair art at the aim's depth, if the generic reticle is missed.
5. Per-eye history for SSR/SSDO, then re-enable TAA per eye.
