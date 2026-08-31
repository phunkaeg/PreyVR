# Live protocol A1 — the first write to the render path

**Question:** does modifying `CSystem::m_ViewCamera` change the rendered image?

Everything to date has been read-only. This is the precondition for every stereo
design considered so far: if the answer is no, the seam we have been building
toward is not a seam. If yes, the same wrapper that proves it is the one that
later renders twice.

Bounded, reversible, and instrumented, in the manner of `LIVE_A0B_WRENCH_PROTOCOL`
and `LIVE_INTERACTION_A0_PROTOCOL`.

## What is written, exactly

One `memcpy` of `0x240` bytes into `CSystem+0x788`, held for the duration of a
single `CSystem::Render` call, then a second `memcpy` putting the original bytes
back. Nothing else in the process is modified. The original bytes are captured
immediately before the write and compared byte for byte immediately after the
restore; a single differing byte disarms the hook.

The edited camera is built **entirely in our own memory**. `CCamera::UpdateFrustum`
(RVA `0x121D70`) is called on that private copy, so the engine function never
touches game state, and an edit that fails its safety checks costs nothing
because it is abandoned before any write.

## Why `CSystem::Render` and not `SetViewCamera`

The game rewrites `m_ViewCamera` every frame from `ArkPlayerCamera`, so a write
issued from the frame observer at `RT_EndFrame` would be overwritten before the
next render reads it. `CSystem::Render` (R-058) is the window in which the camera
is read and handed to both consumers, which makes wrapping it the smallest place
the edit can live and still be seen. It is also the shape that generalises: to
render two eyes, call the original twice with two cameras.

## Getting Prey into a testable state

Recorded because the first attempt at these protocols was run against a **menu**
without realising it. The tell was `preyvr_snapshot view_camera pos=0.0000,0.0000,0.0000
res=640x480` -- that is the default camera, not a real one, and every frame
comparison against it would have been meaningless.

Launch is four gated steps, none of which can be skipped:

1. Logo videos, then a disclaimer screen.
2. A "Prey" title screen that waits for **any key** before it will build the menu.
3. The main menu. The first item is **Continue**, which loads the previous save.
4. Once the save has loaded, **another key press** is needed to enter the game.

Only after step 4 is the game actually running and the camera real.

**And the game slips back to its in-game menu whenever it loses focus.** There is
no cvar for this -- a string search finds `sys_no_crash_dialog` and a family of
`Dof_Focus*` and audio-focus entries, but nothing that disables pause-on-focus-loss.
So it has to be worked with rather than turned off:

- Arm everything first (observer, scene freeze, the edit), *then* click into the
  game. The camera edit applies every frame and the capture is serviced from the
  render thread, so both act on whatever frames follow.
- A capture taken while the window is unfocused shows the menu, not the scene. If
  a dump looks like a menu, that is what happened -- discard it and retake.
- `t_Scale 0` freezes the simulation but not the menu state, so the freeze does
  not make focus loss harmless.

## Preconditions

1. Prey running, `PreyVR.dll` injected, `preyvr_smoke_result status=verified`.
2. Frame observer enabled (`PreyVR_SetFrameObserverEnabled(1)`), since captures
   are serviced from it.
3. **A deterministic scene.** Queue both, and confirm each returns 0:
   - `t_Scale 0` — freeze the simulation
   - `r_AntialiasingMode 0` — temporal AA makes a static scene differ frame to
     frame, which is exactly the noise this test must not be confounded by
   - `r_MotionBlur 0`
4. Player standing still, looking at geometry with visible structure. A blank
   wall makes a yaw nearly invisible and would produce a false negative.

## Step 0 — the noise floor, before any write

**Do this first and do not skip it.** Two captures with nothing armed:

```
PreyVR_RequestFrameCapture(0)   ; wait for the completed count to increment
PreyVR_RequestFrameCapture(0)
preyvr_frame_diff frame-<a>-tag0.pvrframe frame-<b>-tag0.pvrframe
```

That number is what "unchanged" looks like on this machine, and it is the first
row of the separation table. Without it, the A/B result below has nothing to be
compared against and no threshold can be honestly derived. It also proves the
capture path works before anything depends on it.

