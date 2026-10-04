# Native two-hand rerun without the xr-sim alias

The GLOO positive contract passes with independent controller bindings. Right
squeeze alone does not acquire support. Left squeeze alone acquires support;
moving only the left controller changes native aim by **25.94 degrees**. Releasing
left squeeze restores the original one-handed direction within **0.0013 degrees**.
This supersedes the simulator-alias limitation in the September 14 report.

## Identity and scope

- Steam `PreyDll.dll`, x64, SHA-256
  `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
- Injected PreyVR source `80a186e`, DLL SHA-256
  `f0b66e1bfe680f908462e7c6278fe2f49b2f4d3847cb3484c70549306a77e24e`.
- xr-sim updated binding implementation `ca14f5b`; isolated x64 RelWithDebInfo
  runtime SHA-256 `6be19cdbb5dae7b196f9deee4e28e6cfa8023fcd65242d526076032e28c5f25a`.
- Owned run `run-20261004-103830`, PID 113584, loaded Talos Lobby / GLOO.
  Launch backbuffer 1344 × 1440, simulator captures 672 × 720 per eye.
  xr-tape was disabled. No shots or intentional locomotion; no save written.

The launcher selected xr-sim only for its child process. Other fleet games were
checked before launch; none occupied the runtime. VR was disabled, the owned game
closed normally, and absence of Prey was recorded. The system runtime remains VDXR.

## Discriminating observations

Head and right controller were fixed. Right grip/aim was
`(0.18, 1.30, -0.35)`, identity orientation; left started at
`(0.034, 1.325, -0.561)`, near the measured support region. Each acquired and
steered simulator state reports `gripL=1`, `gripR=0`. No alias was used.

| Phase | Native observation | Coverage |
| --- | --- | --- |
| Neutral near foregrip | held=0, regionReady=1, squeeze=0 | Usable geometry positive control |
| Right squeeze only | held=0, regionReady=1, squeeze=0 | Independent left/right action control |
| Settled left squeeze only | held=1, regionReady=1, applied counter=1834 | Actual native acquisition and sustained updates |
| Move only left to `(0.20,1.40,-0.56)` | held=1, counter=1909, aim changes 25.94° | Steering through native aim, fixed head/right hand |
| Release left | held=0, regionReady=1, aim restored | Return to one-hand direction |

The first left-only attempt acquired briefly, then lost usable geometry. A fresh
release/squeeze acquired the sustained hold recorded above. The counter is
cumulative, including that first brief acquisition; it is not a contiguous-frame
duration. Retained captures show the GLOO and supporting hand in both eyes.

Later empty-air, held-return, reacquisition and inventory controls have
`regionReady=0`; their applied counter stays at 1948. They are **inconclusive**:
the instrument no longer supplied fresh native support geometry. The cause of
that loss was not established. These records cannot renew the earlier negative
control claims or prove a regression. Simulator error counters remain zero.

## Reproduce and inspect

Raw reports, simulator states, logs and captures are under
`docs/evidence/twohand-no-alias-2026-10-04/`. Offline comparison:

```powershell
& 'C:/Users/meise/AppData/Local/Programs/Python/Python312/python.exe' -E -B tools/re/verify_twohand_no_alias.py
```

For a native rerun, use `Invoke-PreyVRLaunch.ps1` with the isolated x64 runtime and
`-NoTape`; see retained `run.json` for exact process configuration. Enable
`vr.enable`, `ik.align 1`, `aim.twohand 1`, then calibrate with `view.calibrate`.
Set both simulator hands to independent poses (`hand l/r follow off`), clear
inputs, and compare right-only squeeze, left-only squeeze, left movement and
left release while keeping the head and right hand fixed.

## Limits

This is an actual native-game run under xr-sim, not physical-controller or
headset acceptance. Only the GLOO positive contract was established here.
Toggle/free-support preferences, other long weapons, gameplay shots and comfort
remain unmeasured in this run. The captured baseline retains the known horizontal
rendering band, so full renderer acceptance is still failed independently of the
confirmed two-hand contract. New posture UI and slot cards were built after this
game closed and were not injected into this test.
