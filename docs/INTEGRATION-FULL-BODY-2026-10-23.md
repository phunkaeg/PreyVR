# Full-body integration — 23 October 2026

Integrated Jordi's body work after fixing the issues in
[the full-body audit](REVIEW-FULL-BODY-2026-10-23.md). Upstream body source was
`16a69ed3d9fa98803ce9562953122335bcd699d9`; the separate fix commit is
`55bc75b7e5d8199aacc77c683737e6c3611f82e1`. Integration started from
`b298f97bc62d902a715fbbd25871ef2cbb8b96bd`.

## Corrections

- Crouch now binds to player, tracking epoch, room reference and a VR enable
  generation. Standing-eye compensation needs a stable native standing baseline
  before acquisition. A replacement player cannot inherit a stand request or
  camera lift. Tracking loss and disabled view driving invalidate the state.
- Physical crouch requests are single, expiring, atomic consumes, validated again
  against the current player, gameplay pose, tracking epoch and room reference
  before the input drain uses them. Menus/cinematics discard requests. A disabled
  option can request cleanup only for the still-bound player; native acceptance
  is not inferred from posting. Manual crouch remains available.
- Ordinary recenter on the same player/session retains compensation for an
  acknowledged native crouch, while invalidating queued input and motion history.
  That distinction prevents a second native camera-height drop on recenter.
- Torso steering publishes one synchronized sample containing yaw, player,
  tracking epoch, reference, weapon generation and timestamp. The consumer checks
  its own input context and the current gameplay publication. Estimator and elbow
  history reset on context changes or long gaps. Missing torso data releases
  body-relative movement instead of silently using an unrelated direction.
- Body rig, feet, blend and prior location reset on owner/context changes and
  toggles. Long animation gaps and horizontal jumps over 30 cm free stale feet.
  The pure leg solver clamps the actual endpoint to reachable length, keeps the
  shin and foot connected, and refuses degenerate legs.

## Merge decisions

Kept integration's WORLD tab, saved opt-outs, 64-bit settings mailbox, throw
lifecycle, FOV remapping and status-card correction. Added body Physical Crouch
and Head in Walls controls to VIEW. A new persisted **Body Posing** row allows
turning the torso/leg solver off while retaining controller-driven arms.
The WORLD row labels are shortened to fit beside their values; experimental
status remains explicit in the help text. The inherited native camera-joint exclusions and body/arm application
order are preserved. Simulator fake focus stays an explicit process option;
normal player launch uses the actual selected OpenXR runtime.

## Verification boundary

The separate body branch built and passed **72/72 offline checks**, including
two new production-adapter tests and expanded pure leg tests. The combined
integration suite passed **76/76 checks**. Its logs and exact package identity are recorded in
`evidence/full-body-integration-2026-10-23/` after the final build.

The boundary tests cover changed player/epoch/reference, stale/blocked crouch
input, disabled view/pose/feet/drive, long callback gaps, same-skeleton ownership
replacement, rebased torso coordinates, reachable and unreachable leg targets,
and settings above bit 31. They substitute native/XR boundaries and do not launch
Prey. They do not establish native event acceptance, headset appearance or speed.

Known prototype limits remain: shadows use the unmodified second character;
stick walking uses native animation; floor-edge probing still has the disclosed
mat/corpse-edge limitation. This is a development test build, not a numbered
public release. See [the short headset checklist](HEADSET-FULL-BODY-2026-10-23.md).