Expect near zero with the scene frozen. **If it is not near zero, stop** — the
scene is not actually static and every later number is meaningless.

## Step 1 — the A/B pair

```
PreyVR_RequestFrameCapture(0)          ; unmodified
PreyVR_SetCameraYawEdit(10.0)          ; returns 2 = armed
PreyVR_RequestFrameCapture(1)          ; edited
PreyVR_SetCameraYawEdit(0.0)           ; disarm
```

Then check, in this order:

- `PreyVR_GetCameraEditRestoreFailureCount()` — **must be 0.** Anything else means
  the camera did not come back byte-identical, and the session is contaminated:
  stop and restart Prey before drawing any conclusion.
- `PreyVR_GetCameraEditAppliedCount()` — must be non-zero, or the edit never ran
  and a null result proves nothing.
- `preyvr_frame_diff` on the two dumps.

## Acceptance

**No threshold is stated here on purpose.** It has to come from the measurement,
not from this document. What the run produces is a table:

| pair | expected |
| --- | --- |
| unmodified vs unmodified (step 0) | the noise floor |
| unmodified vs 10-degree yaw | must be far above the floor |

"Far above" gets a number once both rows exist. A 10-degree yaw at 88 degrees
horizontal FOV moves the image by roughly an ninth of the screen width, so the
separation should be large and obvious rather than marginal. **If it is marginal,
that is a result to investigate, not a threshold to widen** — widening is how a
gate becomes looser than no gate.

Also worth capturing while armed: whether *culling* follows the camera or lags
it. The engine header marks the asymmetry fields "not used for culling", and
`UpdateFrustum`'s plane rebuild is the thing we are relying on to keep them in
step. Geometry popping in or out at the frame edges under a yaw would be the
visible symptom.

## Abort conditions

Stop immediately, restore, and record the outcome if any of these hold:

- restore failure count is non-zero
- `preyvr_camera_edit result=refused detail=unsafe_rotation` appears in the log
  (the edited matrix failed the orthonormality or degeneracy gate)
- the game hitches, corrupts, or crashes
- the log shows `result=unavailable` for either prologue — the DLL does not match
  this build and nothing should be written at all

## What this does not test

It does not render twice. It does not touch per-eye projection, the asymmetry
fields, `SetPreviousFrameCamera`, or the viewmodel. Those are separate
experiments and each has its own failure modes; bundling them into this one would
make a negative result uninterpretable.

---

# A2 — synthetic stereo, still with no headset and no double-render

Once A1 shows that writing `m_ViewCamera` changes the image, the next question is
whether a *per-eye* camera is constructed correctly. That does not require a
headset, and it does not require rendering twice in one frame.

**With the scene frozen, a left-eye frame followed by a right-eye frame is a
stereo pair.** So the hook alternates the eye every frame and the whole per-eye
construction -- pose offset, asymmetric projection, frustum rebuild -- can be
proven and *looked at* before anyone calls the render function twice.

Splitting it this way matters because the two risks are unrelated. Per-eye camera
construction being wrong is a maths bug with a visible signature. Rendering twice
in one frame is an engine-architecture question that might simply not work. Doing
them together would make a failure uninterpretable.

## Preconditions

Everything from A1, and the scene freeze is now **mandatory** rather than
advisable: `t_Scale 0`, `r_AntialiasingMode 0`, `r_MotionBlur 0`. Two consecutive
frames of a moving scene are not a stereo pair, they are two different moments.

## Steps

```
PreyVR_SetStereoAsymmetry(1.0)            ; symmetric frusta -- see below, this matters
PreyVR_SetSyntheticStereo(0.064, 50.0)    ; 64mm IPD, 50-degree half-FOV -> 2 = armed
PreyVR_SetStereoEyeLock(0)                ; hold the left eye
   ... let several frames pass ...
PreyVR_RequestFrameCapture(0)
PreyVR_SetStereoEyeLock(1)                ; hold the right eye
   ... let several frames pass ...
PreyVR_RequestFrameCapture(1)
PreyVR_SetStereoEyeLock(2)                ; back to alternating
PreyVR_SetSyntheticStereo(0, 0)           ; disarm
```

