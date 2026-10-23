# Arms with no weapon drawn, holster/draw on the right grip, carried objects held by their near side

Branch `jordi/polishing`. Measured on the mock (xr runtime simulator, Steam `PreyDll.dll`), not yet in a
headset. Everything is switchable at runtime; the switches restore the previous behaviour exactly.

## What a player saw before

- **Holstered** (Prey's own holster: hold the use/reload button): no arms, no hands.
- **Carrying** anything (towel, bench, body): no arms either. The carried object floated with no hand.
- A light object carried in the left hand (phase 2, `CarryLane`) sat with its **centre** in the palm, pushed
  out along the palm normal: a 66 cm towel stack filled the middle of the view.

## Why the arms vanish (measured)

The first-person arms are the player's own character (the 101-joint rig the IK drives), not the weapon's:
`ArkPlayer::GetAnimatedCharacter` (`0x157C6D0`) is `[player+0x1808]`, and the animated character caches the
`ICharacterInstance` at `+0x710`. With no weapon drawn it is the **same instance**, still skinned and still
drawn every frame (RenderCHR gets it, near pass); nothing hides it (`character+0xAC8` draw flags unchanged,
`+0x130` hide-master bit clear, its 16 bone and 4 skin attachments unchanged). The game's animation simply
**lowers the arms out of view**: right wrist at model (0.28, -0.15, 0.92) instead of (0.23, 0.21, 1.46),
i.e. 54 cm down and behind the camera. The IK did not drive the rig then because its owner comes from the
equipped weapon (`TryGetEquippedRig`), which fails with nothing selected.

(A render-matrix offset, `frame.offset`, does not move the arms even with a weapon drawn, so it cannot be used
to test visibility; the IK can.)

## What changed

### `arms.free` (AnimIkTakeover.cpp; default 1 when VR starts)

With no equipped rig, the IK owner falls back to the player's own arms character, validated by its vtable
(`0x1D22200`) and the usual rig signature, and not during a cinematic (`ArkPlayerInput` = `player+0x8E0`,
`+0x94`, the gate the body holsters use). That owner gets its own calibration generation (bit 62), so the
hand rig and calibration rebind on every switch; `gOwnerGeneration` is kept at the equip generation so the
other characters' fast pass-through still works.

- Both hands take the existing **free-hand path** (anatomical wrist from the grip pose, `pose.mode 1`); the
  right hand skips the weapon-alignment path when there is no weapon (`owner.weapon == 0`).
- Fingers: the relaxed open hand on **both** hands (the finger plan existed for the left only); the left one
  closes into a fist while it holds a carried object (`CarryHoldingInHand`, eased over ~70 ms;
  `handpose::ShapeRelative`, relaxed -> fist by flexion angle). Fingers ease in over 150 ms after the switch.
- After the native pass the wrist's **rotation** is also written, with everything the hand carries, about the
  wrist (`ArmPlan::forceRotation`): the lowered pose need not run any native IK. In practice the native ADIK
  pass does run there (`freeGateZero=0`), so this is a no-op safety net.
- Measured: holstered and carrying, both hands on the controllers, the same `pose.marks` geometry as with a
  weapon; `ikWritten` climbs on both hands; `freeFrames` counts the frames.

### Holster / draw on the right grip (InteractionUse, ArmsLane.cpp)

Holding the right grip already held X, which **is** Prey's holster (UnequipWeapon). Prey draws again on the
trigger, which a VR player expects to fire. Now, with nothing drawn, a right-grip press draws the last weapon:
`ButtonInput::holstered` -> `Owner::Draw`, `drawWeapon` (one frame; X never goes down, so holding on does not
holster again). The game thread calls the native `Equip` (`0x1274820`, component = `player+0x14B8`, id =
`+0x5C` last equipped) behind the body holsters' gates: `GetPlayer` (`0x157C990`) == the frame's player,
`CanEquip` (`0x1273EB0`), not mid-unequip (`+0x78`), no cinematic, items not restricted (`player+0x7BC`).
Holstered = `+0x58 == 0`, `+0x5C != 0`, `+0x64 == 0` (a carry keeps the weapon to re-equip there), not
carrying. Prologue bytes are checked before the first call.

Measured: hold 2 s -> holstered (`rig=0`, `holstered=1`, hands visible); press -> `draws=1 lastDraw=0`,
wrench back in the hand.

### Light objects held by their near side (`carry.grabpoint`, HandCarry/CarryLane; default 1)

At `StartCarrying`, the left pointer's ray (`TryGetUsePointer`, same entity, < 0.5 s old) is intersected with
the object's **local** bounds through the inverse of its world Matrix34 (`carry::GrabPointLocal`, scale
allowed; the bounds point nearest the ray when it passed beside). That point goes in the fist and the grab's
hand-to-object rotation is kept (`carry::LightHoldAt`), so the object extends beyond the hand along the
direction it was pulled from -- how HIGGS and Alyx's gloves hold what they catch. Without a grab point (a
carry the pointer did not start), the previous centre-in-palm rule is used.

The point is moved 3 cm towards the centre (at most half way) so the fingers close round the edge. Measured:
towel stack (diag 0.66 m): grab point (0.21, 0.11, 0.16) local, centre error 0.000 m, rotation error 0.0 deg; the fist holds the stack's edge and the view above it is clear (A/B with `carry.grabpoint 0`
and `arms.free 0` in the evidence).

## Verification on the mock (`docs/evidence/arms-2026-10-07/`; PNGs are kept out of git)

- `build/jordi-mock/ArmsCases.ps1` from a clean boot, judged by the drawn skeleton (`pose.marks` palms against
  the controllers' grip points) and the lane reports: **15/15** (`ac1_report.txt`): armed, holstered (still and
  raised/rolled), `arms.free 0`, drawn again, a press when drawn reloads, carrying (fist, towel by its pointed
  side), no draw while carrying, the game's own re-equip after the drop. Palm-to-grip 0.000 m in every pose.
- `CarryCases.ps1` (phase 2) from clean boots, every case passing (`cc8_report.txt`; K re-run in `cc8k` after
  widening its candidate list: the props it tried had ended up out of reach). Case J's "drag point at the hand"
  had failed since before this change (`cc5`): its hand sat 0.3 m from the player while the drag point is kept
  0.75 m away by design; the case now stretches the arm (0.04 m).
- Captures: `ac1_*` (armed / holstered / moved / `arms.free 0` / drawn / carrying), `k3`/`k4`/`k5` (towel: centre
  rule / grab point / `arms.free 0`), `m1`/`m2` (bench, fist), `h1`/`h2` (holstered / drawn), `r4` (shoe with the
  controller axes: it hangs from the grab point inside the fist).

## Switches and instruments

- `arms.free 0|1`, `arms.report` (holstered, last weapon, draws, free-arm counters), `arms.draw` (test),
  `arms.att` (the arms' attachments: name, flags, joint), `arms.joints [substring|all]` (final pose).
- `carry.grabpoint 0|1`; `carry.report` shows `grabPoint=` and `grabLocal=`.
- `mem.peek|scan|deep|str|watch|hits|stack`: memory probes and hardware watchpoints (DebugWatch) for
  investigation; `player`, `arms`, `dll` resolve to the live addresses.

## Not done / to check in a headset

- Cinematics are gated by the cinematic-input flag only; deaths, terminals and ladders were not exercised.
- The relaxed/fist finger shapes are hand-authored; judge them on real controllers.
- Grenades (EMP): still pending, unchanged.
