# Native wrist candidate evidence

- `native-contract.json`: Steam SHA256, inspected Ghidra body extents, raw PE
  hashes and receiver/slot contracts. The nine ranges contain no base relocations.
- `verification.json`: independent PE verification and altered-byte refusal
  controls from `tools/re/verify_native_wrist.py`.
- `ctest.txt`: final offline suite, including production GPU fitting on D3D11
  WARP. Native game functions are not executed by that harness.
- `build.json`: local candidate DLL size/hash, not an installed or released build.

The owning [implementation note](../../NATIVE-WRIST-IMPLEMENTATION-2026-09-28.md)
records the producer-to-consumer proof and remaining acceptance checks. The
September 27 extraction packet owns the native asset identities and widget map.
Game assets and decompiler exports remain local under ignored `build/`.

This is static/native-contract and offline graphics evidence. It does not prove
that a running Prey session traverses this callback, that the expected widgets
appear in the headset, or that capture costs are acceptable. No game run,
injection, attachment, shared runtime change or installed DLL replacement occurred.

Fleet intake/graph refresh remain deferred: the fleet maps PreyVR to the canonical
checkout and does not recognize this integration worktree as its project root.
