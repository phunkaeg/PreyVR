# H-005B — exact model frame, native arm solve, and weapon mount

**2026-09-05. Static investigation of the supported Steam DLL.** The earlier
consumer takeover and controller translation are now headset-confirmed by the
live lane (R-084/R-085/R-087). This investigation did not access the running
process, inject, deploy, or modify the mod's runtime code.

## Answer and recommended next change

1. **The render matrix is the first 48 bytes of `CRenderObject`.** For the
   entity-slot near-character route, it maps model space into coordinates
   relative to **camera position, with world-oriented axes**. Use its full
   inverse, including translation, pitch, roll and scale. Do not assume the
   near hands and world shadow have the same matrix.
2. **A native two-bone solver survives and is called by the known animation
   path:** `0x871CA0`. It has no pole/hint input. It modifies both relative and
   absolute joint arrays, can stretch segments by up to 25% per call, and can decline a
   degenerate solve. It is a candidate for the existing private consumer path;
   the current absolute-only pose substitution is insufficient for calling it.
3. **`SetAttAbsoluteDefault` really is attachment vtable `+0x48`.** Its value
   survives the inspected normal animation updates, which rebuild the relative
   default and current mount from it. It sets a **default-pose model transform**,
   not the desired current weapon transform. Compensate for the current bone
   and bind pose, and arrange the write before the attachment update consumes it.

Two newly verified traps affect implementation:

- `0x81D377`, the existing render-object marker, is **after** skinning dispatch.
  Reading the right matrix there does not guarantee the current job sees it.
- The post-physics modifier queue loops over its count while repeatedly calling
  **entry zero**. Appending a second top-level modifier can be ineffective. The
  nested `CPoseModifierStack` has a correct advancing loop.

**Recommended order:** establish the full matrix and matching camera origin at a
causally earlier marker; prove world-to-model round trips; then exercise native
IK on private relative/absolute arrays at `0x82EE10`. Keep weapon placement a
separate attachment-consumer timing problem. None requires revisiting scratch
IK targets or moving the near camera.

## Evidence and reproduction

- Module: `PreyDll.dll`, preferred base `0x180000000`. Addresses below are RVAs;
  object fields and vtable slots are explicitly labelled as offsets.
- SHA-256:
  `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`.
- Ghidra preflight succeeded, with `/Prey/PreyDll.dll` open. All Ghidra memory
  reads were static program bytes. PE scans read the installed file only.
- `py -3 tools/re/verify_h005b_model_frame.py` passes **28 instruction/vtable
  landmarks and 8 synthetic transform fixtures**. An optional argument selects a
  DLL file. It refuses another hash and reports `runtime_tested: false`.
- Raw decompilations, disassembly and reference candidates are in ignored
  `captures/re/h005b/`, with earlier render/skinning evidence in
  `captures/re/h005/`. Raw byte xrefs were treated as candidates and their
  relevant owners checked in Ghidra; unwind fragments are not function entries.
- Project graph timestamp was September 3; the graph query did not include the
  September 5 hand-control results. Current handovers and R-084–R-087 take
  precedence. Fleet viewmodel guidance led to checking native IK before choosing
  a custom solver. Local CryEngine source supplied vocabulary, never offsets.

## 1. The render matrix and its coordinate system

### Exact object layout at RenderCHR

`0x81D0D0` takes character in RCX, `SRendParams*` in RDX, `Matrix34*` in R8 and
pass information in R9. It retains character in RDI, render object in RBX and
input matrix in R14.

| Object offset | Meaning on this path | Static evidence |
| --- | --- | --- |
| `+0x00..+0x2F` | Twelve floats, row-major 3x4 model/render transform | `0x81D194..0x81D1E3` copies each input float |
| `+0x0C,+0x1C,+0x2C` | Translation column | Stores at `0x81D1AB`, `0x81D1C7`, `0x81D1E3` |
| `+0x40` | Render flags; `FOB_NEAREST = 0x800000` | Existing R-085 marker and flag construction |
| `+0x90` | `SRendParams+0x48`, instance token | Copy at `0x81D20A..0x81D20E` |
| `+0x98` | Skinning data | Existing assignment at `0x81D377` |

