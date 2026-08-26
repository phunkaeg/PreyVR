# Test plan

Each completed row requires a log or capture manifest in `captures/` and a link from `RESEARCH_LOG.md`.

| Stage | Proof | Required evidence | Status |
| --- | --- | --- | --- |
| H0 | Clean configure/build/test runs without launching Prey or requiring a headset. | CTest output covering pure math, engine-map, captured gameplay-query fixtures, observer/OpenXR planning and file inspection, fail-closed DLL loading/log parsing, optional target doctor, and DXGI probe. | Passed: fresh 11/11 Release loop on 2026-08-07. |
| B0 | Process and module identity are logged; lifecycle is safe. | Bounded startup log with exact DLL hash/build/signature result and process-exit pin state. | Historical `0.2.0` proof passed 21/21 and unloaded cleanly. Current `0.3.0` passes headless/on-disk checks but awaits supported-host validation; it deliberately pins until process exit. |
| G0 | D3D/DXGI path is observed without mutation. | Present/draw counters plus device/swapchain metadata. | Partial: the default-off EndScene observer counted 301 callbacks on one render thread and detached cleanly; swapchain/device are resolved. Immediate-context and buffer metadata remain. |
| X0 | OpenXR instance/session binds to the real game device. | Runtime/system/formats log; desktop path remains intact. | Planned; runtime manifest and pinned x64 loader preflight pass, but no OpenXR call has occurred. |
| X1 | Each OpenXR eye receives a distinct solid-color smoke image. | Synchronized left/right eye dump or headset observation. | Planned |
| X2 | Live desktop image is bridged to both eyes. | Final-eye dumps with freshness counters. | Planned |
| S0 | A known world subset has distinct per-eye geometry. | Paired eye dumps plus IPD/residual proof. | Planned |
| S1 | Engine view/culling follows position and rotation. | Translation/peek test with no missing peripheral geometry. | Planned |
| A0a | The engine aim ray can move independently of the head/world camera. | Reversible synthetic reticle test: fixed camera, changed cached world ray, full byte restoration. | Passed 2026-07-31; see the detached-aim capture. |
| A0b | A native firearm, melee, or interaction endpoint follows the detached ray. | Two one-shot wrench queries using the stack-local ray copy: unchanged baseline, then bounded 10-degree yaw; record both result vectors. | Passed 2026-08-01: same wall collider and depth, with a `0.127165`-unit lateral contact displacement following the written ray. See [`../captures/traces/2026-08-01-prey-wrench-a0b.md`](../captures/traces/2026-08-01-prey-wrench-a0b.md). |
| V0 | The rendered arms/weapon can move independently of camera and gameplay aim. | Reversible visual-only pose offset with an unchanged shot endpoint. | Planned |
| I0 | Controller pose drives native gameplay aim and actions. | Controller/aim trace plus hitscan, projectile, melee, and use-ray validation. | Partial/reproduced: native melee and use targeting are live-steerable from transient detached rays; keypad `0xFDE0` changed to usable entity `0x1117` in the no-button interaction capture. Projectile proof and controller-pose injection remain. |
| V1 | Controller pose drives the visual weapon and remains aligned with gameplay aim. | Muzzle/controller residuals across representative weapons and animations. | Planned |

## Camera restore validation: compare in pose space, not raw coefficients

Not yet needed — nothing writes a camera — but recorded now so the mistake is not made later.

Contributed by the cross-engine playbook from FC2VR's experience. Their original camera-restore guard
thresholded all 16 raw view-matrix coefficients at `0.010`. Because the translation terms are
rotation multiplied by world position, that produced **yaw-dependent false rejects that got worse the
further the player was from the world origin** — a guard that passes in the test level and fails in
the real one.

**Rule.** Decompose before comparing, and threshold rotation and position separately:
`eRot >= 0.0025 || ePos >= 0.010`.

Two Prey-specific notes on top of that:

- For an exact save/restore, comparison is not needed at all. `CCamera::operator=` is a plain
  memberwise copy (R-042), so a restored camera is **bit-identical** to the saved one and can be
  checked with `memcmp` over `sizeof(CCamera) == 0x240`. Pose-space thresholds are for validating a
  camera the *engine* rebuilt, not one we copied.
- FC2VR's failure mode — a transient eye camera overwriting a stored primary — is **structurally
  impossible** on the seam this project prefers: `CRenderView::SetCamera` copies by value into the
  render view's own storage (R-049) and never writes `CSystem::m_ViewCamera`.

## A validation threshold is a property of the metric, not of the problem

Adopted 2026-08-27 from the cross-engine playbook, and applied immediately to code in this repo.

**The rule.** Build the wrong-input table *first*. Confirm the metric actually separates correct from
wrong — if a wrong input scores like a correct one, the metric is blind and no threshold saves it.
Only then derive the limit from the measured gap. **Never import a threshold from another project.**

The playbook records what happens otherwise. FarCry2-VR imported BioshockVR's residual limit of `1.0`
and got *a gate looser than no gate at all*, because the two metrics were scaled differently and every
wrong case still scored under it. Worse, the obvious projection check — that `WVP · P_center⁻¹` comes
out affine — is **mathematically incapable of detecting a wrong FoV**: a deliberately guessed 75°
scored `0.000031`, identical to correct, because FoV lives in the scale terms and the affine test
reads the projective row.

**Applied to `RenderCameraMatchesSource`.** That function shipped on 2026-08-23 with
`tolerance = 0.001f` — a number chosen by eye, never derived. It now exposes
`RenderCameraResidual()` and takes its limit from a measured separation table:

| case | residual | vs limit |
| --- | ---: | ---: |
| correct (synthetic) | `0.000000000` | bit-exact |
| tiny `0.004` asymmetry error | `0.004000008` | 40x above |
| asymmetry symmetrised away | `0.059999943` | 600x |
| 5% FoV error | `0.108985543` | 1090x |
| wrong aspect (4:3) | `0.372933149` | 3729x |
| wrong near plane | `0.750000000` | 7500x |

Limit: **`1e-4`**. The tests assert every wrong case fails *and* that the gap is real — correct an
order of magnitude below, tightest wrong an order of magnitude above — so raising the limit later
breaks a test instead of quietly weakening the gate.

**One caveat that matters for the live capture.** The correct case scores exactly zero only because
the test recomputes with the same formula on the same inputs. A live `CRenderCamera` is derived by the
engine's own float ops, so expect a small non-zero residual from rounding alone. If a live capture
ever lands *between* the rounding floor and `0.004`, that is an unexplained result to investigate —
not a reason to widen the limit.
