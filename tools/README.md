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

## The virtual VR view

`New-StereoView.ps1` composes two eye dumps into one image that can be judged by
eye, with no headset, no OpenXR session and no submission path involved. That
separation is the point: per-eye rendering being *correct* and the XR swapchain
being *plumbed* are independent problems, and this lets the first be finished and
verified before the second starts.

```bash
./tools/New-StereoView.ps1 -Left frame-100-tag0.pvrframe -Right frame-100-tag1.pvrframe -Mode Anaglyph
```

| mode | the question it answers |
| --- | --- |
| `SideBySide` | Is each eye individually sane? Framing, culling, the viewmodel. |
| `Anaglyph` | Is the stereo *correct*? Disparity appears directly as colour fringing. A swapped pair, a zero IPD, or an inverted eye offset are all obvious here and nearly invisible side by side. |
| `Difference` | How much disparity, and where? Amplified abs(L-R): bar width encodes magnitude, so near objects give thick bands and far ones thin. A viewmodel drawn from a single camera shows as a black region while the world behind it shows bands. |

Verified on a synthetic stereo pair 2026-08-30: background at infinity produced
no fringing and a black difference, a 22-pixel near-object disparity produced
strong fringes and thick bands, and a 7-pixel mid-object disparity produced thin
ones.

`FrameDumpCommon.ps1` holds the shared reader. The dump format is described in
exactly one place on the PowerShell side for the same reason it is described once
in C++: a format contract duplicated across two readers drifts, and the drift
shows up as plausible-looking wrong images.

## Automating a protocol run

`live/preyvr-harness.js` is pasted into a Frida session attached to Prey and
exposes the A1/A2/A3 protocols as functions:

```js
PreyVR.enableObserver();
PreyVR.freezeScene();          // t_Scale 0, r_AntialiasingMode 0, r_MotionBlur 0
PreyVR.runNoiseFloor();        // step 0 - must be done before A1 means anything
PreyVR.runA1(10.0);            // does writing m_ViewCamera change the image?
PreyVR.runA2(0.064, 50.0);     // alternating-eye stereo, no headset
PreyVR.runA3(1);               // the double render, budget of ONE frame
```

Every call returns a plain object, and each protocol evaluates its own acceptance
criteria rather than leaving them to be remembered -- `restoreFailures` and
`applied` are checked inside `runA1`, and `runA2` keeps capturing until it has
actually seen both eyes instead of assuming two captures land on different ones.

Two F-009 behaviours are worked around deliberately. Several exports raise a
Frida `system error` even though the call takes effect, so everything goes
through a tolerant wrapper that reports what the *status getters* say rather than
what the call returned -- trusting the return value alone reports failures that
did not happen. And nothing depends on disabling the observer, since that path
has never been observed restoring a prologue on a live host.

`Invoke-PreyVRStereoAnalysis.ps1` turns the resulting dumps into the separation
table the acceptance criteria are stated in terms of, plus the PNGs and stereo
views:

```bash
./tools/Invoke-PreyVRStereoAnalysis.ps1 -Path "$env:LOCALAPPDATA/PreyVR/captures"
```

It pairs dumps **non-overlapping**, because the protocols capture in twos and a
sliding window would also compare the last dump of one experiment against the
first of the next -- a row that looks like an enormous noise floor and means
nothing. It reports numbers and passes no verdict: no threshold has been derived
yet, and a script printing PASS from an invented constant would be worse than one
printing nothing, because it would look like evidence.

Verified on a simulated run 2026-08-30: a jittered same-camera pair and a
22-pixel-parallax eye pair produced a 5.3x mean separation, and the maximum
channel difference discriminated far harder still (2 against 194).
