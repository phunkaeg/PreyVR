# Handover — bootstrap hardening, 2026-08-07

This is the current handover for the PreyVR repository. It supersedes the 2026-08-03 snapshot
where current build identity or lifecycle behavior differs.

## Outcome

The project now has a safer, reproducible bootstrap baseline. It is still not a VR renderer: no
OpenXR function is called, no session or swapchain exists, and no game render state is changed at
startup. The current DLL is ready for one bounded supported-host validation.

| Item | Current state |
| --- | --- |
| Version | `0.3.0-lifecycle-hardening` |
| Release DLL | `build/headless/Release/PreyVR.dll` |
| DLL SHA-256 | `1D21F6D8DE722926187D4377E6F1CAEDC33456F665CFABD03BB57A9CFC3BE614` (**was** `179652AA...`; see F-005 - the build is not byte-reproducible and that artifact no longer exists) |
| OpenXR loader SHA-256 | `6DF5C6ECBE0BDEB2ED91A3F92151C54328264C705981A19D768EB047192A914A` |
| Engine gate | 22 landmarks from one compiled C++ table |
| Fresh Release loop | 11/11 CTest targets passed |
| Installed-game doctor | 7 pass, 0 warn, 0 fail; one result covers all 22 landmarks |
| Unsupported-host stress | 100 consecutive load/status/unload iterations passed |
| Live status | Current DLL has **not** been loaded into Prey |

The old `0.2.0` 21-landmark artifact did complete a 301-callback live observer proof. That proves
the `EndRendererScene` seam, but it does not prove this binary. Do not reuse the old capture as a
current-artifact pass.

## What changed

### Lifecycle and observer safety

- The bootstrap worker acquires a module reference of its own before leaving `DllMain` work behind.
  Publishing a result can no longer let a normal waiting host unload code while the worker returns.
- After the exact Prey gate, observer plan, and no-call OpenXR preflight all succeed, the module
  pins itself permanently. A supported `PreyVR.dll` lives until Prey exits and must not be passed
  to `FreeLibrary` or hot-replaced.
- Disabling the observer restores the target entry bytes but retains the inactive MinHook entry and
  trampoline. A render callback already in flight can therefore finish without calling freed code.
- The render callback does only atomic counter/pointer updates and calls the original function.
  Formatting and file logging were moved to one control-thread summary emitted on disable.
- New export `PreyVR_GetModulePinStatus` reports the lifecycle state. The unsupported-host test
  requires it to remain zero and confirms that path is still unloadable.
- The observer enable export also checks the pin state, so the brief `ready`-before-publication
  window during bootstrap cannot be used to activate a hook in an unloadable module.

The tradeoff is deliberate: supported-host unload/reload testing is replaced by process-exit
lifetime. Restart Prey to test another DLL.

### OpenXR preflight

- Filesystem queries use non-throwing/error-code paths and bounded reads: 1 MiB for a runtime
  manifest and 64 MiB for the loader.
- The manifest check is intentionally a shape check, not a complete JSON/schema parser. It requires
  an object-like file containing `file_format_version`, `runtime`, and `library_path`.
- The adjacent loader is parsed on disk. It must be AMD64, PE32+, marked `IMAGE_FILE_DLL`, and
  export `xrGetInstanceProcAddr` by name.
- The preflight never loads the loader and still makes no OpenXR call. New statuses distinguish an
  invalid manifest, non-DLL image, and missing loader entry point.

### Build, identity, and headless loop

- MinHook and OpenXR FetchContent inputs now use immutable commits rather than movable tag names:
  - MinHook: `c3fcafdc10146beb5919319d0683e44e3c30d537` (`v1.3.4`)
  - OpenXR SDK: `64f2b37c8c6da3d83c9b4d11865ba1fb752cb8ec` (`release-1.1.60`)
- The duplicate 22-signature PowerShell table was deleted. `preyvr_engine_map_probe` maps the PE
  with `SEC_IMAGE_NO_EXECUTE` and calls the same `engine::ValidateLandmarks` used by the DLL.
- The build manifest asks that executable for the landmark count and records schema 2, dependency
  commits, self/loader hashes, and the process-exit lifecycle.
