# H-005 — finished-pose consumer and independent hand takeover

**2026-09-05. Static findings; independent hand control has not been tested live.**
The running game was reserved for another agent. This investigation used local
engine source, Chairloader headers, Ghidra, and the installed DLL as a file.
No process attachment, injection, breakpoint, runtime write, or build deployment.

## Answer

The consumer is **`CCharInstance::SkinningTransformationsComputation` at Steam
RVA `0x82EE10`**. It reads the finished **absolute joint poses**, converts them
against the inverse bind pose into **32-byte dual quaternions**, then publishes
completion to the software-skinning path. The renderer subsequently waits for
the associated job and uploads those same bones.

The proposed takeover is at **entry to `0x82EE10`**, supplying a private copy of
its pose input with the selected arm/hand joints replaced. Let the original
function build and publish the skinning data. This bypasses the previously
observed scratch-target overwrite problem, retains the native conversion, and
does not move the camera or character root.

This resolves the handover's static question. It does **not** establish which
runtime character draws the first-person body, prove a visible displacement, or
complete controller integration. A precise experiment is specified below.

## Three corrections to the inherited interpretation

1. **The observed RSI values are not native limb handles on the active IK path.**
   At `0x877F79`, `IMUL RSI,R10,0x1C` makes RSI a byte offset into the joint arrays.
   R10 comes from the last joint-chain record (`0x877CFD`), and its parent is read
   from the default skeleton's joint table (`0x877D02`–`0x877D09`). Thus the
   observed `0x4EC` and `0x7E0` correspond to **end-effector joint candidates 45
   and 72**, respectively. Confirm their names in the selected runtime skeleton.
   On the low/invalid-blend path (`0x877CBF` / `0x877CD5` -> `0x878708`), the
   multiply is skipped and RSI retains the weight-joint index loaded at
   `0x877C6D`. Do not decode every log record as an end-effector offset.
2. **R-077's register labels do not identify character instances.**
   `0x877B8D` loads R13 from the entry owner's `+0x10` default-skeleton pointer.
   `0x877B96` loads RDI from `poseData+0x10` (relative poses); `0x877B9A` loads
   RBX from `poseData+0x18` (absolute poses). R12 is an IK-target entry within
   the absolute array, formed at `0x877C98` / `0x877CB1`. Distinct RDI values
   demonstrate distinct pose buffers; they do not alone prove distinct character
   instances or visible-body/shadow ownership. Preserve the observations, but
   reacquire identity at the consumer where the instance is an actual argument.
3. **Different meshes can share the same bone buffer.** Prey's remapped skinning
   allocator aliases the master bone pointer, job states, and character GPU
   buffer. Mesh-specific remaps distinguish their joint indexing. The handover's
   claim that two meshes must read different matrices is not a valid mapping
   test. Likewise, there is a conversion per cached character/pool frame, with
   multiple possible mesh, shadow, and render-pass consumers afterward.

## Exact target and reproduction

- Module: `PreyDll.dll`, Steam, preferred image base `0x180000000`.
- SHA-256: `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`.
- All addresses below are **RVAs**, unless explicitly described as field offsets.
- Run `py -3 tools/re/verify_h005_skinning.py` from the repo. It reads only the
  installed file, checks its hash and **16 instruction/vtable landmarks**, and
  reports `runtime_tested: false`. An optional argument selects a different file.
- Raw decompilations, assembly and PE-reference scans are in ignored
  `captures/re/h005/`. This document and the verifier carry the durable findings.

Chairloader's EGS addresses were search leads. Steam addresses below were checked
against the actual functions and vtables; a single EGS-to-Steam delta does not
work across this region.

## Consumer chain

```text
CCharInstance::GetSkinningData  81BB60
  or inlined allocation in RenderCHR  81D0D0
      -> cached SSkinningData per character and pool frame
      -> BeginSkinningTransformationsComputation  82D7A0
          -> job wrapper 82E580
              -> SkinningTransformationsComputation 82EE10  <-- proposed entry seam
                  absolute joint pose * inverse(default absolute joint pose)
                  -> SSkinningData.pBoneQuatsS
                  -> publish finished list / launch waiting software jobs

Hardware path:
  FX_UpdateCharCBs F3CC20
      -> wait pAsyncJobs
      -> UpdateBuffer 107EE50, nNumBones * 32 bytes
      -> FX_DrawBatchSkinned F0EE30 binds current/previous character buffers

Attachment path:
  GetVertexTransformationData 7AC9F0
      -> master GetSkinningData if necessary
      -> EF_CreateRemappedSkinningData FE05F0
          shares master bones/CB/jobs; adds attachment remap table
      -> DrawAttachment 7AABE0 chooses hardware or software skinning
```

### Conversion arguments and ordering

At **entry** to `0x82EE10`, the Win64 arguments are:

