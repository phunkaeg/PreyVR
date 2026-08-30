# Tools

Place small, reproducible helpers here: log parsers, capture-index generators, byte-signature validators, and Ghidra-export normalizers. Keep generated output in `captures/` or `docs/`, not alongside the tool.

## DXGI method probe

`dxgi_probe/` creates a two-pixel hidden D3D11 swapchain in its own process, prints the system `dxgi.dll` RVAs for `IDXGISwapChain::Present` and `ResizeBuffers`, and immediately releases every resource. This lets a live tracer hook the shared implementation without creating probe resources inside the game process.

## Smoke-log checker

`Test-PreyVRSmokeLog.ps1` validates the latest inert-DLL result and logged DLL SHA-256. For a verified run it infers the expected landmark count from the result and requires that every emitted line matches; `-ExpectedLandmarkCount` and `-ExpectedDllSha256` can pin a particular artifact. CTest also runs it against the isolated host's intentional `unsupported` log.

## Engine-map probe

`preyvr_engine_map_probe` maps an on-disk PE with `SEC_IMAGE_NO_EXECUTE` and validates it through the same compiled C++ landmark table used by the DLL. The build manifest queries `--count`, and the doctor invokes it against `PreyDll.dll`; no second PowerShell signature list exists.

## Detached-ray probe helper

`preyvr_ray_probe` accepts a normalized live direction and bounded Z-up yaw, then prints the rotated direction and exactly twelve little-endian bytes. It uses the same tested pure policy as the wrench and interaction stack-local protocols.

## OpenXR adapter probe

`preyvr_xr_adapter_probe` answers the one Hurdle 2 question a running Prey cannot: which GPU the
OpenXR runtime requires, and what index that is in the order R-025's `EnumAdapters1` loop walks --
which is exactly the value `r_overrideDXGIAdapter` (R-052) takes. It creates a real `XrInstance`,
calls `xrGetD3D11GraphicsRequirementsKHR`, and matches the returned LUID against the DXGI
enumeration, ending in one actionable line:

```
preyvr_xr_adapter result=matched enum_index=N r_overrideDXGIAdapter=N override_needed=yes|no
```

It runs **out of process** -- no Prey, no injection, no writes -- and is the first code here that
actually calls into OpenXR rather than inspecting its files. It needs a headset connected and the
runtime streaming to reach the LUID; without one it stops at `XR_ERROR_FORM_FACTOR_UNAVAILABLE` and
still prints the adapter list, which is half the answer and costs nothing.

It tries `XR_CURRENT_API_VERSION` first and falls back to `XR_API_VERSION_1_0`, printing which was
accepted. That is not defensive padding -- see F-010, where the pinned 1.1 SDK is rejected outright by
a 1.0 runtime.

## Frame capture and the console bridge — the test harness

Three pieces that together turn *"does the view move?"* from something a human squints at into a
number that can be re-measured tomorrow. All three are inert until the frame observer is enabled.

**`PreyVR_RequestFrameCapture(tag)`** arms a one-shot backbuffer readback of the next observed frame,
serviced from inside the existing `RT_EndFrame` observer. That is the only correct place: the
callback runs on the engine's render thread, and `ID3D11Multithread` protection is **off** on this
device, so touching the immediate context from anywhere else would race the engine. Captures are not
queued -- a second request while one is pending is refused, because a queue would silently spread one
A/B experiment across frames that are not adjacent. The dump lands beside the DLL, or in
`PREYVR_CAPTURE_DIR` if set.

`Map` with `D3D11_MAP_READ` stalls the GPU, so a capture perturbs frame timing. It is default-off and
one-shot for that reason, and must never be left armed during a measurement that cares about rates.

**`PreyVR_QueueConsoleCommand(cmd)`** types an **allowlisted** command into Prey's console via
`CXConsole::ExecuteString` (RVA `0xE1FBE0`, verified against its prologue at call time rather than
trusted). Everything goes through `preyvr::console::Classify` first, which denies anything not on a
short justified list, denies `exec` by name, and denies any line containing a command separator --
without that, `t_Scale 0; quit` would pass a first-token check. Commands are submitted with
`bDeferExecution = true` so the engine drains them on its own update instead of running console work
on our thread.

This is what makes frame comparison meaningful. A scene that is still simulating differs frame to
frame regardless of the camera, and temporal AA (`r_AntialiasingMode = 3`) and motion blur
(`r_MotionBlur = 2`) are both live, so even a *static* scene does. `t_Scale 0` plus
`r_AntialiasingMode 0` is the difference between evidence and noise.

**`preyvr_frame_diff a.pvrframe b.pvrframe`** prints the numbers:

```
preyvr_frame_diff result=ok width=2560 height=1440 pixels=3686400 \
  meanAbsolute=... maxAbsolute=... changedPixelRatio=... tagA=0 tagB=1
```

It reports `result=incomparable` distinctly rather than as a zero difference, because "these frames
are not the same shape" and "the image did not change" are opposite conclusions. **It passes no
judgement**: no acceptance threshold exists yet, and inventing one before the separation table has
been measured would produce something that looks like evidence without being any. Build the table
first -- identical frame, consecutive frozen frames, small yaw, large yaw -- the same way the
render-camera residual limit was built.

**`Convert-FrameDump.ps1`** encodes a dump to PNG for viewing. Encoding happens out of process so the
in-game path stays dependency-free, and the PNG is only ever for eyes -- `preyvr_frame_diff` reads the
raw mapped bytes, so nothing about the conversion (including its default downscale) can affect a
measurement.

Verified end to end on synthetic dumps 2026-08-30: a 40-pixel bar shift across differing row pitches
produced `changedPixelRatio = 0.125`, exactly the 80 of 640 columns that changed, and the same file
against itself produced exactly zero.
