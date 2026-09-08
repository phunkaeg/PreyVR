# Headset session 2026-09-08 — findings, observations, and what is still open

Build `2767000d7fd3e5ed` (identical in `configure`, `verify`, `headless` and the
audit tree). Everything below is from a live headset run on a Quest 3 through
VirtualDesktopXR at 2688x2880 per eye, `pixelRatioPercent=100`, `sizeMismatch=0`.

Wearer observations are quoted as given. Where I reasoned past the evidence I
have said so, because two of my conclusions in this session were wrong and one
of them wasted headset time.

---

## 1. The reticle slides off a fixed object when the head turns

**This is the open one, and the wearer's last observation reframes it entirely.**

### The observation

> "it slides off the object to the right if i turn my head left"

and, later, the one that matters most:

> "the reticle actually gets smaller the closer it gets to the edge of the
> screen, almost as if it were wrapping a cylinder"

### Why the shrinkage is the important clue

A 2D sprite at a screen position does not change size. Something that shrinks
toward the edges is being drawn on a **flat plane in 3D, viewed through a wide
field**: a plane at fixed depth is `1/cos θ` further away at angle θ off-axis, so
content there shrinks by `cos θ`. At the edge of a 120-degree field that is
`cos 60 = 0.5`, half size. On a 90-degree monitor it is barely visible; at 120+
it is obvious.

**That predicts the slide without any head coupling in the aim.** The reticle
position is computed as a normalised fraction from the *camera's* tangents, and
the movie lays that fraction across *its own plane*. If the plane subtends a
different angle than the camera frustum, the fraction lands at the wrong angle,
with **zero error at centre and growing error toward the edges**. Turning the
head moves the target toward the periphery, so the reticle would sit correctly on
something you look straight at and drift progressively as it moves out.

This reconciles the observation with the code, which is what nothing else did:
the aim direction really is head-independent, and the fault is downstream in the
fraction-to-plane mapping.

**The discriminating test, not yet run:** point at an object dead centre of view
and check the reticle is on it, then turn the head so the object drifts to the
edge without moving the controller. Accurate at centre and worsening outward
confirms it. Wrong at dead centre kills it.

### Measurements

Controller hand-held (so contaminated — see the caveat), head swept:

| | |
|---|---|
| head sweep | 26.1 degrees |
| play yaw across every sample | **137.957, constant** |
| reticle X travel | 104 milli |
| slope | **-2.43 milli per degree** of camera yaw |

A world-locked reticle at the centre of a symmetric 120-degree frustum moves
**5.04 milli per degree** (`1 / (2*tan(60))`), and the rate rises off-axis.
Measured 2.43 is roughly half, and `cos 60 = 0.5` is the edge-of-field factor for
exactly the geometry above. **Suggestive, not proof** — the sample spans the
field rather than sitting at one angle, and reticle Y moved 284 milli in the same
window, so the controller was not still.

**I earlier reported this as "70% of head yaw leaking".** That is withdrawn. I
divided viewport width by FOV, which assumes a linear angular projection; the
tangent mapping is not linear. Codex caught it and is right.

### The frustum the reticle actually projects through

From the coherent `reticleProjection` record:

```
rpTans = -1.73205101, 1.73205101, -1.85576892, 1.85576892
```

Perfectly **symmetric**: 120.000 degrees horizontal by 123.363 vertical, with
**zero asymmetry**. `TangentsFromCamera` does read and apply
`asymLeft/Right/Bottom/Top`, so a symmetric result means the camera it read has
all four at zero. A Quest 3 frustum through VirtualDesktopXR is asymmetric, and
`CameraEditHook` explicitly writes per-eye asymmetry.

Two candidates, not yet separated:

- **The reticle reads a camera that has not had the per-eye edit applied.** It
  reads `systemPtr + SystemLayout::viewCamera`; if the edit targets a different
  camera object, or if `UpdateAimReticleForRender` fires on a call path before
  the edit, the tangents would be Prey's own symmetric gameplay frustum.
- **The edit itself is synthetic and symmetric.** The log carries
  `preyvr_camera_edit result=0 detail=stereo_armed ipd=0.064 halfFovDegrees=50`,
  which is the synthetic stereo path, not the runtime's frustum.

### Eliminated, with the evidence

