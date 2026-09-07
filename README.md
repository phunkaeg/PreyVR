# PreyVR

Research and implementation workspace for an OpenXR VR mod for **Prey (2017)**. The intended end state is native, engine-owned 6DoF world rendering and motion controls; a flat OpenXR bridge is useful only as a reversible early milestone.

## Workspace map

| Path | Purpose |
| --- | --- |
| `docs/` | Evidence, decisions, RE notes, build identity, and test plans. |
| `captures/` | Local RenderDoc captures, eye dumps, screenshots, and traces. Large/generated evidence is ignored by Git. |
| `graphify/` | A reproducible, local knowledge-graph corpus joining PreyVR with the cross-engine VR-mod research corpus. |
| `src/`, `include/`, `tests/` | Implementation lanes for lifecycle, signature-gated frame observation, OpenXR preflight, engine integration, input, and deterministic tests. |
| `config/` | Versioned default/configuration schemas; machine-local settings stay ignored. |
| `tools/` | One-off utilities and capture/research helpers. |

## Current binary baseline

The installed release at `D:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release` is a 64-bit Windows build. `Prey.exe` is the small launcher; `PreyDll.dll` is the primary engine/research target. Full hashes and Ghidra context are recorded in [`docs/BUILD_BASELINE.md`](docs/BUILD_BASELINE.md).

## Current research state

**Updated 2026-09-07, after a headset session.** This is no longer a bootstrap:
stereo, head tracking and controller-driven hands all run in a real headset on a
real level.

**Confirmed in the headset**

- **Stereo submission and head tracking.** Native per-eye projection, synthetic
  stereo by camera translation, 6DoF head pose. Bring-up is scripted end to end
  by `tools/Invoke-PreyVRStartup.ps1`, in the one order that works: `xr.srgb`
  before `xr.start` (gamma is a swapchain format, not a cvar), `view.recenter`
  before `view.apply`, and `r_DrawNearFoV 88.507`.
- **The hand and the weapon move together, and the arm bends** (R-104). Writing
  the controller's wrist into the rig's own animation-driven-IK target with the
  weight joint at 1, at the entry of `ProcessAnimationDrivenIK`, makes Prey's own
  two-bone solver solve the arm. Because that runs before bone attachments sample
  the pose, the weapon follows for free. Wrist rotation, finger travel and elbow
  bend are all the engine's, not ours.
- **Weapon transform in world space** (R-100), **wrist rotation** (R-101), and
  **native input acceptance** (R-091) each confirmed in earlier sessions.

**Open, and specific**

- The hand yawed with the headset. Diagnosed and fixed: the engine's view camera
  carries head tracking because this mod writes it, so a head-relative offset must
  be rotated by the *body* yaw, `camera - head`. Built and unit-tested; **awaiting
  the next headset run.**
- Aim direction follows the controller, but a shot still converges from the
  weapon's authored muzzle helper toward the reticle ray. Per-weapon barrel
  alignment is unproved; `aim.origin` is a diagnostic, not a muzzle mode.
- Locomotion is written and tested as a pure layer and remains unwired.
- Menu navigation cannot yet be driven synthetically past the action-map stage.

**Engineering baseline**

`PreyVR.dll` version `0.3.1-static-ik-integration` gates on **34** exact
in-memory landmarks, pins itself for the process lifetime, and drives everything
through a file command channel so routine testing needs no debugger. OpenXR
1.1.60 at pinned commit `64f2b37c`. 27 deterministic tests plus two offline
target-byte verifiers (24 + 8 checks) run without the game.

**Where to start reading:** [`docs/ADDRESS_REGISTRY.md`](docs/ADDRESS_REGISTRY.md)
for what is proved and how, [`docs/RE-INVESTIGATION-GUIDE.md`](docs/RE-INVESTIGATION-GUIDE.md)
for how static claims must be established here, and
[`docs/FAILURE_REGISTRY.md`](docs/FAILURE_REGISTRY.md) for the traps that have
each cost a session.

## Working rules

- Treat every address as build-specific. Record RVAs, module names, byte signatures, confidence, and validation state in [`docs/ADDRESS_REGISTRY.md`](docs/ADDRESS_REGISTRY.md).
- Keep desktop presentation intact while each VR route is being proved.
- Promote a finding only when a capture, trace, runtime observation, or reproducible static-analysis result supports it.
- Keep pure pose/projection/input math independent of hook callbacks and covered by tests.

See [`docs/README.md`](docs/README.md) to begin research, or [`graphify/README.md`](graphify/README.md) to build the combined knowledge graph.

Run the no-game/no-headset verification loop with:

```powershell
./tools/Run-Headless.ps1
```

The Release DLL is written to `build/headless/Release/PreyVR.dll`. See [`docs/SMOKE_BUILD.md`](docs/SMOKE_BUILD.md) for its load contract and [`docs/FRAME_OBSERVER_BOOTSTRAP.md`](docs/FRAME_OBSERVER_BOOTSTRAP.md) for the historical proof and current observer protocol.
