# PreyVR performance priorities — 2026-09-09

> **Update after R-125/R-126:** Timing counters and a runtime-frustum prototype
> now exist. The [handover audit](RE-PERFORMANCE-HANDOVER-AUDIT-2026-09-09.md)
> identifies limits in their measurement scope and gives the current next steps.
> Repair timing/retention and establish controlled CPU/GPU evidence before
> prioritizing the pixel-saving implementations proposed below. This document
> retains the original investigation at `bd7f281`.

Read-only investigation of implementation at `bd7f281`, with saved Quest 3 / VDXR
evidence from `run-20260908-143942`. No runtime interaction, settings changes or
new benchmark was performed. The saved session predates the latest HUD fix.

The first falsifiable question is: **are missed display deadlines dominated by
scene GPU work, CPU work, or frame scheduling?** Existing receipts identify
opportunities but do not provide a CPU/GPU timing breakdown that answers this.

## 1. Measure the limiting stage before changing the render architecture

Add opt-in, buffered timing to the current path:

- CPU frame intervals and time spent in the mod's render-end service;
- separate `xrWaitFrame`, swapchain acquisition/wait, and `xrEndFrame` durations;
- desktop Present duration and effective VSync / frame-limit settings;
- asynchronous D3D11 GPU timestamp queries around scene work and eye copies,
  with disjoint checks and deferred, nonblocking result retrieval;
- each submitted eye's source frame, source pose time and image age;
- runtime display period, missed deadlines and p50/p95/p99 frame times.

Use the same loaded save, view and settings for an initial comparison. Start with
an easy room and a complex scene. Run without screenshot requests or verbose tape
recording, then measure diagnostic overhead separately. A screenshot performs
`CopyResource` followed immediately by CPU `Map` in `FrameCaptureWin32.cpp:200`;
this is a potential synchronization stall when capture is requested, not proof of
an ordinary-frame bottleneck.

Decision rule: prioritize scene workload when GPU duration approaches/exceeds the
display budget; prioritize scheduling when GPU work fits but measured waits,
Present or CPU work consume the deadline. At 90 Hz the display interval is 11.11
ms; at 72 Hz it is 13.89 ms. These are budgets, not measured Prey performance.

## 2. Match rendered coverage to the runtime's eye frusta

The startup script selects `xr.native 1` (`tools/Invoke-PreyVRStartup.ps1:152`).
This preserves Prey's projection; it does not select OpenXR eye FOVs. Changing the
half-FOV argument to `xr.stereo` has no effect on projection in this mode.

Saved evidence: `docs/evidence/headset-session-static-2026-09-08.json`,
`last_tape_views` versus `last_tape_end`, sequence 12380:

| Quantity | Current submitted left eye | Runtime requested left eye |
| --- | --- | --- |
| Horizontal angles | -60 / +60 degrees | -54 / +40 degrees |
| Vertical angles | -61.6815 / +61.6815 degrees | -55 / +44 degrees |
| Submitted image size | 2688 x 2880 | — |

The right eye has the mirrored horizontal asymmetry. For a rectilinear image,
the runtime rectangle's width fraction is
`(tan(R_runtime)-tan(L_runtime))/(tan(R_render)-tan(L_render))`; the height
fraction uses up/down in the same way. This gives **0.63955 x 0.64497 = 0.41249**.

Thus about 41% of the submitted pixel rectangle covers the requested frustum,
assuming corresponding eye orientation. This is not an exact lens visibility
mask, a measured GPU-work fraction, or a predicted FPS gain. Some overscan can
serve reprojection; measure the margin required by the submission timing.

Engineering opportunity: derive asymmetric scene/near-pass projection and target
sizes from each runtime, with an explicit resolution scale and tested overscan
margin. The saved example's same tangent-space sampling density would need about
1719 x 1858 pixels before margin/alignment, rather than 2688 x 2880. This is an
illustration, not a launch preset. Alternatively keep the larger target and spend
the improved coverage on visible detail.

Changing FOV alone at an unchanged target size does **not** reduce the number of
pixels rasterized. Reducing targets alongside corrected coverage, or applying an
appropriate visibility mask early enough in rendering, provides the pixel-work
savings. Geometry and CPU savings depend on actual culling/pass behavior.

