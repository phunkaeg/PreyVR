# Handover — the right controller drags the left hand

For Codex. Wearer-observed, reproduced repeatedly on 2026-09-09 against
`PreyVR.dll` 800,768 bytes (SHA-256 `FE8C93CC…7277AC`), Quest 3 / VDXR,
run `run-20260909-134810`.

## The symptom, in the wearer's words

> "The left hand is tracking and rotating AS expected, and the RIGHT hand is
> tracking and rotating as expected HOWEVER, for whatever reason, the position of
> the right motion controller, or right weapon mesh is dragging the left hand
> (and therefore arm rig) with it."

**It is additive.** Both hands follow their own controllers correctly; the left
hand is *additionally* carried by the right controller. This is not a swapped
mapping and not a mix-up of hands.

## Facts, all wearer-confirmed unless marked

| Observation | Consequence |
| --- | --- |
| Drag disappears when the left hand's motion control is **disabled** | **It requires our write.** The native rig does not do this on its own. |
| A one-handed weapon (wrench) shows it too | Not the two-handed foregrip, not the weapon mesh's authored left-hand grip. |
| `aim.enable 0` → drag **stops** | The aim takeover is in the path. |
| `aim.bodyyaw 0` → drag **remains** | Body yaw is *not* the path. This one refutes the obvious theory. |
| Fixed goal `ik.test 0 0 300`, no controller in the loop, left chain only | Raised the **correct** (left) hand. Chain, limb indices and target joint are right. |
| `ik.hands 2`: `ikWrittenR +0` while drag persists | The right IK target (joint 38) is never written. Not target-to-target bleed. |

Measured state during the final reproduction, so nothing below is from a
degraded configuration: `ikWrittenR +420` / `ikWrittenL +420` (both driven
equally), `ikNoPose +0`, `ikClamped +0`, `ikCalR=1 ikCalL=1`, `ikReach=65`,
both controllers located (`xrLocatedL`/`xrLocatedR` both +410).

## The suspect: a shared yaw, contaminated by aim

`AnimIkTakeover.cpp:358` computes **both** hands' goals through:

```cpp
const Pose world = animik::ControllerWorldFromHead(frame.yaw, frame.nativeEye,
                                                  frame.tracking.head, state.gripPose);
goal = animik::WorldToModel(location, world.position);
```

`frame` is a `GameplayPoseFrame` published by **the aim lane**
(`AimTakeover.h:37`), and its `yaw` is built from the game camera
(`AimTakeover.cpp:158/177/188`):

```cpp
frame.yaw = frame.cameraYaw - frame.referenceYaw;   // aim.bodyyaw 0
frame.yaw = frame.cameraYaw - *headYaw;             // aim.bodyyaw 1
```

`ControllerWorldFromHead` rotates the controller's **head-relative offset** by
that yaw. So if `cameraYaw` moves when the right controller moves — which is
what the aim takeover exists to cause — then *every* hand's world position is
rotated by a quantity the right controller controls, and the left hand swings.

**This explains the `aim.bodyyaw 0` result that killed the earlier theory.**
Both branches subtract something different from `cameraYaw`, but `cameraYaw` is
in both. Toggling body yaw changes *which* reference is removed; it never removes
the camera's own rotation. That is exactly the observed behaviour: the toggle
changed nothing, while disabling aim entirely stopped it.

It also explains why the right hand looks correct — it is the source of the aim,
so rotating its offset by a yaw derived from its own aim is self-consistent.

**This mechanism is inferred, not measured.** It is consistent with every row in
the table above, and no competing explanation survives them, but nobody has
logged `cameraYaw` against right-controller motion.

## The measurement that would confirm or kill it

Publish `frame.yaw`, `frame.cameraYaw`, `frame.headYaw` and `frame.referenceYaw`
in the report. With `aim.enable 1`, the head still and **only the right
controller moving**:

- `cameraYaw` tracks the right controller → confirmed, and the fix belongs in
  what `yaw` is built from.
- `cameraYaw` static while the left hand still drags → the contamination is
  elsewhere in the goal, and `nativeEye` is the next candidate (a shared eye
  position moving with aim would translate both hands rather than rotate them —
  distinguishable by whether the left hand swings on an arc or slides).

## Why this is not the previously recorded candidate

`HANDOVER-HEADSET-SESSION-2026-09-08.md` §4 proposed that targets 38 and 39
(`r_hand_spine_target` / `l_hand_spine_target`) are both spine-attached, so
driving one pulls the other's chain. **That is refuted:** with `ik.hands 2` the
right target is never written and the drag persists. The prescribed clean test
in that document — drive only the left, move the right — has now been run, and
its answer eliminated its own hypothesis.

## Process note, for whoever picks this up

This took four sessions partly because the diagnosis kept restarting. Two
specific traps:

1. **Read `HANDOVER-HEADSET-SESSION-2026-09-08.md` §4 first.** The crosstalk was
   recorded there as open, with the correct next test already specified. It was
   re-derived from scratch instead.
2. **A degraded configuration produces confident nonsense.** Several rounds were
   run while the left hand was uncalibrated (no rotation written), clamping on
   ~90% of frames (`ik.reach 100`), or with the left controller briefly
   un-located. Check `ikCalR/ikCalL`, `ikClamped`, `ikNoPose` and
   `xrLocatedL/xrLocatedR` **before** interpreting any hand behaviour. All four
   are already in `report`.

Do not treat a wearer's repeated, specific description as suspect because a
counter disagrees; check whether the counter is measuring the thing you think it
is. That mistake was made here and cost a full round of testing.