- `Run-Headless.ps1` configures with `cmake --fresh` by default. `-NoFresh` permits an explicit
  incremental loop. The installed game baseline is optional by default and can be made mandatory
  with `-RequireGameBaseline`.
- The doctor always reports whether game checks ran. With no game directory and the default policy,
  it warns and exercises the portable checks rather than silently disappearing from CTest.
- A Win32 test now validates real loader/manifests on disk. Project-owned targets compile with
  elevated warnings. Empty landmark result sets and zero-length tracked quaternions fail closed.
- The smoke-log checker verifies the logged DLL SHA-256 and infers/validates a verified result's own
  landmark count, avoiding another hardcoded default.

### Evidence corrections

- Current documentation clearly separates the historical 21-landmark live proof from the current
  22-landmark artifact, which is live-validation pending.
- H-001 is a strong negative for an exposed stock stereo/HMD control surface and for an eye pair in
  the probed resident R-026 frame slots. It is not proof that every internal scene re-entry or
  dynamically constructed multi-view route is impossible.
- R-025 is already promoted into the 22-landmark gate; only the second device-creation consumer at
  `PreyDll.dll+0xD87710` remains to classify.

## Verification performed

The final source state was rebuilt and tested after the lifecycle changes:

```text
Fresh configure: Visual Studio 18 2026, x64 Release
CTest:          11/11 passed
Build doctor:   pass=7 warn=0 fail=0
DLL stress:     100/100 unsupported-host load/status/unload passes
Portable mode:  missing game baseline -> explicit warning, fail=0
```

The 11 tests are: VR math, engine map, aim fixture, wrench fixture, interaction fixture, runtime
bootstrap state, unsupported DLL host, Win32 OpenXR file preflight, DXGI method probe, smoke-log
parser, and build doctor.

## Exact next live test

1. Start Prey, load a stable save, and attach the chosen x64 debugging/injection tool. Do not arm
   unrelated renderer, weapon, or interaction breakpoints.
2. **Compute the SHA-256 of the DLL you are about to load, immediately before loading it, and
   record that value.** Do not rely on the hash in the table above: per F-005 the build is not
   byte-reproducible, so any rebuild changes it. The table's value identifies one historical
   build output, not the source. Then load that DLL and wait for `PreyVR_GetSmokeStatus()` to
   become nonzero.
3. Require all of:
   - `preyvr_smoke_start` reports version `0.3.0-lifecycle-hardening`, the expected DLL hash, and
     `expectedLandmarks=22`;
   - 22 `preyvr_landmark` lines, all `status=match`;
   - `preyvr_frame_observer_plan status=ready ... enabled=0`;
   - a bounded `preyvr_openxr_preflight` result with `action=none`;
   - `preyvr_lifecycle module=pinned unload=process_exit_only`;
   - `preyvr_smoke_result status=verified landmarks=22 ... module=pinned`.
4. If and only if that gate passes, call `PreyVR_SetFrameObserverEnabled(1)`. Let at least 120
   normal gameplay frames pass, then call `PreyVR_SetFrameObserverEnabled(0)`.
5. Require one disable summary with `frames >= 120`, nonzero first/milestone threads, and non-null
   first/milestone renderer pointers. Confirm Prey remains responsive and the original
   `EndRendererScene` prologue is restored.
6. Do **not** call `FreeLibrary`. Close Prey normally to release the pinned module. Preserve the log
   as a new capture and validate it with:

```powershell
./tools/Test-PreyVRSmokeLog.ps1 `
  -ExpectedStatus verified `
  -ExpectedLandmarkCount 22 `
  -ExpectedDllSha256 <the hash you computed in step 2>
```

Any hash mismatch, unsupported result, missing/mismatched landmark, crash, or hang is a stop
condition. Because the supported module is pinned, a failed post-pin experiment requires restarting
Prey before another DLL can be tried.

After this live proof, the next authorized rung remains a default-off OpenXR
**instance/system-only** experiment with deterministic cleanup and intact desktop rendering. Session,
D3D11 binding, swapchain, and stereo work stay behind separate gates.
