# Rung 1b: both eyes in one frame by submitting the render view twice

Branch `stereoscopy`, 2026-10-10. Code: `src/dll/DualSceneStereo.{h,cpp}`, commands
`dual.observe`, `dual.mode`, `dual.ipd`, `dual.capture`, `dual.report`. Measured in
the real game on this machine (Steam `PreyDll.dll` `7D6E322F...`, flat window
1440x900, xr-sim selected but no XR session). **Not yet wired into OpenXR
submission and not yet seen in a headset.**

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

## Not established

- Anything in a headset; eye order through OpenXR; comfort.
- **The near (weapon/hands) pass.** It is camera-relative by construction; whether
  `NearViewStereo` picks the right eye when both are drawn in one frame is untested.
  Near particles (`NearParticleStereo`) look the eye up from the main-thread camera
  and will not find it.
- **Culling** is the frame's single camera; geometry at the outer frustum edges of
  each eye may be culled.
- **Temporal effects.** TAA would blend the other eye (as today). SSR/SSDO history
  and exposure adaptation now advance twice per frame; not measured.
- Cost at headset resolution (2688x2880) and with VR mode active.

## Next

1. Wire into OpenXR: main thread keeps the centre (head) camera instead of
   alternating eyes; copy the backbuffer after the first submission into the left
   held-eye image and after the second into the right; one pair contract per frame
   with both poses and the same display time.
2. Near pass per eye inside each submission; re-check `NearViewStereo` provenance.
3. xr-sim + xr-tape run in VR mode, then a headset session on the Steam Frame.
