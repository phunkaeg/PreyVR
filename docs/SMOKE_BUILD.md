# Signature-gated bootstrap build

`build/headless/Release/PreyVR.dll` is the first game-loadable bootstrap artifact. It is intentionally not a VR renderer yet.

The adjacent `PreyVR-build-manifest.txt` records the DLL and OpenXR-loader hashes, version, x64 architecture, supported `PreyDll.dll` hash, count obtained from the compiled landmark table, default-off observer state, process-exit lifecycle, preflight-only OpenXR state, and exact MinHook/OpenXR dependency commits. Both binaries use the static MSVC runtime.

## Startup behavior

On process attach, the DLL starts a short worker outside the loader-lock callback. The worker:

1. logs its version, absolute DLL path, DLL SHA-256, host executable path, and compiled landmark count;
2. locates the already-loaded `PreyDll.dll`;
3. validates 22 exact renderer, device-init, camera, player, detached-aim, firearm, melee, movement, and interaction landmarks against mapped memory;
4. plans—but does not install—the `EndRendererScene` observer;
5. discovers the active OpenXR runtime manifest, checks its bounded file shape, and verifies the adjacent loader is an x64 DLL exporting `xrGetInstanceProcAddr`, without loading or calling it;
6. pins the module until process exit only after every supported-host check succeeds;
7. writes one supported/unsupported result and exits.

The normal log is `%USERPROFILE%\Documents\PreyVR\PreyVR.log`. `PreyVR_GetSmokeStatus` returns `0` while starting, `1` for unsupported/fail-closed, and `2` after every landmark matches and the observer plan succeeds.

## Default-off boundary

- No hook is installed during startup.
- The observer is only reachable through the explicit enable export and only after all signatures match.
- Its callback counts the engine frame boundary and forwards immediately to the original function; it changes no renderer or game data.
- OpenXR preflight does not load the loader, create an instance/session/swapchain, or create D3D11 resources.
- Camera, reticle, weapon, input, and presentation remain unchanged.
- A supported module cannot be unloaded or hot-replaced. This is intentional lifecycle safety; restart Prey to test another DLL.

See [`FRAME_OBSERVER_BOOTSTRAP.md`](FRAME_OBSERVER_BOOTSTRAP.md) for exports, status codes, and the bounded enable/disable protocol.

## Supported-host load gate

With Prey loaded into a stable save, load the exact Release DLL once using the attached debugging workflow. Do not arm unrelated renderer or weapon breakpoints. The required result is 22 matching landmark lines followed by values equivalent to:

```text
preyvr_frame_observer_plan status=ready ... enabled=0
preyvr_openxr_preflight status=ready ... action=none
preyvr_lifecycle module=pinned unload=process_exit_only
preyvr_smoke_result status=verified landmarks=22 hooks=compiled_default_off observer=ready module=pinned openxr=preflight_only openxrStatus=ready
```

The read-only checker reduces the landmark portion to one pass/fail line:

```powershell
./tools/Test-PreyVRSmokeLog.ps1
```

Any `unsupported`, `mismatch`, missing result, crash, or hang is a stop condition. Never enable the observer after a failed load gate.

The 2026-08-01 supported-host run proves the seam for the older `0.2.0`, 21-landmark artifact. The current `0.3.0` binary has different identity and process-exit lifecycle and is **pending live validation**. Do not cite the historical run as a current-binary pass.

## Headless contract

`dll_load_fail_closed` loads the same DLL into an isolated executable where `PreyDll.dll` is absent. It must return status `1`; remain unpinned; keep observer/OpenXR statuses unavailable; reject observer activation; leave the count at zero; log `PreyDll.dll_not_loaded`; and unload cleanly. `runtime_bootstrap` separately proves that observer planning and OpenXR readiness decisions fail closed under malformed/missing inputs. `openxr_preflight_files` inspects the packaged loader and an arbitrary DLL on disk, proving the architecture, DLL flag, and required export checks without loading either as OpenXR.
