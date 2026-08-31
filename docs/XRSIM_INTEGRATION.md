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

---

## The session probe — the whole path, run

`preyvr_xr_session_probe` does what the DLL will eventually do: instance, system,
a D3D11 device on the runtime's required adapter, session, a texture-array
swapchain with a slice per eye, the frame loop, and a submitted projection layer.
It exists as a standalone executable so it can be *run*; until xr-sim there was no
way to execute any of it without a headset.

```bash
./tools/Invoke-PreyVRUnderXrSim.ps1 -Executable build/headless/Release/preyvr_xr_session_probe.exe -Arguments 10
```

First run, 2026-08-31, complete lifecycle, exit 0:

```
attempt api_version=1.1.60 result=ok
views count=2 recommended=2064x2208 samples=1
required luid=0x00000000:0x0001EB8E
adapter matched enum_index=3 r_overrideDXGIAdapter=3
formats count=8 offered=[ 29 28 91 87 10 24 40 45 ]
format chosen=28 match=0 colour_conversion=no
swapchain created 2064x2208 arraySize=2 images=3
engine_space left=-0.0315,-0.0000,1.6000 right=0.0315,-0.0000,1.6000 ipd_m=0.0630
fov_left l=-0.9425 r=0.7679 u=0.9599 d=-0.9599 (radians)
prey_projection fov=1.919862 ratio=0.963753 asym=0.000000,-0.041069,0.000000,0.000000
frames submitted=10 completed=10 skipped=0 protocol_errors=0
```

Four modules that had only ever been unit-tested were exercised against a real
runtime here, and all four came out right.

**`xrswapchain::SelectFormat` overrode the runtime and was correct to.** The
runtime offered `29 28 91 87 ...` — sRGB *first*, which the specification says is
its own preference order. The policy deliberately ignores that and takes the exact
match to Prey's backbuffer, and it did: `chosen=28 match=exact`.

**The LUID-to-index translation held through a real session.** Index 3, device
created on it, session accepted it. On the real headset the device was at index 0,
where this could not have failed visibly.

**The basis change is right.** OpenXR reported eyes at Y-up height 1.6 m; engine
space came out `z = 1.6000` with the separation on engine X, and the IPD matched
xr-sim's configured 63 mm exactly.

**The asymmetry solve reproduces by hand.** `asymRight = tan(0.7679) x 0.1 -
0.14281 = -0.04102` against the reported `-0.041069`, and the two symmetric
vertical edges produced exactly zero shift. The envelope put the zero on the wider
edge and the asymmetry on the narrower one, as designed.

**`xrframe::FrameContract` agreed with the runtime's own accounting.** The contract
reported 10 completed, 0 skipped, 0 protocol errors; xr-sim's `state.json`
independently reported `waitFrames 10, beginFrames 10, endFrames 10,
framesDiscarded 0, endsOutOfOrder 0`. Two separate accountings of the same frames.

### Per-eye capture proves the array indexing

The probe clears slice 0 red and slice 1 blue, so a capture that showed one colour
would mean the two eyes were never addressed separately. `xrsim-shot.ps1` returned
a side-by-side that is red on the left and blue on the right, with
`ProjViews 2`, `EyeSeparationM 0.063`, and `MeanLumaL 146.0 / MeanLumaR 152.0`.

### And an unplanned finding: xr-sim gamma-encodes format-28 content

Those luma numbers answer a question the swapchain-format policy had left open.
A `(0.85, 0.15, 0.15)` clear gives luma **91.5** if the bytes are taken as written
and **145.8** if linear→sRGB encoded. xr-sim reported **146.0**.

So xr-sim treats a format-28 swapchain as linear and encodes it for display. For
Prey that is a problem in waiting: its backbuffer is *already* gamma-encoded,
being what would have been presented, so a second encode is exactly the washed-out
result the policy header anticipates.

The policy is **not** being changed on one runtime's behaviour. What this buys is a
cheap repeatable method — clear to a known colour, read the reported luma, compare
against both hypotheses — which should be run against VirtualDesktopXR before the
format is fixed. If the two runtimes differ, the format must be chosen per runtime
rather than once.

---

## xr-tape — an outside witness on what we submit

`xr-tape` (`D:\Dev Debug\xr-tape`) is an OpenXR **API layer** that records what a
client submits — both eye poses, both projections, the layer set, frame timing —
into a versioned trace, then runs checks over it. It needs no code change here:
it attaches at the loader, so it works on today's build.

```bash
./tools/Invoke-PreyVRUnderXrSim.ps1 -Executable build/headless/Release/preyvr_xr_session_probe.exe -Arguments 30 -Tape
```

Current baseline, 2026-08-31: **19 passed, 0 failed, 1 skipped**.

### The two checks that are worth more than the other eighteen

`submitted_fov_matches_located` and `submitted_pose_matches_located` compare what
the runtime was *told* against what it *said*, from outside the process. Both
report max difference **0**.

That matters because of a limitation this project had already written down. The
synthetic half of the render-camera residual test "is exactly zero because the
test recomputes with the same formula on the same inputs" — self-referential by
construction, and noted as such in `RuntimeSnapshot.h`. These two checks are the
independent second opinion that comment was asking for, and no test running
inside our own process could ever be one.

### What it replaces

`HEADLESS_TESTING.md` already states the principle: every runtime probe should
emit a bounded fixture that can be replayed headlessly afterwards, so a live
session becomes permanent coverage rather than a one-off observation. We have
been hand-carving those one finding at a time — `aim_state_fixture`,
`wrench_query_fixture`, `frame_dump`. A trace is that fixture, produced
automatically, for every run.

