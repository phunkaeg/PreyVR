# Performance handover audit — 2026-09-09

**The resolution comparison is useful evidence against a simple pixel-count
explanation, but it does not clear GPU work, the XR path, or pixel-saving options.**
Prioritize a controlled timing experiment over implementing an upscaler now.

Reviewed `D:/Downloads/HANDOVERPERFORMANCE20260909.md`, its repository counterpart,
and source at `d0a64f12ff84162a1ecb1fd0be5809ac8ac240e5`. The new `xr.timing`,
`xr.coverage` and opt-in `xr.frustum` implementation supersede the original
performance plan's statement that those instruments/prototype were absent.

No game launch, injection, attachment, command-channel write or runtime setting
change was performed. Other-agent edits to `CommandChannel.cpp` and the startup
script were observed and left alone. Source cited below is pinned to the commit,
not those changing working files.

## What the evidence supports

The handover reports 43.75% fewer pixels, with a frame-interval median change from
13.128 to 12.771 ms (2.72%). That is a small observed change in this statistic.
It is reasonable to defer substantial upscaler/foveation work until better timing.
It is not a bound on GPU savings or on other scenes and runtime conditions.

The saved run manifests independently match the reported DLL identity and sizes:

- `run-20260909-134151`: 2688 x 2880, VirtualDesktopXR;
- `run-20260909-134810`: 2016 x 2160, VirtualDesktopXR;
- both identify DLL SHA-256
  `FE8C93CC3752C46C00EB0472F79077121318B018922E945548973AB6657277AC`, 800768 bytes;
- both record `tapeDir: null`; the relevant log lines establish actual runtime
  identity despite the launcher retaining an `xrsimDir` field.

The inspected `PreyVR.log` and current `results.txt` files do not retain either
original timing response. The latest result is coverage for the larger run and
a general report for the smaller run. Thus the table remains author-reported
timing evidence in this review, not a reanalysis of raw per-frame samples.
This search is limited to those named files and the relevant project documents.
See the saved `run-identity-checks.json` for coverage and read-time hashes.

## Corrections to the instrument interpretation

### 1. The percentiles are a rolling 512-sample window, not 30-second statistics

`FrameTiming.h` declares `kCapacity = 512`. `DurationSeries::Compute` summarizes
only that ring; `IntervalSeries::MissedDeadlineCount` counts since the last reset.
`XrTimingReport` emits `count` but discards the already available `total`.

The final window is on the order of 6–7 seconds at the reported cadence, not the
entire nominal 30-second experiment. Its exact duration cannot be recovered from
the median alone. Stages also occupy separate rings and are not paired by frame.

A compiled offline control using the unmodified committed implementation feeds
2000 intervals of 20 ms followed by 512 intervals of 10 ms. It reports:

```text
n=512 total=2512 p50_us=10000 max_us=10000 lifetime_over_budget=2000
window_over_budget=0 window_seconds=5.12
```

Both quantities are individually correct, but have different populations. A raw
17% decrease in cumulative counts is not a miss-rate comparison without a common
time window and denominator. Equal nominal collection time does not establish
equal frame counts. It can support an events-per-second comparison only if the
actual complete collection durations and counter epochs are preserved.

### 2. `frameMissed` is an interval threshold count, not a compositor drop count

The code increments once when the truncated service-to-service interval exceeds
`1000000 / displayHz` microseconds. The command defaults to 90 Hz; it does not
obtain or report the runtime's actual `predictedDisplayPeriod`.

An offline control alternating 11110 and 11112 microseconds produces 500 counts
in 1000 intervals against 11111. No compositor is present in that test. This
demonstrates threshold semantics, not 500 actual missed displays. A single long
interval also counts once regardless of how many display slots it spans.

Call it an **over-budget interval count**. Report configured budget, runtime
period and session totals separately. Actual compositor misses/reprojection need
runtime evidence. A service interval includes waits and work outside the timed
service, and can span skipped service calls.

### 3. CPU service medians do not clear the XR path

534 / 13128 is about 4.07%, but it is a ratio of marginal medians. It is neither a
worst-case percentage nor an additive allocation of the frame. Subtracting the
service median from the interval median does not recover a measured remaining
Prey CPU/GPU duration.

The service clock begins after the mutex/status/event gates. Early returns can
produce an interval/wait sample without a completed service/end sample. GPU
copies are asynchronous and unmeasured by CPU call durations. Desktop Present
and runtime scheduling remain outside or incompletely represented by these spans.

The lower-resolution `waitP95 = 5.216 ms` itself prevents saying that service time
is always below 4% of a roughly 13 ms interval. The wait population and completed
service population should first be aligned before comparing their percentiles.

