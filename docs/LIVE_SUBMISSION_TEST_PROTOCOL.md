# The submission test -- declaring what we render

**Status: set up, not run. Written 2026-09-02, before any execution.**

This is the first time PreyVR puts a stereo pair in front of a human eye with a
frustum it claims is true. Everything up to here was measured against a monitor,
where the one failure that matters is invisible.

## What is actually being tested

Not "does an image appear". The flat mirror already proved that. The question is
**whether declaring Prey's own frustum makes depth read correctly**, and it is a
question only a headset can answer, because the runtime reprojects to whatever is
claimed. A wrong claim does not produce an error. It produces a plausible image
with wrong depth and wrong scale, which on a monitor looks like nothing at all.

Two changes make the test meaningful, and without either one it is not worth
running:

1. **The declared FOV is Prey's, not the runtime's.** `projViews[eye].fov` came
   from `views[eye].fov` -- what the Quest would like. These pixels came out of
   Prey's 120 x 88.507 frustum, so that is what is now declared, read live
   through the same path `PreyVR_ReadDeclaredFovPtr` verified.
2. **Each eye carries its own image.** Both slices used to receive the same
   backbuffer, which is mono. Prey renders one eye per frame, so each eye's most
   recent image is now held and a complete pair is submitted every frame.

## Preconditions

- Quest 3 on, tracking, VDXR runtime (the adapter LUID question is already
  answered).
- Prey in gameplay on a **saved game**, not a menu.
- `PreyVR.dll` injected, smoke gate 2.
- **`t_Scale 0`.** Not optional for the first run -- see below.

## The sequence

```
PreyVR_SetFrameObserverEnabled(1)
t_Scale 0                                ; freeze the scene
r_AntialiasingMode 0 ; r_MotionBlur 0

PreyVR_SetNativeProjectionPtr(1)         ; inherit Prey's projection
PreyVR_SetSyntheticStereoPtr(0.064, 50)  ; 64 mm; the half-FOV is now unused
PreyVR_SetXrSubmissionDwellPtr(4)
PreyVR_StartXrSession()                  ; wait for status = running
PreyVR_SetXrStereoSubmissionPtr(1)       ; arm -- the image should go stereo here
```

Disarm is `PreyVR_SetXrStereoSubmissionPtr(0)`, which reverts to the flat mirror
without ending the session, then `PreyVR_StopXrSession()`.

### Why the scene must be frozen for the first run

Alternate-eye means each eye refreshes every `2 * dwell` frames -- at dwell 4,
roughly every 8. In a moving scene that is judder, and judder is a **confound**:
it produces exactly the discomfort that a wrong frustum produces, and the two
cannot be told apart by feel. Freezing removes it completely. The first run asks
one question and should have one variable.

## Predictions, written before the run

| | prediction |
| --- | --- |
| **Stereo appears** | the image goes from flat to solid depth when armed |
| **`submitted_fov_matches_located`** | **FAILS**, by ~20 degrees on the right edge |
| **Geometry scale** | objects at a plausible human size, not doll-sized or giant |
| **Comfort** | tolerable for a static look; no verdict on motion |
| **Submitted frame count** | rising steadily; no stall |

### The xr-tape check is expected to fail, and that is the pass

`submitted_fov_matches_located` compares the declared FOV against what the
runtime reports for the eyes. It will fail. **That failure is the correct
result** and is the whole point of this build.

Prey declares `l -60.00, r +60.00, u +44.25, d -44.25`. The Quest 3 reports
`l -54.0, r +40.0, u +44.0, d -55.0`. The right edge differs by **20 degrees** --
the magnitude `SUBMISSION_CONTRACT` predicted before any of it was measured.

If that check **passes**, something declared the runtime's FOV instead of Prey's,
and the image is a lie that happens to satisfy the validator. For an injected mod
this check passing is a failure signal, and it is the one result here that should
be treated as alarming rather than reassuring.

## Abort immediately if

- **Nausea of any kind.** Not a data point worth having. Stop, disarm, report.
- Double vision, or one eye black -- a half-empty pair should be impossible by
  construction (`SubmitStereoPair` returns false until both eyes are valid), so
  either would mean that guard is wrong.
- The image is stereo but the *wrong way round* -- eyes swapped. Cheap to confirm
  and cheap to fix; the eye order through the optics is already established.
- Prey stalls. The submission path runs on the render thread; a wedged render
  thread is F-013's failure and no flag clears it.

## What can be concluded, and what cannot

A comfortable, correctly-scaled stereo image establishes that the declared
frustum matches the rendered one and that rung 3 produces usable stereo. It does
**not** establish head tracking, which is a separate seam: the submitted pose is
the runtime's located pose while the *rendered* image still comes from Prey's own
camera, so looking around will not move the world. Expect a stereo image locked
to the game's camera. That is the next rung, not a fault in this one.

`unitsPerMetre` is still assumed to be 1 for Prey and has never been measured. If
the world looks convincingly stereo but wrongly *scaled* -- a correct-looking room
that feels like a dollhouse or a cathedral -- that assumption is the first suspect,
and it is measurable rather than a matter of taste.