### `layer_budget` fixed, 2026-08-31

It used to SKIP: the probe never called `xrGetSystemProperties`, so the system's
maximum layer count was unknown, and **a SKIP is not a pass**. One call fixed it —
now `peak 1 of 16 layers`. The same call surfaced `maxSwapchain 16384x16384`, and
the probe now refuses a recommended size larger than the system's own maximum,
because a runtime is not obliged to keep its recommendation inside its own limit
and the failure would otherwise land three calls later at swapchain creation.

### The gamma question is now a diff, not an experiment

The open format-28 question — does VirtualDesktopXR encode the way xr-sim does —
becomes: tape the same probe under both runtimes and compare the traces. That
turns "must be measured before the format is fixed" into one command, run on
headset day alongside the LUID.

### Reading a stereo failure

| result | means |
| --- | --- |
| a geometry check fails alone | the runtime reported a bad rig and we passed it through — look at the runtime |
| geometry **and** submitted-vs-located both fail | we mangled a good rig — look at our code |

And if a check fires and the check looks wrong rather than the code, that argument
is settled by adding a fault case to `tools/xrtape_selftest.py`, not by arguing.

---

# B1 — an OpenXR session hosted inside Prey

**Question:** can `PreyVR.dll` create an OpenXR session bound to *Prey's own*
D3D11 device, and submit Prey's backbuffer to a runtime?

This is Hurdle 2, and it is now runnable without a headset. It is a separate
track from A1/A2/A3: those are about the camera, this is about the plumbing, and
neither depends on the other's result.

## What is different from the session probe

The probe created its own device on whatever adapter the runtime asked for. That
is the easy case. Here the device belongs to Prey, was created before we loaded,
and sits on whatever adapter Prey chose — so the adapter question stops being
theoretical.

**xr-sim requires adapter index 3; Prey creates its device on index 0.** That
mismatch is exactly what `r_overrideDXGIAdapter` (R-052) exists to fix, and the
real headset can never test it, because there the required adapter *is* index 0
and a broken override looks correct. The simulator hands us the adversarial case
inside the real game.

## The pre-launch step, which cannot be done afterwards

`r_overrideDXGIAdapter` is **read once during device creation**, long before this
DLL loads. Setting it from the console during play does nothing, and the
resulting session failure points somewhere unhelpful.

Prey is CryEngine-derived, so the command line should take it without touching
any installed file — which matters, because **this project does not modify the
installed game**:

```
Prey.exe +r_overrideDXGIAdapter 3
```

*Unverified:* whether Prey honours `+cvar` on the command line has not been
tested. If it does not, the fallback is the **user** config under
`Documents\My Games\Prey`, which is not part of the installation. Editing
anything inside the game directory is out of scope.

Run `PreyVR_StartXrSession()` **without** setting it first if you want the
mismatch demonstrated: it will return `2` and the log will name the index to use.
That is a useful first result rather than a wasted run.

## Steps

1. Launch Prey with the adapter set, inject `PreyVR.dll`, confirm
   `preyvr_smoke_result status=verified`.
2. `PreyVR_SetFrameObserverEnabled(1)` — the XR frame is serviced from the
   observer, on the render thread, because that is the only place Prey's
   backbuffer is valid.
3. `PreyVR_StartXrSession()`.

| return | meaning |
| ---: | --- |
| 1 | running |
| 2 | **adapter mismatch** — the log names the index; relaunch with it |
| 3 | unavailable (no runtime, no system, or `openxr_loader.dll` missing) |
| 4 | failed — see the log's `step=` |

4. `PreyVR_GetXrSubmittedFrameCount()` should climb.
5. `PreyVR_StopXrSession()`. Teardown happens on the render thread at the next
   frame boundary, because the D3D resources belong to that thread.

## What it submits

A **flat mirror**: Prey's backbuffer copied into both eye slices. Deliberately
not per-eye — that depends on the camera track, and bundling them would make a
failure uninterpretable. This proves device, session, swapchain and submission;
nothing more.

The swapchain is built at Prey's backbuffer size rather than the runtime's
recommendation, so the copy is a straight `CopySubresourceRegion` with no scaling
blit. The runtime scales for display. Matching the recommendation is a later
optimisation and a different problem.

## Taping it

```bash
& 'D:\Dev Debug\xr-tape\tools\Invoke-XrTape.ps1' -Executable <Prey.exe> -Architecture x64 -RuntimeJson $manifest -Check -ExpectRuntime xr-sim
```

Expect `both_eyes_submitted` and `eye_pair_shares_display_time` to pass, and
`eye_subimages_distinct` to **fail** — the two eyes are genuinely the same image
in a flat mirror. That failure is the correct result for this step and becomes
the pass condition once per-eye rendering lands.

## Known deviation, recorded rather than hidden

Bring-up runs wait, begin and submit all on the render thread, because that is
where the backbuffer lives. XR-005 wants the wait on the game thread. The frame
contract's rule is opted out of explicitly with `AllowSingleThreaded(true)`
rather than left to fire every frame — and it must go back before per-eye
submission ships, because the split is what keeps head-to-photon latency down.

## A robustness note

`openxr_loader.dll` is **delay-loaded**. A hard import would make `PreyVR.dll`
fail to load at all when the loader is absent — the landmark gate would never
run and the failure would read as "the DLL is broken". Verified empirically by
removing the loader and confirming the fail-closed load test still passes.