**The eye is locked rather than tagged.** Per-frame tagging was tried first and
does not work: the camera hook runs on the game thread, the capture runs on the
render thread, and the engine's MT/RT double buffer offsets them. Observed live
2026-08-31, two captures reported as eye 1 then eye 0 both landed in files tagged
1 -- exactly the silently-swapped pair the tagging existed to prevent. Locking the
eye makes the signal far longer than the uncertainty, so no ordering assumption is
needed at all.

### Why the asymmetry is switched off first

The default synthetic frustum is deliberately asymmetric per eye (outer 55
degrees, inner 45) so that the asymmetry path gets exercised. Measuring it at the
same time as the eye offset is what made the first two A2 runs unjudgeable.

Run 2026-08-31 with the asymmetry left on: mean absolute difference 30.18, 81.7%
of pixels changed -- and a shift scan found a single uniform offset of **-462 px**
that dropped the residual to 9.34. So roughly 69% of the difference was frustum
shear, and the 64 mm eye separation was buried underneath it.

That magnitude was predicted from the geometry to within 2.4% before it was
measured, which is the reason to trust the explanation. The frusta span
`tan(55) + tan(45) = 2.428` tangent units across 2560 px, and the two eyes'
centres differ by `tan(55) - tan(45) = 0.428` of that: `0.428 / 2.428 * 2560 =
451 px` predicted against 462 px measured. The scan was re-run at `--max-shift`
512, 640, 768 and 1024 and returned -462 every time, so it is an interior
minimum rather than a clamped search.

**Two effects in one image is one effect too many.** With `SetStereoAsymmetry(1.0)`
the shear is zero by construction and whatever remains is the eye offset alone.

Then check `PreyVR_GetCameraEditRestoreFailureCount()` is 0, and:

```bash
./tools/New-StereoView.ps1 -Left frame-<a>-tag0.pvrframe -Right frame-<b>-tag1.pvrframe -Mode Anaglyph
```

## What to look for

| view | what it tells you |
| --- | --- |
| `Anaglyph` | Distant geometry should show little or no fringing, near geometry clear fringing, and the fringe should run the same way throughout. Uniform fringing everywhere means the eyes differ by a rotation rather than a translation. Reversed fringing means the pair is swapped or the IPD sign is inverted. |
| `Difference` | Bands should scale with proximity. **A black region where the weapon is, while the world behind shows bands, is the viewmodel failing to follow the per-eye camera** -- expected, since it renders at `r_DrawNearFoV` 54 degrees against the world's 88 and that value is latched once per frame (R-069). |
| `SideBySide` | Each eye individually sane: no inverted culling, no geometry missing from one eye only. |

Geometry vanishing at the **outer** edge of each eye is the predicted
cull-versus-render divergence: the engine header marks the asymmetry shifts "not
used for culling", so the render frustum is per-eye while the cull frustum stays
symmetric. Expected, worth measuring, and not a reason to stop.

## The prediction, written down before the run

A test that can only be interpreted after the numbers arrive is not a test. With
symmetric frusta the only difference between the eyes is a 64 mm sideways
translation, and that has a closed-form signature.

The horizontal span is `2 * tan(50) = 2.384` tangent units across 2560 px, so
1074 px per tangent unit. A point at distance `d` metres shifts by `0.064 / d`
tangent units, hence **`68.7 / d` pixels**:

| distance | predicted disparity |
| --- | --- |
| 0.5 m | 137 px |
| 1 m | 69 px |
| 2 m | 34 px |
| 5 m | 14 px |
| 10 m | 7 px |
| 30 m | 2 px |
| infinity | 0 px |

So, before running:

1. `improvementRatio` **< 2.0**, verdict `no_single_shift_explains_it`. Disparity
   varies with depth, so no single offset can align the pair. This is the load-
   bearing prediction -- it is what separates a translation from a shear.
2. `|bestShift|` **small, under ~140 px**, being the modal depth of whatever is on
   screen rather than a property of the projection.
