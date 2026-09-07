# H-021 static report -- absolute wrist, arm chain, weapon-to-wrist, controller aim

**Verification update, 2026-09-07:** [all four static questions are answered](RE-H021-STATIC-VERIFICATION-2026-09-07.md).
The animation object is character +0x140, the pose object +0x700; character
+0x610 is command-derived animation state, not ADIK presence. The branch formerly
called a sync alternative is facial post-processing. Ordinary modifier layers
are 0..15 or exactly -1, with a special identity path before those checks.
Weight 1 does not remove the native solver's reach/singularity limits. The
proposed complete hand/weapon behavior remains conditional on runtime evidence.

**Static only, 2026-09-07.** Nothing here ran the game. Every address is
`PreyDll.dll` (image base `0x180000000`); RVAs are given as `0x...` and the
Ghidra database carries names and plate comments; the verification update corrects
the post-process name. Decompiler types and argument counts require instruction
checks; **INFERENCE** marks a name or meaning taken from the
CryEngine 3.8 source or the Chairloader member order rather than from these
bytes.

**Proposed route.** The four items connect through a native animation seam.
Prey contains first-person arm drivers using CryEngine's *animation-driven IK*: a
target joint and a weight joint per arm, conditionally solved by
`ProcessAnimationDrivenIK` (`0x877B50`). Writing the controller's wrist pose into
that target, with the weight at 1, makes the engine solve the arm, set the wrist
rotation, re-propagate the fingers, and -- because the weapon is a bone
attachment updated *after* the animation job -- carry the weapon with it. No
`IKLimb` is constructed, no pose is fabricated, and the projectile origin
can follow through a helper on the weapon entity, subject to binding and the
firing function's camera fallback. The static chain does not prove the complete
route is active for the selected rig in a live frame.

---

## 1. Why the current wrist write moves the hand and not the weapon

The per-character frame has three stages, and the current hand-rig hook sits in
the last one.

**Animation job** -- `Prey_AnimCommandBufferExecute` `0x877600` (job entries
`0x8387B0`, `0x839000`). In order:

1. Blend commands `0..0xB`. **Case 9 is `Command::PoseModifier::Execute`
   (`0x8797C0`)**: it builds `SAnimationPoseModifierParams` and calls
   `modifier->vtable+0x28` (`Execute`). So a pose modifier pushed on an
   animation layer runs *here*, inside the command buffer.
2. FK, `Prey_PoseDataComputeAbsolutePose` `0x87B8E0` -- this contains R-083's
   write site `0x87BBA0`.
3. **`Prey_ProcessAnimationDrivenIK` `0x877B50`** -- gated by
   `charInst+0x834 & 0x20 == 0`, `charInst+0x610 != 0` and the cvar
   `ca_useADIKTargets` (`DAT_18225780C`).
4. `0x80EE40(charInst+0x820, pose, dt)` -- skeleton physics (INFERENCE).
5. The post pose-modifier list at `charInst+0x580 + [charInst+0x590]*8`, each
   `vtable+0x28` (`Execute`).
6. `Prey_AttachmentManagerUpdateProxiesAndProjections` `0x8299D0`, only when
   `charInst->vtable+0x1F0() == 0`. This updates **proxies** (0xA8-stride
   by-value records at `attMgr+0x28`: joint id `+0x0A`, default QuatT `+0x1C`,
   model-relative `+0x54`, world-oriented `+0x84`) and a one-shot list at
   `charInst+0x48` of attachments needing projection
   (`Prey_CAttachmentBONEProjectAndUpdate` `0x7A37E0` computes
   `m_AttRelativeDefault = inverse(jointAbs) * m_AttAbsoluteDefault` -- this is
   the H-017 `SetAttAbsoluteDefault` mechanism, sampled once). It is **not** the
   weapon's per-frame update.

