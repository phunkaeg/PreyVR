# Headset session static review — 2026-09-08

Reviewed `b8ded45` and the saved artifacts of `run-20260908-143942`. No game
launch, injection, attach, command posting or live capture was performed.
The reported reticle slide and peripheral shrinkage remain wearer observations;
the new evidence below does not claim a headset fix for them.

## 1. The symmetric FOV is the selected policy

`Invoke-PreyVRStartup.ps1` explicitly sends `xr.native 1` before `xr.stereo`.
The saved log confirms native projection enabled at **04:40:08.422Z**, then
stereo armed with IPD 0.064 and half-FOV 50 at **04:40:09.849Z**.

In `CameraEditHook::BuildSyntheticEye`, native mode applies the eye translation,
publishes the unchanged camera tangents and returns **before** synthetic FOV or
asymmetry writes. The function name and old `stereo_armed` log were misleading
when read without that branch. The half-FOV parameter is inactive in this mode.

The ordinary render branch copies the edited camera into the CSystem camera,
then calls `UpdateAimReticleForRender`, then the original renderer. The captured
`rpTans` of +/-1.73205101 horizontally and +/-1.85576892 vertically are expected
for Prey's 120 x 123.363-degree projection at this aspect. Their symmetry does
not establish four individually zero asymmetry fields: symmetric nonzero shifts
can also preserve symmetry.

The last complete saved tape frame independently declares the same symmetric
FOV to OpenXR. The runtime's requested FOV is different and asymmetric: left
eye -54/+40 horizontally, +44/-55 vertically, with horizontal sides mirrored
for the right eye. That difference alone is **not** a broken submission:
the declared FOV must describe the rendered image, not simply copy the runtime's
preferred FOV. The wide native image can contain unused peripheral pixels; that
is a coverage/efficiency issue, not proof of reticle/world separation.

Added `projectionPolicy=prey_native|synthetic` to `report` and the stereo-arm
log. It is a configuration reading, separate from the coherent `rpTans` record.
Corrected the startup comment. No FOV policy or rendering settings were changed.

## 2. Peripheral shrinkage does not establish the HUD surface

For a constant-pixel sprite in a rectilinear image, a horizontal coordinate maps
to viewing angle as `theta = atan((2*u-1)*tan(HFOV/2))`. Consequently a small
constant width `du` has angular width proportional to `cos(theta)^2`.
At the horizontal midline its small vertical angular size scales as `cos(theta)`.

| Horizontal bearing | Relative angular width | Relative angular height |
|---|---:|---:|
| 0 degrees | 1.00 | 1.00 |
| 30 degrees | 0.75 | 0.866 |
| 45 degrees | 0.50 | 0.707 |
| 60 degrees | 0.25 | 0.50 |

This needs no extra HUD geometry. A frontoparallel plane at fixed camera Z also
has constant pixel scale (`x_pixel = fx*X/Z`), despite the larger slant distance
to its edges. Slant distance alone is not the pinhole projection denominator.
The 60-degree example is illustrative; the saved runtime eye FOV does not expose
the full +/-60-degree horizontal native render region.

Thus apparent shrinkage can be normal angular behavior of a baked-in sprite.
It neither proves a cylinder nor proves an incorrect plane/FOV mapping.
Likewise, many wrong mappings preserve the centre; centre agreement is useful
but not a unique discriminator. FOV or HUD canvas scale mismatch **remains a
candidate for the sliding**, pending actual pixel placement evidence.

## 3. One retained live projection record passes the numerical checks

The saved `results.txt` contains one coherent record, `rpIndex=3638`,
`rpSeq=5313`, epoch 3, reference 2. The offline analyzer finds:

- controller-to-world direction residual: **0.00000460 degrees**;
- projection residual: **0.00002794 milli** of viewport fraction;
- camera basis orthonormal error: **9.58e-8**;
- no clamp and both HUD dispatch return codes zero.

This verifies this sample's arithmetic. **One record cannot measure drift or
constant play yaw over a sweep.** It also does not locate the symbol's pixels.
The earlier 26.1-degree sweep must not be treated as a time series of these new
coherent records. Requested `report` output now also persists as `preyvr_report`
lines in `PreyVR.log`; previously each command replaced `results.txt`.

The tape snapshot has 12380 complete views/begin/end records, 12381 waits and
one incomplete final JSON record. It has no controller-space location records.
Complete records remain usable; no failure conclusion is drawn from its partial
tail. The saved report and the last tape frame are not presented as one frame.
Receipt: `docs/evidence/headset-session-static-2026-09-08.json`.

## 4. The right stick has a second mod-owned producer

