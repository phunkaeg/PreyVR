# Left-hand lightweight grabbing: static seam checkpoint

No grabbing or throwing feature is shipped by this checkpoint. This records the
Steam carry research while the posture UI and body-slot messages are implemented.
Runtime acceptance, controller ownership and resource cleanup are separate gates.

## Target and method

Steam x64 `PreyDll.dll`, image base `0x180000000`, SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
Ghidra requests explicitly select `/Prey/PreyDll.dll`; the bridge's current
global program is another project and must not be changed underneath its owner.

The local `[WIN] Prey [2021-08-19]` EGS archive supplies named function leads.
`tools/re/map_carry_candidates.py` decodes relocation-masked instruction patterns
and checks the Steam hash. Its output is a candidate list, not a native ABI gate.
Patterns can extend beyond short leaf functions into neighbours; a unique match
does not promote those bodies or prove their receiver/vtable. Never infer a
module-wide constant RVA delta from this local group.

Raw Steam decompiles/assembly and candidate output are retained under
`docs/evidence/physical-grab-2026-10-04/`. Decompiler types are not authoritative;
register producers and consumers must resolve ambiguous parameters.

## Inspected Steam producer/consumer contracts

| Steam RVA | Role and evidence |
| --- | --- |
| `0x1593690` | `ArkPlayerInteraction::Interact`: resolves current usable entity ID at `interaction+0x46C`, reads one 0x18-byte info record at `+0x128+mode*0x18`, calls the evaluator, then script callbacks and the native action dispatcher. |
| `0x1593D50` | `PopulateInteractionInfo(IEntity*, Info[4]*)`: initializes four records, invokes `GetUsable` / native listeners, maps action names and carry hold duration. |
| `0x1594860` | `TestInteractionType(interaction, Result*, IEntity*, int mode, Info*)`: caller/entry assembly places mode in R9D and the fifth pointer on the stack. Carry action type is 6. Leverage, current carry state, standing-on-object and carry-restore timing checks remain native. |
| `0x1593980` | `PerformInteraction`: type 6 calls Drop and Start with receiver `interaction+0x28`. Only mode 4 or non-positive hold duration enters immediate carry. Positive duration starts the native hold path. |
| `0x125C3D0` | Start carrying: runs native carry initialization, ability checks and callbacks; this alone does not replace the interaction evaluator's eligibility checks. |
| `0x125A750` | `GetLerpTargetLocation(carry, QuatT* out, IEntity*, original Quaternion*)`: output is quaternion xyzw followed by position xyz. A potential narrow controller-target seam, consumed by native carry smoothing. |
| `0x125E400` | Carry update: consumes the target through the entity grabber/physics path and handles native pending throw/restore state. |
| `0x125A250` | Drop: guarded by native carry/restore state; may refuse temporarily. It calls Stop with zero throw scalar. |
| `0x125CF70` | Stop: native physics/collision restoration, inherited player velocity, callbacks and weapon restoration. Safe-carry handling includes camera-relative placement; it needs an explicit policy before controller carry can admit those props. |
| `0x125E2D0` | Throw request: sets a pending flag; it does not immediately release and does not supply tracked hand velocity. |
| `0x1231940` | Scaled physics impulse producer: input is mass followed by the existing 56-byte action. R9D proves part ID is an integer despite the decompiler's float guess. Queue mode is the fifth integer argument. Existing PhysicalNative contracts already gate this body. |

The project previously proved the embedded interaction receiver at `player+0xAC8`.
The dispatcher establishes carry at `player+0xAF0`; the carry consumers read its
entity ID at `+0xB0`. A caller-scoped override of the target output can preserve
native collision/smoothing. An override must verify the local player, exact
owned entity, current session/reference, coherent fresh pose and native thread.
It must not affect ordinary native carry or repeat a simulation callback.

## Temporary data ownership matters

Interaction info is 4 × 0x18 bytes; each record owns a string pointer at +8.
The evaluator returns a 0x28-byte result owning four strings at +8/+0x10/+0x18/+0x20.
The actual Interact assembly releases their headers at **pointer minus 0xC**,
decrements nonnegative reference counts and calls native free at `0xA8B20` when
the count reaches zero. Native empty storage is read through `0x224D03C` in the
evaluator. A zeroed or mod-CRT string substitute is not an ownership contract.

`GetEntityProperties` at `0x14AD0F0` returns an add-referenced script table;
`0x10D7E60` can query `bUseSafeCarry`. Crucially, helper `0x14ACEF0` is a
**copy constructor**, not assignment or destruction: it AddRefs and does not
release prior storage. Using it with an empty source would leak the property table.

## Remaining shipping gates

1. Complete concrete Steam callee proof for physics-to-entity conversion and
   script-table release. Leads are `0x921A50` and `0xD13F30`; Ghidra has no defined
   function at those leaf addresses. Their pattern hits are not enough to call.
2. Preserve the full Interact script/listener sequence for a controller-selected
   entity without writing over global cached target/info fields. Calling Start
   or only Perform directly would skip behaviour used by ordinary interaction.
3. Own initialized temporary info/result strings and property-table references,
   with native cleanup on each refusal and exception path.
4. Initially admit exact lightweight rigid props only. Refuse characters,
   constrained/safe-carry props and actions requiring a hold duration until their
   correct native paths are implemented. Native leverage restrictions must stay.
5. Add left-grip arbitration with medkit, foregrip, psychoscope and modal input;
   invalidation must release through the native guarded lifecycle rather than
   clearing carry IDs or bypassing collision-restore timers.
6. Derive throw velocity from hand motion in reference space, excluding player
   locomotion/snap turns, then bound its native impulse. The game's scalar Throw
   request is not equivalent to physical throwing.

The next cheap proof is offline decoding of the two leaf candidates plus their
concrete vtables/native callers, followed by a scoped interaction contract that
retains script callbacks. No additional game launch is required for those proofs.