### 4. More waiting after reducing work does not prove pixels were irrelevant

OpenXR can absorb saved work time as additional waiting. A small change in paced
cadence can coexist with a useful change in GPU headroom. A wait does not uniquely
identify which processor was limiting before or after the change. Conversely,
the reported 5-microsecond wait median also does not establish that every frame
is paced; frame-linked samples are needed to quantify either explanation.

This does not prove the game is pixel-bound. It leaves CPU, GPU geometry/pixel
work, mixed bottlenecks, Present and runtime scheduling to be discriminated.
Frustum coverage can also change culling, so it is not necessarily equivalent to
downscaling. Equal output size still means no automatic reduction in raster pixels.

Reference: [Khronos frame submission guide](https://github.com/KhronosGroup/OpenXR-Guide/blob/main/chapters/frame_submission.md).

### 5. Re-arming and retention need fixing before relying on a long comparison

At the reviewed revision, `SetXrTimingEnabled(1, ...)` resets the series on the
command thread without excluding an in-flight render-thread writer. On re-arm,
`gTimingEnabled` can still be true. Even setting it false would not drain a call
that already read true. Atomics prevent torn values but do not make reset an
atomic measurement-epoch transition: a writer can read the old count, race the
reset, then publish the old count plus one. This is a source-proven possible
interleaving; it was not observed in the game or deterministically exercised by
the audit harness.

Apply reset on the recording thread at a defined frame boundary, or exclude the
writer using the existing lifecycle synchronization. Tag a measurement generation
and preserve incomplete/failed-frame accounting. Do not introduce a render-thread
wait for the reporting consumer.

At this revision, `xr.timing` is not appended to the durable lifecycle log; the
command poller overwrites `results.txt`. Persist requested reports and sample
metadata on the command side. For quantitative experiments, export frame-linked
records asynchronously, with visible overflow/drop counts and measured recorder
overhead. Do not log or sort on every render callback.

## The next experiment

1. **Repair measurement scope first.** Expose window and lifetime counts, elapsed
   time, runtime period, reset generation and over-budget denominators. Preserve
   paired frame/stage samples across an explicitly bounded collection window.
2. **Add nonblocking GPU timing.** Use timestamp pairs and a timestamp-disjoint
   query around a proved render-thread scene boundary and around the eye copies;
   collect results several frames later without forcing completion. Keep CPU
   Present/wait times separate. Never call a render-end-to-render-end GPU span
   pure scene time: it may include idle time and runtime work. See
   [Microsoft D3D11 queries](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query).
3. **Repeat A/B/A in a fixed scene.** Preserve save, position, view, weapon, lane
   state, runtime settings/refresh, VSync/cap, supersampling and layer/capture
   state. Stabilize each case before a measured window. Repeat the baseline at
   the end to detect drift. Maintain the other agent's runtime ownership.
4. **Take compatible baseline controls.** Uninjected vanilla needs an external
   observer because `xr.timing` cannot exist there. Use the same external metric
   on both vanilla and modded runs; do not compare desktop Present FPS directly
   with XR-service intervals. Record matched resolution/FOV/settings. Vanilla
   identifies stock-game headroom, but VR changes rendering and pacing, so a
   direct subtraction does not isolate hook overhead. Also compare injected
   lanes-off versus lanes-on with XR/render policy held constant where feasible.
5. **Attribute the confirmed bottleneck.** If GPU pixel time changes materially,
   reconsider coverage/upscaling/foveation. If CPU work dominates, time native
   scene submission and mod callbacks separately before assuming draw calls are
   the culprit. Include the near-view packing hook, which can run many times per
   frame, as well as camera, IK, aim and reticle work. Avoid changing visual
   geometry in a way that invalidates the comparison merely to disable a hook.

The handover correctly rejects the moving-view lane bisect as unusable. It cannot
then attribute its 31.5 ms outlier specifically to scene change either; scene
variation is an uncontrolled alternative explanation, not isolated causation.

## Verification and limits

The audit harness compiled with MSVC 19.50.35729.0 using a snapshot of the committed
`FrameTiming.cpp`/header and passed its empty-data, mixed-window, reset and threshold
controls. Reproduce with CMake using the evidence directory as `-S` and a separate
build directory, then run `frame_timing_audit.exe`. Exact commands/output are in
`docs/evidence/performance-handover-audit-2026-09-09/verification.txt`.

These are instrument-semantics results in an offline harness, not game timing,
GPU attribution, a fix deployed to the DLL, or headset acceptance. The research
receipt keeps that distinction and does not promote R-126's bottleneck conclusion.