Startup with `-Controls` leaves **both** these paths enabled:

```text
right.thumbstickX -> MoveLane -> xi_thumbrx (0x216)
right.thumbstickX/Y -> MenuNavigator -> D-pad taps (0x200..0x203)
```

`XrInput.cpp` feeds `MenuNavigator` whenever `gMenuNavigation` is true. There is
no active-menu predicate. At +/-0.6 horizontal deflection, the navigator emits
left/right; `InputEvent::BuildMenuTap` encodes `xi_dpad_left`/`xi_dpad_right`
press and release. A held stick repeats after 0.45 s and then every 0.16 s.
These are real gamepad events and reach the same engine input route as buttons.

Therefore `move.turn 0` cannot establish "not us", and `move.turnscale 40` does
not scale the menu producer. The extra input path is proved in source. Its final
native weapon-selection binding is **not** established by this review.

Runtime-owner isolation: in gameplay, keep turning on and set `menu.nav 0`;
allow queued taps to drain, then move the right stick. Compare weapon selection
and attach-generation changes with navigation on/off. Restore navigation for
menus. This is a temporary diagnostic, not a distribution-ready routing policy.

The permanent route needs a target-proven active input/menu context and a single
owner for right-stick input. A player merely existing is insufficient because
pause/inventory menus can exist in a loaded level. The PDB-derived
`ArkPlayerInput::m_modeStack`/`Mode::menu` is a static lead, not a verified offset
to ship. No guessed UI predicate or global key suppression was installed.

## 5. Fixed: IK loses automatic recovery during repeated rebinds

The existing `CalibrationState` already queued automatic recalibration after a
completed calibration and an owner/reference/epoch change. The handover missed
that path. But it contained a reproducible loss:

```text
calibrated=1
first Bind -> calibrated=0, autoPending=1
second Bind before recovery -> calibrated=0, autoPending=0
all later Tick calls -> Pending(right) remains false
```

The second Bind replaced `autoPending` with the now-empty `calibrated` mask.
Fixed it to retain `calibrated | autoPending`, restarting the wait for the newest
binding. Old offsets remain invalid and never-completed manual requests still
drop. An explicit rig-signature invalidation clears recovery and requests too.

The regression **failed before the fix** at "second re-equip retains recovery
only for previously calibrated hand". It now checks rapid re-equips, reference/
epoch changes during recovery, reset of the settling interval, exclusion of the
never-calibrated hand and explicit invalidation.

This fixes a real route to permanent position-only tracking; it does not prove
that every observed rotation failure followed it. Also, equip generation bumps
on every successful observed `AttachToHand`, not only distinct weapon selections.
The existing wait counts 90 matched owner callbacks, not guaranteed elapsed time
or measured completion of an equip animation; automatic calibration quality
still needs a headset check. The fix preserves that existing policy.

## 6. Other findings and priority

- `r_DrawNearFoV` startup assignment is one-shot. A level-load reset is not
  repaired by that script. The reported A/B only establishes that this cvar
  does not change the reticle's **world-camera** tangents. It does not eliminate
  every possible near-pass influence on apparent barrel/reticle alignment.
- Reach below 100 compresses position travel. It can reduce clamping, but does
  not retain one-to-one hand placement. Resolve shoulder/model placement and
  goal convention before treating a percentage as a geometry fix.
- Separate target joints named `*_spine_target` do not prove a shared solved
  limb or explain crosstalk. Compare per-hand goals, selected owner and native
  limb chains. The reverse one-hand test proposed in the handover is useful.
- Elbow flipping is a candidate; clicks can also result from clamping, rejected
  writes or calibration transitions. No solver-fault conclusion is justified yet.

First isolate menu-generated D-pad taps; this removes a known source of unwanted
input and a confound for calibration testing. Then retest rotation recovery.
For the reticle, retain a supported-controller sweep and settled per-eye images;
compare actual sprite pixel centre/size against `rpXY` before changing FOV gain
or adding a cylinder correction. The existing analyzer reads `PreyVR.log` now.

## Build and verification

Release build succeeded; **28/28 standard CTests and 4/4 integration replays**
pass. PowerShell startup change is a comment only. No native ABI or offsets were
added. Target preflight remains `/Prey/PreyDll.dll`, x64, image base
`0x180000000`, supported SHA256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.

New DLL: `build/h021-integration-audit/Release/PreyVR.dll`, **775168 bytes**,
SHA256 `0d934d8ed6141f42ef1259e406bf4e93223cce106b42be93de64f6315d1644fb`.
The configure/verify/headless artifacts and running process were not replaced.