- **Not the near pass.** `rpTans` was byte-identical with `r_DrawNearFoV` at 70
  and at 123.363. I had suspected this because 123.363 appeared in `rpTans`; that
  was a coincidence of my own making, since I derived that value from
  `2*atan(tan(60)/aspect)` and the camera's vertical FOV follows the same formula
  from the same aspect. **The test came back negative and the suspicion was
  mine.**
- **Not the reference yaw.** `rpPlayYaw` and `playYawMdeg` held at exactly
  137.957 through the entire sweep.
- **Not the reference space.** Controller poses are located in
  `XR_REFERENCE_SPACE_TYPE_LOCAL` (`XrSessionHost.cpp:373`), which does not
  rotate with the head.
- **Not an aim/hand conversion asymmetry.** The hand lane's
  `ControllerWorldFromHead` takes the head pose but subtracts only its
  **position**, passing `openXrController.orientation` through untouched. Both
  lanes derive orientation identically.

### A conclusion of mine that was over-stated

`aim.reticleconverge` 10 m to 100 m made the slide **worse**, and I read that as
uniquely identifying a direction fault. It does not. A correct fixed ray still
separates from a finite-distance object when the eye translates during a head
turn, and the head rotates about the neck rather than the eye. Codex is right
that this is sensitivity evidence only.

---

## 2. The trigger did not fire, and both causes were mine

**The key ids were never the problem.** `FUN_1809D9EF0` registers each device key
as a name/id pair; reading the `MOV R8D, <id>` after each `LEA RAX, [<name>]`
gives the full table, and it self-validates because `xi_thumblx` comes out as
`0x210` and `xi_thumbly` as `0x211`, both already confirmed live by R-089.

```
xi_triggerr     0x20F      xi_thumbr_up      0x218
xi_thumblx      0x210      xi_thumbr_down    0x219
xi_thumbly      0x211      xi_thumbr_left    0x21A
xi_thumbl_up    0x212      xi_thumbr_right   0x21B
xi_thumbl_down  0x213      xi_triggerl_btn   0x21C
xi_thumbl_left  0x214      xi_triggerr_btn   0x21D
xi_thumbl_right 0x215
xi_thumbrx      0x216
xi_thumbry      0x217
```

Every previously inferred value was correct.

**Cause one:** the trigger is two keys. `0x20F` is the analog axis, `0x21D` is
the button, and firing is bound to the **button**. Posting only the axis was
accepted — 36 presses, `fireRefused=0` — and did nothing. A valid key that
nothing is bound to is indistinguishable in the counters from one that works, so
the fail-closed refusal cannot catch this class of error.

**Cause two:** posting `0x21D` was then refused **299 times by our own guard**,
because `PostRawInputImmediate` looks every key up in the mod's `kNames` table
and I had added the constant without the name. Reported honestly as
`fireRefused`. The guard was correct both times; the caller had not finished the
job.

Both fixed. The fire lane now posts the button as a press/release **edge**
alongside the analog travel — a button posted as "changed" is not a press, and
the action map wants the transition.

---

## 3. `r_DrawNearFoV` is vertical, moves with aspect, and is reset by a level load

The wearer, twice: *"weapon model fov is wrong again, you keep forgetting to set
this"*.

88.507 was never a constant. It is the vertical half of a `120 x 88.507` frustum
measured at **16:9**. At 2688x2880 the aspect is 0.9333 and the same number
yields about 84 degrees horizontal instead of 120, which is an over-magnified
weapon. Derived instead:

```
tan(V/2) = tan(60 deg) / (width / height)
```

Two checks that this is the right formula rather than a fitted one: at 2560x1440
it returns **88.507 exactly**, reproducing the value validated weeks earlier; at
2688x2880 it returns **123.363**, and 123.35 was applied live and confirmed
correct by the wearer.

**A level load resets it.** The startup script applies it at the menu, so it must
be reapplied after loading. Confirmed live: reapplying in-level fixed it.

---

## 4. IK findings

- **`ik.hands` is a bitmask defaulting to 1 (right only).** The left hand never
  calibrated or wrote until it was set to 3. Not documented anywhere obvious.
- **Crosstalk with both hands driven.** *"moving the right controller appears to
  affect the left hand mesh too"*. Candidate: targets 38 and 39 are
  `r_hand_spine_target` and `l_hand_spine_target`, both spine-attached, so
  driving one may pull a chain the other's two-bone solve runs through.
  **The test I ran was confounded and the wearer caught it** — turning the left
  hand off removes the observation, not the cause. The clean test is the reverse:
  drive **only the left** hand and move the **right** controller, so the right
  target is never written.
