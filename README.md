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

- The game-owned D3D11 device, DXGI swapchain, and paired renderer Begin/EndScene boundary are mapped and exact-signature guarded.
- `ArkPlayerCamera::UpdateView(SViewParams&)` and Prey's native custom-view callback path are mapped and live-validated; this is the leading HMD pose injection seam.
- A reversible fixed-camera test moved Prey's independent cached world aim ray. A completed two-swing A0b test moved a native wrench contact `0.1272` world units across the same wall, and a no-button interaction test changed both the selected and usable entity from keypad `0xFDE0` to `0x1117`. Both tests changed only one stack-local query direction while leaving the camera and persistent ray untouched. Firearm consumers use the same ray; the live projectile proof remains.
- `PreyVR.dll` version `0.3.0-lifecycle-hardening` checks 30 exact in-memory landmarks and packages a default-off `EndRendererScene` observer. The current binary passes the fresh 11-test headless loop and the installed-module gate, but has not yet been loaded into Prey. The earlier 21-landmark `0.2.0` artifact counted 301 live callbacks; that historical run does not prove this rebuilt DLL.
- The supported-host lifecycle is intentionally process-scoped: after every landmark and preflight check passes, the DLL pins itself until Prey exits. Observer disable restores the target entry bytes but retains the inactive trampoline so an in-flight callback can never return through freed code.
- The bundle includes OpenXR 1.1.60 at pinned source commit `64f2b37c…` and a read-only runtime/loader preflight. It verifies the runtime-manifest shape and the adjacent x64 DLL/export without loading it. No OpenXR instance, session, swapchain, D3D11 resource, stereo image, or headset output exists yet. This is still an engineering bootstrap—not a playable VR build.

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
