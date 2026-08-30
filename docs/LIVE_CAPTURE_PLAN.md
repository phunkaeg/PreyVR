# Live capture plan

**Live-host time is the scarce resource on this project.** Static analysis is cheap and can run
unattended; every question that needs Prey actually running costs a round-trip with the operator, a
game launch, and a save load. Discovering mid-implementation that we need one more number is the
expensive failure mode.

So this document does two things. It names the next three hurdles and what each will need, and it
specifies the captures that answer those questions **ahead of time**, so that a single live session
harvests data for work we have not started yet.

The governing principle: **capture wide, decide later.** Reading a whole struct costs the same as
reading one field, and a logged blob can answer a question we have not thought of yet. Only the
*decoding* should be selective.

## Status at a glance

| Hurdle | State | Blocked on |
| --- | --- | --- |
| 1. Supported-host load | **PASSED 2026-08-29** — all acceptance criteria met; seven registry entries promoted | done |
| 2. OpenXR session and first HMD output | Preflight `ready`; **all blocking device/swapchain/adapter data now captured** | the runtime's required LUID, answerable out of process |
| 3. Per-eye camera | Seam confirmed live: R-033 pool passes, single caller, asym baseline zero, frustum formula corrected | Hurdle 2; culling-vs-asymmetry still untested |

Everything in `ADDRESS_REGISTRY.md` is `static-only` confidence unless its entry says otherwise. No
camera has been written to.

---

## Hurdle 1 — first supported-host load

**Goal.** `PreyVR.dll` loads into a running Prey, the 30-landmark gate passes *in memory* rather than
on disk, the frame observer counts callbacks, the module pins, and disable restores the original
bytes cleanly.

**What we already have.** The gate, the observer, the OpenXR preflight, and the fail-closed lifecycle
are built and headless-verified. The artifact reproduces byte-for-byte from source (F-005).

