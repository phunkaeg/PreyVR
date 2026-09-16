# Stereo inventory prototype

An opt-in prototype now preserves DaniellePDA's native layered geometry in two
eye images. This is implemented and builds; **movie replay, pixels and headset
appearance have not yet been validated in the running game**. Two Omikron
processes (65692 and 81540, started September 10) prevented the authorized
headless test under the user's no-concurrent-fleet-games instruction.

## What changed

- `ui.inventory 1` remains the prerequisite for extracting the PDA.
- `ui.stereo 1` enables paired inventory capture, default **off**.
- `ui.depth 0..100` sets binocular depth strength, default **25**. Zero keeps
  dual rendering active as the native-image equality control.
- `capture.inventory <tag>` dumps both raw eye textures from the same frame,
  with tags `<tag>` / `<tag+1>` and explicit eye suffixes. It does not consume
  the images that XR will submit. Files are beside the injected DLL unless
  `PREYVR_CAPTURE_DIR` is provided. This readback stalls the GPU; do not use it
  while measuring performance.
- `ui.stereo 0` restores the existing mono inventory path. Both inventory
  swapchains are destroyed before XR session teardown. Existing packages and
  `feat/inventory-backdrop` were not changed or merged.

Stereo is currently a **planar binocular display**. Its two quads occupy the
same surface, have LEFT/RIGHT visibility, and use independent images. It
temporarily uses a flat inventory even on a cylinder-capable runtime; the
panel fit and pointer serial change together. Regular menus retain their
configured curvature. The eight native layer transforms and the movie's
mouse-driven tilt remain native. This is not yet a six-degree-of-freedom
internal UI camera or a raycast against individual depth planes. Pointer
selection uses the existing central panel mapping, whose alignment and depth
cursor appearance still need live testing.

## Steam contract and corrected depth direction

Target: x64 Steam PreyDll.dll, base `0x180000000`, SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.

The existing live depth notes identify movie root vtable `0x1EB4B60`, Display
slot `+0x130` -> `0x18AB710`. The native Flash callback `0xE8C5E0` calls it
at `0xE8C72F` while inside its locks, then leaves both locks and optionally
releases the player at `0xE8C756`. The prototype calls that outer callback
**once**, preserving its original release argument. A scoped Display hook
performs two inner displays under those locks; each creates/destroys its own
native draw context. No Flash Advance or input update is explicitly repeated.
Custom display callbacks and render caches still make live replay verification
necessary; absence of an explicit Advance call is not that verification.

The donor symbol `GRendererXRender::CalcTransMat3D` led to Steam `0xDB9620`
through a unique interior byte match. The Steam bytes consume world pointer
at `+0x1F0`, view at `+0x1F8`, projection at `+0x238`, and explicitly clear
the projection's horizontal offsets at `0xDB96CE` / `0xDB96D5`. A
SetPerspective3D-only convergence correction would be discarded.

`tools/re/verify_inventory_stereo.py` checks the Steam hash and landmarks, then
executes the unmodified Steam matrix routine and its actual matrix-multiply
callee in Unicorn 2.1.4. Fixtures include world Z=0, -1000, -15000 and +15000;
the known centre projects to X=Y=0 and a seeded callee-saved register survives.
All return through the supplied return address. Output row 3 is homogeneous
W, and the captured view basis gives **W = C + worldZ**. Negative native Z is
closer, not farther away. The previous `PlaneMetres` helper had this sign
backwards and its tests repeated that assumption; both are corrected.

At the simple 2m reference mapping, -15000 units is roughly 1.19m from the
viewer, not 2.81m. These are axial math examples, not measurements of widget
positions after tilt or recommended comfort settings.

The prototype modifies the final per-draw output, leaving the native cached
matrix and view/projection storage intact:

`Xclip += (2 * signedEyeMetres / panelWidthMetres) * depthScale * (W - C)`

This changes the entire X row for tilted geometry, leaves Y/Z/W intact, and
keeps the zero-depth plane fixed. Eye separation comes from located runtime
poses in a fresh consumer lease, rather than a headset-specific constant.
This first prototype uses symmetric horizontal separation; arbitrary head
translation/roll relative to the anchored panel and canted-view internal
reprojection are not implemented. The outer XR compositor still positions
the panel normally. A planar front-facing test is the initial validation case.

## Validation and next live experiment

Release build and **39/39 tests pass**, including added homogeneous divide,
depth-sign, tilted-transform, zero-depth and refusal checks. Native arithmetic
emulation passes. These do not establish Display replay safety or visible
stereo. Evidence is in `evidence/inventory-stereo-2026-09-13/`.

After other fleet games close, launch the development DLL with process-scoped
xr-sim/xr-tape using `tools/Invoke-PreyVRLaunch.ps1`. No machine runtime switch
is needed. Start with a static front-facing simulated head and inventory open:

```text
ui.inventory 1
ui.depth 0
ui.stereo 1
ui.stereo
capture.inventory 1000
```

Hypothesis: two Displays under one native lock/release produce complete,
identical images when added parallax is zero. Control: existing mono capture;
changed variable: inner Display replay. Require fresh same-frame eye files,
nonempty PDA imagery, matching pixels (or a diagnosed deterministic difference),
and matching successful xrEndFrame records with separate LEFT/RIGHT layers.
Counters alone are insufficient. Stop the depth experiment if the zero-depth
control fails, rather than attributing corruption to stereo geometry.

Then change only `ui.depth 25`, capture tag 1100, and compare disparity at
multiple depth planes. Repeat zero depth to rule out animation-only differences.
Check close/reopen, modal child dialogs, selection/drag, resize/teardown and
stereo disable. Check `refusedMatrices=0` and capture/submission freshness.
Only after those pass request headset acceptance of depth, reading comfort,
pointing and head motion. Keep the feature off by default until that gate.

World rendering behind paused inventory remains separate. This prototype does
not enable it and does not include the magenta diagnostic branch.