| Location | Meaning |
|---|---|
| RCX | `CCharInstance*`; carried by the job, although the conversion body does not need to dereference it |
| RDX | current master `SSkinningData*` |
| R8 | `CDefaultSkeleton*` |
| R9D | render LOD; job builder supplies zero |
| `[entry RSP+0x28]` | `const Skeleton::CPoseData*` |
| `[entry RSP+0x30]` | movement-accumulator `float*` |

`0x82E580` forwards six fields from its job argument block: owner `+0x00`, skinning
`+0x08`, default skeleton `+0x10`, LOD `+0x18`, pose-data `+0x20`, movement `+0x28`.
`0x82D7A0` builds that block from `character+0x10`, **`character+0x960`**, and
`character+0xAD4`. It registers the job with `skinning+0x18`. Its job-name string
is `SkinningTransformationsComputation` at `0x1D22418`.

The conversion loads the input pose pointer at `0x82EE78` and its absolute array
at **`0x82EE8C`**, then gets the joint count through default-skeleton vtable `+8`.
The input array is read-only in this function. Each input is a QuatT, stride
`0x1C`; each output is a DualQuat, stride `0x20`. The body performs:

```text
skin[j] = DualQuat(absolutePose[j] * inverse(bindAbsolute[j]))
```

It makes quaternion signs consistent with each parent's output and computes a
movement measure against the previous frame. The current and previous arrays
can alias on the first usable frame; leave the native behavior intact.

**Publication is inside the function, not after its return.** At `0x82F860`–
`0x82F873`, an atomic compare/exchange clears `*pMasterSkinningDataList`. Waiting
software jobs are started via `0x819D90` at **`0x82F88B`**. An attachment arriving
after publication can also start its job directly (`0x7AABE0`). A post-return
bone overwrite is therefore too late to be a general hardware/software solution.

### Byte-verified layouts

These are the consumed fields, not complete C++ class definitions.

| Object | Offset | Meaning / evidence |
|---|---|---|
| `CCharInstance` | `+0x10` | default skeleton, loaded by job builder |
| | `+0x98 + 0x10*k` | cached master skinning pointer, `k=poolFrame%3` |
| | `+0xA0 + 0x10*k` | matching cached pool-frame ID |
| | `+0x960` | embedded final pose-data object passed to conversion |
| | `+0x978` | its absolute-pose pointer (`poseData+0x18`) |
| | `+0xAD0` | skinning transformation count used for allocation |
| | `+0xAD4` | movement accumulator |
| `CDefaultSkeleton` | `+0x08` | joint-record array; count is `u32(array-4) & 0x7fffffff` |
| | `+0x30` | default/bind absolute QuatT array |
| | `+0x158` | GUID passed when making attachment remaps |
| | `+0x178` | model-path string pointer, used by destructor diagnostics |
| | `+0x238` | object type; job builder requires `0x11223344` (CHR) |
| Joint record | stride `0xA8` | Prey stride, checked in conversion and accessors |
| | `+0x00` | joint-name string pointer |
| | `+0x18` | signed 16-bit parent index; `-1` is root |

The default-skeleton vtable at **`0x1D2A3E8`** is installed by its destructor
`0x8B99C0`, which names `CDefaultSkeleton` and prints the model path. Its accessors
are short functions that Ghidra had not yet defined; their raw instructions and
vtable entries were checked without needing to run them:

| Vtable byte offset | Steam function | Meaning |
|---|---|---|
| `+0x08` | `0x8C1190` | joint count |
| `+0x10` | `0x8BC330` | parent by joint ID; bounds check, stride `0xA8`, signed load at `+0x18` |
| `+0x30` | `0x8BC300` | name by joint ID; bounds check, stride `0xA8`, pointer at `+0x00` |

These allow a passive topology dump. Check indices 45 and 72 first, including
names, parents and descendants. Do not substitute the target-helper joint named
`*IKTarget` for the deforming hand joint merely because its name sounds relevant.

| `SSkinningData` offset | Meaning |
|---|---|
| `+0x00`, `+0x04` | `uint32 nNumBones`, hardware-skinning flags |
| `+0x08` | master DualQuat array |
| `+0x10` | attachment remap-table pointer; null on master |
| `+0x18`, `+0x20` | async job and async data-job state pointers |
| `+0x28` | previous skinning data |
| `+0x30` | remap GUID |
| `+0x38` | `SCharacterInstanceCB*` |
| `+0x40` | custom data, including software vertex-animation job |
| `+0x48` | pointer to master software-job list |
| `+0x50` | next-list node / master's list storage |
| `+0x58..0x60` | precision-offset vector |

