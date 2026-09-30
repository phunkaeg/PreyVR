# Offline native HUD extraction

No game process, injected DLL, runtime or headset was used.

- `extraction.json`: supported DLL hash, both archive hashes/counts, decoded
  directory hashes, and all four asset size/CRC/SHA-256 results. Effective assets
  select the installed patch over the base game.
- `widget-map.json`: JPEXS 26.3.0 ActionScript references cross-checked against its
  named movie placements and the decoded UI dispatch definition. Initial matrix
  values are authored SWF values (translation in twips), not current pixels or
  native in-memory matrices. Proposed capture children retain draw-depth order.
- `native-contract.json`: exact supported binary and byte hashes of the inspected
  Steam functions, concrete root vtable slots and archive public-key location.
  These identify the inspected code; they are not native-call runtime validation.
- `tests.txt`: nine synthetic offline tests, including corrupted-input controls.

The source PAKs remain in the user's installed game. Local extracted assets and
JPEXS output are in `build/native-hud/`, deliberately excluded from Git. Repeat
the commands in [the investigation](../../NATIVE-WRIST-HUD-2026-09-27.md) to
regenerate them from the installed copy. No asset or decompiled movie script is
included in the shipping mod by this change.

EGS PDB-derived exports supplied names and algorithm leads. Steam Ghidra review
established archive Prepare at `0xE6FE60`, directory reading at `0xE6E920`,
Twofish CTR at `0xE652F0`, IV derivation at `0xE653B0` and key-index derivation at
`0xE65400`. The offline reader then passed OAEP validation for all 17 key blocks
in each archive, decoded all 5,455 base / 517 patch directory entries, and checked
the requested files against their archive CRCs. No archive signature
authentication is claimed.

The important status boundary is three sibling meter clips plus optional shared
art/status effects. It is neither the whole DanielleHUD movie nor the whole SW
quadrant. The latter also contains climb, pickup and other gameplay indicators.

Remaining integration work: prove safe value ownership and temporary
display-object mutation inside the native render locks, capture only the selected
widgets without advancing the movie again, restore native state, and feed those
pixels to the existing wrist swapchain. This packet does not claim those steps
are implemented.

The project receipt validates with seven hashed artifacts. Fleet submission was
attempted but refused with `receipt must remain inside its owning project`:
the fleet maps PreyVR to its canonical checkout, not this integration worktree.
Submission and graph refresh remain deferred until the change reaches that
checkout; the project evidence stays here with the implementation.