Basis vectors are **columns**, not contiguous rows: X is floats at
`+0,+0x10,+0x20`, Y at `+4,+0x14,+0x24`, Z at `+8,+0x18,+0x28`.
Copy the matrix values into owned storage; do not keep the render-object pointer
as a durable model frame.

On the entity route, the instance token at object `+0x90` is the
`CEntityObject*` slot: `0x9734A6` writes slot into `SRendParams+0x48`.
Slot fields are local-matrix pointer `+0`, world matrix `+8`, optional
camera-space-position pointer `+0x68`, character pointer `+0x80`, and render
flags byte `+0xAC` (near bit `0x2`). This interpretation is conditional on that
route; other render callers may supply another instance token. Validate the
slot's character against RDI before interpreting it.

### Where the camera translation is removed

`CEntityObject::Render`, **`0x973400`**, builds the character matrix and calls
character vtable `+0xC0`, **`0x81BCB0`**. The latter composes the render matrix
with its QuatTS offset and passes the result to RenderCHR. It can also apply a
character-orientation override, so the final RenderCHR matrix is authoritative.

In the near character branch:

- `0x974551` tests the slot-near condition.
- `0x97455A` tests slot `+0x68`.
- With no camera-space-position pointer, `0x974735` calls
  `ISystem::GetViewCamera` through `+0x388`. `0x974743..0x974780` negate camera
  translation at `+0x0C,+0x1C,+0x2C` and add it to the model translation.
  **The matrix basis is not multiplied by inverse camera rotation.**
- With that pointer present, `0x97456F..0x97472E` derives a translation using
  the camera basis and the supplied camera-space position. The explicit
  cofactor expression is inverse-transpose of the 3x3 camera basis; for an
  orthonormal camera it is the camera rotation itself. It sets translation
  without converting the whole matrix into view space.
- The near branch resets the extra QuatTS offset to identity at
  `0x974785..0x9747B6`, then invokes character render at `0x9747F6`.

For the ordinary near branch, if W denotes the pre-subtraction transform and C
the camera position sampled by this code:

```text
Mnear = T(-C) * W
Mworld = the actual matrix of the world-drawn instance
```

The special camera-space-position route can produce a different effective
placement from the slot's stored world matrix. Reading slot.worldTM instead of
the final render matrix is therefore not a general solution.

### Controller conversion

Let `Pworld` already be converted from OpenXR into the game's world axes and
units by the mod's tracking reference. Use:

```text
world-drawn instance: Pmodel = inverse(Mworld) * Pworld
near-drawn instance:  Pmodel = inverse(Mnear)  * (Pworld - C)
```

C must match the camera-origin convention and sample that built Mnear. Do not
substitute an arbitrary later per-eye camera or apply half an IPD twice; the
live lane already recorded that failure. If tracking supplies a displacement
relative to that same C, it is already the input to `inverse(Mnear)`.

For a rigid basis, wrist orientation is
`qModel = inverse(qRenderBasis) * qWorld * qGripCalibration`. Uniform scale must
be removed before extracting its rotation; nonuniform scale/shear needs an
explicit rotation extraction policy. Position uses a full affine inverse, not
the transpose shortcut unless orthonormality has been checked.

Two instances sharing a default skeleton share joint vocabulary and bind data,
not necessarily placement. Even if their effective W were identical,
`Mnear != Mworld` when C is nonzero. Static code cannot prove the actual pair of
matrices for the two R-085 instances. Keep separate per-instance samples and
compare them; never share the near inverse with the shadow solely by rig ID.

### Timing: move the observation before dispatch

The exact order in RenderCHR is:

```text
0x81D1E3   last matrix float stored
0x81D20E   instance token stored
0x81D26C   renderer GetSkinningPoolID, returns EAX
0x81D272   MOV R12D,EAX
0x81D275   next instruction, pool ID now in R12D
...        obtain/cache skinning data for character and pool frame
0x81D35F   call 0x82D7A0, schedules skinning work
0x81D377   existing object+0x98 marker
```

