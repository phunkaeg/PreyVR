# EGS symbols and decompilation reference

The extracted `[WIN] Prey [2021-08-19]/` directory is a substantial debug-symbol
reference. It contains the EGS DLL, its matching PDB, original and enriched IDA
databases, and an address-indexed Hex-Rays export. It is not the Steam binary
that this mod currently supports.

## Independently checked identity and coverage

`docs/evidence/ui-pointer-2026-09-10/audit_symbol_export.py` reads the PE CodeView
record, MSF/PDB streams, CSV index and SQLite database without modifying them.
Its result is `symbol-export-audit.json` beside the script.

- DLL SHA-256: `0485c85bb741d6d2e5af69114bae54c521540d5868aac476a8d9fbd1899f0a63`.
- PDB SHA-256: `f33ed345bcfeed884b38dff372cf146a89f3ed98acc124349fbf22c67a8b12f0`.
- PE and PDB both identify GUID `9054cdfe-4d2d-4b9b-94a5-183dfe3c1bba`, age 1.
- The PE embeds an `x64-Epic/Release/PreyDll.pdb` build path. The PDB has type,
  module and source information; it is not merely a list of exported names.
- The current index has 99,141 functions: 90,018 ordinary pseudocode bodies,
  9,095 bodies marked with unresolved JUMPOUTs, and 28 assembly fallbacks.
  Every indexed function has an existing body or fallback file in this extraction.
- The navigation database contains 99,141 functions and 297,993 call edges.

This verifies identity and the indexed files, not completeness of every original
symbol or correctness of every reconstructed expression. The export's own
`verification.json` is a historical producer report; the local audit above is
the independent extraction check. Failed pseudocode rows can still name a
nonexistent `.cpp`: check their `fallback_file` before calling a body missing.

## Useful entry points

Under `[WIN] Prey [2021-08-19]/ida_decompile/PreyDll/`:

- `index.csv`: exact names, RVAs, signatures and body paths.
- `readable/navigation.sqlite`: read-only queries for functions and call edges.
- `readable/classes/` and `readable/vr_mod/`: grouped or VR-relevant bodies.
- `types/` and `reference_sources/Chairloader/`: type and declaration leads.
- `readable/README.md`: coverage and how the readable layer was constructed.

Use narrow index queries first and inspect the returned files. The native dump
is excluded from the mod's implementation graph and from Git's normal source
inventory. There is no need to feed 99,000 donor functions into that graph to
answer a bounded question.

## Applied to the current UI investigation

The following associations were checked against Steam consumers or constructor
registrations; the EGS names are discovery aids, while the Steam columns come
from the supported target:

| Function | EGS RVA | Steam RVA |
| --- | --- | --- |
| CFlashUI::OnHardwareMouseEvent | `0x2D0010` | `0x2CFCC0` |
| CFlashUI::SendFlashMouseEvent | `0x2CF280` | `0x2CEF30` |
| CFlashUIElement::SendCursorEvent | `0x2FF800` | `0x2FF0C0` |
| CFlashPlayer::SendCursorEvent | `0xE60820` | `0xE8CB20` |
| CArkInventoryUI constructor | `0x15FDA20` | `0x162B010` |
| CArkInventoryUI::OnPickItem | `0x15FF9F0` | `0x162CFE0` |
| CArkInventoryUI::OnDragItem | `0x15FF580` | `0x162CB70` |
| CArkInventoryUI::OnPlaceItem | `0x15FFD40` | `0x162D330` |
| ArkExaminationMode::UpdateReticlePos | `0x157EBA0` | `0x15ABC40` |

The Steam constructor registers the literal events `inventoryPickItem`,
`inventoryDragPos` and `inventoryPlaceItem` to those concrete functions.
`steam-ui-xrefs.json` records RIP-relative byte candidates and concrete player
slots. The inventory traces record actual calls on this Steam build. The
hardware-mouse callback has a secondary receiver; a donor decompiler's typed
`this[-1]` expression is not permission to skip proving that adjustment.

The reference also reports a source-path comparison favouring CryEngine 5.1.x.
That is a useful way to find source ancestors, not proof that Prey's entire
Arkane fork is stock 5.1.x or that ancestor layouts match Steam.

## How to describe this to the contributor

We now have a matching EGS debug-symbol package and a well-organised,
symbol-backed decompilation. We had mainly been reversing Steam directly,
with some existing PDB-derived references. The new package greatly improves
names, types and navigation, while target addresses still need mapping and
verification before implementation.
