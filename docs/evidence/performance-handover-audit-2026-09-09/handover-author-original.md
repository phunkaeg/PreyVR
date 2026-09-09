# Handover — PreyVR performance, measured 2026-09-09

For Codex. Everything below was measured on the wearer's machine (RTX 5070 Ti,
Quest 3 over VirtualDesktopXR) against `PreyVR.dll` at 800,768 bytes,
SHA-256 `FE8C93CC3752C46C00EB0472F79077121318B018922E945548973AB6657277AC`,
injected into Prey with `+r_Width`/`+r_Height` set at launch. Two 30-second
samples plus one confounded bisect. Repo at `3f07425` plus the commits named below.

## The headline: not pixel-bound

The wearer reported "shockingly bad" performance. The obvious suspect was pixel
count — 2688×2880 per eye, with R-121 measuring that only 41.25% of that
rectangle lands inside the runtime's requested frustum.

**That suspect is cleared.** A controlled comparison, same scene type, same
settings, same 30-second sample length, only the render target changed:

| | 2688×2880 | 2016×2160 | change |
|---|---|---|---|
| pixels | 7,741,440 | 4,354,560 | **−43.7%** |
| frame p50 | 13,128 µs | 12,771 µs | **−2.7%** |
| frame p95 | 16,291 µs | 16,673 µs | +2.3% |
| frame p99 | 17,536 µs | 17,969 µs | +2.5% |
| missed deadlines | 1891 | 1575 | −17% |

Both sizes share the 0.9333 aspect, so `r_DrawNearFoV 123.363` was identical in
both and the near pass is not a variable.

**Cutting 44% of the pixels bought 2.7% of the frame.** The workload is not
pixel-limited.

Corroborating signal: `waitP95` moved from 8 µs to 5,216 µs between the two runs.
At the lower resolution the app sometimes finishes early and starts blocking in
`xrWaitFrame` — which is what you would expect once the GPU stops being the
constraint, and would not happen if pixels were the limit.

## What this rules out

- **Frustum coverage as a performance fix.** `xr.frustum 1` (added at `3f07425`)
  is still worth having, but as image quality at unchanged cost, not as speed.
- **DLSS, FSR, NIS, VRS foveation.** All attack pixel work. All would buy about
  what the resolution drop bought.
- **The mod's XR submission path.** Measured directly, not inferred:

| stage | p50 @ 2688 | p50 @ 2016 |
|---|---|---|
| whole frame | 13,128 µs | 12,771 µs |
| whole mod service | 534 µs | 247 µs |
| `xrEndFrame` | 477 µs | 192 µs |
| `xrWaitFrame` | 5 µs | 5 µs |
| swapchain acquire+wait | 1 µs | 1 µs |

  The mod's entire per-frame XR work is **4% of the frame at worst**. The three
  full-image copies per submission (~93 MB at 2688×2880) are *not* visible here
  and cannot be — `CopyResource` is asynchronous, so the CPU call returns
  immediately and the GPU cost lands somewhere this instrument does not look.

## What is NOT established, and must not be inferred from the above

1. **Whether the remaining ~12.6 ms is CPU or GPU.** The instrument times CPU
   duration inside our own service call. Everything outside it is Prey's frame,
   and no D3D11 timestamp query exists yet to split it. "Not pixel-bound" is a
   measurement; "CPU-bound" is the leading inference from it, not a receipt.
2. **What Prey costs unmodded.** *There is no vanilla baseline.* Every number
   here is the modded game. The share of 12.8 ms that belongs to the mod's
   non-XR per-frame hooks — camera edit, head tracking, hand rig, IK, aim
   takeover, reticle projection — is unmeasured. This is the most important
   missing control.
3. **Any per-lane attribution.** A bisect was attempted and is **not usable**:

| | frame p50 |
|---|---|
| all lanes on | 12,057 µs |
| reticle dispatch off | 12,210 µs |
| reticle lane off | 31,563 µs |
| aim takeover off | 13,475 µs |

  The wearer was moving through the level between samples, so the scene differed
  each time. The 31.5 ms sample is scene change, not the reticle lane costing
  2.5× the frame. The A-to-D spread is the same magnitude as that noise. Nothing
  may be attributed from this table; it is recorded so nobody repeats it the
  same way.

## Tooling now available

Both landed today and are in the DLL above.

- **`xr.timing 1 [hz]`** arms; `xr.timing` reports. Times `xrWaitFrame`,
  swapchain acquire+wait, `xrEndFrame`, the whole service call, and the
  service-to-service interval — p50/p95/p99, max, and a separate missed-deadline
  count. Microseconds, because at 90 Hz the budget is 11,111 µs. Off by default;
  arming resets every series. Recording is one relaxed store plus one release
  increment into a fixed ring of atomics, and the reader races the writer
  deliberately so the reader's cost never enters the render thread.
  (`include/preyvr/FrameTiming.h`, 11 tests.)
- **`xr.coverage`** reports per-eye `Used` / `Covered` / `Short` and the
  equal-density target size. Two numbers rather than one, because a low figure
  means reclaimable pixels only while `Covered` is 1.0; with a shortfall the same
  figure means content is missing. (`include/preyvr/FrustumCoverage.h`, 6 tests.)
- **`xr.frustum 1`** renders the runtime's requested frustum instead of Prey's.
  Off by default. It does not save pixels at an unchanged target size — it
  spends the reclaimed area on angular density — and the near pass does not
  follow it, so the weapon will be wrong-scaled until `r_DrawNearFoV` is matched.

## Recommended next work, in order

1. **Take the vanilla baseline.** Same scene, same settings, same viewpoint,
   mod not injected. Until this exists, no optimisation can be judged, because
   nobody knows how much of the frame is ours. This is cheap and it is the gap
   that most limits every other conclusion here.
2. **Split CPU from GPU.** Asynchronous D3D11 timestamp disjoint queries around
   scene work and the eye copies, retrieved without blocking. Section 1 of
   `PERFORMANCE-PLAN-2026-09-09.md` specifies this; it is the receipt that
   "CPU-bound" currently lacks.
3. **Re-run the lane bisect from a fixed viewpoint.** Standing still removes the
   confound that made the table above useless. Sample each configuration for at
   least 20 s without moving.
4. **Then, if CPU is confirmed:** the lever is draw-call count, not resolution.
   Object detail, view distance and shadow cascades reduce CPU work directly.
   Test one at a time from a fixed viewpoint.
5. **Do not spend effort on upscaling or foveation** until 1–3 change the
   picture. The resolution comparison already bounds what they could win.

## Session context, for anyone reading the frame numbers

The 2688×2880 sample was taken with the **xr-tape recording layer disabled**
(`-NoTape`). An earlier session left it enabled and wrote 27.2 MB of NDJSON — an
OpenXR API layer in the frame path. Any timing captured before this document was
written may include that overhead. 65 frame captures (1.44 GB) were also taken
that session; each performs `CopyResource` then an immediate CPU `Map`, which is
a synchronisation stall by construction. Neither was active for the measurements
above.
