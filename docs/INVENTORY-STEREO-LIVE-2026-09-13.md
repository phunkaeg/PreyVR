# Stereo inventory: native headless pixel test

The built prototype produces layer-dependent binocular disparity in the running
Steam game. This is native in-game evidence through xr-sim, not headset acceptance.
The inventory is still a planar binocular screen without internal 6DoF reprojection.

## Identity and setup

Run `build/inventory-stereo-live/runs/run-20260913-121412`, owned Prey PID 34272.
Steam PreyDll SHA-256 remains
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
Injected Release DLL: 921600 bytes, SHA-256
`E49098B03CEDF252AD1EE24D6F211318524E205C73783B720563F67613521880`.
The tested binary and run manifest are preserved in
`evidence/inventory-stereo-live-2026-09-13/`.

The previously blocking Omikron processes were closed with explicit user approval.
A fleet process check was empty before launching Prey. Runtime and recorder were
selected in the child process environment, not through machine runtime settings.
xr-sim used a Quest 3 profile, 63mm IPD and static head at (0,1.65,0).
Native backbuffer and private inventory textures were 1920x1080.
Loaded Continue into Talos Lobby. No save was written intentionally.

Inventory opens using native gamepad Back, key 517, press then release:
`input.key 517 1 1000`, `input.key 517 2 0`. `menu 9` is X/Secondary,
not an inventory-open command. The initial no-fresh-pair capture before opening
the PDA is retained in the log; it is not counted as a successful capture.

## Control and measured result

At the user's request, enabled depth first: `ui.inventory 1`, `ui.depth 25`,
`ui.stereo 1`. Captured both raw eye textures, then changed only depth to 0 and
100 for controls. These were synchronous same-frame pairs, with matching frame
indices, dimensions and format 28 in their PVRFRAME headers.

| Depth setting | Native frame | Result |
|---|---:|---|
| 25 | 16031 | Complete inventory in both raw eye images; tab disparity about -1px |
| 0 | 37468 | Both complete RGBA images byte-for-byte identical |
| 100 | 38494 | Tab disparity about -4px; footer about -3px; grid best match at 0px |

Disparity here means right-image position minus left-image position, estimated
using an integer horizontal shift scan across fixed image regions. The tab and
footer differ from the grid; this is not a uniform translation of the whole image.
The 25% footer estimate rounds to -1px. Subpixel shifts, overlapping planes and
native chromatic effects limit this simple matcher. It does not identify all
eight native planes or measure their individual depth in metres.

The zero-depth equality control rules out a deterministic replay difference in
this particular static inventory state. It does not prove every dialog or custom
Flash display callback safe. Raw pixel comparisons and the reproduction script
are in `comparison.json` and `compare_pairs.py` beside the six source frames.

Stable compositor capture `inventory_depth25_stable` (sim frame 34429) shows the
inventory in both eyes. Its metadata has two separate quads with complementary
left/right coverage, size 2.3094m x 1.2990m and centre (0,1.65,-2).
The pixels also show panel perspective/skew; internal head-motion correctness and
outer projection quality have not been accepted by this experiment.

Closing with `menu 5` returned to visible gameplay. Reopening produced another
fresh pair (tag 1400). A subsequent stable compositor capture
`inventory_reopened_stable` again has two eye-specific visible inventory quads.
Disabling stereo restores one visible BOTH-eyes inventory quad.

## Limits and diagnostic disturbance

`inventory_reopened` is a **black compositor frame**, captured immediately after
the synchronous raw pair readback. Its metadata shows fallback to one opaque
legacy quad, not the two inventory quads. The following stable capture recovers.
Do not count that frame as successful visibility or silently omit it.

A likely cause is readback delaying submission past the 200ms capture freshness
limit: FrameObserverHook calls ServiceFrameCapture before ServiceXrFrame;
PeekInventoryPair does not consume the pair, but synchronous GPU Map and disk
writes can age it out before InventoryLayerTexture reads it. Timing was not
instrumented, so that cause remains a hypothesis. Next diagnostic fix should stage
copies before submission and perform blocking mapping/writing afterwards, keeping
same-frame ownership explicit. Do not remove the normal stale-capture guard or
use diagnostic readback for performance measurement.

xr-tape 0.1.0 flushed 20000 successful xrEndFrame records at teardown, all result 0.
The trace hit its configured 20000-frame cap and is explicitly truncated; later
compositor shots are beyond its coverage. This recorder writes quads as generic
type 36 and does not prove their eye visibility. Use the xr-sim shot metadata and
pixels for those claims. Final counters before disabling: 34979 stereo pairs,
21264970 matrix corrections, zero matrix refusals. Counts are supporting evidence,
not substitutes for the pixel controls.

Controller pointing/dragging, child dialogs, resize, sustained performance,
cylinder runtimes, headset reading comfort and real head-motion parallax remain
unvalidated. Keep stereo opt-in. World rendering behind paused inventory is not
part of this implementation.

## Teardown and small source correction

`ui.stereo 0`, `ui.inventory 0`, `vr.disable` completed; the log records XR
`torn_down` at 02:25:44.599 UTC. Window-close returned false, so only the exact
owned Prey process was terminated; absence was verified. No test process remains.

Review during testing found the right upload used the immediate context after our
left-upload reference had been released. Moved Release after both uploads. The
new DLL is SHA-256
`953F428AA82B7790CA21614D21055391F4F71D4B849669DAD668A07DF3240D50`.
It builds and passes 39/39 tests, but was not the binary used for the live captures.
The tested E490 binary is preserved; the existing prototype ZIP still contains
that tested version. No further native rendering changes were made in this test.

This report supplements the earlier frozen static/harness receipt
`20260913-inventory-stereo-matrix`; it does not overwrite its artifacts or turn
that earlier harness result into in-game evidence.