3. `residualAtZero` far above the A1 noise floor of 0.0167, so the pair genuinely
   differs.

And the failure signatures, so a bad result says *which* thing broke:

| observed | meaning |
| --- | --- |
| ratio >= 2 with a large `bestShift` | the offset is acting like a projection shift, not a translation -- either the asymmetry did not actually turn off, or the eye offset leaked into the frustum |
| `residualAtZero` at the noise floor | the eye offset is not reaching the camera at all |
| uniform fringing everywhere in the anaglyph | the eyes differ by a rotation rather than a translation |
| reversed fringing | the pair is swapped or the IPD sign is inverted |

## A2b — the IPD sweep, which is the actual acceptance test

A single pair cannot distinguish "small depth-varying disparity" from "noise",
because both produce a low ratio and a small shift. A sweep can: **the residual
must scale with the IPD**, and at zero IPD with symmetric frusta the two eyes are
the same camera, so the difference must collapse to the TAA noise floor.

Four points, all inside the existing bounds (`SetSyntheticStereo` accepts 0 to
disarm, then 0.045..0.085):

| IPD | predicted `residualAtZero` |
| --- | --- |
| disarmed | the A1 noise floor, ~0.0167 |
| 0.045 | some value `R45` well above the floor |
| 0.064 | ~1.42x `R45` |
| 0.085 | ~1.89x `R45` |

Monotonic, and roughly proportional. **Proportionality is the weaker claim** --
disparity saturates once it exceeds the local image structure, so the higher IPDs
may come in under a straight line. Monotonicity is the one that must hold.

The zero-IPD row is the control and the most important of the four: it is the row
that fails if the difference is actually TAA, capture jitter, or the game not
being as frozen as assumed.

## Acceptance

The sweep monotonic with the zero-IPD control at the noise floor, a shift scan
verdict of `no_single_shift_explains_it`, restore failures at zero, and no
unexplained artefacts beyond the two predicted above. That result makes per-eye
camera construction a solved problem and leaves the double-render as the only
open question for native stereo.

---

# A3 — the double render

**Question:** can Prey render the world twice inside one frame?

This is the last architectural unknown for native stereo, and the only genuinely
risky mode in the DLL. **Run A2 first.** If A2 has not passed, a crash here is
ambiguous between "the engine cannot render twice" and "the second camera was
malformed", and that ambiguity would cost far more than the ordering does.

## What it does

Inside one `CSystem::Render`, writes the left-eye camera, calls the original,
writes the right-eye camera, calls the original again, then restores. Each eye is
built from the *original* camera rather than from the previous eye's, so an error
cannot accumulate across the two passes.

The second render overwrites the backbuffer, so the presented image is the right
eye. **A capture here is one eye, not a pair** -- A2 is how pairs are made.

## The frame budget is not optional

```
PreyVR_SetDoubleRenderStereo(0.064, 50.0, 120)   ; ~1 second at 144Hz
```

The mode disarms itself after that many frames, and the budget is decremented
*before* the renders so a call that never returns still costs exactly one frame
of the allowance. There is no unbounded option, and the ceiling is capped at 600.
An experiment that runs for ten thousand frames is not an experiment.

Start at **1**. A single double-rendered frame answers "does it survive" without
committing to a second. Only then go to 120 for a rate measurement.

## What to record

- `PreyVR_GetDoubleRenderedFrameCount()` — how many completed
- `PreyVR_GetCameraEditRestoreFailureCount()` — must stay 0
- the observed frame rate while armed, against `sys_MaxFPS 144`. Roughly half
  would mean the world render dominates the frame and native stereo costs what
  you would expect. Much worse than half means something is being redone that
  should not be.
- anything in the log matching `detail=double_render_`

## What would make this a negative result

A crash, a restore failure, or visibly corrupt rendering. Any of those closes the
native-stereo path and makes the alternative -- one eye per frame, alternating,
with reprojection -- the design to pursue instead. That would be a real answer,
not a failure: A2's per-eye machinery is unchanged by it, and the alternating
mode already exists and works.

---