**Proposed marker relocation: `0x81D272` before its instruction**, where RBX is
the render object, RDI is character and EAX is pool ID. This can replace the
current execute observation; it need not add another marker just to find the
matrix. Preserve all intercepted registers and instruction semantics.

Publish an immutable sample keyed by character and pool frame, with matrix,
near flag, origin provenance and validity. A worker must refuse missing/stale
samples. Release/acquire alone is not enough if a shared struct can be overwritten
while another worker copies it; use owned snapshots or a suitable synchronization
scheme. `GetSkinningData` (`0x81BB60`) is another job-request path, and a cached
job may already exist. Moving this marker proves ordering for this caller only;
the live fixture must establish which caller requests the selected instance's job
first. Do not report the race closed solely because counters increase.

## 2. Native arm solving and modifier reachability

### Direct native leaf at 0x871CA0

`PoseModifierHelper::IK_Solver2Bones` has the x64 ABI:

```cpp
// Names reconstructed; addresses/layouts checked against Steam bytes.
void IK_Solver2Bones(const Vec3* modelGoal, const IKLimbType* limb, CPoseData* pose);
// RCX=modelGoal, RDX=limb, R8=pose; return type void.
```

It reads relative QuatT array at pose `+0x10`, absolute QuatT array at `+0x18`,
stride `0x1C` (quaternion XYZW then translation XYZ). It writes **both** arrays.
It reads limb `+0x18` as a joint-chain array with `0x10`-byte records:

| Record | Index field | Role |
| --- | --- | --- |
| 0 | `chain+0x00` | Parent of the chain's ball/root joint |
| 1 | `chain+0x10` | Upper-arm/root joint |
| 2 | `chain+0x20` | Elbow/link joint |
| 3 | `chain+0x30` | Wrist/end joint |

Limb `+0x28` is an int16 root-to-end path used for reconstruction during stretch;
its CryArray count is at data `-4`, masked with `0x7FFFFFFF`. This is a real limb
definition, not three arbitrary indices. Validate chain sizes, indices, parent
relationships and the name-resolved selected skeleton before calling it.

The known animation-driven path `0x877B50` compares limb `+8` with ASCII
`2BIK` (`0x4B494232`) at `0x877F7D`, then calls this leaf at `0x877FE1`.
The neighboring solver branches call `0x872C60` for `3BIK` and `0x874810` for
`CCDX`. R-077 proves its parent path executes live, but did not prove this
specific tag/branch for the selected instance. The code is not a cvar stub.

Native behavior matters:

- No pole vector, elbow target, or continuity/history argument exists. The bend
  plane comes from the **current posed chain**, followed by alignment to the goal.
- A near-zero goal displacement is skipped (squared distance below `1e-10`).
- If unreachable, relative translations of the elbow and wrist are scaled by
  `min(distance / (0.99999 * totalLength), 1.25)`. That stretches **both segments**;
  the cap is per call, not a persistent maximum relative to the original pose.
- It guards small segment lengths (inverse length above 100), rejects a nearly
  collinear bend plane (normalized cross squared below `1e-5`), and clamps the
  law-of-cosines value to `[-0.99,0.99]`. It is not a guaranteed exact endpoint
  solver, particularly near singular or reach-limit configurations.
- It does not finish every descendant and does not accept a wrist orientation.
  Reconstruct affected descendants from consistent relative poses and apply the
  controller wrist orientation separately, preserving finger animation.

`0x874210` is the higher-level native dispatcher. Its arguments are default
skeleton, pointer to an **opaque 64-bit limb-definition handle**, model goal and
pose (RCX/RDX/R8/R9). It resolves through skeleton vtable `+0x68`, uses the limb
table at skeleton `+0x68` with stride `0x30`, rebuilds the root chain, dispatches
by tag, then rebuilds entries from limb `+0x20`. Do not reinterpret the handle as
a joint ID or assume a CE5 CRC/string API. Its success return means the handle
resolved, not that the endpoint reached the goal.

