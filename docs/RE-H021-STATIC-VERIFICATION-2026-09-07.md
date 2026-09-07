# H-021 verification: object bases, pose setters, and the ADIK gate

Static verification, 2026-09-07. This answers all four mandatory questions in
[the handover](HANDOVER-H021-STATIC-VERIFICATION.md), and corrects the premise
of optional question 5. No game was launched, attached to, injected, or modified.

Target: Ghidra `/Prey/PreyDll.dll`, image base `0x180000000`, Steam module
SHA-256 `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
Addresses below are **RVAs** unless explicitly prefixed VA. Tool preflight
confirmed the target program was open. Constructor/accessor, caller, callee,
and instruction evidence were checked; old analysis names are not evidence.

## Results

| Question | Answer | Consequence |
| --- | --- | --- |
| Weapon helper name | Confirmed: `sAmmoSpawnPointName` populates whole-weapon `+0x2F0`. Loader's `this` is weapon `+8`; its displacement is `+0x2E8`. | Normalize the receiver before comparing offsets. |
| PushPoseModifier layers | Ordinary modifiers: `0..15` select per-layer queues; exactly `-1` selects the post queue; other values are rejected. | Layer 15 is valid before ADIK. Layer 16 is not a post-layer alternative. |
| Pose slots `+0x10/+0x30` | Set whole relative / absolute `QuatT`, respectively; both take joint index and pointer. | The missing third register argument in the decompile was an analysis artifact. |
| Character `+0x610` | Mutable animation-processing state: `CSkeletonAnim+0x4D0`, corresponding to CryEngine's `m_IsAnimPlaying`. | It is not an ADIK table count or proof that the rig has IK targets. |
| Alleged sync alternative | The cited branch applies facial displacement and recomputes absolute pose during skeleton post-processing. | Its existence does not establish an alternative animation route that bypasses ADIK. |

## Establish the receiver first

The original H-021 text interchanged `CSkeletonAnim` and `CSkeletonPose`.
The correct layout was already recorded in
[H-005B](RE-H005B-MODEL-FRAME-ARM-CHAIN-2026-09-05.md#cposemodifiersetup-and-the-first-entry-loop)
and was independently rechecked here:

| Whole character offset | Object / field | Target evidence |
| --- | --- | --- |
| `+0x140` | `CSkeletonAnim` | Character vtable `0x1D22200+0x28` -> `0x10EA500`: returns `this+0x140`. Constructor `0x82BD50` calls `0x837EC0` at this base. |
| `+0x700` | `CSkeletonPose` | Character vtable `+0x38` -> `0x82DFF0`: `lea rax,[rcx+700h]; ret`. Constructor calls `0x8327F0` here. |
| `+0x960` | First embedded `CPoseData` | Pose constructor calls `0x87B670` at pose `+0x260`; second instance at pose `+0x290`. |
| `+0x610` | Animation state | `0x140+0x4D0`; do not confuse this with skeleton `+0x70`'s ADIK table. |

`0x838E40` initializes the animation object with character and pose pointers.
The post-process routine `0x8360A0` receives the **pose** object:
pose `+0x250` is its character pointer, pose `+0x318` its animation pointer.
Its callback and callback data are therefore **CSkeletonPose+0x110/+0x118**.
The old Ghidra name `Prey_SkeletonAnimFinishAnimationComputations` for this
routine described the wrong class/stage; it is corrected to
`Prey_SkeletonPoseSkeletonPostProcess`.

## 1. Follow the property to its store, accounting for secondary this

Base weapon constructor `0x1690030` installs primary vtable `0x1E6C640`
at weapon `+0`, and secondary vtable `0x1E6C888` at weapon `+8`.
The latter's `+0x250` slot (`0x1E6CAD8`) points to parameter loader
`0x1697B90`. The loader consequently receives the secondary base.

The exact chain is:

- String `sAmmoSpawnPointName` at `0x1E6CC68`.
- `0x1697ED6` loads that string; `0x1697EDD` calls the property getter at virtual `+0x38`.
- The returned string is carried in RBX; `0x1697F34` forms `RCX = secondaryThis+0x2E8`.
- `0x1697F59` sets RDX = RBX; `0x1697F5C` calls string assignment `0x95680`.
- Therefore the destination is `weapon+8+0x2E8 = weapon+0x2F0`.
- Firing-position consumer `0x1694BC0` reads whole-weapon `+0x2F0`
  and supplies it to helper transform lookup `0x11A5CF0`.

A useful cross-check: the same loader writes camera test distances at
secondary `+0x3E8/+0x3EC`, matching the consumer's whole-weapon
`+0x3F0/+0x3F4`. The loader's own `+0x2F0` is **sUIElementName**;
searching for a literal matching displacement without resolving `this`
would produce a convincing wrong answer.

This confirms the helper field, not live projectile placement. The firing
function retains its obstruction-dependent camera fallback.

## 2. PushPoseModifier: the actual branch, including its exception

`CSkeletonAnim` constructor `0x837EC0` installs vtable `0x1D22D08`.
Its `+0x120` slot points to `0x839860`, already identified in H-005B.

For the ordinary modifier path:

```text
layer == 0xFFFFFFFF -> SkeletonAnim+0x440          (character+0x580)
unsigned layer <16 -> SkeletonAnim+0x68+layer*0x40
otherwise          -> release shared ownership and return false
```

Instructions: sentinel comparison at `0x83994F`; post-queue address at
`0x839975`; unsigned rejection `CMP EDI,10h; JNC` at `0x83999D`;
layer stride calculation at `0x8399C3..0x8399D3`.

**Exception:** before these layer checks there is a modifier identity comparison
against two qwords at `0x24BD240/+8`. A match replaces a dedicated shared
pointer at animation `+0x5A0/+0x5A8` and returns true without this routing.
Do not state that every possible modifier goes through the layer bounds.
The identity's name was not needed to settle the ordinary queue contract.

H-005B also contains a relevant limitation: the post queue's Execute loop
`0x8779C0..0x8779D2` repeats its first entry without advancing the pointer.
A second top-level `-1` modifier is not a reliable append route; the nested
Setup stack has its own correctly advancing loop. Reuse that earlier result
instead of proposing `-1` as an unqualified workaround.

Route B can enqueue target overrides before ADIK, but writing target joints
does not itself prove the solver will execute. It still needs a nonzero gate,
the cvar, appropriate physics state, valid target/limb definitions, and weights.
Enqueuing work may affect the command-derived gate; that must be demonstrated
for the selected path. It is not an established bypass for a closed gate.

## 3. Resolve the concrete virtual callee before trusting argument count

Pose-data constructor `0x87B670` installs vtable `0x1D27228`.

| Slot | Callee | Actual operation |
| --- | --- | --- |
| `+0x10` | `0x87C9D0` | Copy 28 bytes from R8 to `[RCX+0x10] + uint32(EDX)*0x1C`: SetJointRelative. |
| `+0x30` | `0x87C940` | Copy 28 bytes from R8 to `[RCX+0x18] + uint32(EDX)*0x1C`: SetJointAbsolute. |

Both implement `void setter(pose, uint32 joint, const QuatT* value)`.
Neither derives a relative transform or recomputes descendants internally;
the caller does that math.

The small leaf functions were already disassembled but not defined as Ghidra
functions. Defining the functions at the vtable targets made their three
parameters explicit. No function body was patched.

The operator queue's root-joint paths settle the apparent conflict directly:

```asm
7DD476  call [rbx+48h]   ; get absolute
7DD479  mov edx,edi      ; joint
7DD47B  mov rcx,rsi      ; pose
7DD47E  mov r8,rax       ; QuatT pointer -- omitted in the old decompile!
7DD481  call [rbx+10h]   ; set relative

