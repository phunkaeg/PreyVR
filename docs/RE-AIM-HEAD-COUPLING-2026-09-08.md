# Reticle/head coupling: static audit and a discriminating capture

Reviewed `2c48633` plus the current working-tree changes. No game launch,
injection, attach, command posting or new live capture was performed. The
headset observation remains valid; its cause is **not yet proved**.

The falsifiable question was: with fixed LOCAL controller orientation and fixed
play yaw, does the production aim hook change its world direction as the head
turns? **No in the controlled replay.** The production reticle projection also
matches an independent pinhole calculation. The next useful boundary is the
actual live ray/camera tuple, then the HUD movie's coordinate mapping and timing.

## What the static checks establish

`tools/re/vr_scheme_review/aim_review.cpp` includes production `AimTakeover.cpp`.
Only external game/tracking boundaries are stubbed. `GameCameraYaw` walks a
synthetic system slot and camera using the production offsets. The fixture
rotates head and camera together through -30 to +30 degrees, keeps a nontrivial
controller quaternion fixed, and uses body yaw 0.71 radians. The installed,
published and render-consumed direction stays fixed. Camera-relative mode is a
positive control: it changes the direction. The render context retains the
matching tracking sequence, play yaw and full raw quaternion.

`reticle_review.cpp` includes production `ReticleFollow.cpp`. It supplies camera
bytes at the module boundary and stubs the native HUD call. A 60-degree sweep
matches `x = 0.5 + tan(yaw)/(2*tan(60 degrees))`; finite origin, eye displacement,
asymmetry, clamping and dispatch-off are checked separately. Field writes and
HUD arguments agree. **The stub cannot establish what Scaleform draws.**

The ordinary 28 CTests and all four integration replays pass. The analyzer
reprojects 13 synthetic records with maximum residual **0.0000262 milli**.
Its positive control detects a changed direction with unchanged raw pose;
flipping quaternion sign produces no false rotation. Receipts:
`docs/evidence/aim-head-coupling-static-2026-09-08.json`.

The rebuilt diagnostic DLL is
`build/h021-integration-audit/Release/PreyVR.dll`, 774144 bytes, SHA256
`2767000d7fd3e5ed4a2dca57cb4e944ee4a15bb780cf3dd275730616bd1fc553`.
The regular configure build and running process were not replaced.

## Corrections to the handover's inference

**Screen fractions are not linear in angle.** For a symmetric pinhole camera,
`x = 0.5 + tan(bearing)/(2*tan(HFOV/2))`. At the centre of a 120-degree view,
the derivative is `1000*pi/(180*2*tan(60 degrees)) = 5.0383` milli/degree,
not 8.333. Away from centre it is multiplied by `sec(bearing)^2`. The reported
-2.43 slope does not establish 70% aim coupling. The 104-milli range divided
by the 26.1-degree range is not that fitted slope either; retain the actual
paired samples before fitting. The 284-milli Y range also prevents treating
the observation as a pure-yaw numerical test.

**A worse result at 100 metres is not a unique direction discriminator.**
Counterexample: fixed world ray `(origin=0, direction=+Y)`, fixed object at
`(0,10,0)`, and an eye that translates sideways while turning about the neck.
At convergence 10 m, the projected point equals the object exactly. At 100 m,
it is a different world point and slides relative to the 10 m object through
parallax, despite an unchanged, correct direction and origin. This does not
claim to reproduce the observed magnitude; it disproves the exclusive inference.
Incorrect HUD scaling can likewise convert ordinary screen travel into visible
sliding without changing the ray.

**Centred 0.5 does not prove viewport scale.** Target-native R-109 establishes
the two `reticleXOffset`/`reticleYOffset` names and a centred X value of 0.5.
Many canvas mappings share that centre. Native registration at RVA `0x17180E0`
stores `hud_canvas_width_adjustment` at SCVars receiver `+0x8F8`, default 1;
its help explicitly begins **"MP only"**. The inherited help and a 16:9 menu
capture do not establish DanielleHUD's reticle transform or rule out X scaling.
No blanket aspect correction or gain has been added.