`0xFE06B0` allocates a 0x70-byte-aligned header, optional job states, and the
bone array. `0xFE05F0` makes an attachment header and **aliases** master `+8`,
`+0x18`, `+0x20`, and `+0x38`. Its `+0x48` points to master `+0x50`.
Renderer vtable `0x1DD2E08` slots `+0x800`, `+0x808`, `+0x810`, `+0xAA0` are
respectively create, create-remapped, pool-ID (`0xFE0AE0`), allocate-CB (`0xF39450`).

## Correlating a character to pixels

`RenderCHR` (`0x81D0D0`) receives the character in RCX, render parameters in RDX,
render Matrix34 in R8 and pass information in R9. It copies the matrix into a
`CRenderObject` and writes its skinning pointer at **`object+0x98`**, specifically
`0x81D377` (`RBX=object`, `RBP=skinning`, `RDI=character` at that instruction).
The object's flags at `+0x40` include **FOB_NEAREST `0x800000`**, explicitly set
or cleared according to the render parameters / character flags.

For skin attachments, `0x7AC9F0` gets the master character from
`*(attachment+0x30)+0x18`. It stores the remap pointer from `attachment+0x38`
into `skinning+0x10`, and caches headers at `attachment+0x3D8 + k*0x10`
with frame IDs at `+0x3E0 + k*0x10`. The caller `0x7AABE0` places the returned
header in the render object's `+0x98` as well.

In `FX_DrawBatchSkinned` (`0xF0EE30`), each batch object is loaded separately.
At `0xF0F224`, RBX becomes that object's skinning pointer. At `0xF0F233`, Prey
checks/selects its remap GUID through `0x1000BD0`. At `0xF0F23C` / `0xF0F240`,
it follows `skinning+0x38 -> CB+0` to the character constant buffer. The current
and previous native GPU buffers are placed at renderer `+0xDE0` / `+0xDE8`.
Do not equate entry argument R9 with every object's skinning header in this batch.

`SCharacterInstanceCB` is 0x28 bytes: buffer pointer `+0`, skinning pointer `+8`,
list node `+0x10`, updated byte `+0x20`. Allocation requests **0x6000 bytes**
(768 DualQuats). `FX_UpdateCharCBs` walks renderer
`+0xAE48 + (u32(renderer+0x80EC)%3)*0x10`. It waits at `0xF3CC8D`, then calls
`UpdateBuffer` at **`0xF3CCA5`** with:

```text
RCX = character constant-buffer wrapper
RDX = skinning.pBoneQuatsS
R8  = nNumBones << 5
R9D = 1
RSI = SSkinningData*
RBX = SCharacterInstanceCB* + 0x10
return address in UpdateBuffer = module + 0xF3CCAA
```

This is a useful hardware-path observation point. A global `UpdateBuffer` hook
without the caller filter also sees unrelated buffers, including camera uploads.
The source and Prey both skip already-updated CBs, so the number of calls is not
the number of meshes or eye draws.

Record `(poolFrame, character, defaultSkeleton, modelPath, masterSkinning,
bonePointer, attachmentSkinning, remap, renderObject, objectFlags, pass, CB)`.
Join through shared bones/CB and explicit master ownership. FOB_NEAREST helps
classify a draw but does not alone prove that a mesh is the visible hands. A
default skeleton can be shared by several character instances.

## Proposed takeover implementation

**Hypothesis:** substituting final absolute joints before native conversion will
produce stable independent hand deformation while leaving the root and torso
poses unchanged. **Control:** forwarding untouched inputs through the same seam.
**Variable:** one named deforming hand and its descendants in one selected
character. **Decision rule:** sustained corresponding mesh motion with untouched
control joints, confirmed at the consumed skinning data and pixels.

At conversion entry, validate the module/landmarks, chosen character lifetime,
default skeleton, joint count, buffer capacity, topology, finite transforms and
usable controller snapshot. Use a separate bounded scratch array for each
concurrent invocation. Copy the absolute QuatTs and supply the original function
with a private pose-data view whose `+0x18` points at that copy. The current
function only reads this one field of the pose-data object; a copied 0x20-byte
prefix is enough **for these exact bytes**, not a general `CPoseData` clone/API.
Retain the storage until the original synchronous conversion returns. Do not
change a shared pose pointer temporarily: other animation readers may exist.
Call the original **exactly once per scheduled job**. Calling it twice on the
same skinning packet for an A/B comparison would repeat the list-publication
protocol after its state has already changed; that is not a valid experiment.

Conceptually:

```cpp
// Pseudocode only; no runtime hook is installed by this investigation.
onCompute(owner, skinning, skeleton, lod, poseData, movement) {
    if (!selectedAndValidated(owner, skinning, skeleton))
        return original(owner, skinning, skeleton, lod, poseData, movement);
    auto joints = copyAbsoluteJoints(poseData, skeleton);
    applyIndependentHandsInSkeletonSpace(joints, controllerSnapshot);
    auto input = copyPosePrefixWithAbsolutePointer(poseData, joints.data());
    return original(owner, skinning, skeleton, lod, &input, movement);
}
```

