# Integration consolidation — 27 September 2026

## Shared branch

Use **`collab/integration`** for combined development. Create feature branches
from it and target their pull requests back to it. `main` remains the release
baseline until feature parity and headset acceptance are agreed.

The consolidation joins `b190b1e` (shared integration) and `99c5474`
(`codex/integration`, including comfort commit `e8451e5`) through merge
`c754c41`, followed by the fixes described below. The merge had no conflicts.
The two histories are preserved rather than squashed into unrelated copies.

Other branch dispositions:

| Branch | Disposition |
| --- | --- |
| `codex/comfort-controls` | Included through `99c5474`; retain as feature history |
| `docs/evidence-corrections` | Already an ancestor of the consolidated tree |
| `codex/release-0.5.1` | Already an ancestor; no new release is created |
| `stereo-parallax-and-xr-input` | Already an ancestor |
| `wip/main-worktree-20260918` | Its tree exactly matches preserved baseline `5c30857`; keep the backup, do not replay it over newer fixes |
| `feat/inventory-backdrop` | Keep separate: its unique magenta transparency diagnostic is not in this consolidation. It is an optional experiment, not a finished inventory backdrop |

No branches or worktrees are deleted. No donor source or new native addresses
are introduced.

## Code changes

- Import wired snap turning, head-relative movement, floor-space preference,
  reference-space rebasing, and separate height calibration. Defaults are
  45-degree snap turning and head-relative movement. Floor support still does
  not implement physical crouch, height matching or a comfort vignette.
- Make bare `move.headrelative` a read-only query. Previously querying it set
  the option to 1 and requested a movement release. Values other than 0/1 are
  now refused, and replies include the actual enabled state.
- Read the right and left controller buttons from one `TrackingFrame` for
  recenter, support-grip suppression and gameplay actions. Independent latest
  reads could combine different publications into a chord.
- Move menu/guide cache invalidation onto the XR frame thread. The `ui.scale`,
  `ui.margin` and `ui.guide` setters previously reset `gHost` optionals directly
  from the command thread while rendering could be reading them. Setters now
  publish an atomic generation; the frame service performs resets under its
  existing host lock. Settings sent before session creation are retained.
- If a due reference-space rebase is deferred by lock contention, clear tracking
  and pointer samples instead of publishing hands in the runtime's new space
  against the old mod reference. Retry the rebase on the next frame.
- Persist `SnapTurnDegrees`, `HeadRelativeMovement` and `ReferenceSpace` in
  launcher configuration. Validate saved and explicit values before launch;
  command-line arguments override saved values. `ReferenceSpace` is a child
  process environment override, not a machine-wide runtime change.
- Send startup settings and `vr.enable` in one ordered command batch. Remove
  the successive command-file replacements separated by assumed 400 ms delays.
  Correct the launcher text: runtime tuning commands are session-only; JSON
  holds preferences for future launches.
- Register the existing launcher resolution regressions and the new comfort
  configuration regressions with CTest.

## Validation performed

**No Prey launch, injection, xr-sim game run or headset session was performed.**

- Release x64 DLL and all C++ test targets rebuilt using Visual Studio 2026.
  Existing `strncpy` deprecation warnings remain in `XrInput.cpp`; no build errors.
- CTest: **44/44 passed**. This includes portable math/fixture tests, mock native
  hooks, an offscreen D3D11/mock-XR inventory swapchain test, unsupported-process
  DLL smoke loading, and read-only game-file/hash/landmark checks.
- Both launcher regression suites passed under **Windows PowerShell 5.1**.
  CTest also ran them using PowerShell 7. The tests extract only configuration
  functions from the launcher AST; they never execute its launch body.
- New launcher coverage includes old configurations without comfort fields,
  saved/explicit precedence, explicit off values, valid endpoints, invalid
  values, save/reload persistence, preservation of unrelated settings, and
  command order before VR enable.
- `git diff --check` passed with CRLF-aware whitespace checking.

Built DLL: `build/integration/Release/PreyVR.dll`, **967,168 bytes**.
SHA-256: `F4B4A693518A5B7B5652D7EA7DDDE75AE2C986CAEFD9B3A0CC37F75713A7A422`.
This is a local integration build, not a published player package.

Saved check outputs: [final build](evidence/integration-consolidation-20260927/build.txt)
and [CTest](evidence/integration-consolidation-20260927/ctest.txt).

## Remaining acceptance and next work

Static review establishes the corrected ownership and ordering; the 44 tests do
not simulate every command-thread/render-thread interleaving or prove headset
quality. The September 19 runtime evidence remains historical evidence for that
candidate, not a live test of this DLL.

When game runs are authorized again, test saved settings on a fresh launch;
snap/smooth changes and neutral rearming; menus held open across turns/recenter;
seated and standing calibration; LOCAL, STAGE and supported LOCAL_FLOOR;
runtime reference-space changes; and UI scaling during interaction.

The next implementation priorities remain controller-accessible settings and
UI reliability, scene-hit reticle targeting, physical crouch with a native
capsule/stance contract, special weapon/psi consumers, haptics, and measured
resolution/performance controls. Keep each as a reviewed feature branch; none
is implicitly complete because these branches were consolidated.
