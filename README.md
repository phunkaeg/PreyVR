# PreyVR

Research and implementation workspace for an OpenXR VR mod for **Prey (2017)**. The intended end state is native, engine-owned 6DoF world rendering and motion controls; a flat OpenXR bridge is useful only as a reversible early milestone.

**Player preview (2026-09-10):** the package launcher starts/injects/enables VR, with controller-driven menus and inventory, a floating native HUD, and view reset. See [the player guide](docs/PLAYER-GUIDE.md) for controls, settings, supported build, and validation limits. The older research milestones below retain their original evidence dates.

**Hologram candidate 10:** adds right-stick click for the native weapon wheel to the controller beam, curved menus and inventory drag support. Candidate 09's corrected stereo and curved in-game popup were confirmed in the headset, but that session crashed; stability and the new wheel binding remain unverified live. See [hologram controls](docs/PLAYER-GUIDE-HOLOGRAM.md) and [current validation](docs/HOLOGRAM-VALIDATION.md).

**Validation correction:** the reported intermittent help-card text loss was an image-inspection error. Exact comparison of 112 eye images found matching card pixels, including the historical captures described as faulty. The player DLL is unchanged. See [the pixel verification](docs/RE-UI-CAPTURE-PIXELS-2026-09-10.md); headset comfort/readability acceptance remains open.

## Workspace map

| Path | Purpose |
| --- | --- |
| `docs/` | Evidence, decisions, RE notes, build identity, and test plans. |
| `captures/` | Local RenderDoc captures, eye dumps, screenshots, and traces. Large/generated evidence is ignored by Git. |
| `graphify/` | A reproducible, local knowledge-graph corpus joining PreyVR with the cross-engine VR-mod research corpus. |
| `src/`, `include/`, `tests/` | Implementation lanes for lifecycle, signature-gated frame observation, OpenXR preflight, engine integration, input, and deterministic tests. |
| `config/` | Versioned default/configuration schemas; machine-local settings stay ignored. |
| `tools/` | One-off utilities and capture/research helpers. |

## Current binary baseline

The installed release at `D:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release` is a 64-bit Windows build. `Prey.exe` is the small launcher; `PreyDll.dll` is the primary engine/research target. Full hashes and Ghidra context are recorded in [`docs/BUILD_BASELINE.md`](docs/BUILD_BASELINE.md).

## Current state — work in progress, 2026-09-10

**This is an active research build, not a finished mod.** It runs in a real
headset on a real level: stereo submission, 6DoF head tracking, controller-driven
hands through the engine's own IK solver, stick locomotion and turning, trigger
fire, a floating native HUD, and controller-driven menus and inventory. It also
has known defects, listed below with the same weight as the features. If you are
evaluating whether this accelerates your own work, read the open list first.

### Confirmed in a headset

- **Stereo, head tracking, and per-eye projection.** Bring-up is scripted end to
  end by `tools/Invoke-PreyVRStartup.ps1`, in the one order that works: `xr.srgb`
  before `xr.start` (gamma is a swapchain format, not a cvar), `view.recenter`
  before `view.apply`, and the near-pass FOV derived from the render aspect.
- **The hand and weapon move together and the arm bends** (R-104). Writing the
  controller's wrist into the rig's own animation-driven-IK target, at the entry
  of `ProcessAnimationDrivenIK`, makes Prey's two-bone solver solve the arm.
  Wrist rotation, finger travel and elbow bend are the engine's, not ours.
- **Motion controls.** Left stick moves, right stick turns, trigger fires.
- **The reticle follows the controller.** Prey's HUD draws into a 16:9 canvas
  scaled to *cover* the frame, so a viewport fraction is correct at the centre and
  increasingly wrong outward — measured at up to 271 px of error at 2688x2880,
  and corrected (R-118).

### Open defects, specifically

- **The right controller drags the left hand.** Both hands track their own
  controllers correctly and the left is *additionally* carried by the right. Two
  static investigations have narrowed it and refuted three hypotheses, including
  one of ours. The leading candidate is that the IK's shared world anchor is not a
  head position at all: it holds the native cached *reticle* ray origin, which our
  own reticle write moves. See
  [the handover](docs/HANDOVER-IK-CROSSTALK-2026-09-09.md) and
  [the producer trace](docs/RE-IK-CROSSTALK-2026-09-09.md). `ik.trace 1` publishes
  the per-frame record that should settle it; it has not yet been read in a
  headset.
- **Barrel alignment is unproved.** Aim direction follows the controller, but a
  shot still converges from the weapon's authored muzzle helper toward the reticle
  ray. `aim.origin` is a diagnostic, not a muzzle mode.