**What only a live run can give us.** Whether the static model of the engine is *true of the running
process*. This is the moment to validate the entire address model at once, which is what
`RuntimeSnapshot` does — see [Capture A](#capture-a--engine-identity-and-camera-baseline).

**Acceptance criteria.**

- `preyvr_smoke_result status=verified landmarks=30`
- `preyvr_snapshot result=complete`
- Observer callback count greater than zero with a stable renderer pointer and thread id
- Disable restores the target prologue and the log records it

---

## Hurdle 2 — OpenXR session and first HMD output

**Goal.** Create an instance, system, and session; bind D3D11; create swapchains; present something
to the headset, even a duplicated flat image.

**Binding design constraint (XR-005), adopted before any of it is written.** Wait once early, cache the poses, render
every camera from that same cached pose, submit from an end-of-frame hook after all cameras have rendered, hand off last.
Five of eleven surveyed mods got this wrong; the cost of getting it right is zero while the submission path does not yet
exist. The only legitimate deviation is re-polling inside the render backend at draw time — late-latching, head motion
only. This makes `RT_EndFrame` (R-003) the natural submit site, which the frame observer already targets.

**The questions that will block us, and where the answers come from.**

| Question | Why it blocks | Source |
| --- | --- | --- |
| Which adapter did Prey create its device on? | `XR_KHR_D3D11_enable` requires the app device to sit on the LUID the runtime names. A mismatch is a failure to present, not an artefact | Live: swapchain/device query (Capture B) |
| Which adapter does the runtime require? | Same | `xrGetD3D11GraphicsRequirementsKHR` — **needs an instance and systemId but no session**, so it is answerable early and cheaply |
| Do they differ? | If so we must set `r_overrideDXGIAdapter` (R-052) **before** the renderer initialises, and translate LUID to adapter index ourselves | Comparison of the two above |
| Swapchain format, size, buffer count, MSAA | Determines what XR swapchain format to request and whether a resolve or format conversion is needed | Live: `IDXGISwapChain::GetDesc` (Capture B) |
| Is the device multithread-protected? | We will submit from a thread the engine does not own. Without `ID3D11Multithread` protection, sharing the immediate context is unsafe | Live: `ID3D11Multithread::GetMultithreadProtected` (Capture B) |
| Which thread runs `RT_EndFrame`, and at what interval? | Frame pacing and where `xrWaitFrame`/`xrEndFrame` can legally be called | Live: observer telemetry (Capture A) |

**Pre-answered already, statically.** Prey honours `r_overrideDXGIAdapter` as an `EnumAdapters1`
index inside the real device-creation path (R-025/R-052), so adapter agreement is reachable natively
with no hook. Two constraints: it is an *index*, not a LUID, so we must enumerate adapters ourselves
to translate; and it is read once during device creation, so it must be set before the renderer
initialises.

---

## Hurdle 3 — per-eye camera

**Goal.** Drive a per-eye camera through `CRenderView::SetCamera` (R-030) and get two distinct eye
images with correct culling.

**What is already settled statically.** The seam copies the camera **by value** into the render
view's own `m_camera` at `+0x11A0` (R-049), so it cannot alias `CSystem::m_ViewCamera` — the H-008
contamination is structurally impossible on this path. It derives the render frustum from the camera
it is handed, including all four asymmetry shifts, and `fWL/fWR/fWB/fWT` are tangents, the same
parameterisation as OpenXR's `XrFovf` (R-050). `CCamera`'s full layout is verified (R-048).

**What only a live run can give us.**

| Question | Why it matters |
| --- | --- |
| Does `RenderCameraMatchesSource` hold on a real frame? | **This single boolean validates our entire model of `SetCamera`.** If the derived tangents recompute from the live source camera, R-030, R-048 and R-050 are all confirmed at once |
| Which `CRenderView` in the pooled `[2][2]` array is live per frame? | We must write the view the renderer will actually consume |
| Baseline `m_asymL/R/B/T` | Expected all-zero. A non-zero baseline would mean something already uses asymmetry and we must compose rather than overwrite |
| What calls `SetCamera` per frame, from which thread, how many times? | It is virtual with no static call sites (R-053), so this is unanswerable statically |
| Does culling follow the asymmetry? | The engine header marks the asym fields *"not used for culling atm"* (R-048). Invisible at IPD scale, a correctness question at wide asymmetry |
| Which of the 84+ `GetViewCamera` readers are hot per frame? | Static analysis found the sites; only a live run says which execute |

---

## The captures

Organised by **what the game must be doing**, because that is what determines how many separate live
sessions we need. Run them in order; each is a superset of the questions its hurdle needs.

### Capture A — engine identity and camera baseline

**Game state:** anywhere the game is running. Main menu is sufficient; a loaded save is better.
**Status: BUILT.** Runs automatically on load, read-only, immediately after the landmark gate passes
and before any hook is armed.

It resolves `gEnv` from the module base and follows `pSystem`, `pRenderer`, `p3DEngine`, checks each
object's vtable against the RVA we resolved statically, confirms `IProcess` slot 3 really is
`RenderWorld`, confirms `gEnv->pRenderer` and the `CD3D9Renderer` singleton are the same object, and
decodes `CSystem::m_ViewCamera`.

Grep the smoke log for `preyvr_snapshot`. One live load validates **R-039, R-040, R-043, R-044,
R-048, R-054 and R-005 simultaneously**, plus gives the asymmetry baseline Hurdle 3 needs.

Failure is never fatal — a partial snapshot localises the first bad pointer and the mod proceeds
unchanged.

### Capture B — device, swapchain, and render views

**Game state:** in-game, a save loaded, standing still.
**Status: MEMORY-ONLY HALF BUILT** (2026-08-27). Reached through the `PreyVR_CaptureRenderViews`
export, and **never run on load** — Hurdle 1's job is to prove a never-loaded lifecycle with the
smallest possible payload, so a failure here can never be mistaken for a lifecycle fault. The export
refuses unless the gate has already verified the host (smoke status 2). Returns `0` complete,
`2` partial, `1` refused; results land in the smoke log as `preyvr_renderview` lines.

It walks `m_pRenderViews[2][2]` (R-033) and runs **that entry's own stated acceptance test** — all
four non-null, and `[t][1]` distinct from `[t][0]` — checking each entry's vtable against R-053 so a
wrong pool offset is reported rather than decoded as garbage. For each live view it decodes `m_camera`
(R-049) and the `CRenderCamera` block (R-050) and logs the **residual as a number**. It also captures
the R-026 per-frame block as an independent cross-check, and the swapchain and device pointer
*values* only.

**The COM half is still not built, deliberately.** `IDXGISwapChain::GetDesc`, `IDXGIDevice::GetAdapter`
and `ID3D11Multithread::GetMultithreadProtected` are read-only queries but they are calls into the
game's objects, a step beyond reading memory. They stay out until Hurdle 1 has passed.

| Item | Source | Serves |
| --- | --- | --- |
| `IDXGISwapChain::GetDesc` | `CRenderer+0xAE88` (R-005) | Hurdle 2 |
| Adapter `DXGI_ADAPTER_DESC1`, including `AdapterLuid` | device → `IDXGIDevice::GetAdapter` | Hurdle 2 |
| Feature level, creation flags, `ID3D11Multithread` state | `CRenderer+0xAF28` (R-005) | Hurdle 2 |
| `RT_EndFrame` thread id and interval | observer telemetry | Hurdle 2 |
| `CRenderView` pool at `CRenderer+0x6F38`, all four slots | **R-033** (was mis-cited as R-026) | Hurdle 3 |
| Per view: `m_camera` at `+0x11A0`, `CRenderCamera` at `+0x1620` | R-049, R-050 | Hurdle 3 |
| `RenderCameraResidual(m_camera, m_RenderCamera)` **value**, not just the verdict | computed | Hurdle 3 |
| The four `m_asym*` baseline (expected all-zero) | R-048 | Hurdle 3 |

`DecodeRenderCamera`, `IsPlausible`, `RenderCameraResidual` and `AsymmetryFromFovTangents` are already built and
tested — Capture B needs the *reads*, not the analysis. **Log the residual as a number, not a pass/fail.** The
synthetic correct case is bit-exact zero, but a live block is derived by the engine's own float ops and will carry
some rounding; recording the actual value is what establishes where that floor sits, and a bool would throw it away.

**Note on scope.** Everything in Capture A is a pure memory read. Capture B calls COM methods
(`GetDesc`, `GetAdapter`) on objects the game owns. Those are read-only queries, but they are a step
beyond reading memory and should be treated as such: guarded, on a known-safe thread, and reversible
by construction (they change nothing).

### Capture C — what changes per frame

**Game state:** in-game, moving and looking around.
**Status: SPECIFIED, NOT BUILT.**

Sample the same fields as A and B across N frames and report, per field, whether it is *stable* or
*per-frame*. This produces the restore list: **anything that changes per frame is something the
engine rewrites for us; anything stable is something we must restore ourselves.** Getting that list
from a capture is far cheaper than deriving it from a crash.

### Capture D — scenario-specific

**Status: SPECIFIED, NOT BUILT.** Each needs the game in a particular situation.

| Scenario | Question |
| --- | --- |
| A Looking Glass in view | Does a second scene render change the `CRenderView` pool occupancy? Corroborates H-009 |
| Weapon fired at a surface | The outstanding projectile proof for the detached-aim lane |
| Console: `g_detachCamera 1` | Settles R-056 outright — is the detached camera live or vestigial Crysis code? One command |

---

## Working rules for live sessions

1. **Read before write, always.** Capture A and B are read-only by construction. No capture may
   become a write without its own bounded, reversible written protocol.
2. **Capture wide.** Log whole structs, not just the fields today's question needs.
3. **Emit verdicts, not just numbers.** Every capture reports a pass/fail against an expectation, so
   reading the log is not another round-trip. `IsPlausible` and `RenderCameraMatchesSource` exist for
   this.
4. **Seed with a known answer.** Any new enumeration must include something we already know, so a
   silent failure shows up as a missing known result rather than a clean-looking empty one. This rule
   comes from [F-006](FAILURE_REGISTRY.md) and [F-007](FAILURE_REGISTRY.md); both were methods that
   answered confidently while operating on the wrong input.
5. **Never modify the installed game.**

## What is built today

| Component | State |
| --- | --- |
| `preyvr::snapshot` — typed views, decoders, plausibility, capture, report | Built, 12/12 tests, mutation-checked |
| Page-validated reader (`RuntimeSnapshotWin32`) | Built; `VirtualQuery`-guarded, fails closed |
| Wired into the bootstrap, read-only, after the gate | Built |
| Capture B / C / D | Specified above, not built |

The tests verify the **decoding and capture logic** against a synthetic engine image. They do not and
cannot verify the **offsets** — those are verified against the installed `PreyDll.dll` by the build
doctor and by the static analysis recorded in `ADDRESS_REGISTRY.md`. Keep the two kinds of
verification distinct when reading a green test run.

---

## Results — Hurdle 1 session, 2026-08-29

Full record in the [live capture](../captures/traces/2026-08-29-prey-hurdle1-live-capture.md). Headlines:

- **Hurdle 1 passed.** `smoke_result status=verified landmarks=30`, `snapshot result=complete`, all
  identity checks yes. Seven entries promoted from `static-only` to `reproduced`.
- **The residual earned its keep.** It scored `1.5588` on the first live frame, which is how the
  missing near-plane factor in the `SetCamera` frustum formula was found. Logging it as a number
  rather than a verdict is the only reason the cause was diagnosable from one sample.
- **Asymmetry baseline is zero**, so Hurdle 3 may overwrite rather than compose.
- **Both seams have exactly one caller** — `SetCamera` from `+0x2110E5`, `RenderWorld` from
  `+0xE0BC62`, both on thread 44208 at 33/s. The return-RVA gate is trivially implementable.
- **Adapter selection genuinely needs LUID matching**: five adapters, four of them identical
  `RTX 5070 Ti` entries distinguishable only by LUID.
- **`ID3D11Multithread` protection is OFF**, which confirms submission must happen on the render
  thread — exactly where `RT_EndFrame` already sits.

### Still outstanding after this session

| Question | Why it is still open |
| --- | --- |
| The LUID `xrGetD3D11GraphicsRequirementsKHR` returns | **Tooled, 2026-08-29**: `preyvr_xr_adapter_probe` asks and prints the matching `r_overrideDXGIAdapter` index. Verified out of process up to `xrGetSystem`; needs only a powered headset to finish. Its headset-off run already re-confirmed the five-adapter enumeration and exposed F-010 **Mechanism proven 2026-08-30 under xr-sim**, which required adapter index 3 and so exercised the LUID-to-index translation for the first time — yesterday's real device sat at index 0, where a broken translation would have looked correct. **The value is still unknown**: `0x1EB8E` is which adapter the *simulator* chose, not what VirtualDesktopXR requires. Mechanism done, measurement outstanding, and still one headset-on command away. |
| Does culling follow the asymmetry? | Needs a write, which has not happened |
| The `RT_EndFrame` rate discrepancy | Measured 144/s and ~34/s in two windows; needs a controlled re-measure |
| Does anything downstream perturb the projection (TAA jitter)? | Needs a pass census |
| Observer disable restoring the prologue | Never observed; see F-009 |
