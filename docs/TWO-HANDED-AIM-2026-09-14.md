# Two-handed aim development build

Implemented in the current working tree. Release DLL SHA-256:
`BFCDBB272FFF37663233F5631D1741EEE386FAFF2CF01584E74C6E8D47FA01E4`.
This development build includes the pre-existing experimental inventory work;
it is not a new GitHub release or a claim of complete headset acceptance.

## Player controls

Bring the left controller to the visible foregrip of a supported long weapon,
then squeeze the left grip. Keep squeezing to aim with both hands. Release to
blend back to one-handed aiming. The right hand keeps ownership and fires.
Squeezing in empty air does not grab; release before trying again.

Both grips alone no longer recenter. Hold both grips and press left Y to
recenter, or use F12. Paired squeezes suppress use/reload until the right grip
is released. The recenter Y press is consumed by the menu navigator until release.
The left grip still changes tabs in menus. Menu entry, tracking loss, recenter,
weapon changes and long frame gaps invalidate a support hold and require release.

`aim.twohand` reports enablement, hold state, region availability, applied update
count, native support location, gate status, squeeze value and measured local
hand position. `aim.twohand 0` disables; `aim.twohand 1` enables (default).

## Research and integration

The existing `MotionController::TwoHandedWeaponPose` was unused. Its fixed
world-up reference refuses vertical holds, and it puts the origin at the front
hand. Connecting it directly would change the primary pivot and leave aim and
rig consumers independent. The new pure solver keeps the primary wrist anchor
and uses a shortest rotation between the authored support vector and the tracked
hand vector, preserving primary roll without a world-up singularity.

Fleet prior art inspected:

- `D:/Dev Debug/Other VR Mods/fear-vr/src/common/two_handed_grip.h`:
  proximity acquisition, squeeze hysteresis, a latched hold and minimum steering
  separation. Its source distinguishes raw steering from displayed hand placement.
- `D:/Dev Debug/VR Modding/docs/02-viewmodels-and-hands.md`, sections
  "The pose you draw and the pose you steer with are different variables" and
  "Two-handed support, and the socket that grew into a region", and HAND-017 in
  `docs/pattern-catalog.md`: one owner, capsule acquisition, release blending.
  The shock2quest account is a fleet source lead, not a newly reproduced test here.

The fleet graph query did not complete and was stopped. Scoped fleet references
and actual FEAR source supplied the useful evidence instead. No donor code was
copied; the implementation uses Prey's existing math and alignment contracts.

Native geometry comes from the already verified selected-equipment/character
owner and `WeaponRigAlignment::ReadBasis`. Before changing the native ADIK targets,
read both wrist positions and express their separation in the native barrel basis:

    nativeBarrel = nativeRightWrist * weaponInWrist * barrelInWeapon
    supportLocal = inverse(nativeBarrel) * (nativeLeft - nativeRight) * modelScale

An 8 cm segment along the barrel surrounds this native support location, with a
10 cm acquisition radius. Require native forward separation of 16–70 cm and
bounded lateral/vertical displacement; explicitly exclude the wrench. These are
presentation policy limits, not target enum values or a complete weapon census.
They intentionally refuse short/unsupported poses rather than invent a foregrip.
The GLOO measurement was approximately (-143,392,-16) mm in this frame.

The owned right-wrist write publishes geometry plus its visual displacement from
the raw primary controller. This accounts for existing 65% arm reach compression.
Only a fresh, matching owner/reference/tracking epoch can acquire. A young geometry
sample survives a snapshot try-lock miss; publication contention is not tracking
loss. Once held, the selected point is fixed until release.

`AimTakeover` solves once against the coherent head/both-hands tracking publication
and publishes the answer in `GameplayPoseFrame`. Cached native firing direction,
reticle sample and aligned right wrist consume this same orientation. Calibrated
muzzle offsets receive the same angular correction. The displayed support target
is relative to the successfully written primary target, avoiding independent
reach compression pulling the support hand off the weapon. Native arm reach
clamping still applies. Displayed hand positions never feed back into steering.

Squeeze thresholds are 0.65 to acquire and below 0.45 to release. A fresh press is
required. Hand separation below 12 cm releases immediately. Entry/exit use a 40 ms
exponential time constant; steady held steering is direct. No native function,
vtable slot or engine offset was added by this feature.

## Validation

All 40 CTest tests passed. New cases exercise acquisition regions, failed-grab
rearming, release blend, squeeze hysteresis, owner/reference/session changes,
tracking/modal loss, near-coincident hands, vertical/180-degree aiming, non-finite
input, frame gaps, reach displacement and the real wrist-to-barrel composition.

Native run: `build/twohand-live/runs/run-20260914-075011`, owned PID 38828.
Steam target SHA-256 `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
Process-scoped xr-sim, Quest 3 FOV, 63 mm IPD, 1344x1440 backbuffer, Talos Lobby.
The process was closed after clearing simulated inputs and disabling VR.

The positive control held the right controller at XR (0.18,1.30,-0.35), orientation
identity, with head (0,1.65,0). Left position (0.034,1.325,-0.561) was inside the
measured foregrip region. Moving only the left controller to (0.20,1.40,-0.56)
changed the native published direction from approximately
(-0.95393,0.30002,-0.00085) to (-0.74167,0.64968,0.16686), while raw primary
orientation remained identity. Captured images show both gun and support hand
turning. Release returned direction to (-0.95367,0.30085,0).

One uninterrupted hold accumulated 8,345 solver updates. The moved hand was
outside the acquisition region (`near=0`) while still held, confirming the latch.
Empty-air squeeze was refused, and returning near while squeezed did not acquire.
Inventory entry cancelled support; subsequent equipment generations did not inherit
it. Native wrench selection (generation 6) retained normal wrist alignment, reported
no support region, and refused a squeeze. Final alignment report: 39,106 applied,
zero refused. No projectile was deliberately fired in this test.

## Simulator limitation and acceptance limits

The installed xr-sim reports one cached value for a shared action regardless of
`XrActionStateGetInfo.subactionPath`. Its `xrsim_actions.cpp` float getter returns
`a->syncedFloat`; the selected binding for the shared squeeze action is the right
hand. A left-only simulator squeeze was visibly 1 in state.json but Prey's input
was 0. Driving the right simulator squeeze delivered 1 to the requested left
action and enabled the positive control. The same action/subaction pattern exists
for other input types, so this is not proof of independent left/right input in
the simulator. Prey's standard per-hand OpenXR binding/query path is unchanged.
Do not change the product to accommodate this harness defect.

The simulator also starts with an aim trim; explicitly set `hand r aimtrim 0 0`
for identity-axis comparisons. Do not infer aim orientation from grip-only state.

Evidence proves a bounded GLOO implementation, not all weapons, finger fit,
headset comfort, projectile impact alignment, physical controller isolation or
complete renderer quality. The captured baseline has a horizontal rendering band
also visible before support acquisition; its cause was not investigated here.
Some immediate transition captures precede the settled menu/world. The recorder
was capped at 20,000 frames; later observations use identified xr-sim captures
and mod reports, not claimed tape coverage. Shotgun and Q-Beam coverage remains
unverified; they were not present/selected in this bounded test.

Next headset check: acquire/release GLOO naturally, try shotgun/Q-Beam when
available, aim vertically, change weapons while squeezing, and verify the explicit
recenter chord. Report `aim.twohand` and `ik.align` for any unsupported weapon.
