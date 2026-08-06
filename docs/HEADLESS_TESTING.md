# Headless testing

The default engineering loop must finish without launching Prey, loading OpenXR, or wearing a headset:

```powershell
./tools/Run-Headless.ps1
```

By default the script performs a fresh CMake configure, Release build, and CTest run. `-NoFresh` is available for a deliberate incremental loop. The installed game is read-only and optional by default; use `-RequireGameBaseline` when a missing or mismatched installation must fail the run. The current baseline contains:

| Test | Contract |
| --- | --- |
| `vr_math` | Quaternion fail-closed behavior, pose composition, radial input deadzone, and tracked-pose freshness policy. |
| `engine_map` | All 22 promoted runtime signatures match a synthetic supported image; a one-byte mutation and truncated image both fail closed. It also pins the ArkPlayer interaction/selector/usable-entity offsets and the `0x70`-byte candidate-record contract. |
| `aim_state_fixture` | Exact reversible reticle and wrench-query captures decode into typed fields; the bounded A0b policy produces a normalized 10-degree Z-up yaw, preserves origin/UI state, emits twelve direction bytes, enforces a 15-degree limit, and rejects corrupt data. |
| `wrench_query_fixture` | The exact 88-byte component snapshot, original four-hit capture, and completed A0b baseline/synthetic records decode into typed layouts. The A0b fixture preserves the same collider/depth, `0.127165`-unit lateral displacement, and exact `origin + writtenDirection * distance` endpoint; truncated data fails closed. |
| `interaction_query_fixture` | Exact edge-baseline and +/-15-degree candidate records decode at the evidenced `0x70` stride. It reproduces the two-to-one useful-null transition, keypad `0xFDE0` to entity `0x1117` committed-target change, exact twelve direction bytes, and exact 2.5-unit native-query vector; truncated records fail closed. |
| `runtime_bootstrap` | The exact `EndRendererScene` signature produces one observer plan; a changed prologue, truncated image, null base, and address overflow all fail closed. The OpenXR readiness state machine covers disabled, missing/invalid runtime manifest, missing/non-x64/non-DLL/wrong-export loader, and ready states without loading OpenXR. |
| `dll_load_fail_closed` | The actual Release DLL loads in an isolated host, exports all bootstrap statuses, rejects observer activation, keeps its frame count at zero, and reports unsupported when `PreyDll.dll` is absent. |
| `openxr_preflight_files` | Bounded Win32 inspection recognizes the runtime-manifest shape and proves the packaged PE is an x64 DLL exporting `xrGetInstanceProcAddr`; missing, malformed, and arbitrary-DLL inputs fail closed. |
| `smoke_log_parser` | The read-only log checker accepts the isolated host's expected unsupported result, validates the logged DLL SHA-256, and requires internally consistent counts for a verified result. |
| `dxgi_method_probe` | A separate helper process can create a hidden two-pixel D3D11 swapchain and resolve the local system Present implementation. It never enters Prey. |
| `build_doctor` | When the optional game baseline is present, the installed launcher and engine module must be x86-64 and match their recorded SHA-256 values; one SEC_IMAGE probe then validates every landmark from the compiled C++ table. The ReGenny notebook is required and Graphify availability is reported. |

On 2026-08-07 the fresh Release loop passed 11/11 tests. With the researched game installation available, the deduplicated build doctor reports 7 passes, 0 warnings, and 0 failures; the single landmark result covers all 22 compiled entries. If the game directory is absent and not required, it emits an explicit warning and still tests the portable parts. The smoke-host log is redirected into the CTest temporary directory, so the headless loop does not overwrite a real in-game smoke log.

## Boundary

Headless tests should own deterministic policy and data transforms:

- pose composition and coordinate conversions;
- asymmetric-FoV/projection construction once the engine matrix convention is evidenced;
- controller role mapping, deadzones, snap/smooth-turn envelopes, and tracking-loss fallbacks;
- render-view classification from captured metadata fixtures;
- cbuffer companion transforms from captured raw-byte fixtures;
- configuration parsing, bounds, implications, and fail-closed defaults;
- address/signature validation against exact on-disk modules;
- log/capture schema validation and regression summaries;
- injector/manifest/configuration preflight before any launch.

Runtime smoke tests remain necessary for questions involving lifecycle, renderer ownership, threading, GPU resource identity, OpenXR timing, culling, native actions, and visual coherence. Every runtime probe should emit a bounded fixture or summary that can be replayed in the headless suite afterward. This turns each headset session into new permanent test coverage instead of a one-off observation.

## Promotion rule

A live discovery is not ready for implementation until its deterministic parts have been extracted behind a pure API and exercised headlessly. Hook callbacks should gather validated native state, call those APIs, apply a bounded result, and restore transient state.