- **Performance is below the display rate** — roughly 78 fps against 90 Hz, so the
  compositor reprojects. Measured: **cutting pixel count by 44% bought 2.7% of the
  frame**, so this is not pixel-bound and upscaling or foveation would not fix it.
  What that leaves — CPU versus GPU inside Prey — is *not* established, and there
  is **no vanilla baseline at all**, which is the single most useful missing
  control. See [the performance handover](docs/HANDOVER-PERFORMANCE-2026-09-09.md)
  and, importantly, [the audit that corrects it](docs/RE-PERFORMANCE-HANDOVER-AUDIT-2026-09-09.md).
- **Stability.** The most recent hologram session crashed. Candidate 10's weapon
  wheel binding is unverified live.

### Instruments, which are most of what is reusable here

Every lane is off by default and switchable at runtime through a file command
channel, so routine testing needs no debugger:

| Command | What it does |
| --- | --- |
| `xr.timing 1 [hz]` | Frame-stage timing: `xrWaitFrame`, swapchain acquire/wait, `xrEndFrame`, whole service, service interval. p50/p95/p99 with window *and* lifetime populations reported separately. |
| `xr.coverage` | Per-eye fraction of the submitted rectangle inside the runtime's frustum, plus the equal-density target size. |
| `xr.frustum 1` | Render the runtime's requested frustum instead of Prey's. Quality at unchanged cost, **not** a speed-up. |
| `ik.trace 1` | One coherent per-hand IK record: anchor, yaw, head, grip, shoulder, and all three goal stages (raw/scaled/clamped). |
| `near.fov <decidegrees>` | Assert the viewmodel FOV where the renderer actually reads it. |

**The measurement discipline is the point.** Several of these exist because a
plausible number turned out to mean something other than its name — a profiler
that summarised a 512-sample window while its report called it a 30-second
sample; a "missed frames" counter that never consulted a compositor. Both were
caught by audit and are documented in the log rather than quietly fixed.

### Engineering baseline

`PreyVR.dll` gates on exact in-memory landmarks, pins itself for the process
lifetime, and refuses to run against an unrecognised build. Supported target is
the Steam `PreyDll.dll`, SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`. OpenXR 1.1.60
at pinned commit `64f2b37c`. **35 deterministic tests** plus offline
target-byte verifiers run with no game and no headset:

```powershell
./tools/Run-Headless.ps1
```

### Where to start reading

- [`docs/ADDRESS_REGISTRY.md`](docs/ADDRESS_REGISTRY.md) — what is proved, how, and
  at what confidence. Every entry names its own acceptance test.
- [`docs/FAILURE_REGISTRY.md`](docs/FAILURE_REGISTRY.md) — the traps, each of which
  cost a session. Read this before trusting a counter.
- [`docs/RE-INVESTIGATION-GUIDE.md`](docs/RE-INVESTIGATION-GUIDE.md) — how a static
  claim has to be established here.
- [`docs/RESEARCH_LOG.md`](docs/RESEARCH_LOG.md) — dated findings, including the
  ones that were later withdrawn.

**Addresses are build-specific.** RVAs in this repository are for the Steam build
named above and will not transfer to the Epic build, even though much of the
mechanism does.

## Working rules

- Treat every address as build-specific. Record RVAs, module names, byte signatures, confidence, and validation state in [`docs/ADDRESS_REGISTRY.md`](docs/ADDRESS_REGISTRY.md).
- Keep desktop presentation intact while each VR route is being proved.
- Promote a finding only when a capture, trace, runtime observation, or reproducible static-analysis result supports it.
- Keep pure pose/projection/input math independent of hook callbacks and covered by tests.

See [`docs/README.md`](docs/README.md) to begin research, or [`graphify/README.md`](graphify/README.md) to build the combined knowledge graph.

Run the no-game/no-headset verification loop with:

```powershell
./tools/Run-Headless.ps1
```

The Release DLL is written to `build/headless/Release/PreyVR.dll`. See [`docs/SMOKE_BUILD.md`](docs/SMOKE_BUILD.md) for its load contract and [`docs/FRAME_OBSERVER_BOOTSTRAP.md`](docs/FRAME_OBSERVER_BOOTSTRAP.md) for the historical proof and current observer protocol.

## Licence and scope

This repository is [MIT licensed](LICENSE) — use it, fork it, ship from it.

**It contains no game content.** Prey and `PreyDll.dll` are the property of
Bethesda and Arkane. Nothing here redistributes them, and the local copy of the
game used for reference work is excluded from the repository by `.gitignore`.
You need your own legally obtained copy of Prey (2017) on Steam, matching the
SHA-256 recorded above, for any of this to run.

Third-party components keep their own licences: OpenXR (Apache 2.0) and MinHook
(BSD 2-Clause) are vendored by the build and their licence files ship inside the
player package.
