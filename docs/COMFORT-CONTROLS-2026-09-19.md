# Comfort controls integration — 19 September 2026

## Implementation

The integration base now calls the existing SnapTurn math from the gameplay input
path. Default: 45 degrees, horizontal stick engage >0.65, neutral <0.30 on both
axes. A held stick turns once. Activation, menus, tracking loss and mode changes
require neutral before another snap. Smooth turning remains available with
`move.snap 0`; `move.turn 0` disables turning.

Turning changes the shared tracking reference used by view, aim and rig ownership.
The reference origin rotates around the current tracked head position, preserving
world head position even when leaning away from the calibrated origin. The native
body yaw is not directly rewritten. Movement uses head yaw relative to this same
reference, so forward follows the resulting view after a snap.

`move.headrelative 1` is enabled by default. `move.headrelative 0` restores native
body-axis inputs. Directional engine speed modifiers are preserved by default.
`move.scales <strafe-percent> <backward-percent>` permits explicit compensation;
100/100 is the default. Do not assume the donor's 80/50 values are authoritative
for every Steam player state. This is not a uniform-speed FSM hook and does not
claim to remove native sprint/backward/stance speed differences.

OpenXR selection prefers LOCAL_FLOOR (core 1.1 or enabled EXT_local_floor), then
STAGE, then LOCAL, using advertised reference spaces and checking creation results.
If the initial floor space cannot locate views but LOCAL can, startup switches to
LOCAL. `PREYVR_REFERENCE_SPACE=local` forces LOCAL before launch. All views,
controller locations and composition layers use the selected space.

Runtime reference-space change events are deferred until their changeTime. A valid
gravity-aligned transform rebases the reference to preserve world position and
yaw; an invalid transform requests calibration from fresh tracking. Held eye images,
head fallback samples, tracking snapshots and panel anchors are invalidated.

`view.recenter` / `vr.recenter` / F12 / both grips + left Y reset horizontal origin
and facing while preserving height. `view.calibrate` explicitly captures a new
standing or seated height. `view.height` reports selected space, reference height,
head height and relative height. LOCAL values are relative coordinates, not proof
of floor height. No physical crouch action, avatar-height matching or vignette is
added by this batch.

The reference-space contract follows the [OpenXR event definition](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrEventDataReferenceSpaceChangePending.html).

## Validation

Release build passed, 42/42 CTest tests passed. New tests cover hysteresis,
modal/tracking re-arming, head-relative axes, compensated direction, bounded stick
magnitude, both snap directions, head-pivot invariance, height preservation and
runtime-space rebase invariance.

Native run `run-20260919-115603`, PID 34812, loaded Talos Lobby with process-scoped
xr-sim/xr-tape. DLL SHA-256:
`32ad102ccd96ff08a0c8fb67d10d69bbf31004332014359e1ac5a747d8915ca2`.
STAGE selected and VR became active. A held right stick logged one +45-degree snap;
a new left deflection logged -45. World captures show gameplay after both.
Height 1.6 -> 1.1 metres plus ordinary recenter retained referenceHeight=1.6 and
relativeHeight=-0.5. Explicit calibration changed referenceHeight to 1.1.
At head yaw +90 degrees, forward stick 0.7 produced native moveAxisMilli=-647,0
after the radial deadzone, with no dropped posts. Inputs were cleared and the
owned process closed without saving gameplay.

### Simulator limitation

The current xr-sim source stores one `SimAction::syncedVector`, and its vector
getter does not select by subactionPath. PreyVR correctly uses one action with
left/right subaction paths. Left-only simulated input was not returned to the mod;
matching values on both sticks exercised the movement transform. Thus this run
proves native dispatch and the transform, not independent hand bindings. Snap
input can also appear on the movement stick in this simulator. Do not use this
run to claim turn-induced translation is absent; the pure pivot test covers the
geometry, and headset testing must cover actual independent controller input.

The game initially remained in its pause menu. Native backbuffer capture identified
Resume; accepting it enabled gameplay. The simulator menu composition capture was
mostly black despite a readable native menu. Menu visual quality remains outside
this batch's acceptance; gameplay captures succeeded.

## Headset acceptance still required

- Right/left snap direction, one turn per deflection, no body translation while
  leaning; weapon/reticle remain aligned through repeated turns.
- Independent left-stick movement with head turned, snap/smooth switching, menus,
  inventory and weapon-wheel ownership, loss/recovery of controller tracking.
- Quest 3 / Virtual Desktop startup in available floor space and forced LOCAL;
  runtime recenter while standing/crouching, in-game height calibration.
- LOCAL_FLOOR selection and an advertised-but-unlocatable STAGE fallback need
  runtime coverage beyond this xr-sim build.

## Collaboration

Work is isolated from main on the integration branch via a feature branch. Main
now requires pull requests, including administrators; force pushes/deletion are
disabled. Required external approvals are zero so the owner can still merge while
the collaborator invitation is pending. No release asset is published by this batch.

## Final run accounting

The trace contains 17,986 xrEndFrame records, including 11,292 projection frames, with 0 xrEndFrame errors. Frozen logs, captures, manifest, trace and test results are in `docs/evidence/comfort-controls-20260919/`.

A forced-LOCAL second launch (PID 91192) was not injected: Windows Defender had
quarantined the injector after the first run (ThreatID 2147749377). No exclusions
or Defender changes were made. The unmodded process was closed. LOCAL runtime
fallback and runtime reference-change delivery are not claimed as live-tested.

Final DLL SHA-256: `60640dc57ee89b07edc7cd06ab517864337645bca97ca442efa0a742e9713852`. After the native run, final review added generation checks around simultaneous recenter commands, moved old-space input invalidation inside the reference update, and made a busy height-reference read refuse instead of silently recalibrating. The final binary rebuilt and passed 42/42 tests; it was not reinjected because the injector was quarantined.
