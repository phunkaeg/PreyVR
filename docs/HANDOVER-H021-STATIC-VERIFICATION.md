# Handover -- verify the four things H-021 inferred rather than read

**Written 2026-09-07.** Static only; nothing here needs the game. The H-021
report ([`RE-H021-ABSOLUTE-WRIST-ARM-CHAIN-WEAPON-AIM-2026-09-07.md`](RE-H021-ABSOLUTE-WRIST-ARM-CHAIN-WEAPON-AIM-2026-09-07.md))
is decompiled fact except where it says INFERENCE. This brief is the INFERENCE
list, each with the cheapest way to turn it into a fact. None blocks the build
that is going into the next headset session; all four would make its
diagnostics unambiguous.

The Ghidra database (`/Prey/PreyDll.dll`, project `SS2_VR`) already carries the
names used below -- `Prey_ProcessAnimationDrivenIK`, `Prey_COperatorQueueExecute`,
`Prey_ArkHandIKContextUpdate`, `Prey_CArkWeaponGetFiringPosition` and the rest of
R-102 -- with plate comments that state what was read and what was inferred.

## 1. `CArkWeapon+0x2F0` is `m_ammoSpawnPointName`

`Prey_CArkWeaponGetFiringPosition` `0x1694BC0` passes `this+0x2F0` as the helper
name to `Prey_GetEntitySlotHelperWorldTM` `0x1811A5CF0`. The member name comes
from the Chairloader header's declaration order anchored on `+0x2B0`
(`m_pAttachment`, R-024) and `+0x3F0/+0x3F4` (`m_spawnFromCameraTestDistance`
1.1f / `m_spawnBehindCameraDistance`, both matching their use as ray distances).

**To confirm:** find the store to `+0x2F0` in the weapon's parameter loader --
the XML key it reads (`sAmmoSpawnPoint`? `ammoSpawnPointName`?) names the
member. Route: callers of the GLOO constructor `0x169E1D0` -> base `CArkWeapon`
constructor -> the `ReadParams`-style function that walks `Weapon` XML with the
same `vtable+0x38(node, "name", &out, 0)` idiom `0x169B450` uses for
`sAttachmentName`. A string search for `SpawnPoint` in the game range
(`0x181E9....`) is the shortcut.

## 2. `ISkeletonAnim::PushPoseModifier` layer semantics

Three call sites prove the slot is `+0x120` (`0x1803A3600` layer `baseLayer +
params`, `0x18039CC80` layer 15, `0x181801830` layer 6). `Command::PoseModifier
::Execute` `0x8797C0` is command case 9 of `Prey_AnimCommandBufferExecute`
`0x877600`, so *some* pushed modifiers run inside the command buffer before the
ADIK pass, and the list at `CCharInstance+0x580` runs after it. **Which layers go
where is INFERENCE from CE 3.8** (`layer < numVIRTUALLAYERS` -> the layer's
queue -> a command; otherwise the post list).

**To confirm:** locate `CSkeletonAnim::PushPoseModifier`. The `CSkeletonAnim`
object is embedded at `CCharInstance+0x700`; its vtable is stored by the
`CCharInstance` constructor, or find any function that appends to
`CCharInstance+0x580` (the pointer array indexed by the byte at `+0x590`) and
walk its callers. The answer decides whether Route B needs layer 15 or layer
`>= 16`, and it is the only thing Route B still lacks.

## 3. `IAnimationPoseData` slots `+0x10` and `+0x30`

`Prey_COperatorQueueExecute` calls `pose->vtable+0x10(joint)` after an absolute
override and `+0x30(joint)` after a relative one, with one argument each as the
decompiler shows it; `0x7C4C60` calls `+0x10(joint, &QuatT)` and `+0x30(child,
&QuatT)` with two. The measured slots that matter are unambiguous (`+0x18/+0x20`
set relative P/O, `+0x28` get relative, `+0x38/+0x40` set absolute P/O, `+0x48`
get absolute); these two are not, and they are what a "compute relative from
parent" or "set whole QuatT" would be.

**To confirm:** find the `CPoseData` vtable (the object at `CCharInstance+0x960`)
and decompile slots 2 and 6.

## 4. What sets `CCharInstance+0x610`

`Prey_ProcessAnimationDrivenIK` is only called when `charInst+0x610 != 0`. The
report assumes it is "the rig carries ADIK targets" because the loader appends
to `skeleton+0x70` and the pass iterates that table. If `+0x610` is instead a
per-instance runtime flag (an "IK enabled" bit that game code toggles), a rig
with ADIK entries could still skip the pass, and Route A's detour would count
zero calls for it.

**To confirm:** xrefs to writes at `+0x610` on the `CCharInstance` (search for
`0x610` displacement stores in the CryAnimation range `0x1807A....-0x1808C....`).
The live counter `ikCalls` in the new build answers the same question for the
one rig that matters, so this is verification, not a blocker.

## 5. (Optional) which characters animate on the sync path

`Prey_SkeletonAnimFinishAnimationComputations` `0x8360A0` runs FK itself when
`[skelAnim+0x318]+0x38 != 0` and (`+0x4D0 != 0` or `skelAnim+0x308 & 2`), and
that branch does **not** call the ADIK pass. If the first-person rig were
animated that way, the detour would never fire for it. `ikCalls`/`ikMatched`
settle this live in one report; statically, the meaning of those three fields
would say it in advance.

## Standing constraints

* Never modify the installed game. Static reads only.
* The PDB-derived Chairloader headers are an oracle, not the target; member
  *order* has carried for `CArkWeapon`, function RVAs never do.
* A negative is valuable and should be stated as one: "`+0x610` is a runtime
  flag" is worth more than "it is the table count" because it changes the
  build.
