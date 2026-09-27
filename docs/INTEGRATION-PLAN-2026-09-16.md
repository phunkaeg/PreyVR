# PreyVR integration branch

Worktree: `D:/Dev Debug/PreyVR-integration`, branch `codex/integration`.
Baseline commit: `5c30857` preserves the 124 changed/untracked files copied from
our development checkout. The original `D:/Dev Debug/PreyVR` checkout and its
uncommitted files were not changed. No remote push or main merge is implied.

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