7DE2BC  call [rbx+28h]   ; get relative
7DE2BF  mov edx,edi
7DE2C1  mov rcx,rsi
7DE2C4  mov r8,rax
7DE2C7  call [rbx+30h]   ; set absolute
```

## 4. The gate has writers unrelated to the ADIK definition count

The relevant writers establish mutable per-instance processing state:

| Routine / store | Effect |
| --- | --- |
| `0x838E40`, store `0x838E64` | Initializes animation `+0x4D0` to zero as part of an eight-byte initialization. |
| `0x82E760`, store `0x82E847` | Resets character `+0x610` before animation update `0x839500`. |
| `0x82F910`, store `0x82FBCA` | Another character update resets `+0x610` before `0x839500`. |
| `0x840EC0`, store `0x8410EF` | Sets animation `+0x4D0 = 1` while generating animation commands. |
| `0x841A50`, store `0x841D5C` | ORs animation `+0x4D0` with `0xFFFE` on a command-building branch. |
| `0x8384B0`, store `0x83858E` | After `0x840EC0`, replaces it with `uint32(commandBuffer[0x2008] != 0)`. |

The final byte is the command count: command allocation advances the write
pointer at buffer `+0x2000` and increments byte `+0x2008`.
Thus the exact observed writer is a **nonempty-command-buffer predicate**.
CryEngine source's `m_IsAnimPlaying` is a useful corresponding name, not a
license to infer which animation or rig is active from this value.

Searching only for displacement `0x610` misses writers using animation
`this+0x4D0`. Find aliases first, then search both displacements; reject
stack offsets and unrelated objects after examining each receiver.

The new runtime diagnostic's raw `gate` value remains useful. Its current
source comment saying “non-zero when the rig carries ADIK targets” is wrong.
A zero hook count alone cannot distinguish wrong rig identity, empty animation
work, physics state, cvar state, or a hook problem.

## 5. The “sync path” is facial post-processing

At `0x8360A0`, define `pose = param_1` and
`anim = *(pose+0x318)`. The actual condition is:

```cpp
anim[0x38] != 0 &&
(read_u32(anim + 0x4D0) != 0 || (pose[0x308] & 2) != 0)
```

Its body calls `0x867B40(anim+8, skeleton, poseData, defaultPose)`,
then `0x87B8E0` to recompute absolute transforms.
`0x867B40` applies per-joint displacement records, writes relative
transforms, and propagates affected descendants.

This matches the local CryEngine
`SkeletonPose_Process.cpp::CSkeletonPose::SkeletonPostProcess` branch:
`m_facialDisplaceInfo.HasUsed()`, `m_IsAnimPlaying`,
`m_bFullSkeletonUpdate`, and
`CFacialModel::ApplyDisplaceInfoToJoints`. These descriptive field names
come from source correspondence; the receiver, condition and calls come
from Prey's instructions.

The branch does not call ADIK, but that does **not** imply ADIK did not run in
the preceding job. Nor does it prove that a facial overwrite can never affect
a particular joint afterward. The hand rig's actual invocation and final pose
still need the existing runtime observation.

## Verification and handback

Run:

```powershell
C:/Python314/python.exe -B tools/re/verify_h021_static.py
```

**24 checks passed** against the supported disk image: constructor/property
instructions, both complete pose setters, caller R8 setup, layer bounds and
routing, gate writers, facial condition, and six virtual-slot mappings.
Expected values are in `tools/re/h021_static_landmarks.json`; this is an
address-specific regression verifier, not a general function finder.
Raw static receipts are in the ignored local capture
`captures/traces/2026-09-07-h021-static-verification.json`.

No runtime behavior is promoted to proven by these checks. In particular,
weight 1 selects the full IK goal/rotation blend but does not guarantee an
exact wrist endpoint: H-018 established the native two-bone solver's reach,
stretch, singularity and angle-clamp limits.

See [RE investigation guidance](RE-INVESTIGATION-GUIDE.md) for the small
workflow changes that would have avoided these gaps.