For a hand-only discriminator, compute a model-space rigid delta from the old
wrist to the desired wrist and apply it to the wrist **and all its descendants**:

```text
delta = desiredWrist * inverse(oldAbsoluteWrist)
newAbsolute[j] = delta * oldAbsolute[j]    for wrist subtree j
```

Do this separately for left and right, with disjoint subtree masks. Changing
only a wrist while leaving finger absolute poses unchanged tears the hand. The
first test may stretch the wrist/forearm transition; that is a discriminator,
not the completed arm solution. The product implementation should solve each
upper-arm/forearm/wrist chain, retain segment lengths and elbow constraints, and
propagate its new local transforms to finger/twist descendants. It can replace
the game's arm animation completely; preserving the engine idle is not required.

Controller poses already have a tested world conversion in
`src/common/MotionController.cpp` (`ControllerPoseInWorld`). Map that world pose
into the selected skeleton's model frame and include controller-to-wrist grip
calibration. Establish the actual render/model transform first, particularly
for camera-relative near objects. Do not feed raw OpenXR coordinates, world
positions, or the R-077 scratch-target transforms straight into absolute joints.

Do not write XYZ directly at DualQuat `+0x10`: that is the dual quaternion part,
not a position. Let the native function perform inverse-bind conversion and
hemisphere correction. Nor should a post-original hook be used to patch the
master bones: software consumers may already be running by then.

This visual seam does not update gameplay aim, collision, attachment transforms,
or animation bounds. The weapon binding remains a separate H-005 work item
(`AttachToHand` R-024, `0x16914F0`). A hand can deform here while a rigid weapon
attachment retains its earlier transform. Large arm excursions also need a
separate bounds/culling check. These are limits of the located consumer, not
evidence that its per-hand deformation fails.

## First live proof, for the agent owning the process

1. With active gameplay, observe the conversion and dump each instance's
   topology/model path. Resolve names for candidate joints 45 and 72. Correlate
   the instance with render objects/attachments and their consumed skinning data.
   Preserve the full mapping as a replayable fixture. No writes in this step.
2. Run the selected-instance hook with an unchanged private input, invoking the
   original once. Require byte-identical source/copy joints and normal pixels.
   Save input joints, bind pose, parents, previous bones and output so conversion
   can be checked offline. Compare matched input records against a baseline run;
   never replay the native function twice on the same live skinning packet.
3. On one confirmed wrist subtree, apply a fixed **0.10 m model-space Z shift**
   for two seconds, then forward untouched inputs. Use the same displacement
   for the other wrist in a separate run. Verify a continuous visible change,
   source/copy/output values, unchanged opposite-hand/root/torso transforms, and
   return to baseline. A trap counter or occasional flicker is insufficient.
4. Repeat across a weapon swap; map shadows separately. If GPU bones change but
   only a shadow moves, ownership is wrong. If software vertices remain unchanged,
   verify their master/remap and that substitution happened before publication.
   Do not return to racing the R-077 target buffers.
5. Once the visual seam passes, wire two controller snapshots, arm-chain math,
   wrist calibration, tracking-loss behavior and weapon handling. Extract the
   deterministic pose transforms and captured layouts into the headless suite
   before promoting any live hook, per `HEADLESS_TESTING.md`.

## Provenance

The project graph (2026-09-03 snapshot) led to existing controller math; it
predates R-083 and did not answer the final consumer question. Fleet hand/pose
guidance in `VR Modding/docs/02-viewmodels-and-hands.md` provided topology and
coordinate-frame checks, not Prey offsets.

Local primary reference code:

- `D:/Dev Debug/source code/CRYENGINE/Code/CryEngine/CryAnimation/CharacterInstance.cpp:483,623`
  — job setup, conversion and publication.
- `.../CryAnimation/CharacterInstance_Render.cpp:189,330` — render ownership/cache.
- `.../CryAnimation/AttachmentSkin.cpp:592,811` — render data, remaps and software jobs.
- `.../CryAnimation/Model.h:325` — joint vocabulary/layout, checked against Prey.
- `.../CryCommon/CryRenderer/RenderObject.h:88` — skinning-data vocabulary.
- `.../RenderDll/Common/Renderer.cpp:4658,4701` — master/remapped allocations.
- `.../RenderDll/XRenderD3D9/D3DRendPipeline.cpp:5621,5659` — wait/upload and CB allocation.
- `tools/Chairloader-src/Common/Prey/RenderDll/XRenderD3D9/DriverD3D.h:1215,1216,1262`
  — EGS function-name leads; all reported Steam endpoints checked independently.

No static finding here has been promoted to a reproduced runtime result.