# A2b result, 2026-09-01 — PASSED

Run in the Talos I lobby (near planter and leaves, mid signage, far windows), scene
frozen, symmetric frusta, eye locked rather than tagged.

| IPD | residualAtZero | bestShift | improvement | verdict |
| --- | --- | --- | --- | --- |
| 0 mm | **0.0112** | 0 px | 1.00 | control |
| 45 mm | 6.970 | -7 px | 1.36 | `no_single_shift_explains_it` |
| 64 mm | 8.242 | -9 px | 1.375 | `no_single_shift_explains_it` |
| 85 mm | 9.565 | -15 px | 1.383 | `no_single_shift_explains_it` |

Every prediction recorded above held.

**The load-bearing one:** improvement below 2.0 at every armed IPD. No single
offset re-aligns the pair, so the difference is depth-varying parallax rather
than the uniform shear that made the first two runs unjudgeable.

**The control:** 0.0112, below the A1 noise floor of 0.0167, and 620x below the
smallest armed IPD. With symmetric frusta and no eye offset the two eyes are the
same camera, so this is the row that would have failed had the difference been
temporal AA, capture jitter, or a scene that was not as frozen as assumed. It is
the reason the other three rows can be believed.

**bestShift stayed small and grew with IPD** -- 7, 9, 15 px against a predicted
ceiling of 140 -- which is what modal-depth disparity should do.

**Monotonic, and sub-proportional as predicted.** IPD x1.42 gave residual x1.18
and IPD x1.89 gave x1.37. Proportionality was written down beforehand as the
weaker claim, because disparity saturates once it exceeds the local image
structure. It came in under the line, as expected.

For contrast, the same test with the asymmetry left at 1.1: `meanAbsolute 30.17,
bestShift -462 px, improvement 3.23x, uniform_shear_dominates`.

`restoreFailures` was 0 across all four steps and 240+ applied frames.

## What the anaglyph adds

Fringing scales with proximity and runs the same way throughout -- a translation
between the eyes, not a rotation. Two further observations:

**The wrench barely fringes** while foliage beside it fringes clearly. That is
R-069 confirmed once more: the viewmodel renders at `r_DrawNearFoV`, latched once
per frame, so it does not follow the per-eye camera. Predicted, and now seen.

**The HUD fringes, and that was not predicted.** The health and shield widget and
the reticle are drawn through the per-eye camera, so a 2D overlay picks up
disparity it should not have. Harmless here, but a real VR build needs the HUD at
a fixed comfortable depth or on its own layer. Recorded now rather than
discovered as a comfort problem later.

## Status

Per-eye camera construction is a solved problem. The double render (A3) is the
only remaining architectural unknown for native stereo.

---

# A3 result, 2026-09-01 — the double render does NOT work as built

**Answer: Prey survives rendering the world twice in one frame exactly once, and
wedges when it is sustained.**

| budget | outcome |
| --- | --- |
| 1 | works: `done=1`, clean budget exhaust, ~142 fps continued, restore verified |
| 300 | wedges: ~4 frames completed, frame counter stopped, never resumed |

The level vanished leaving only the skybox. Since the skybox draws unculled, that
is occlusion culling rejecting all world geometry -- which points at the
per-frame `CRenderView` and coverage buffer being filled once and consumed once,
and the second pass culling against state the first pass already used.

Splitting A2 from A3 paid for itself here: because A2b had already passed, this
failure is unambiguously "the engine cannot be re-entered like this" and not "the
second camera was malformed".

**The frame budget did not contain it.** It bounds attempted frames and disarms
when exhausted, but the engine stopped completing frames at about frame 4 of 300,
so the budget never drained and the mode never disarmed. Disarming by hand did
not recover it. See F-013 -- a budget in frames cannot bound a failure that stops
frames.

**Next, and neither needs a policy change:** re-run with
`e_CoverageBufferDebugFreeze 1` and `e_CameraFreeze 1`, which freeze exactly the
subsystem implicated; and investigate `e_Recursion`, the engine's own mechanism
for drawing the world more than once per frame, which is a better foundation for
native stereo than re-entering the top-level render function.
