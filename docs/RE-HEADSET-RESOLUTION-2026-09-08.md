# Headset resolution independent of the desktop

2026-09-08. Static source/specification investigation; no game changes or runtime
operations. User reports a playable but soft headset image, with desktop
3440x1440 and Windows scaling 125%. Those desktop settings are not a measurement
of the actual game backbuffer or the headset's recommended eye size.

## Confirmed current restriction

`src/dll/XrSessionHost.cpp:725-738` obtains width/height from Prey's DXGI swapchain
description. `CreateSessionAndSwapchain` at lines 388-403 explicitly uses that
size instead of the runtime recommendation, with arraySize=2 (two full eye
images, not two halves of a side-by-side desktop image).

`SubmitStereoPair` creates held eye textures from the backbuffer description,
copies backbuffer pixels, and copies each held eye into an XR array slice.
The submitted imageRect also uses that same width/height. The application
therefore supplies desktop-sized imagery for the runtime to scale. There is
no independent headset resolution policy in this path.

The standalone `tools/xr_session_probe/main.cpp:225-240` already demonstrates
querying runtime recommended view dimensions. It is a reusable API example;
it does not mean the injected host currently follows the recommendation.

## MEASURED, 2026-09-08: the headset receives 48% of the pixels it asks for

First run of `xr.resolution` on this machine, Quest 3 over Virtual Desktop:

```
recommended=2688x2880   max=16384x16384   views=2   viewsDiffer=0
backbuffer=2560x1440    heldEye=2560x1440  submitted=2560x1440
pixelRatioPercent=48    widthRatioPercent=95   heightRatioPercent=50
```

**The vertical axis is exactly half.** Width is nearly right at 95%, so this is
not a uniform scale factor and not a DPI question: the runtime wants each eye
**taller than it is wide** (2688x2880), and the game supplies a 16:9 desktop
frame (2560x1440) whose height is half of that. The compositor upscales 2x
vertically, which is the reported soft image, now a number instead of an
impression.

This is the shape mismatch section "Desktop DPI and ultrawide aspect" predicted
in the abstract. The measurement makes it concrete and gives the target: height
is where the deficit is, so a change that widens the frame buys almost nothing.

**Still not measured:** whether Prey's scene is drawn at the backbuffer size or
resolved down to it from something else. `r_Supersampling` exists in this binary.
The ratio above bounds what the compositor receives; it does not prove the scene
was drawn at 2560x1440 rather than upstream of a resolve. Section 2's leads are
the route to that, and they now have a reason to matter.

## Step 1 is implemented, 2026-09-08

`xr.resolution` on the command channel reports the chain this document asks for
before any resolution work:

```
recommended=WxH  max=WxH  backbuffer=WxH  heldEye=WxH  submitted=WxH
recommendedSamples=N  viewsDiffer=0|1  heldFormat=N  views=N
pixelRatioPercent=N  widthRatioPercent=N  heightRatioPercent=N
```

**The runtime's recommendation was never queried by the injected host.** Only
the standalone `xr_session_probe` did. `CreateSessionAndSwapchain` now
enumerates the view configuration and logs the recommendation alongside the size
it actually builds, so the gap is recorded at session start rather than argued
from desktop settings later. Nothing about what is submitted changed: the policy
is still Prey's backbuffer, deliberately, and this makes that policy visible.

`pixelRatioPercent` is the per-eye pixel count the compositor receives against
the count it asked for. Below 100 means the runtime is upscaling, which is the
measurement behind "soft"; above 100 means pixels are being discarded.

**What this does NOT measure**, and must not be read as: the scene's own render
target. Prey may render internally at another size and resolve before the
backbuffer, and `r_Supersampling` exists in this binary. The ratio bounds what
the compositor receives, not what was actually drawn -- so a good ratio does not
prove there is no upstream bottleneck. Section 2's leads remain the route to
that, and they need the ratio first to know whether they matter.

## Recommended order

1. **Measure the complete chain.** Log runtime-recommended/max size per eye,
   actual scene color/depth dimensions, active viewport, game backbuffer,
   held-eye texture, XR swapchain and submitted imageRect. Preserve eye FOV,
   frame identity and GPU frame time. Current `swapchain created WxH` log gives
   the submitted allocation but not the upstream scene's effective resolution.
2. **Near-term bridge: larger game render/output size.** Test the engine's own
   resolution-change route in a controlled restart, accepting arbitrary eye
   dimensions if possible, with a smaller desktop mirror. This is simpler than
   replacing the rendering pipeline but may encounter window/display clamps.
   Arbitrarily resizing DXGI alone is insufficient: engine color/depth targets,
   viewports, camera dimensions and dependent postprocessing must agree.
3. **Preferred architecture: independent engine eye rendering targets.** Set
   target dimensions from the selected OpenXR runtime's recommended per-eye
   width/height times an explicit linear scale. Render Prey's scene at that
   size, capture its resolved color before desktop-only reduction, and submit
   a matching XR image. Downscale separately to a modest spectator window.
   This can retain the current alternating-eye schedule; it does not require
   another RenderWorld invocation or a new stereo scheduling experiment.
