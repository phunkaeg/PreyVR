# PreyVR integration branch

Canonical shared development branch: **`collab/integration`**. Base new feature
branches on it and target pull requests back to it. `main` remains the release
baseline, gated on feature parity and headset acceptance.

The September 27 consolidation brings the comfort-controls ancestry from
`codex/integration` into the shared branch. The existing feature branches are
retained as history; they are not competing integration targets. See
[consolidation and static validation](INTEGRATION-CONSOLIDATION-2026-09-27.md).

Historical baseline: `5c30857` preserved the 124 changed/untracked files copied
from the development checkout on September 16. `b190b1e` is the shared baseline
before consolidation; `e8451e5` contains the comfort work merged through
`99c5474`. No donor source is introduced by this consolidation.

## Acceptance gates

1. Correctness: fail closed on missing rendering metadata; preserve the pose,
   source time and projection with each rendered eye; read back startup CVars.
2. Adopt comfort policy through our interfaces: snap turn, head-relative movement,
   physical crouch without duplicate camera lowering. Test modal input ownership.
3. Map the selected donor gameplay functions, prioritising psi, projectile/tracer/
   beam consumers and native weapon-wheel selection. Enforce local-player scope.
4. Preserve native weapon rig, two-handed aim, HUD/menu/inventory capture and
   controller interaction. Keep optional inventory stereo explicitly experimental.
5. Validate a clean launch, load, play, equipment changes, inventory, recenter,
   tracking loss and shutdown under xr-sim, then in a headset with Jordi/user.
6. Merge into main only after agreed feature parity and headset acceptance.

Use small feature branches from integration and review each change back into it.
Keep our render pipeline unless a separately measured replacement wins. Obtain
Jordi's source-reuse terms before copying source; no donor source is included in
this correctness batch. A function address map is not proof of object layout or ABI.

## Evidence boundaries

Build/tests prove portable contracts and supported binary landmarks. Native
xr-sim validates exercised engine and submission behaviour. Neither proves
headset comfort, visual stereo correctness or complete feature parity.

## Status — 19 September

Snap turning (default 45 degrees), head-relative movement, optional floor reference
selection and separate height calibration are implemented. See
[comfort controls and validation](COMFORT-CONTROLS-2026-09-19.md).
Physical crouch, avatar-height policy and vignette remain future work; having a
floor does not implement those features automatically. STAGE was exercised in-game;
headset, LOCAL_FLOOR and runtime reference-change acceptance remain open.

Main requires pull requests (including administrators), with zero mandatory
external approvals and force pushes/deletion disabled. Keep feature work on
integration until the headset/parity gates above are met.