- **`ScaleReach` multiplies.** I raised `ik.reach` to 125 in response to
  clamping, which pushed the hand 25% *further* from the shoulder than the
  controller and broke 1:1 — the wearer reported it immediately. To reduce
  clamping for a player with longer arms the value must go **below** 100, which
  compresses their range into the character's and keeps motion continuous instead
  of pinning. 80 was applied; not yet judged.
- **Clamping was about half of all writes** at reach 100
  (`ikClamped=7068` of `ikWrittenR=14407`), which is the *"some movements
  impossible"* the wearer described: past the character's reach the goal clamps
  to a sphere and further extension does nothing.
- *"some that click into place"* — unexplained. Consistent with a two-bone
  solver's elbow flipping between the two valid solutions as the arm passes
  through a degenerate configuration, which would be the native solver rather
  than our write, but this was not measured.

### The calibration invalidation chain, which is worth knowing

`ikEquipGen` went **5 to 29** in minutes. Calibration binds to the equip
generation, so **every weapon switch throws it away**, and hand rotation dies
with it — rotation is only written once calibrated. The wearer's report that
rotation *"broke again"* after working is fully explained by this.

And the equip generation was churning because of item 5.

---

## 5. The right stick turns and also switches weapons

Unresolved. We post only `xi_thumbrx` (`0x216`, confirmed correct). Candidates:

- The engine derives the digital `xi_thumbr_left` / `xi_thumbr_right`
  (`0x21A`/`0x21B`) from the analog axis crossing a threshold, and weapon
  switching is bound to those.
- Prey binds weapon selection to the analog axis directly.

`move.turnscale 40` was applied as a threshold probe; the result was never
reported back. **A clean test exists and was set up but not completed:** with
`move.turn 0`, push the right stick. If weapons still switch, it is not us.

---

## 6. Confirmed working in the headset

- **Resolution.** `recommended=2688x2880 backbuffer=2688x2880 sizeMismatch=0
  pixelRatioPercent=100`. The 48% deficit is closed, via launch arguments.
- **Head tracking.** `viewApplied` and `posApplied` climbing together,
  `posOffsetMm` up to 346, `viewPoseFallback=0`.
- **Near pass.** Millions applied, `nearRefused=0`, `nearNoEye=0`.
- **Body yaw.** `playYaw` held at exactly 137.957 across a 26-degree head sweep.
  R-106 holds.
- **Locomotion.** Left stick walks. `moveNative=0` throughout, so nothing is
  fighting our posts, and `moveDropped=0`.
- **Reticle dispatch.** `reticleDispatched=25110` against
  `reticleDispatchFailed=0`. **This is not visual acceptance** (F-011) — it was
  dispatching perfectly while being visibly wrong the entire time.

---

## 7. Instrumentation available

`report` carries a coherent `reticleProjection` snapshot: `rpHeadPos`, `rpHeadQ`,
`rpRawPos`, `rpRawQ`, `rpOrigin`, `rpDir`, the full `rpCamera` matrix, `rpTans`,
`rpDistance`, `rpRawXY`, `rpXY`, `rpClamped` and the dispatch results, all from
one frame. `tools/re/analyze_aim_projection.py` reads saved report text offline.

The older `aimDirMilli` / `aimRawQMilli` are independent atomics that omit
quaternion W and do not belong to the same snapshot as `reticleXY` — prefer the
`rp*` record.

New since: `recenters` counts the both-grips recentre chord.

---

## 8. Reproducing

```
tools/Invoke-PreyVRLaunch.ps1 -Headset -RenderWidth 2688 -RenderHeight 2880 -NoHudBob
```

Inject, then `Invoke-PreyVRStartup.ps1 -Controls`. **Reapply
`console r_DrawNearFoV <derived>` after loading a level.** Then `aim.enable 1`,
`aim.reticle 1`, `ik.mode 2`, `ik.drive 1`, and `ik.calibrate` with the hand held
still.

Recentre is **both grips squeezed together**. It deliberately invalidates the IK
calibration, because the hands were calibrated against the old reference — so
recentre, then recalibrate.

Rest the controller on something solid before any reticle measurement. The last
sweep was hand-held and the contamination cost the magnitude its value.
