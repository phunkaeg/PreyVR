# Frame observer and OpenXR preflight

This is the first mutation-capable runtime rung. It is deliberately smaller than a VR renderer: the observer can count one verified engine frame boundary, while the OpenXR lane only validates loader/runtime availability.

## Safety contract

- `PreyVR.dll` validates all 22 exact `PreyDll.dll` landmarks before the observer can become ready. The completed supported-host proof below was run at 21; the 22nd landmark (R-025) was added afterwards and is so far verified only headlessly and against the installed on-disk module.
- The observer is compiled **off by default** and is never enabled from `DllMain` or the bootstrap worker.
- The enable export rejects activation until the supported module has reached its process-exit pin state.
- The sole target is `EndRendererScene` at `PreyDll.dll+0xF7E210`, with ABI `void(renderer*)` confirmed by Ghidra and live execution.
- Enabling installs a MinHook trampoline whose callback only updates atomic telemetry and calls the original function. It performs no file or string I/O on the render thread.
- Disabling restores the target entry bytes and emits one control-thread summary. The disabled hook entry and trampoline are retained until process exit so a callback that was already in flight cannot return through freed executable memory.
- After the exact supported-host gate succeeds, the DLL is permanently pinned until process exit. It must not be passed to `FreeLibrary`; restart Prey to load another build.
- OpenXR preflight uses bounded, fail-closed file reads. It checks a minimal runtime-manifest shape and verifies that the packaged PE is an x64 DLL exporting `xrGetInstanceProcAddr`, without loading it, creating an instance, querying a system, creating a session, or touching D3D11.

## Exports

| Export | Contract |
| --- | --- |
| `PreyVR_GetSmokeStatus` | `0` starting, `1` unsupported/fail-closed, `2` all 22 landmarks verified. |
| `PreyVR_SetFrameObserverEnabled` | Argument `1` installs/enables; `0` disables/removes. Returns the resulting observer status. |
| `PreyVR_GetFrameObserverStatus` | `0` unavailable, `1` ready/off, `2` enabled, `3` failed. |
| `PreyVR_GetObservedFrameCount` | Monotonic 64-bit callback count for the current enable interval. |
| `PreyVR_GetOpenXRPreflightStatus` | Pure readiness status; see `OpenXRBootstrapStatus` in `include/preyvr/OpenXRBootstrap.h`. |
| `PreyVR_GetModulePinStatus` | `0` before/without supported-host promotion; `1` after the module is pinned until process exit. |

## Supported-host proof

1. Load the exact Release DLL while Prey is in a stable saved game.
2. Require `status=verified landmarks=22`, `observer=ready`, and no unsupported/mismatch line.
3. Require an OpenXR preflight result. `ready` authorizes a later bootstrap experiment, but starts no XR work.
4. Call `PreyVR_SetFrameObserverEnabled(1)` for a bounded interval.
5. Let at least 120 frames pass and require a running game.
6. Call `PreyVR_SetFrameObserverEnabled(0)` and require one `status=disabled frames=N` summary with non-null first/milestone renderer telemetry.
7. Confirm the target prologue is restored and no debugger breakpoints remain. Do not unload the supported module; close/restart Prey when replacing it.

The first completed proof is recorded in [`../captures/traces/2026-08-01-prey-frame-observer-smoke.md`](../captures/traces/2026-08-01-prey-frame-observer-smoke.md). That historical `0.2.0`, 21-landmark artifact counted 301 callbacks, restored the original prologue, unloaded, and left Prey running. The lifecycle-hardened `0.3.0` artifact deliberately changes unload semantics and still needs the supported-host protocol above.

## Next authorized rung

The next step is an equally bounded OpenXR **instance/system-only** smoke test. It must remain default-off, preserve desktop rendering, create no session or swapchain yet, and always destroy what it creates. Binding OpenXR to Prey's real D3D11 device is a separate later gate.
