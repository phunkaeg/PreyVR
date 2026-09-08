# The reticle follows the head, and the code says it cannot

**Static follow-up:** [audit, corrected inference and next capture](RE-AIM-HEAD-COUPLING-2026-09-08.md).
The production aim hook passes a fixed-controller, 60-degree head-yaw replay;
the production projection matches perspective. The visible fault remains open.
`report` now includes one coherent `reticleProjection` record and an offline
analyzer is available. This has been built/tested, not injected or headset-tested.

**Superseded in part by the full session handover:**
[headset session 2026-09-08](HANDOVER-HEADSET-SESSION-2026-09-08.md). That
carries the measured `rpTans` frustum, the wearer's edge-shrinkage observation
and the mapping hypothesis it implies, plus the trigger, near-FOV, IK and
weapon-switch findings from the same run.

A headset measurement and two independent code readings disagree. This is the
brief for whoever takes it next; the observation is solid and the explanation is
not mine yet.

## The observation

Controller held steady on a fixed object, head yawed left. **The reticle slides
off the object to the right.** Reported unprompted by the wearer, then measured.

| | |
|---|---|
| head sweep | 26.1 degrees |
| play yaw during the sweep | **137.957 constant**, every sample |
| reticle X travel | 104 milli |
| slope | **-2.43 milli per degree** of camera yaw |

The original estimate of **70% head-yaw leakage is withdrawn**. Dividing the
viewport width by FOV assumes a linear angular projection. At the centre of a
symmetric 120-degree pinhole view, the slope is **5.04 milli per degree**, and
it increases away from centre. The recorded slope alone cannot determine how
much head motion entered the aim ray; pitch, roll, translation, clamping and
the HUD movie mapping also matter.

The sampling is noisy -- the wearer was holding the controller by hand and
reticle Y moved 284 milli in the same window -- so treat the magnitude as
indicative and the sign and the A/B results as solid.

## Two live A/B results, which is the useful part

**`aim.bodyyaw 0`** -- the reticle becomes *fully* head-locked, riding the view
exactly. So the body-yaw path is doing real work; this is a partial leak, not an
absent correction.

**`aim.reticleconverge 100000`** (10 m to 100 m) -- **the slide got noticeably
WORSE.** The projected point is
`rayOrigin + direction * d` seen from the eye: as `d` grows the screen position
converges on the pure **direction**, and as `d` shrinks it is dominated by the
**origin offset**. This is useful sensitivity evidence, but **does not uniquely
identify a direction fault**. Even a correct fixed ray at a fixed world origin
has depth-dependent parallax against a finite-distance object when the eye
translates during a head turn. HUD scaling is another downstream possibility.

## Why that should be impossible

Both readings below were done against this build, and both say a pure head
rotation cannot move the published direction.

1. **The controller pose is located in `XR_REFERENCE_SPACE_TYPE_LOCAL`**
   (`XrSessionHost.cpp:373`), which does not rotate with the head.
2. **The direction is built from the body yaw and the raw controller
   orientation only.** `AimFromController` calls `EyePoseInWorld`, which is
   `Compose(YawQuaternion(reference.yawRadians), ToEngineSpace(pose))`. The head
   pose is never an input. And `reference.yawRadians = frame.yaw`, whose reported
   value held at exactly 137.957 through the entire sweep.

A third reading rules out the obvious asymmetry. The hand lane uses
`ControllerWorldFromHead`, which *does* take the head pose -- but it subtracts
only the head **position** and passes `openXrController.orientation` through
untouched. So both lanes derive orientation identically. The asymmetry is real
but confined to the ray's origin. The convergence test does not eliminate that
origin or the downstream mapping.

## What is instrumented for the next run

The original `aimDirMilli` publishes the latest producer's world direction, and
`aimRawQMilli` the raw controller orientation it came from, both in thousandths.
These legacy fields are independent atomics, omit quaternion W and do not belong
to the same snapshot as `reticleXY`. Use the new coherent `rp*` record instead.
For a physically fixed controller and constant play yaw, raw orientation and
world direction should stay fixed. Interpret movement as follows:

- **raw moves** -- establish whether the controller physically moved or tracking
  shifted. LOCAL space does not promise a stationary controller or noiseless
  tracking. Compare quaternion rotations, accounting for equivalent `q` and `-q`.
- **raw still, direction moves** -- the composition is at fault, despite reading
  as head-independent.
- **both still** -- the direction is fine and the fault is downstream, in the
  projection or in the camera basis it is projected through.

## Things worth suspecting that were not eliminated

- **The projection reads the live view camera during the render seam**, and Prey
  renders one eye per frame. The reticle is written from whichever eye is
  installed. That should produce IPD-scale jitter rather than a systematic
  slide, but it was not measured.
- **`ikLocYawMdeg` was 111.5 degrees while the play yaw was 138.0.** The
  character's facing and the aim reference differ by 26.5 degrees. A constant
  offset does not explain a head-dependent slide, but nothing here has
  established that these two *should* differ, and the aim is anchored to one of
  them.
- The reticle is projected as a normalised viewport fraction. R-114 measured a
  16:9 **menu** content band, but the cvar help cited as HUD evidence says
  **"MP only"**. Neither proves DanielleHUD's reticle mapping. A native centre
  value of 0.5 also does not establish its off-centre scale. Do not rule out X
  scaling or apply a 52% Y correction without the actual consumer/pixel mapping.

## Reproducing

Launch with `-Headset -RenderWidth 2688 -RenderHeight 2880 -NoHudBob`, run
`Invoke-PreyVRStartup.ps1`, then `aim.enable 1` and `aim.reticle 1`. Hold a
controller still on a fixed object and yaw the head. Sample `report` repeatedly
and correlate `reticleXY` against `camYawMdeg`.

**Do not trust a dispatch count as visual acceptance** (F-011): `reticleDispatched`
was 25110 against `reticleDispatchFailed=0` throughout, while the thing was
visibly wrong the whole time.
