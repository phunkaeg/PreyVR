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
PreyVR_SetSyntheticStereo(0.064, 50.0)    ; 64mm IPD, 50-degree half-FOV -> 2 = armed
PreyVR_RequestFrameCapture(0)             ; tag is overridden with the real eye index
PreyVR_RequestFrameCapture(0)             ; the next frame renders the other eye
PreyVR_SetSyntheticStereo(0, 0)           ; disarm
```

Captures are stamped with the eye the hook actually rendered, not with the tag
passed in -- the requester cannot know which eye is next, and a silently swapped
pair inverts depth while looking almost right.

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

## Acceptance

A stereo pair whose disparity is consistent with the geometry, restore failures
at zero, and no unexplained artefacts beyond the two predicted above. That
result makes per-eye camera construction a solved problem and leaves the
double-render as the only open question for native stereo.