**Main thread, after the job** -- `Prey_SkeletonPoseSkeletonPostProcess`
`0x8360A0` (and `0x8366F0`), reached from `0x8387F0` which waits on the job.
`param_1` is the `CSkeletonPose` embedded at `CCharInstance+0x700`
(`+0x250` = `m_pInstance`); `param_2` is the `CPoseData` at `CCharInstance+0x960`.
In order: conditional facial displacement and FK -> physics -> proxies (when `+0x1F0` is set, the
complement of the job's condition, so proxies run exactly once) ->
**`Prey_AttachmentManagerUpdateLocationsExecute` `0x8297E0`** -> AABB
(`0x836780`) -> **the post-process callback at `CSkeletonPose+0x110`** with data
`+0x118` (CryEngine's `SetPostProcessCallback`, INFERENCE on the name).

`0x8297E0` walks the attachment pointer array `attMgr+0x20` by sorted type
ranges (`+0x4A..+0x68`) and calls the per-type `CAttachmentBONE` updates:
`Prey_CAttachmentBONEUpdateStatic` `0x7A3390`, `Prey_CAttachmentBONEUpdateExecute`
`0x7A40E0`, `0x7A2FD0`, and `CAttachmentFACE` `0x7B3B00/0x7B3C20/0x7B3B40`. Each
bone attachment does exactly this:

```
abs = pose->vtable+0x48(att+0x15C)                 // GetJointAbsolute(jointId)
att+0x130 (m_AttModelRelative) = abs * att+0xF8 (m_AttRelativeDefault; +0xFC was an off-by-4 read) * att+0x14C
if (att+0x30 type == 4) Prey_CAttachmentBONESimulateEntityBinding 0x7C4C60(att+8, pose, -1, &modelRelative)
else                    0x7C0F00(...)
```

**Render** -- `Prey_SkinningTransformationsComputation` `0x82EE10` (job thunk
`0x82E580`) reads the **absolute** array at `pose+0x18` directly and composes it
with the skeleton's inverse bind pose (`skeleton+0x30`) into dual quaternions.
It never recomputes absolute from relative.

So the ordering that matters is:

```
job:    commands + layer modifiers -> FK -> ADIK -> post modifiers -> proxies
main:   ... -> BONE ATTACHMENTS SAMPLE THE POSE (0x8297E0) -> post-process callback
render: skinning reads pose+0x18
```

The existing hand-rig hook writes a *private clone* at the skinning stage. That
is after the attachments sampled the real pose, which is exactly the observed
result: hand moves, weapon does not. The post-process callback is also too
late. Any write that must reach the weapon has to land in the **real** pose
before `0x8297E0` -- in the job, or at that function's entry on the main thread.

## 2. The engine already solves this arm every frame: animation-driven IK

`0x877B50` is CryEngine's ADIK pass. Per entry of the skeleton's ADIK table
(`skeleton+0x70`, stride `0x28`, built by `Prey_LoadADIKTargets` `0x8B4060` from
`<ADIKTarget Handle= Target= Weight=/>`):

| offset | field | evidence |
|---|---|---|
| `+0x00` | handle, the first 8 bytes of the `Handle` string | loader stores `*(u64*)attr` |
| `+0x08` | target joint index | loader `GetJointIDByName(Target)`; dispatcher reads `absolute[idx]` |
| `+0x10` | target joint name | loader |
| `+0x18` | weight joint index | dispatcher reads `relative[idx].pos.x` |
| `+0x20` | weight joint name | loader |

Per entry, per frame: `w = relative[weight].pos.x` (skip `<= 0.01`, clamp 1);
resolve the limb by handle through `skeleton->vtable+0x68`; FK-propagate the
limb's root-to-end path from relative; `goal = lerp(absolute[end].t,
absolute[target].t, w)`; dispatch `2BIK`/`3BIK`/`CCDX` by the tag at `limb+8`
(the 2BIK leaf is H-018's `0x871CA0`, unchanged); re-propagate the chain; slerp
`absolute[end].q` toward `absolute[target].q` by `w`, derive `relative[end]`;
re-propagate the limb's descendant list (`limb+0x20`) so the fingers follow.

**With `w = 1` the native path uses the full target position/rotation blend and
propagates the fingers.** Exact wrist position is still constrained by the native
solver's reach, stretch, singularity and angle clamps (H-018); it is not an
unconditional endpoint guarantee.

The hand rig has exactly the joints this needs (validated dump, 101-joint rig):
`r_hand_spine_target = 38`, `l_hand_spine_target = 39`, `r_hand_spine_blend = 4`,
`l_hand_spine_blend = 5`, `r_hand_jnt = 45`, `l_hand_jnt = 72`.

**Prey's own code drives it this way.** `Prey_ArkHandIKContextInitialize`
`0x181802630` resolves those three names by `GetJointIDByName` and creates an
`AnimationPoseModifier_OperatorQueue`; `Prey_ArkHandIKContextUpdate`
`0x181801830` (vtable slot 5 of the context at `0x181EA62D8`, immediately
before the `r_hand_spine_target` string) pushes, every frame:

```
anim = character->vtable+0x28()                              // GetISkeletonAnim
anim->vtable+0x120(anim, 6, &sharedPtr, "ProceduralWeapon")  // PushPoseModifier(layer 6)
queue->vtable+0x70()                                         // Clear
queue->vtable+0x40(queue, blendIdx, 1, &(1,0,0))             // PushPosition, eOp_OverrideRelative -> weight 1
queue->vtable+0x40(queue, rTargetIdx, 3, &pos)               // PushPosition, eOp_Additive
queue->vtable+0x48(queue, rTargetIdx, 3, &quat)              // PushOrientation, eOp_Additive
... same for lTargetIdx
```

CryAction's `CFirstPersonHandIKContext` (`Initialize` `0x18039CBE0`, `Update`
`0x18039CC80`) is the same pattern with the SDK's `Bip01 ...` names, layer 15,
and the weight set through the absolute form `parentAbs * (1,0,0)` with op 0.
Both exist in this binary; Prey's is the live one for the arms.

## 3. The two seams, in order of preference

### Route A -- detour `Prey_ProcessAnimationDrivenIK` (recommended)

One MinHook detour on `0x877B50`, on the animation job thread (the hand-rig
code already keeps thread-local scratch for this reason). At entry, for the
hand rig only:

```
pose  = params->pPoseData          // params+8; +0x10 relative, +0x18 absolute, stride 0x1C
loc.q = params+0x14, loc.t = params+0x24, loc.s = params+0x30   // model -> world
goalModel = conj(loc.q) * (controllerWorldPos - loc.t) / loc.s
rotModel  = conj(loc.q) * controllerWorldRot
absolute[38].t = goalModel;  absolute[38].q = rotModel   // r_hand_spine_target
relative[4].t.x = 1.0f                                    // r_hand_spine_blend
(left hand: 39 and 5)
call original
```

Why this is the recommendation: the dispatcher hands over the character's own
model-to-world transform, so the body-yaw approximation and R-089's origin
pairing are replaced by the exact inverse the engine itself uses for its debug
draw; the write is consumed in the same call, so there is no frame of latency;
nothing is allocated or fabricated; and everything downstream -- attachments,
skinning, AABB -- sees the result. The writes are self-restoring: the next
frame's command buffer rebuilds the pose from animation.

Preconditions, all one live read each (section 6): the arm entries exist in
`skeleton+0x70`, the limb resolved by their handle ends at joint 45/72,
`charInst+0x610 != 0` for the hand rig, and `ca_useADIKTargets != 0`.

### Route B -- the engine API: OperatorQueue on a layer

The same thing through the public seam Prey uses. `Prey_CryCreateClassInstance`
`0x2C30D0("AnimationPoseModifier_OperatorQueue", &sharedPtr)` returns
`{interface, controlBlock}`; the interface object sits at `alloc+0x10`. Its
vtable is `0x181D1F6C0`:

| slot | offset | method | evidence |
|---|---|---|---|
| 4 | `+0x20` | Prepare | position; INFERENCE |
| 5 | `+0x28` | **Execute** `0x1807DD2A0` | called by the modifier loops |
| 6 | `+0x30` | Synchronize | INFERENCE |
| 8 | `+0x40` | **PushPosition(jointId, EOp, const Vec3*)** `0x1807DD0A0` | three call sites |
| 9 | `+0x48` | **PushOrientation(jointId, EOp, const Quat*)** | two call sites |
| 13 | `+0x68` | PushComputeAbsolute | INFERENCE from order |
| 14 | `+0x70` | **Clear** | called before pushing in both contexts |

`EOp`, from `Execute`: **0 override absolute, 1 override relative, 2 override
world (converted by the engine with `inv(location.q) * (v - location.t)`, no
scale), 3 additive absolute, 4 additive relative**, 5/6/7 store rel/abs/world,
8 compute absolute. After ops 0..4 it derives the relative from the parent and
re-propagates every later joint, so children follow.

So Route B is: create the queue once; every frame on the game thread
`Clear`, `PushPosition(38, 2, worldPos)`, `PushOrientation(38, 2, worldRot)`,
`PushPosition(4, 1, (1,0,0))`, then
`ISkeletonAnim::PushPoseModifier` = `anim->vtable+0x120(anim, layer, &sharedPtr,
"PreyVR")` with `anim = character->vtable+0x28()`. Use **layer 15**: layers run
in numeric order inside the command buffer, so our override on 15 lands after
Prey's additive push on 6, and both precede the ADIK pass. Op 2 removes the need
for any model-space conversion on our side.

Costs: the push must happen before the job is kicked for that frame; a push from
a render-time hook applies next frame (one frame of latency, ~11 ms at 90 Hz);
and it depends on four vtable offsets rather than one function address. This is
an alternative target-write route, not a demonstrated bypass of Route A's gate:
the later ADIK pass still needs its native conditions. See the verification update.

### What not to do

* Do not write at the skinning hook for anything the weapon must follow.
* Do not use the post-process callback (`CSkeletonPose+0x110`): it runs after
  `0x8297E0`.
* Do not write during FK (`0x87BBA0`, R-083): ADIK, modifiers and the descendant
  re-propagation all run afterwards and overwrite the chain. That is the
  complete explanation of R-083's "the write lands and the value does not
  survive" -- the write was upstream of three passes that rebuild those joints.

## 4. Item 3 -- the weapon follows the wrist without further work

`CArkWeapon::AttachToHand` `0x16914F0` binds the weapon into the `IAttachment*`
at `CArkWeapon+0x2B0` through `vtable+0xD8` (`AddBinding`); the attachment name
comes from the item XML `Weapon.sAttachmentName` (`0x169B450`; the default
string at `0x181C7CE7B` is empty, so every weapon names its own). That
attachment is a `CAttachmentBONE` on the arms rig, updated in `0x8297E0` from
`GetJointAbsolute(att+0x15C)`. Once the wrist is written before `0x8297E0`, the
weapon's model-relative transform is recomputed from it, its entity/skeleton
binding is pushed, and its own `RenderCHR` draw composes against the parent
matrix (H-018 section 1). The authored grip offset (`m_AttRelativeDefault` at
`att+0xF8`) is kept, so the weapon sits in the hand as the animators placed it.

Two things to know:

* **Which bone.** Read `[weapon+0x2B0]+0x15C` live. Expect 45 (`r_hand_jnt`) or
  47 (`r_handProp_jnt`, a child of 45); either is inside the solved subtree.
* **The binding has a spring simulation.** `0x7C4C60` is a per-attachment
  pendulum/spring (`att+8+0x28` enabled, `+0x2B` redirect) driven by the
  character's location; when enabled it lags the weapon behind the hand and,
  with redirect set, **writes back into the pose** (`SetJointRelative`, then
  `SetJointAbsolute` for the joint's children at `+0xA8`). This is a concrete
  H-020 candidate: if the weapon visibly trails a fast wrist, the flag at
  `[weapon+0x2B0]+8+0x28` is the thing to clear, not an animation.

## 5. Item 4 -- aim from the controller: origin and direction

**Projectile origin already follows the weapon.** `Prey_CArkWeaponGetFiringPosition`
`0x1694BC0` returns the world position of the helper named by `this+0x2F0`
(`m_ammoSpawnPointName`, now confirmed by the `sAmmoSpawnPointName` property
store with secondary-this adjustment) on slot 0 of the weapon entity (`this+0x40`), via
`Prey_GetEntitySlotHelperWorldTM` `0x1811A5CF0` (statobj helper, attachment, or
joint by name, times the entity world TM). It falls back to the camera position
only when a ray along the *owner's* forward over `this+0x3F0`
(`m_spawnFromCameraTestDistance`, default 1.1) / `this+0x3F4`
(`m_spawnBehindCameraDistance`) is blocked -- a wall-clip guard, unaffected by
the aim cache. GLOO (`0x169F420`) then fires from that position toward the
reticle ray's hit point (H-018 section 3).

**Direction.** The reticle ray is the cache at ArkPlayer `+0x17D4` (origin) and
`+0x17E0` (direction), producer `0x1585320`, getter
`GetReticleViewPositionAndDir_ArkPlayer` `0x157CBB0`. The aim takeover already
replaces the direction with `AimFromController`; it deliberately keeps the
engine's origin (the camera). With the muzzle at the hand and the ray from the
camera, a projectile flies from the hand *toward where a camera ray along the
controller direction lands* -- a convergence error that grows with the
hand-to-eye offset. **Integration correction (2026-09-07):** `aim.origin 1` writes the tracked aim
point, which is distinct from both the wrist/grip and native muzzle helper.
It does not establish a common projectile/raycast origin or barrel alignment.
The native cache is also consumed by interaction selection and HUD projection,
so changing its origin needs acceptance for those consumers separately.

Wrist-preserving calibration and an attachment default are not a proved
controller-to-barrel transform. The bone attachment relative default starts at
**+0xF8**, not +0xFC; it additionally participates in bind/current-joint and extra
rotation composition. The authored muzzle forward axis still needs evidence.
The [integration audit](RE-H021-INTEGRATION-AUDIT-2026-09-07.md) fixes shared-origin
feedback and adds passive native firing-origin/aim/grip separation diagnostics.

## 6. Live reads required before building, each one read

1. **ADIK table for the hand rig.** `skeleton = *(character+0x10)`; `table =
   *(skeleton+0x70)`, `count = *(u32*)(table-4) & 0x7FFFFFFF`; per entry
   `+8` target, `+0x18` weight. Expect entries with (38, 4) and (39, 5).
2. **Limb for each entry.** `limbs = *(skeleton+0x68)` stride `0x30`; the limb
   whose chain (`+0x18`) record 3 index (`+0x30`) is 45 or 72, and its solver
   tag at `+8`. The dispatcher resolves by handle; matching by chain end is the
   fabrication-free way to know the arm is defined.
3. **Gate.** `*(u32*)(character+0x610) != 0` on the hand rig, and
   `ca_useADIKTargets` at RVA `0x225780C != 0`.
4. **Weapon bone.** `*(int*)([weapon+0x2B0]+0x15C)`.
5. **Location sanity.** At the detour, log `params+0x24` against the player's
   world position and `params+0x14` against the body yaw: it should be the
   viewmodel entity's world QuatTS (INFERENCE that it equals R-088's `W`).

All are reads of a running game through the existing Frida session; none needs
a headset.

## 7. Measured vtable offsets (facts) with inferred names

| interface | offset | name (INFERENCE) | call site |
|---|---|---|---|
| `ICharacterInstance` | `+0x28` | GetISkeletonAnim | both hand-IK contexts, clip Update |
| | `+0x38` | GetISkeletonPose | `0x18039CC80`, `0x181801830` |
| | `+0x48` | GetIAttachmentManager | `0x1811A5CF0`, `0x169B450` |
| | `+0x58` | GetIDefaultSkeleton | four sites |
| `ISkeletonAnim` | `+0x120` | PushPoseModifier(layer, sharedPtr*, name) | three sites |
| `ISkeletonPose` | `+0xC0` / `+0xC8` | GetAbsJointByID / GetRelJointByID | `0x1811A5CF0`, `0x18039CC80` |
| `IDefaultSkeleton` | `+0x08` `+0x10` `+0x20` `+0x30` `+0x38` `+0x48` `+0x68` | GetJointCount, GetJointParentIDByID, GetJointIDByCRC32, GetJointNameByID, GetJointIDByName, GetDefaultAbsJointByID, resolve limb handle | loaders, dispatcher, contexts |
| `IAnimationPoseData` | `+0x08` `+0x18` `+0x20` `+0x28` `+0x38` `+0x40` `+0x48` | GetJointCount, SetJointRelativeP, SetJointRelativeO, GetJointRelative, SetJointAbsoluteP, SetJointAbsoluteO, GetJointAbsolute | `COperatorQueue::Execute`, attachment updates |
| `IAnimationPoseModifier` | `+0x28` | Execute(params) | both modifier loops |
| `IScope` | `+0x18` / `+0x70` | GetCharInst / GetBaseLayer | `0x1803A3600` |

`SAnimationPoseModifierParams`: `+0` character, `+8` pose, `+0x10` dt, `+0x14`
location quat, `+0x24` location pos, `+0x30` location scale -- the same struct
`Command::PoseModifier::Execute` builds and `ProcessAnimationDrivenIK` receives.

## 8. What this changes in the checklist

* Absolute wrist: seam identified, exact transform available, no approximation
  needed -- build.
* Arm chain: solved by the engine's ADIK from two joint writes; H-018 Gap 4's
  `IKLimb` construction is no longer required -- build.
* Weapon to wrist: automatic once the wrist write precedes `0x8297E0` -- verify.
* Aim: origin write in the takeover plus a per-weapon grip rotation -- build.
* Animation suppression (H-020): the first named candidate is the attachment
  spring at `[weapon+0x2B0]+8+0x28`, not an animation asset.

Verification of all offline claims is by the five reads above followed by one
headset session; the counters that matter are "dispatcher entries seen for the
hand rig" and "target written per frame", and the instrument that matters is a
wearer's hand landing where the controller is.