4. **Then optimise clarity and performance.** Assess temporal AA, sharpening,
   material texture filtering and independent high-quality HUD composition.
   Preserve the existing headset-tested baseline: `STEREO_ROUTE.md` records
   mode 3 AA chosen by the wearer and motion blur disabled on September 2.
   That receipt explicitly retracts a previous blanket TAA warning. Do not
   disable TAA speculatively; investigate histories only if a controlled new
   comparison shows a problem at the new size/cadence.
   Spatial upscaling is a performance option, not equivalent to rendering all
   detail at the target resolution. Dynamic resolution/foveation are later
   options after the independent size and ownership contract is reliable.

OpenXR exposes recommended and maximum dimensions per view. Use those rather
than hard-coded monitor dimensions or the headset panel's marketing resolution.
For differing view sizes, use appropriately sized subrectangles or separate
swapchains; validate against view/system/API limits. Keep headset FOV tied to
the actual per-eye projection, not the spectator window's aspect ratio.
[OpenXR view configuration](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrViewConfigurationView.html).

Simply enlarging the XR destination is not a fix. D3D11 CopyResource requires
matching dimensions and does not rescale; the current whole-image copies need
matching sources or a deliberate resampling pass. Resampling existing pixels
does not provide the detail of a higher-resolution scene render.
[D3D11 CopyResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource).

## Promising Prey-native leads, not yet proved seams

Steam `/Prey/PreyDll.dll` string search found:

| VA | String/meaning |
| --- | --- |
| `0x181DC85F8` | `r_Supersampling` |
| `0x181DC85B0` | Help describes 1=1x1, 2=2x2, 3=3x3 SSAA |
| `0x181DC8690` | `r_SupersamplingFilter` |
| `0x181DC8610` | Help describes box/tent/Gaussian/Lanczos output resolve filters |
| `0x181DBEA38` | `r_AntialiasingMode` |
| `0x181DBECA0` | `r_AntialiasingTAASharpening` |

Chairloader's renderer headers also name `IsSuperSamplingEnabled`,
`ResolveSupersampledBackbuffer`, `ChangeResolution` and `SetViewportDownscale`.
These are strong leads to an existing engine-owned high-resolution target and
downsample boundary. Next static question: **which color resource is resolved
by the supersampling path, how is its size selected, and can XR consume it
before the desktop reduction?** Establish target instructions/resource ownership;
do not copy EGS header addresses into this Steam build.

Enabling SSAA alone may improve antialiasing while still resolving down to the
old desktop-sized image before XR copies it. It is therefore not sufficient
proof of increased headset input resolution. The help's 2x2 mode also implies
four times the scene pixel count, not a modest 2% or twofold pixel change.

## Desktop DPI and ultrawide aspect

125% Windows scaling concerns desktop logical/physical coordinate handling. It
can influence an application's chosen window size when DPI handling is wrong,
but it is not an OpenXR eye-resolution setting. The mod copies GPU resources
directly, so a screenshot's apparent dimensions or a DPI-virtualized window
measurement must not replace inspection of the actual texture descriptor.
Do not change the user's global display scaling to solve the VR architecture.
[Windows DPI behaviour](https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows).

3440x1440 has a very wide sampling grid. A headset view generally benefits from
dimensions selected for its own optics/FOV rather than inherited ultrawide
proportions. As an illustration only, 2560x2560 is about 32% more pixels than
3440x1440 but 78% more vertical samples. Neither is asserted to be the optimal
size for this user's unconfirmed headset/runtime.

Define a mod scale unambiguously: 1.25x width and height is 1.5625x pixels;
1.5x is 2.25x pixels. Runtime settings may express percentages differently,
so always display the resulting WxH. Higher pixel counts must be assessed
against GPU time; lower refresh can worsen clarity during movement even when
stationary detail improves.

## Acceptance and lifecycle

- Same headset render dimensions and FOV with different desktop window sizes
  and monitor/DPI settings; mirror may vary independently.
- Fine world detail improves in a stationary matched scene, with no hidden
  lower-resolution bottleneck or frustum mismatch.
- Rebuild held-eye textures and XR resources as a coherent size generation;
  discard stale eye pairs on resolution changes. Current dimensions are latched
  at session start and held eye textures are allocated lazily without a resize
  comparison in SubmitStereoPair. Prefer controlled restart for initial tests.
- Verify per-eye metadata, viewport/UV edges, menus, near pass, postprocessing,
  resource limits and frame time. XR-sim/tape can verify dimensions/submissions;
  graphics evidence must prove scene detail was actually rendered at that size.

The user confirms RTX 5070 Ti, Quest 3 via Virtual Desktop. Distribution across
headsets is the requirement: discover whichever OpenXR runtime is active,
rather than assuming Virtual Desktop selects VDXR versus SteamVR. Default to
the runtime recommendation at 1.0 linear scale, expose scale and optional
explicit dimensions, and keep spectator resolution independently configurable.
Do not hard-code Quest 3 presets, depend on virtual monitors/driver display modes,
or silently change global desktop/runtime settings. Treat streamed-image
compression as a separate quality stage after engine rendering and XR submission.
No specific resolution or performance level is claimed validated here.
