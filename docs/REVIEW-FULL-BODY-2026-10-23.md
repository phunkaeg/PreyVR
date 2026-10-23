# Full-body review — 23 October 2026

Historical review of the upstream branch. The findings below are addressed by
[the subsequent integration fixes](INTEGRATION-FULL-BODY-2026-10-23.md); the
original measurements remain unchanged for provenance.

Jordi's full-body implementation is worth integrating, after fixing its state
lifetime boundaries. It is a substantial body solver, not just a visible mesh.
It adds torso estimation, leaning, native physical crouching, spine and leg
posing, planted feet and body-relative movement. Keep it separate until the
three issues below are addressed.

## Scope and evidence

- Reviewed `origin/jordi/robustness-body` at
  `16a69ed3d9fa98803ce9562953122335bcd699d9`; body feature commit `8de6574`.
- Compared with current integration
  `b298f97bc62d902a715fbbd25871ef2cbb8b96bd` and the shared robustness baseline.
  Already-fixed common robustness defects are not new body findings.
- Fresh x64 Release DLL builds; the branch's existing **70/70 tests pass**.
  Additional boundary probes reproduce the failures below. Those probes include
  actual production adapters with native/XR calls stubbed, rather than rewriting
  the algorithms in test code.
- This review uses source and synthetic fixtures only. No game or headset run;
  existing tests passing does not establish gameplay acceptance.
- Reproduction, frozen logs, numbered source excerpts, hashes and exact probe
  output: [evidence](evidence/body-audit-2026-10-23/REPRODUCE.md).

## Findings

### P1 — Physical crouch ownership survives player and session changes

`src/dll/BodyFollow.cpp:319–360, 583–603, 783–791`.

The new `gCrouchOwned`, standing-eye baseline, eye stability and compensation
are process-global. They have no player/session/reference binding or lifecycle
reset. UpdateCrouch applies old ownership to whichever player it is passed.
Disabling the option changes a flag but does not invalidate ownership or lift.

The production-adapter probe first proves ordinary crouch acquisition and
compensation work. Replacing actor A with crouched actor B then produces an
inherited stand-up tap. After feeding a valid new standing eye height of 1.5 m
against the old 1.7 m baseline for 120 samples, ownership remains set and the
camera lift remains **0.20 m**, even with physical crouch disabled. That is a
fixture showing the stale-state failure; it is not a measured game camera bug.

There is also an initialisation hole: crouch can acquire before a standing eye
baseline exists. Baseline learning requires `!gCrouchOwned`, so it cannot learn
while that crouch is owned; the probe gets zero eye lift throughout it.

**Fix:** bind state and queued crouch requests to the current player/session
context; invalidate on replacement, tracking loss, session shutdown and relevant
reference changes. Release an owned native crouch only against its original
valid receiver, otherwise discard ownership. Clear camera compensation safely.
Require a valid stable standing baseline before acquiring physical crouch.
Test disabled/blocked input and failed native acceptance separately.

### P1 — Torso steering publishes age, but no tracking-space identity

`src/dll/AnimIkTakeover.cpp:694–697, 957–986, 1975–1981, 2532–2537`;
consumer `src/dll/MoveLane.cpp:350` onwards.

The existing calibration Bind resets arm calibration for owner, reference and
tracking-epoch changes. It does not reset the new torso estimator or invalidate
its published yaw. TorsoYawNow accepts any sample younger than 200 ms without
checking its owner, epoch or reference. MoveLane validates its own reference
sample, which does not validate the separately cached torso sample.

In a synthetic 90-degree tracking-coordinate rebase, the unchanged production
estimator returns **20 degrees**, while the Reset control returns **90 degrees**
in the new coordinates. Mixing the old filter basis with the new room basis can
temporarily steer body-relative movement in the wrong direction. Ordinary
head turning should retain smoothing; a coordinate rebase must not. The fixture
does not show that every ordinary recenter causes this 90-degree case.