`0x871960` is a recursive native descendant reconstruction helper. It consumes
default-skeleton child metadata (`joint+0x10` first-child displacement,
`joint+0x14` uint16 child count) and private relative/absolute poses. A validated
parent traversal already available to the mod is also suitable; do not propagate
from stale relative poses after imposing a wrist transform.

**Consumer integration:** clone relative and absolute arrays from the same
finished pose, keep both private, solve before the original `0x82EE10`, reconstruct
children and pass the private absolute result to the existing conversion. Do not
call the native solver on the current 32-byte pose-prefix copy while its `+0x10`
still points to engine-owned memory. Do not feed an already teleported wrist with
an unchanged relative chain into the solver. For fixed avatar proportions,
pre-clamp to inside the native stretch threshold and still measure output
segment lengths. Persistent elbow orientation across singularities remains a
mod-level requirement; the native routine does not solve that policy.

### AnimationPoseModifier_Ik2Segments is also implemented

Factory creation leads through `0x7E5860 -> 0x7EE5B0 -> 0x7EEB60` to an object
of size `0x98`, with IAnimationPoseModifier vtable `0x1D1FBF8`:

- `+0x20 -> 0x7E3A20`, Prepare: resolves root/link/end and optional target/weight
  nodes, cached as indices at object `+0x80..+0x90`; initialized byte `+0x94`.
- `+0x28 -> 0x7E3B00`, Execute: contains the two-segment math, chain writes and
  descendant propagation. Uses end offset `+0x60`, target offset `+0x6C`, weight
  `+0x78`; no pole/hint parameter. It also guards degenerate geometry and clamps
  the cosine to `[-0.99,0.99]`.

This is a second positive implementation check, not merely a factory string.
Calling this larger method requires a real modifier object, valid description
and CPoseData virtual contract. The small prefix sufficient for the direct leaf
is not sufficient for arbitrary modifier methods.

### CreateIKLimb survives, but is an indirect legacy route

```text
0x1807520   script registration
0x1809C60   CreateIKLimb adapter, resolves actor
0x16CB760   CActor::CreateIKLimb, resolves skeleton joint names and appends record
0x16D8290   ProcessIKLimbs, iterates actor+0x1390..+0x1398, record stride 0x9C
0x16DC190   SIKLimb::Update, blends/recoveries and conditional hand requests
0x8345E0   skeleton-pose vtable+0x148, SetHumanLimbIK
0x86D910   native LimbIk setup insertion/replacement
```

The iteration has a direct caller at `0x17CB396` within `0x17CB350`, gated by an
entity query. SIKLimb::Update gets the configured character slot; if neither
blend nor recovery is active it updates its bookkeeping and returns without a
solve. Right/left flags select legacy `RgtArm01`/`LftArm01` request arguments.
The inspected Prey SetHumanLimbIK consumes an eight-byte handle through its third
argument; do not copy the CE5 string/CRC signature. A registered legacy name does
not prove the selected rig has a matching native handle.

SetHumanLimbIK lazily creates `AnimationPoseModifier_LimbIk`, converts a world
target through inverse character location (`character+0xA90` QuatTS), and queues
the handle/goal. `0x86D910` caps setups at 16. Thus creation, iteration and setup
insertion are **compiled and connected**, rather than stripped registrations.
Whether the player owns a nonempty actor limb vector, its update caller runs for
that player, and its requested handles match the 101-joint FP rig remain runtime
conditions. None is needed to reuse the direct leaf on private data.

### CPoseModifierSetup and the first-entry loop

Character embeds skeleton animation at `+0x140` and skeleton pose at `+0x700`.
`0x8390E0` prepares modifiers:

- Setup shared pointer is at `SkeletonAnim+0x458` (**character+0x598**).
- If nonnull, setup object's stack shared pointer at `+0x20/+0x28` is enqueued
  with layer `-1`, name `Setup`.
- `PushPoseModifier` is **`0x839860`**, skeleton-animation vtable `+0x120`.
  It routes layer `-1` to main queue `+0x440`, layers 0..15 to
  `+0x68 + layer*0x40`, and handles shared-pointer ownership and buffer state.