Target checked in Ghidra: `/Prey/PreyDll.dll`, image base `0x180000000`, x64.
Supported binary SHA256:
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
The native UI helpers and renderer findings remain static contracts, not
evidence of when this movie samples them or where its pixels land.

## What changed for the next run

The legacy `aimDirMilli`, `aimRawQMilli`, yaw and `reticleXY` values are read
independently. They can describe different publications, and the old raw Q
omits W. They remain for compatibility; use **one `rp*` group from one report**
for attribution.

The render consumer now carries the selected gameplay context with the ray.
`ReticleFollow` copies the camera once, uses that copy for its existing
projection and publishes one synchronized diagnostic record after dispatch.
It contains full float precision, full XYZW head/controller quaternions, raw
positions, play yaw, tracking sequence/epoch, reference generation, predicted
display time, world origin/direction, the 12-float render camera, tangents,
convergence, raw/clamped coordinates and both dispatch return codes.

`reticleProjection=fresh|stale|unavailable` and `rpAgeNs` identify old or absent
records. `rpStampNs`/`rpIndex` identify duplicate samples. Disabled dispatch is
explicit. The camera identifies the actual projection input; no independently
sampled latest-eye counter is presented as belonging to it. A projection refusal
through the live wrapper clears the record. These records are not automatically
saved every frame: save repeated `report` responses using the existing channel.

The production projection and aim arithmetic are unchanged. There are no new
hooks or native calls. Formatting runs only when a report is requested; the
render path copies a small fixed-size record under the existing snapshot idiom.

## Next capture for the runtime owner

Keep the controller physically supported, keep `aim.bodyyaw 1`, and hold the
same origin mode, convergence, weapon and render/FOV configuration throughout
one short sweep. Save complete `report` lines, including the `rp*` group. Do
not collapse them into rounded X/yaw pairs. A tracking-space pose moving does
not by itself incriminate the runtime: physical motion and tracking noise are
possible. Do not compare across recentres or tracking restarts.

Analyze the saved text offline:

```powershell
python -B tools/re/analyze_aim_projection.py path/to/saved-reports.txt
```

The analyzer deduplicates projection records, excludes unavailable/stale or
context-free records, groups by tracking epoch/reference generation, compares
quaternion rotations modulo sign, recomputes the raw-controller-to-world
direction, and independently reprojects through the recorded camera. It reports
residuals, not an automatic headset verdict.

| Result | Next boundary to examine |
|---|---|
| Raw controller rotation changes | Physical stability, aim pose versus grip pose, tracking and space provenance |
| Raw rotation stable; world direction changes | Same-record play yaw and composition residual; track actual body turns |
| Composition residual near zero, but camera basis/projection residual wrong | Captured camera/frustum integrity and projection implementation |
| Coherent arithmetic correct; visible symbol still slides | DanielleHUD coordinate mapping, native overwrites/animation and which render consumes the update |

For the last case, use three **settled** head poses and corresponding saved eye
images with the controller still supported. A report and a capture command are
not automatically frame-paired; hold each pose while both are obtained or add
frame correlation at capture time. Compare actual symbol pixels with the
normalized `rpXY` coordinates for that eye. An affine scale/offset mismatch
is a movie/canvas lead; disagreement that changes with update timing is a
consumer-order lead. Keep a finite target's depth/parallax separate from both.

The existing run `run-20260908-134458` could not replace this capture. Its
19969-frame tape lasts about 629.84 seconds from 03:45 UTC and ends before the
04:05/04:06 UTC body-yaw/convergence A/B entries in the log. It records views
and submitted layers but no controller-space locations. It therefore cannot
answer whether the raw controller quaternion changed during that experiment.