**Fix:** publish yaw together with its owner, epoch, reference and validity;
read it coherently and require a matching current context. Reset or explicitly
transform estimator history when its input coordinate basis changes. Invalidate
on unavailable owner/tracking/session; test a rebase separately from a physical
turn and ordinary room-reference yaw adjustment.

### P2 — Planted feet retain world positions across ownership discontinuities

`src/dll/AnimIkTakeover.cpp:763–773, 919–940, 1975–1981`;
`src/common/BodyPose.cpp:108` onwards.

The new FootPlanter and previous location are also process-global and excluded
from calibration resets. Feet are freed during stick movement, large vertical
velocity and blending. The planter itself resets beyond a one-metre displacement,
but a smaller horizontal discontinuity can retain unreachable old feet.

The actual PlanBody probe changes owner/epoch and shifts the body and matching
anchor by **0.9 m**. Both plans report valid, but the shin's foot endpoint and
the independently moved foot disagree by **0.388378 m** on the first step frame.
Resetting the planter reduces that gap to approximately **0.00000003 m**.
The pure solver fixture reproduces it independently. This is an adversarial
valid input, not a claim of a 39 cm gap during normal walking.

**Fix:** invalidate planter, location history, pose blending and affected rig
state on ownership/session/reference discontinuities. Add a reachability guard:
a finite valid plan must keep the thigh/knee/shin/foot chain closed even when
the planted target cannot be reached. Test sub-metre teleports, same-skeleton
player replacement, re-enable and long frame gaps.

## What is good and should be preserved

- Camera independence is deliberate: BuildBodyRig pins `camera_jnt`, `look_jnt`
  and `hud_jnt` to the unchanged group instead of inheriting body movement.
  This is source evidence; game-level camera invariance was not rerun here.
- Ownership gating is retained. Body planning runs before the native ADIK pass;
  body application and arm solving preserve their required order. Hand rotation
  compensation prevents simply inheriting the torso's extra rotation.
- The pure solver and planter are separated from engine memory, with real unit
  tests. Torso voting, crouch hysteresis, leaning and blending are sensible
  foundations. The issues are primarily missing context invalidation.
- Physical crouch and wall darkening have options. Body/feet master switches
  are currently commands; expose a clear body toggle before general release.

## Limits and integration work

Jordi's checked-in report names **FunkMockXR** and records actual-game mock
measurements, including camera invariance, body following and crouch. Those are
useful author-supplied results, distinct from this review's synthetic probes and
from headset acceptance. The report's harness scripts/screensheets are described
under ignored `build/jordi-mock`; they are not a fully reproducible checked-in
test suite in this checkout.

Documented remaining limitations are real: the shadow still uses the unmodified
second character instance; stick walking uses native animation rather than the
planter; the floor rule can refuse mat/corpse edges and once produced a 10.8 cm
step near furniture. Reported fast-hand error/lag exists with body posing both
enabled and disabled; this review does not establish its root cause or fix it.
No performance comparison or headset comfort approval was performed.

A non-mutating merge preview against current integration finds four conflicts:
PLAYER-GUIDE.md, VrOptions.h, VrOptions.cpp and VrOptionsRuntime.cpp. Resolve the
options deliberately: retain integration's WORLD settings, persistence and
64-bit dirty mailbox while adding the body rows. Preserve the already-shipped
throw lifecycle, FOV remapping and status-card fixes. Textual auto-merging of
the other files is not evidence that their lifecycle contracts compose.

Recommended sequence:

1. Fix the three state boundaries on the separate body branch and add production
   adapter regression assertions for the reproduced cases.
2. Resolve integration conflicts, retain common fixes and run the combined suite.
3. Perform a short game check for crouch/recenter/player replacement, then headset
   acceptance for camera stability, body/weapon alignment, feet, shadows and feel.

No body source was changed, merged, deployed or pushed by this investigation.
