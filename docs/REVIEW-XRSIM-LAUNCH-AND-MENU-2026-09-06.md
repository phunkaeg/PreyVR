# Injected Prey, menu control and xr-sim/xr-tape: capability audit

Audited HEAD: `8c5561cdd2dc029bbdd55231cbb2b25c6d0f73e6`.
Date: 2026-09-06, Australia/Sydney.

## Answer

There is no established technical requirement for a person to launch Prey,
inject the mod, navigate a menu or load an existing save. The tools and several
individual mechanisms exist. There is **not yet a verified, complete runner**
joining them, and the new menu path has an unresolved thread-ownership problem.

The statement "no scripted launch path in the repo" is substantially correct
if it means a turnkey launch/inject/load-save script. "Therefore that step must
be the user's" does not follow. These are implementation and validation tasks.

This audit did not launch, attach to, inject into, or control Prey. The user has
another agent owning that lane. Only existing files, tool availability, the
Release test suite and an isolated XR probe were examined. No game files or
runtime code were changed.

## Evidence against a user-only launch requirement

- [F-004](FAILURE_REGISTRY.md#f-004--renderdoc-blocks-vendor-extensions-prey-null-derefs-at-nvapi-init)
  records a scripted direct launch with `SteamAppId=480490` and the correct
  working directory. It reached renderer initialization and created the game
  window. The later failure was under RenderDoc instrumentation at the vendor
  extension stage. This does **not** prove a complete clean launch, but it
  directly contradicts treating every direct launch as necessarily terminated
  by Steam. Revalidate that normal Steam launch context without RenderDoc; do
  not repeat the known graphics-instrumentation failure.
- [F-008](FAILURE_REGISTRY.md#f-008---x64dbg-loadlib-freezes-prey-by-leaving-the-hijacked-thread-suspended)
  records successful `LoadLibraryW` injection through Frida. It also names CE
  `inject_dll` as a fallback, not as a separately demonstrated success in that
  entry. **Do not use x64dbg `loadlib`**: its suspended-thread failure is recorded.
- `Bootstrap.cpp:258-262` starts the file command channel automatically after
  the supported-build landmark gate. Routine commands therefore need no further
  Frida calls. Its actual paths are logged: `<log directory>/commands.txt` and
  `results.txt`; the log directory can be selected by `PREYVR_LOG_PATH` before
  loading the DLL.
- `XrSessionHost` can select xr-sim inside the game and submit Prey's own D3D11
  backbuffer. xr-sim can capture submitted eye images and accept scripted poses.
  Those are suitable building blocks for a menu observation loop.

Tool preflight **in this audit's session**: Frida process enumeration worked;
Steam was running; no `Prey.exe` was running. CE's bridge was unreachable. Its
tool listing alone is not evidence that it could inject now. This does not
establish exactly which tools were callable in Claude's earlier session.

The old `tools/live/preyvr-harness.js` assumes an attached session and an already
loaded DLL; it is not an injector. Its export-calling workaround is also older
than `CommandChannel.h`, which records four Frida-agent crashes. Preserve the
distinction between a historically successful one-time bootstrap and an unsafe
repeated Frida control loop. Use the file channel for subsequent commands.

## Findings that prevent claiming the loop is finished

### 1. Native input is dispatched directly on the file worker thread

`CommandChannel.cpp:325` creates `PollThread`; line 308 calls `Execute` on that
thread; lines 260/264 call `PostMenuAction`/`PostRawInput`; `InputPost.cpp:71-75`
immediately invokes native `PostInputEvent` there.

The native function synchronously walks input listeners and the action-map
path. It is not an engine-provided asynchronous queue. The new code supplies
neither native thread ownership nor synchronization with the normal input
update. Its SEH guard and atomic counters do not establish that contract.
This is an unresolved race/reentrancy risk, **not a reproduced crash**.

Queue owned input requests and drain them at a validated native input/update
seam that also runs in menus. A render-thread hook or a gameplay-only camera
callback is not sufficient evidence of the right owner. Observe delivery there
before treating a successful `menu result=0` as a safe implementation.

The pure event construction and press/release tests are useful. They cannot
prove listener thread safety, active bindings or UI acceptance. Press and
release currently arrive back to back; verify that the target menu consumes
both edges, and use separate native updates if its state sampling needs that.

### 2. `menu` commands are not yet XR controller bindings

The only callers of `PostMenuAction` and `PostRawInput` in `src/` are the file
command handler. There is no xr-sim/OpenXR button-to-native-menu bridge yet.
Command-driven menu testing is a valid intermediate capability; it does not
establish that a VR controller can navigate the game menus.

Posting is disabled initially: `input.post 1` must resolve the measured native
path before `menu` can work. Actions are `0 up`, `1 down`, `2 left`, `3 right`,
`4 accept`, `5 cancel`, `6 start`. The active key-name binds remain unverified,
as the commit correctly acknowledges. Observe the selected menu item and the
resulting screen, not just a returned counter. Raw input commands are also not
scoped to menu mode by the implementation.

### 3. Runtime selection alone does not configure the recorder or state directory

`SetXrRuntimeManifest` sets only `XR_RUNTIME_JSON`. The mod currently has no
equivalent configuration for `XRSIM_DIR`, `XR_API_LAYER_PATH`,
`XR_ENABLE_API_LAYERS` or `XRTAPE_DIR`.

The existing wrappers set those variables for their child. If an already
running Steam process launches/relaunches Prey, it does not acquire the invoking
shell's new environment. Wrapping `Steam.exe` is not equivalent to wrapping
the actual game, and does not inject `PreyVR.dll` either.

Two bounded approaches are available for the owning lane to validate:

1. Launch the real game executable with the documented `SteamAppId=480490`,
   correct game working directory and an explicit child environment. Verify
   that the resulting process survives as the intended child, then inject.
2. Launch through Steam, resolve the actual game PID and configure the small
   allowlist of XR variables **inside that process before OpenXR starts**,
   through a bounded bootstrap/configuration path. Then load/start the mod.

For either route, select the installed x64 runtime/layer, and give the run its
own absolute state and trace directories. The required settings are:

```text
XR_RUNTIME_JSON = C:\Users\meise\AppData\Local\xr-sim\runtime\xrsim-x64.json
XRSIM_DIR = <unique run directory>\xrsim
XR_API_LAYER_PATH = C:\Users\meise\AppData\Local\xr-tape\layer-x64
XR_ENABLE_API_LAYERS = XR_APILAYER_XRTAPE_recorder
XRTAPE_DIR = <unique run directory>\tape
XRTAPE_MAX_FRAMES = <bounded frame limit>
```

This is a configuration contract, **not an implemented launcher**. Do not
change the machine-wide runtime or assume setting variables after creating
the XR instance attaches the layer. The existing tape wrapper waits for its
child and kills it on timeout; it is not already a nonblocking game supervisor.

### 4. The observation script can accept stale captures

`Invoke-PreyVRControllerSweep.ps1:71-85` checks saved `sessionRunning`, graphics
and session state, but not PID liveness, executable identity or frame progress.
At inspection both the default state and the `preyvr` state said `FOCUSED` and
running, while their PIDs (59728 and 105080) no longer existed.

Lines 108-126 reuse `step-00`, etc. After a capture exception, an existing
sidecar under the shared state directory is accepted without checking its age,
frame, run identity or corresponding image. A previous probe's image can thus
be reported as a new game capture. The existing `step-00_sbs.png` under the
`preyvr` state directory was inspected: it contains the probe's flat red/blue
test pattern, not a Prey screen.

Require the actual live Prey PID and a progressing session, unique run/tags,
and fresh matching sidecar/image receipts after each request. A timed-out shot
may still have produced a valid image, but file existence alone cannot decide
that. Pass the exact same state directory to producer, controller and camera.
The current sweep defaults to `%LOCALAPPDATA%\xr-sim\preyvr`; setting only
`xr.runtime` inside Prey would leave xr-sim using its default root instead.

### 5. Old build paths and adapter assumptions can invalidate a run

The current Release DLL is `build/configure/Release/PreyVR.dll`, 626688 bytes,
SHA-256 `48414BBA19D1F110D69BB8F7E26504217E122448FE69F8CAC9E3AB36B84423F0`.
`build/headless/Release/PreyVR.dll`, frequently shown in older instructions,
was an August 31 artifact of 425472 bytes. Record and verify the injected hash.

The fresh probe selected adapter **0**, LUID `00000000:000155A0`. Statements
that xr-sim necessarily requires adapter 3 are historical. Compare the actual
game device LUID with the runtime requirement; do not blindly apply the old
adapter override.

## Validation performed here

- `ctest --test-dir build/configure -C Release --output-on-failure`:
  **24/24 passed** using the existing Release build. No shared DLL rebuild.
- Ran `preyvr_xr_session_probe.exe 10` through the installed x64 xr-sim runtime
  and `Invoke-XrTape.ps1 -Check -ExpectRuntime xr-sim`, with private audit state.
  Probe exit 0, D3D11, `FOCUSED`, 10 frames, 19 probe invariants checked with
  zero failures; **xr-tape 19 passed, 0 failed, 1 skipped** (depth absent).
- Replayable trace:
  `captures/audit-2026-09-06/tape/trace-preyvr_xr_session_probe.exe-95652-20260905-221503.ndjson`.
  Its header explicitly names the **probe**, not Prey. These receipts live in
  ignored capture storage and do not prove injected-game behavior.

## Next live acceptance sequence

Coordinate with the current owner of the Prey process, then complete the
launcher/configuration and input-thread work above. The hypothesis is that
native menu taps plus fresh XR images can load a save without physical input.
The control is a neutral menu frame; the variable is one bounded native tap;
the decision is an observed menu selection/screen change followed by an actual
loaded level. Keep binding failures distinct from launch and capture failures.

1. Verify the game/build, fresh PID, loaded DLL hash and supported bootstrap.
   Configure the runtime/layer before their first use. Require matching Prey
   PID/executable in the tape header and xr-sim state.
2. Through the file channel, enable `observer 1`, select the runtime with
   `xr.runtime <manifest>` if not supplied at launch, then `xr.start`. Read each
   result and require frame progress. Start with the flat backbuffer mirror
   for menu observation; it does not require a gameplay recenter.
3. After dispatch ownership is fixed, enable `input.post 1`. Capture the current
   screen, issue one menu tap, wait for a fresh capture and inspect the result.
   Navigate to the intended existing save based on what is actually visible.
   Do not send a guessed batch of accept inputs through changing screens.
4. Require a gameplay image and live gameplay state after loading. Only then
   recenter and enable the intended head/stereo/hand features. Record neutral
   and commanded pose images with matching sidecars, alongside the tape.
5. Check the actual **Prey** trace with `xrtape_check.py --expect-runtime xr-sim`
   and inspect the relevant images. Tape verifies submission contracts; it
   does not prove which save loaded, native input acceptance, correct hand
   placement, culling or visual correctness by itself.

No launch, menu, save-loading or injected-Prey gate is marked passed by this audit.