- LimbIK is queued at layer 15 when skeleton-pose byte `+0x308` has bit 2 and
  byte `+0x134` lacks bit `0x20`. These are real conditional consumers; finding
  an inert console control did not demonstrate that all IK code was stripped.
- Animation task `0x8384B0` calls Prepare at `0x838564`; command execution
  `0x877600` runs animation-driven IK and physics, then the main queue at
  `0x87799E..0x8779D2`, before redirected attachment processing.

The main queue is character `+0x580/+0x588`, active-buffer selector `+0x590`,
entry stride `0x18`; the modifier object is entry `+8`. Its loop is literally:

```asm
8779C0  mov rcx,[r15+8]
8779C4  lea rdx,[rbp+30h]
8779C8  mov rax,[rcx]
8779CB  call qword ptr [rax+28h]  ; Execute
8779CE  sub rbx,1
8779D2  jne 8779C0               ; no advance of R15
```

This is checked instruction evidence, not a decompiler simplification. An
additional top-level layer -1 entry after Setup is not a reliable append route.
The **nested** stack is different: vtable `0x1D203F8`, Execute `0x7F31A0`, vector
begin/end at stack `+8/+0x10`, `0x10`-byte shared-pointer entries. That function
advances through every entry and invokes each Execute correctly.

Appending *inside* the existing nested stack is structurally viable, but the
native allocator/refcount ABI and update ownership must be respected; do not
write a raw pointer into its vector. Verify that it is the first top-level entry
and that the top-level count is one, or the whole nested stack can be repeated.
A later instruction patch to repair the
top-level loop is unnecessary for this hand consumer task. The exact runtime
first-person Setup pointer/stack contents cannot be established statically.

## 3. Attachment persistence and current-pose compensation

The concrete bone-attachment vtable is **`0x1D212B8`**, independently cross-checked
against AddBinding and its implementations:

| Slot | RVA | Meaning |
| --- | --- | --- |
| `+0x48` | `0x828D70` | SetAttAbsoluteDefault |
| `+0x50` | `0x822C70` | GetAttAbsoluteDefault, returns this+0x114 |
| `+0x58` | `0x828DC0` | SetAttRelativeDefault |
| `+0x68` | `0x822C80` | GetAttModelRelative, returns this+0x130 |
| `+0x80` | `0x7A2560` | ProjectAttachment |
| `+0xB8` | `0x7A21B0` | AlignJointAttachment |
| `+0xD8` | `0x7A2110` | AddBinding |

Bone attachment fields: flags `+8`, binding object `+0x20`, manager `+0x28`,
relative default QuatT `+0xF8`, absolute default QuatT `+0x114`, current model
QuatT `+0x130`, extra quaternion `+0x14C`, joint index `+0x15C`, joint-name token
`+0x160`. Manager `+0x18` points to its character. These are Prey layouts;
do not overlay the CE5 C++ class.

SetAbs copies 28 bytes to `+0x114` and clears projected flag `0x4000`. SetRel only
copies to `+0xF8`. `ProjectAttachment` resolves the joint and computes:

```text
relativeDefault = inverse(defaultAbsJoint) * absoluteDefault
```

It sets projected again. Normal bone updates `0x7A3390` (execute) and
`0x7A40E0` (static) reproject when required; redirected update `0x7A37E0`
recomputes relative default from the default bone and absolute default on each
call. This confirms the relative-default overwrite mechanism in this fork.
The header's warning is broader than the exact branches: a projected ordinary
attachment can retain a relative write until reprojection, while redirected
updates regenerate it each time. It is not a dependable controller surface.

The inspected animation paths read absolute default and write relative/current
mount fields; they do **not** restore absolute default every frame.
`AlignJointAttachment` **does** overwrite absolute default with the default
joint and reset relative default to identity. Its only raw direct reference
found is its vtable entry, so static analysis cannot rule out a per-frame
virtual caller elsewhere. Equip/rebind/setup and gameplay callers can invoke
SetAbs too. The supported conclusion is persistence through the inspected
normal updates, **not** a whole-program proof that nobody else writes it.

