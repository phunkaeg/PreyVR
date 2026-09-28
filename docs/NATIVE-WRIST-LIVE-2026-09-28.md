# Native wrist: brief in-game simulator check

The native isolation/replay and wrist-layer lifecycle executed successfully in
Prey. **Visual placement and readability are not accepted by this run.**

One game session occupied approximately 3 minutes 12 seconds, including loading
the existing Talos Lobby save. Prey PID 135192 launched at 22:59:49 Sydney time;
XR teardown completed at 23:02:57 and the game then closed. No other game process
was found before launch. Only this PID was closed; shared XR registration and
the simulator installation were untouched. The temporary wrist-enabled options
file was removed after loading (no options file existed before this test).

## Build and method

- Source: `27b15fe1d2f3aea32c8b4cf26a847e52b2501801`, `codex/vr-options`.
- Candidate DLL SHA-256:
  `94c0aa050201438a1660a349a18f5b89137612c898d4ba4d7425d39c60846f78`.
- Steam PreyDll SHA-256:
  `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
- Process-scoped x64 xr-sim plus xr-tape, 1280×1440 game backbuffer,
  1000×1000 compositor captures. No installed mod replacement.
- `Invoke-PreyVRLaunch.ps1`, explicit candidate DLL and existing injector;
  `vr.enable`, native `menu 4` taps to continue/load, `menu 6` for pause.
- Head `(0,1.65,0)`, identity rotation. Left grip initially
  `(-0.10,1.42,-0.42)`, pitch 90°; lowered to `(-0.25,1.0,-0.35)`,
  identity rotation; returned to `(-0.10,1.55,-0.42)`, pitch 90°.

Question: does raising/lowering the left wrist request native meter replay and
add/remove the wrist layer, without capture faults? The lowered-wrist control
changes only the simulated hand pose. Success requires native capture to stop
while ordinary HUD capture continues, then resume with a wrist quad submitted.

## Observed results

| Check | Observation |
|---|---|
| Native replay | Final `captured=1495 refused=0 fault=0` |
| Wrist submission | `wristLayerFrames=1493` before shutdown |
| Lowered wrist | Native wrist captured remained 1392 across two reads; normal HUD captured advanced 1445 → 3281 |
| Raised again | Native wrist captured resumed to 1456, then 1495 |
| Layer control | Gameplay raised: projection + forward HUD + 18×12 cm wrist quad; lowered: projection + forward HUD |
| Pause | Wrist quad absent; native pause panel present |
| Teardown | `vr=off`, XR `torn_down`, owned game PID exited |

The trace is bounded at 10,000 frames and explicitly **truncated**. Its recorded
end-frame results and exact capture matches are summarized in
[summary.json](evidence/native-wrist-live-2026-09-28/summary.json).
Do not treat its footer as coverage of the entire session or teardown.

## Visual limitation and next discriminator

Captured panels are distorted/misplaced. The small lower-left status graphic
also exists with the wrist layer absent; it cannot establish wrist readability.
The source snapshot reveals an xr-sim compositor matrix-layout inconsistency:
CPU row-vector matrices with translation in the last row are copied directly
into an unqualified HLSL `float4x4`, consumed by `mul(position, mvp)`, without
row-major compile flags. The installed runtime contains that shader text.
This is a concrete simulator defect candidate, **not yet a corrected-runtime
comparison proving the cause of these particular pixels**.

Also, xr-sim writes `pixelsCovered=1` unconditionally after each quad draw.
Despite the field name, this is a sentinel, not measured coverage. It neither
proves a one-pixel defect nor proves that the texture contains visible pixels.

Next: use an isolated simulator build with a known translated/rotated quad
positive control, or dump the wrist swapchain directly; then perform a brief
repeat. Do not change the shared runtime while other fleet tests are active.
Native values changing under damage/healing, psi/suit changes, save/load,
two-hand suppression, feature-disabled behavior, frame cost and headset comfort
remain untested. The 50/50 prior offline suite is separate evidence.

Raw captures, command replies, mod log, runtime identity and matching trace
records are in [the evidence folder](evidence/native-wrist-live-2026-09-28/).
The full bounded trace remains in `build/native-wrist-live-20260928/`.