Validate scene camera, culling, near-pass asymmetry, weapon scale, submitted FOV
and source-pose ownership together. Exercise the latest HUD canvas mapping at
the new aspect ratio and off-centre positions. Do not replace the headset-tested
near-pass setup with an unverified generic FOV setting.

## 3. Address scheduling and per-eye freshness

`CameraEditHook.cpp:1114` alternates the eye each ordinary game frame.
`XrSessionHost.cpp:697` refreshes one held image and submits both. At 90 successful
fresh game images/second, each eye receives approximately **45 fresh scene
images/second**. Runtime presentation and reprojection can occur more often.

`FrameObserverHook.cpp:63` calls `ServiceXrFrame` from the render-end hook, before
the original end-frame function. `XrSessionHost.cpp:915` calls `xrWaitFrame` there;
swapchain waiting is also in that service. The try-lock comment at line 891 only
means the service does not wait for its control mutex. It does not make OpenXR
calls nonblocking. Much of the game's rendering has already been issued before
this wait/begin sequence.

Measure these waits first. A later architectural solution should schedule the
frame prediction and per-eye render work coherently, sharing once-per-frame game
work where possible. Both-eye rendering improves freshness but also adds GPU
work; it is not a free throughput gain. Respect OpenXR frame/image ownership and
D3D11 context ownership. Earlier double-render/re-entry attempts have UI and
deadlock failures (F-014/F-015); enabling an old experimental path is not an
optimization plan.

Reference: [Khronos frame submission guide](https://github.com/KhronosGroup/OpenXR-Guide/blob/main/chapters/frame_submission.md).
It documents the blocking/prediction role of `xrWaitFrame` and frame pipelining.

## 4. Establish inexpensive settings controls, then add scalable rendering

- Check actual `r_Supersampling` and use **1** for the baseline. The startup
  parameter defaults to 0, meaning leave unchanged. Native setting 2 means 2x2
  scene samples (four times the pixels), and 3 means 3x3. See the verified startup
  contract at lines 64–66 and its console send at line 147.
- Retain the current SMAA 1X and motion-blur-off baseline. Alternating eyes share
  temporal histories; temporal AA previously produced the displaced weapon ghost.
- If scene GPU time dominates, compare supported shadow/AO/reflection settings
  one at a time. Do not lower texture quality merely because it is a graphics
  setting; first check memory pressure.
- Expose a per-runtime linear resolution scale. 85% on both axes uses 72.25% of
  the pixels; 80% uses 64%. This trades detail for workload at unchanged FOV.
- Investigate a spatial upscaling pass and fixed foveated shading as subsequent
  options. Keep central weapon/reticle/HUD detail intact, and gate GPU-specific
  paths on capabilities. Temporal reconstruction needs a separate per-eye history
  and motion-data investigation first.

The current XR swapchain size is fixed at session creation and later backbuffer
resizes are refused (`XrSessionHost.cpp:994`). Dynamic resolution therefore needs
an internal rendering scale plus a compatible output/upsampling path; changing
`r_Width` during the session is not a working implementation.

[vrperfkit](https://github.com/fholger/vrperfkit) supplies D3D11 prior art for
spatial upscaling and NVIDIA VRS fixed foveation. Its documented supported VR APIs
are Oculus and OpenVR, so treat it as implementation prior art rather than a
drop-in OpenXR/VDXR solution.

## 5. Optimize eye-image copies if measured material

The normal completed stereo submission performs three full-image copies:
backbuffer to the refreshed held eye, then both held eyes to the acquired XR
array image (`XrSessionHost.cpp:700` and `:717`). At 2688 x 2880 and four bytes per
pixel this is about 92.9 MB of copied payload per submission, excluding read/write
doubling and other traffic. It is GPU work, but copy count alone does not establish
that it dominates scene rendering on the 5070 Ti.

Direct target rendering or fewer intermediates may help after timing confirms
the cost. Preserve acquisition/release lifetime and the held other-eye image.
Timing the CPU `CopyResource` call alone will not measure the asynchronous GPU
copy cost.

## Recommended next bounded implementation

Implement the buffered performance counters first, then a runtime-frustum coverage
prototype behind an explicit opt-in. Benchmark at equal visible sampling density
and separately at equal output size. Accept only with lower measured cost or
improved clarity, stable frame pacing, correct eye/pose provenance and wearer
acceptance of the weapon, reticle and HUD. Static tests and xr-sim can validate
math/contracts; they cannot establish real-headset performance or visual quality.
