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
