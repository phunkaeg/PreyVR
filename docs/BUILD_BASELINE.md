# Build baseline

## Target installation

`D:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release`

| Module | Role | Size | SHA-256 |
| --- | --- | ---: | --- |
| `Prey.exe` | Launcher / bootstrap executable | 567,808 bytes | `F179987F9786C57F9394A93B1009FC3AED3001E629C5B0B6CFF11882C55199F1` |
| `PreyDll.dll` | Primary game/engine module | 39,789,568 bytes | `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7` |

`Prey.exe` reports product/file version `1.0.1.0`. Record any Steam update or altered file hash as a new baseline before reusing a result.

## Ghidra state

The active shared Ghidra project is `SS2_VR`. The matching installed `Prey.exe` was already imported under `/Prey/Prey.exe`; `PreyDll.dll` was imported under `/Prey/PreyDll.dll` on 2026-07-30 with automatic analysis enabled.

`PreyDll.dll` is an x86-64 PE with image base `0x180000000`, 8 memory blocks, and 79,303 discovered functions after the initial analysis pass.

All static findings must name the exact module and use an RVA in addition to the preferred image address. `PreyDll.dll` is the first engine-analysis target; do not mistake launcher imports or DXGI forwarding for the actual renderer ownership.

## PDB-backed reference bridge