### Equation for the desired weapon pose

Ignoring simulation and the extra mount rotation initially, let B be the bind
joint absolute pose, J the **actual current joint pose used by attachment update**,
and G the desired current model-space mount:

```text
currentMount = J * inverse(B) * absoluteDefault
absoluteDefaultToWrite = B * inverse(J) * G
```

Writing G straight into SetAbs instead yields `J * inverse(B) * G`, retaining
animation motion. The current baseline-plus-controller-delta approach therefore
cannot guarantee an absolute controller pose merely by using the correct slot.

**Prey-specific rotation:** normal static/execute updates additionally
right-multiply orientation by quaternion K at attachment `+0x14C` (XYZW). Position
is unchanged by that step. Thus use `G0 = { qG * inverse(K), tG }` in the equation
above when K is nonidentity. The redirected path inspected does not include that
same step. Simulation helpers can further alter the current mount, so classify
the attachment's actual update route before promising exact orientation.

J must come from the attachment's owning character and its consumed pose. The
late skinning-only hand edits do not change the engine-owned pose that the
attachment update reads. Substituting the private VR wrist as J would compensate
the wrong animation transform unless attachment evaluation is deliberately
made to consume that same private pose.

### Timing is separate from persistence

SetAbs neither updates `+0x130` nor invokes the binding's ProcessAttachment. A
write inside `0x82EE10` is late, on a skinning worker; attachment evaluation can
already have run and other draw work can be reading the mount. Persistence of
`+0x114` does not establish same-frame visibility or thread safety.

The static attachment consumer routes are now concrete: manager `0x8297E0`
dispatches empty/static/execute attachment buckets, and `0x829940` dispatches
execute buckets; each calls bone execute `0x7A3390`. A current-frame weapon
placement should publish a desired pose for the appropriate attachment-update
owner/thread and consume it before composition/ProcessAttachment, or use a
separately validated late render-transform seam. Do not invoke the whole manager
again from the skinning worker: it has gameplay and simulation side effects.

## Bounded next live proofs for the owning agent

These are proposed experiments, not results of this static session. Keep one
variable per experiment and save a replayable numeric fixture.

1. **Frame hypothesis:** a matching Mnear and C remove the yaw approximation.
   Control: unmodified pose and the world-shadow instance. Variable: full inverse
   conversion on the selected near instance. Record owner, rig, pool ID, near
   flags, copied matrix, camera-origin provenance, target and round-trip residual.
   Require valid samples before the consumer uses them and stable placement while
   pitching/rolling/turning, with no camera/world/shadow displacement. Missing
   ordering evidence is a refusal, not a last-frame fallback disguised as success.
2. **Native IK hypothesis:** the leaf bends the selected chain on private data.
   Control: same private pose with solve bypassed. Variable: one name-resolved
   arm and small reachable model-space target. Record native solver tag/chain,
   both segment lengths, residual, wrist orientation and all affected joints.
   Require original relative/absolute buffers unchanged, finite output, expected
   arm motion and shadow unchanged. Extend to singular/reach-limit sweeps before
   deciding whether its elbow behavior meets the product requirement.
3. **Attachment hypothesis:** SetAbs survives and reaches current mount after
   its update. Control: retained original absolute default. Variable: a small
   compensated mount offset on the selected weapon. Record absolute default,
   projected bit, consumed J/B/K, current mount and ProcessAttachment ordering
   through animation, equip and reload. A surviving default with an unmoved
   current mount is a timing/consumer result, not a failed setter.

Local source counterparts: `CryEntitySystem/EntityObject.cpp`,
`CryAnimation/CharacterInstance.cpp`, `SkeletonAnim.cpp`,
`SkeletonAnim_Commands.cpp`, `PoseModifier/PoseModifierHelper.cpp`,
`PoseModifier/PoseModifier.cpp`, `AttachmentBone.cpp/.h`, and the older
`CryGame/Game/GameDll/Actor.cpp`. Differences called out above were established
from Prey bytes rather than copied from those trees.
