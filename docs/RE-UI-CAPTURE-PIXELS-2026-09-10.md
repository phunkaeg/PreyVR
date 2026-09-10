# Correction: the help-card pixels were intact

The intermittent missing-help-text warning in the earlier player-preview report
is refuted by the saved images themselves. It was an incorrect interpretation
of full-frame previews, not evidence of lost texture data or a mod fault.
The runtime and mod upload were not changed in this follow-up.

## Discriminating proof

Question: do the historical captures described as missing text contain different
help-card pixels from the correctly displayed reference, at the same head pose
and panel layout?

`check-card-pixels.py` reads the actual saved PNGs and their JSON sidecars. It
restricts comparison to a fixed head position/orientation, dimensions, eye
frusta and panel layout, then hashes the RGB crop containing the entire card.
Left crop: `(395,633)-(832,695)`; right: `(200,633)-(637,695)`, on 1032×1104
captures. The two eyes and opaque/translucent versions are compared in separate
groups. Three moved-head captures are excluded rather than mixed into the check.

**112 eye images pass.** Four groups contain 8, 8, 48 and 48 images, each with
exactly one pixel hash. Historical candidate-07 captures `main-menu-07`,
`gameplay-07`, `actual-inventory-07` and `guide-probe-07` all have the same left
card hash:

`7e93623d87ab` (prefix; full SHA-256 is in the verification JSON).

Each has 2,134 bright card pixels at the recorded threshold. The threshold is a
diagnostic count, not a substitute for the exact pixel comparison or reading the
reference text. The other groups have their own consistent counts because eye
sampling and the older card alpha differ.

![Exact crop from the historical image previously described as faulty](evidence/ui-compositor-2026-09-10/old-actual-inventory-07-crop.png)

The earlier raw 1024×160 GPU readback also contains all three text lines. That
readback alone did not establish the state of uninstrumented frames; the new
comparison directly checks those stored frames, avoiding the timing ambiguity.

## New live controls

The unchanged candidate-07 DLL was loaded into owned Prey PID 121908, run
`player-20260910-072944`, using the same private xr-sim validation runtime.
No other fleet game was running. Source hashes matched the frozen candidate
before launch. Target remains Steam PreyDll SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.

- Six title/main-menu captures and sixteen inventory captures recorded D3D state.
  All cards matched. Extra GS/HS/DS stages and predication were absent; sampled
  shader constants, sampler and rasterizer state were consistent. No other
  context thread was observed binding render targets during the sampled windows.
- Sixteen additional inventory captures had no Frida hooks. They also matched.
- Four inventory close/reopen cycles, without hooks, produced matching cards.
- `vr.disable` and normal close completed. The native error-log timestamp stayed
  at the older candidate-04 fault; this run introduced no new error log.

This is bounded coverage. The D3D sampler did not measure every API, thread or
possible race, and no such exhaustive claim is needed for the pixel correction.

## Consequence for the player preview

No additional mod rebuild is necessary. The tested DLL remains:
`7e38452274fc570b0adfaa67db46c0c2417755356c38f6a5512436fc42f755b0`.
The existing player ZIP remains:
`8659263d406697d3fced9b1bbe936c5563d21b3434e9d4ef5cb8d899cd235593`.

The preceding **33/33 CTest pass** applies to this unchanged implementation.
The card's opaque appearance remains a presentation choice, not a bug fix
validated by the old disappearance hypothesis. Physical Quest/VDXR comfort,
text readability, animated weapon coverage, terminals and performance still need
their own acceptance. Existing native-frustum/optical-frustum differences and
the bounded xr-tape trace limitations remain as documented.

The frozen [earlier report](RE-VR-INTERFACE-2026-09-10.md) and its receipts are
preserved. This correction supersedes only their missing-card interpretation;
it does not retroactively turn every earlier check into a PASS.

## Reproduction and evidence

Run with the Pillow-capable Python used by the evidence script:

```powershell
& 'C:/Users/meise/AppData/Local/Programs/Python/Python312/python.exe' -B `
  'docs/evidence/ui-compositor-2026-09-10/check-card-pixels.py'
```

Evidence directory: `docs/evidence/ui-compositor-2026-09-10/`.
`card-pixel-verification.json` records every PNG/sidecar hash, layout group,
pixel hash, count and exclusion. `title-state.json` and `inventory-state.json`
contain the bounded D3D observations. `sample-compositor.py` records CPU-side
state without a GPU readback; its COM slots are extracted from the installed
Windows SDK in `d3d-slots.json`. All live probes detached before normal close.

Future missing-text diagnoses must first inspect an exact crop and compare
stored pixels. Reopen this issue if an actual captured card loses or changes
glyph pixels at an equivalent layout, or a headset capture demonstrates a
separate runtime presentation fault. An impression from a full-frame preview
alone is insufficient.