[Chairloader](https://github.com/thelivingdiamond/Chairloader) targets one canonical Epic Games Store build whose PDB was released. Its generated `Common/Prey` headers name Arkane classes, members, and function RVAs for that build. Chairloader's version database also contains the exact installed Steam hash above and the binary diff needed to reconstruct the canonical reference image.

| Reference | Size | SHA-256 | Ghidra program |
| --- | ---: | --- | --- |
| Canonical EGS `PreyDll.dll` | 39,740,416 bytes | `0485C85BB741D6D2E5AF69114BAE54C521540D5868AAC476A8D9FBD1899F0A63` | `/Prey/reference/PreyVR-PreyDll-EGS-0485c85b.dll` |

The EGS image was reconstructed in the user's temporary directory from the unmodified installed Steam DLL and Chairloader's official diff. Both input and output hashes were verified before import. The installed game was not changed.

Chairloader/PDB RVAs are **reference-build-only**. Never copy one into a Steam hook table. A reference function must be translated by an invariant byte/control-flow signature, checked against the exact Steam hash, decompiled on both sides, and preferably validated live before promotion to [`ADDRESS_REGISTRY.md`](ADDRESS_REGISTRY.md).

### There is no global EGS-to-Steam RVA delta

Measured on 2026-08-15 across seven functions whose Steam and EGS addresses are both known:

| Function | Steam | EGS | delta |
| --- | ---: | ---: | ---: |
| `CRenderView::SetCamera` | `0xEE7E80` | `0xEBB960` | `0x2C520` |
| `GetArkPlayerInstance` | `0x157C990` | `0x154FA60` | `0x2CF30` |
| `IArkPlayer::GetReticleViewPositionAndDir` | `0x157CBB0` | `0x154FC80` | `0x2CF30` |
| `ArkPlayerCamera::SetCustomViewFunction` | `0x1456460` | `0x14298B0` | `0x2CBB0` |
| `CArkWeapon::FindIronsightsTarget` | `0x16930A0` | `0x16655F0` | `0x2DAB0` |
| `CArkWeapon::GetReticleInfoForFiring` | `0x1694890` | `0x1666DE0` | `0x2DAB0` |
| `ArkPlayerMovementController::GetMovementState` | `0x159AF10` | `0x156E050` | `0x2CEC0` |

Five distinct deltas across seven pairs, spanning `0x1590` (5,520 bytes). Code was inserted and
removed unevenly between the builds, so **no arithmetic shortcut exists**. This matters now that
the Chairloader headers supply 14,622 EGS function RVAs: each one must be translated by its own
byte signature. Applying any observed delta to a second function is unsound even when it happens
to work, and two of the pairs above sharing `0x2CF30` shows how easily that could look convincing.

### RIP-relative displacements are build-specific

A signature that embeds a `[RIP+disp32]` operand encodes the *distance* to a global, which moves
whenever anything between the instruction and its target changes size. Such a signature is exact for
one build and worthless for translation.

An audit of the then-22 promoted signatures on 2026-08-15 found **three** affected:

| Landmark | RVA | Embedded instruction | disp32 |
| --- | ---: | --- | ---: |
| `renderer.dispatch` (R-004) | `0xFE9D14` | `4C 8B 15 …` `mov r10,[rip+d]` | `0x1B54BC5` |
| `player.get_instance` (R-008) | `0x157C990` | `48 8B 0D …` `mov rcx,[rip+d]` | `0xCD108D` |
| `movement.get_state` (R-016) | `0x159AF10` | `48 8B 0D …` `mov rcx,[rip+d]` | `0xCB2B43` |

The decode was verified: `renderer.dispatch`'s instruction ends at RVA `0xFE9D1B`, and
`0xFE9D1B + 0x1B54BC5 = 0x2B3E8E0` — exactly R-005, the renderer singleton pointer.

**No runtime risk.** Signatures are applied only after the exact `PreyDll.dll` SHA-256 matches, so
the displacement is correct by construction. The exposure is translation: these three cannot be used
to locate their functions in the EGS build or in any future Steam patch.

**Rule.** When translating, either mask the four displacement bytes or anchor on a stretch without
one. This is why R-031's signature stops at 39 bytes — the next instruction is a `LEA RAX,[RIP+…]`
vtable load, and including it would have guaranteed a miss.

### A promoted signature was not unique

Verified against the installed DLL on 2026-08-22: R-002's original 16-byte signature
`48 8B C4 55 53 48 8D 68 A1 48 81 EC B8 00 00 00` is a generic MSVC frame setup that occurs **twice**
in the Steam image, at `0xF7D710` and `0x1449620`. The same collision exists in EGS at `0x141CAD0`,
which the header table names `ArkCystoid::ProcessNearbyCystoids`.

The gate was never wrong — it compares bytes at a fixed RVA rather than scanning — but the signature
could not identify the function on its own, which is what cross-build translation needs. The two
diverge at byte 16: R-002 continues `4C 89 78 E8` (`mov [rax-0x18], r15`) where the collision has
`48 8B 59 38`. The promoted signature is now **23 bytes**, instruction-aligned and unique.

**Rule.** A signature's length should be chosen by measuring uniqueness against the target image, not
by taking a fixed number of prologue bytes. All 30 current landmarks were checked this way; R-002 was
the only non-unique one. The rule earned its keep again on 2026-08-22: `ISystem::AutoDetectSpec`
(R-045) matches at **three** sites on a 24-byte prologue, so it was left out of the gate rather than
padded into uniqueness for a function on no hook path.

### Leading REX prefixes are not invariant

A prologue is a weak anchor for cross-build translation because x86-64 REX prefixes (`0x40`–`0x4F`)
are partly a codegen choice rather than a semantic requirement. Two builds of the same function can
differ by exactly one leading byte while everything after it still disassembles as plausible code,
so the failure is silent rather than a crash. This was contributed to the cross-engine playbook from
SOMAVR, where it broke two hooks at once in a way that presented as two unrelated faults.

An audit of the current 22-signature table on 2026-08-07 found:

| Leading byte | Count | Notes |
| --- | ---: | --- |
| Bare `0x40` REX | 5 | `renderer.end`, `aim.update_cached_ray`, `movement.get_state`, `interaction.select_candidates`, `interaction.interact` |
| Other REX `0x41`–`0x4F` | 16 | mostly `0x48` (`REX.W`) |
| No REX prefix | 1 | `aim.get_cached_ray` only |

The bare-`0x40` cases are the most fragile: `40 55` and `55` are the same `push rbp`, so the prefix
carries no meaning. `aim.update_cached_ray` begins `40 55 57` — the first push takes a REX prefix and
the immediately following one does not, which marks the prefix as MSVC frame/unwind convention
rather than necessity.

**This is not a runtime risk in the current design.** Signatures are applied only after the exact
`PreyDll.dll` SHA-256 is confirmed, so a different build fails on the hash before any signature is
read. The exposure is in the translation step above, where a prologue-anchored search across two
builds can silently miss or mismatch.

**Rule.** When translating a reference function, prefer a unique *interior* anchor past the prologue,
or ignore leading bytes in the REX range when searching. R-024 already does this — its registry entry
records that its "unique interior begins at `+0x27`" because its prologue was not distinctive enough.
Treat that as the normal method for cross-build work, not an exception.

### Identical COMDAT folding shares one address between unrelated functions

Found on 2026-08-22 while mapping the `CRenderView` vtable. `IRenderView::GetFrameId` and
`ISystem::GetGlobalEnvironment` both resolve to `0x903CA0`. They are not related: one returns a frame
counter, the other the global environment pointer. They compile to the same five bytes —
`48 8B 41 28 C3`, `MOV RAX,[RCX+0x28]; RET` — and MSVC's `/OPT:ICF` folded them onto a single
address. Several other trivial accessors in that vtable point outside `CRenderView`'s own code region
for the same reason.

**Two consequences, and the second is a real hazard.**

1. *An address does not identify a function.* A trivial accessor's address may be shared by any
   number of unrelated classes, so "this vtable slot points at `0x903CA0`" says nothing about which
   source function it came from.
2. *Hooking a folded address hooks every caller of every folded function.* Patching `0x903CA0` to
   intercept `GetFrameId` would also intercept `GetGlobalEnvironment` and anything else folded there
   — a class of bug that would present as unrelated subsystems misbehaving at once.

**Rule.** Before hooking any short function, check that its bytes occur exactly once in the image.
The uniqueness test already used for signature promotion answers this directly: a unique signature
means no folding partner exists.

Checked for the three functions the camera lane would touch, all unique and therefore safe on this
count: `CSystem::GetViewCamera` (8B), `CSystem::SetViewCamera` (7B), `CRenderView::SetCamera` (31B).
This is also why `R-040`'s eight-byte signature is worth having despite covering an entire two-
instruction function — it doubles as the anti-folding proof.

### Vocabulary does not survive engine generations; structure does

Found on 2026-08-23 while testing a Far Cry 1 `CryAnimation` tip against Prey. Every specific symbol
from the older generation was absent — no `IKSolver`, `SolveIK`, `m_additLen` or `ApplyToBone` in
`PreyDll.dll`, no `CryAnimation` directory in the Chairloader PDB headers, zero `IK` matches across
all 1,131 header files. The FC1 solver belongs to the CryEngine 1 generation; Arkane's CryEngine is
far downstream of it.

**But the search still succeeded**, because what transferred was the *shape* — a two-bone solver, a
named limb, a goal, a definition block. Searching for that shape rather than those names found
CryEngine 3's replacement: `AnimationPoseModifier_Ik2Segments`, `AnimationPoseModifier_LimbIk`,
`IKLIMB_LEFTHAND`/`IKLIMB_RIGHTHAND`, `CreateIKLimb`, and the `*_Definition` CHRPARAMS keys.

**Rule.** When using an older engine version as an oracle, expect the names to fail and the structure
to hold. Search for the concept's shape — the arity, the naming convention, the adjacent definition
keys — not the literal identifiers. A negative on the exact symbol is not evidence the feature is
missing; it is usually evidence you are one generation off.

This sits alongside the translation hazards above for the same reason: like a REX prefix or a folded
COMDAT address, it is a case where an exact-match search returns a confident wrong answer.
