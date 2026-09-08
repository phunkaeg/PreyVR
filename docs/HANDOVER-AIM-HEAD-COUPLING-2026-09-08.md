# The reticle follows the head, and the code says it cannot

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

A perfectly world-locked reticle in this frustum moves about **8.3 milli per
degree** (1000 milli across roughly 120 degrees horizontal). A perfectly
head-locked one moves **0**. Measured 2.43. So roughly **70% of head yaw is
reaching the aim**, and the correction is removing only about 30%.

The sampling is noisy -- the wearer was holding the controller by hand and
reticle Y moved 284 milli in the same window -- so treat the magnitude as
indicative and the sign and the A/B results as solid.

## Two live A/B results, which is the useful part

**`aim.bodyyaw 0`** -- the reticle becomes *fully* head-locked, riding the view
exactly. So the body-yaw path is doing real work; this is a partial leak, not an
absent correction.

**`aim.reticleconverge 100000`** (10 m to 100 m) -- **the slide got noticeably
WORSE.** This is the discriminating result. The projected point is
`rayOrigin + direction * d` seen from the eye: as `d` grows the screen position
converges on the pure **direction**, and as `d` shrinks it is dominated by the
**origin offset**. Worse at 100 m means the fault is in the **direction**, and
that the origin offset at 10 m was partially masking it.

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
but confined to the ray's origin, which the convergence test has now largely
exonerated.

## What is instrumented for the next run

`aimDirMilli` publishes the world direction actually handed to the reticle, and
`aimRawQMilli` the raw controller orientation it came from, both in thousandths.
Under a pure head rotation **both must hold still**. Whichever moves names the
culprit:

- **raw moves** -- the pose coming out of `xrLocateSpace` is not what LOCAL space
  implies, and the problem is in the runtime or in how the space is used.
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
- The reticle is projected as a normalised screen fraction, and **Prey's 2D
  layer clamps to a centred 16:9 box** inside the frame (R-114). At 2688x2880
  that box is 52% of the frame height. If the movie maps our fraction to the box
  rather than the frame, the mapping is wrong by that ratio -- which would affect
  Y strongly and X not at all, so it does not explain this, but it is unresolved
  and will matter for placement.

## Reproducing

Launch with `-Headset -RenderWidth 2688 -RenderHeight 2880 -NoHudBob`, run
`Invoke-PreyVRStartup.ps1`, then `aim.enable 1` and `aim.reticle 1`. Hold a
controller still on a fixed object and yaw the head. Sample `report` repeatedly
and correlate `reticleXY` against `camYawMdeg`.

**Do not trust a dispatch count as visual acceptance** (F-011): `reticleDispatched`
was 25110 against `reticleDispatchFailed=0` throughout, while the thing was
visibly wrong the whole time.
