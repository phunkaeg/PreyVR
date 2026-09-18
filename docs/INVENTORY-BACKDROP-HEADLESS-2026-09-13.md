# Inventory backdrop: real PDA alpha confirmed

The `feat/inventory-backdrop` build shows magenta through the **interior** of
the inventory in both simulated eyes, including the item grid and description
area. The PDA capture contains usable transparency. This removes the need to
cut out an opaque rectangle for those regions; it does not supply a rendered
world behind the inventory or prove that every PDA region is transparent.

## Build and run

- Source: `9b38d025b865e67d56b0a7ce097c087163321f1a`, exported to an isolated
  source tree without changing `main` or the feature branch.
- DLL SHA-256: `8079BA513861F6B4FC3A275C54B25C0D0597D87A55B1B97D7B79037CB7B0746D`.
- Release build and **39/39 tests passed**.
- Player package: `build/packages/PreyVR-inventory-backdrop-9b38d02.zip`.
  Backdrop remains off by default; the package contains the unmodified feature.
- Live Prey PID 89128, Steam build, 1920x1080 backbuffer, D3D11, main format 29.
- Runtime: process-scoped x64 xr-sim; simulated Quest optics. No fleet game was
  running before launch. Shared OpenXR registration was unchanged.
- Scene: existing Talos Lobby save, inventory Items tab, Disruptor selected.
  Native `menu 4` taps advanced title/load prompts; `menu 9` opened the PDA.

This is **in-game evidence with a simulated headset**, not a headset acceptance
run. xr-sim advertises no cylinder extension in this run; the mod used its quad
fallback. Curved-panel appearance and real-runtime stereo comfort remain untested.

## Controlled comparison

| Capture | Separate inventory | Magenta backdrop | Observed centre | xrEndFrame |
| --- | --- | --- | --- | --- |
| `inventory_before`, frame 7704 | on | off | Ordinary dark inventory | success |
| `inventory_magenta`, frame 9171 | on | on | Magenta through grid and description | success |
| `inventory_opaque_control`, frame 11152 | off | on | Opaque centre, magenta border only | success |
| `inventory_magenta_repeat`, frame 12734 | on again | on | Magenta returns through centre, both eyes | success |

The first requested `ui.backdrop` readback reported `enabled=1 frames=38`.
A later readback reported `frames=3597`. At teardown it reached 9084.
These count construction of backdrop submissions, not accepted frames: the
increment is before `xrEndFrame`, and the counter also advances on ordinary
opaque menu panels. They are not alpha evidence by themselves.

Each capture's display time matches an xr-tape `end` record with result 0.
The alpha captures submit two layers in order: backdrop flags 0, inventory flags
2 (source-alpha blending). The opaque control retains both layers but has flags
0 on the panel. The complete tape records 18,281 frames, is not truncated and
has no nonzero `end` results. The older tape decoder labels quad struct type 36
as `other`; the sim capture sidecars identify the quads and record their sizes.

The interior comparison uses a fixed 140x70 pixel ROI, x=420..559/y=550..619,
away from the border. Red and blue both increased by more than 30 byte values
in 99.96% of left-eye pixels and 99.93% of right-eye pixels between the opaque
control and repeated alpha capture. A representative left pixel changed from
RGB (20,23,16) to (141,23,140). This is a colour-change check, not an estimate of
the movie's exact alpha or a claim that the entire inventory is transparent.

## Evidence and restoration

PNG pairs, sidecars, command replies, native log, matching trace excerpt and
pixel metrics are under `docs/evidence/inventory-backdrop-2026-09-13/`.
The complete raw tape remains in the dedicated run directory under
`build/backdrop-test-20260913/runs/run-20260913-090216/tape/`.

`xrsim-shot.ps1` exhibited a wait race: after sending the shot it waits for
`captureSeq+1` relative to a newly read state, which can already include the
requested capture. The named files were present despite its timeout. Subsequent
captures used the documented `shot <unique-name>` command and verified the
resulting PNG/JSON pair and matching tape frame, avoiding an extra relative wait.
No xr-sim source or shared helper was edited.

At completion `ui.backdrop 0`, `ui.inventory 0`, and `vr.disable` were sent.
The native log confirms XR teardown. Only the launched PID was closed using
CloseMainWindow; it exited and no Prey process remained. The original packages
and production source were unchanged.

## Next step

Provide a gameplay-world image behind the alpha-blended inventory layer. First
choose and test whether it should be a retained last scene or a separately
rendered live scene while the native inventory path is active. Preserve the PDA's
authored alpha; then evaluate contrast and readability with actual scene content.
This experiment does not establish a safe world-render re-entry or deep/stereo PDA
layers, and it does not resolve the separate 0.5.0 input/HUD review findings.
