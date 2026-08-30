# xr-sim — a headset-free OpenXR runtime for PreyVR

`xr-sim` (`D:\Dev Debug\xr-sim`) is a deterministic OpenXR runtime built for
testing VR mods without a headset. It closes the gap this project had explicitly
left open: the OpenXR session, swapchain and submission path was previously
unwritable-with-confidence because none of it could be *run*, and F-010 had
already demonstrated that untested OpenXR assumptions come out wrong.

## Why it fits Prey specifically

Prey is **x64 / D3D11**, which is xr-sim's richest tier.

| capability | Prey's tier |
| --- | --- |
| `XR_KHR_D3D11_enable`, x64 | supported |
| instance / session / swapchain / frame lifecycle | supported |
| per-layer **PNG + JSON capture** | supported — only D3D11 gets this |
| texture-array swapchains | supported; capture selects the submitted `imageArrayIndex`, so a two-slice stereo texture captures the correct eye |
| scripted head and controller poses, actions, fault injection | supported, renderer-independent |

Verified on this machine 2026-08-30: `test-backends.ps1 -Architecture x64`
reported all seven backends exercising the full lifecycle, D3D11 included.

## It does not disturb the real runtime

xr-sim is selected **per process** through `XR_RUNTIME_JSON`, which the OpenXR
loader consults ahead of the registry. Nothing writes to the registry, so
VirtualDesktopXR remains the machine's active runtime and a connected Quest keeps
working while a test runs against the sim. `Invoke-PreyVRUnderXrSim.ps1` also
saves and restores the environment variables, so the calling shell is not left
silently redirected.

## Running

```bash
./tools/Invoke-PreyVRUnderXrSim.ps1
```

Defaults to `preyvr_xr_adapter_probe`, which is the cheapest end-to-end check
that the loader, the runtime and our client all agree. `-Executable` runs
something else; `-ShowState` prints xr-sim's `state.json` afterwards.

Runtime state, commands and captures live under `%LOCALAPPDATA%\xr-sim\preyvr`.

## What the first run established

```
extensions count=9 d3d11_enable=yes
attempt api_version=1.1.60 result=ok
runtime name="xr-sim" version=1.0.0
required luid=0x00000000:0x0001EB8E
adapter index=3 ... required=yes
result=matched enum_index=3 r_overrideDXGIAdapter=3 override_needed=yes
```

Three things worth separating out.

**The version fallback earned its keep.** xr-sim accepts `1.1.60`; VirtualDesktopXR
rejects it outright (F-010). The same binary works against both only because the
probe tries newest-first and falls back rather than hardcoding a version. That
design was a reaction to one runtime and is now validated against two.

**The LUID-to-index translation was exercised for the first time.** xr-sim's
`best_adapter_luid()` picked index **3**, so `override_needed=yes`. Yesterday's
real device sat at index 0, where a broken translation would have looked
perfectly correct. This is the first run that could actually have caught the bug
R-052 exists to prevent.

**But the value is xr-sim's, not the headset's.** `0x1EB8E` is which adapter the
*simulator* chose. The number VirtualDesktopXR requires is still unknown and still
needs a headset. What is now proven is the *mechanism*; what remains is the
*measurement*. Anything that reads this file and writes `r_overrideDXGIAdapter 3`
into a real config is making exactly the mistake this paragraph exists to prevent.

## Known differences from the real runtime

Worth holding in mind, because a pass under the sim is not a pass under VDXR:

- **9 extensions against VDXR's 31.** Code that depends on an extension the sim
  does not implement will behave differently.
- It reports `systemName = "Meta Quest 3"`, but the poses are simulated.
- Default rig at first run: 63 mm IPD, per-eye FOV `l -54, r 44, u 55, d -55`
  degrees — genuinely asymmetric and mirrored, which is the shape
  `preyvr::stereoframe` is written for.
- It is a developer runtime, not a conformant consumer one. It is the right tool
  for "does our client behave correctly", and the wrong tool for "will this feel
  right in a headset".

## What it unlocks

Everything in `preyvr::xrframe`, `preyvr::xrswapchain`, `preyvr::stereo` and
`preyvr::controller` was written to be testable without hardware, but only as
*pure policy*. The actual OpenXR calls that consume them had no way to be
exercised. They do now, which makes the session bootstrap worth writing.

The scripted control channel (`xrsim-cmd.ps1 "head rot 30 0 0" "hand r point 0 -5"`)
also means `preyvr::controller` can be driven through a real action pipeline
rather than only through unit tests.

## Catalog

xr-sim's own `catalog/profiles.json` carries a PreyVR profile
(CryEngine/Arkane fork, x64, D3D11, direct binding) marked **client probe
verified** — that verification is this project's adapter probe.
