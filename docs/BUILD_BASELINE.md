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
