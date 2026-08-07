# Prey multi-view probe — H-001 remaining half

## Scope

Decide whether Prey's renderer retains a view structure carrying more than one view info, per
[`../../docs/LIVE_MULTIVIEW_PROBE_PROTOCOL.md`](../../docs/LIVE_MULTIVIEW_PROBE_PROTOCOL.md).
Read-only ReGenny session against a live loaded save. No writes, no breakpoints, no code changes.

## Identity

- Host: `Prey.exe` PID `705808`, x64, loaded save, player holding a wrench
- Engine target: `PreyDll.dll` SHA-256 `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`
- Mapped base this session: `0x00007FFE31730000`, size `48,353,280` — matches the runtime baseline
- Renderer singleton: `0x00007FFE34254E80`, i.e. `PreyDll.dll+0x2B24E80`, module-resident as R-005 records

## Gate

All protocol step-3 conditions held:

| Check | Result |
| --- | --- |
| `swapchain +0xAE88` | `0x290B0E29C10`, vtable in `dxgi.dll` |
| `device +0xAF28` | `0x290B08B66D0`, vtable in `d3d11.dll` |
| `scene_nesting +0xAEF8` | `1` |
| `frame_slot +0x499C` | `0`/`1`, alternating |

The Present fields carried over from static analysis also matched the earlier Cheat Engine capture
exactly: `present_vsync_selector +0xB1EC = 1` and `present_flags +0xB1F0 = 0` against that capture's
`SyncInterval=1, flags=0`. This independently confirms the R-003 Present argument map.

Liveness was confirmed throughout: the frame counter at `+0xAC28` advanced from `18,363` to
`519,011` across the session.

## The command-ring hypothesis is refuted

The protocol nominated the 2-entry ring at `+0xAC30` as the leading `CRenderView` candidate. It is
not one.

Both entries share a single vtable at `0x00007FFEF5A93380` inside `d3d11.dll`, and that vtable has
**11 contiguous `d3d11.dll` entries**. `ID3D11DeviceContext` has roughly 145; `ID3D11Query` has
roughly 9 to 11. Neither entry returned an RTTI name, which is expected for a system COM object.

These are two `ID3D11Query`-shaped objects alternated by frame parity — the standard
double-buffered GPU-timing pattern, where frame N records while frame N-1's result is read. They
carry no view data.

This kills the hypothesis cleanly. The protocol anticipated it, so it is a recorded null.

## The real view block

Scanning the per-frame region for orthonormal 3x3 rotations followed by a plausible world position
located the actual camera. Base `renderer+0x4A08`, stride `0x328`, two slots indexed by
`renderer+0x499C`.

Confirmed layout, offsets relative to a slot base:

| Offset | Field | Observed |
| --- | --- | --- |
| `+0x040` | aspect ratio | `1.77778` |
| `+0x230` | position copy | `(792.0701, 1568.1400, 17.1000)` |
| `+0x240` | rotation row 0 | `(-0.989097, 0.147266, -0.000361)` |
| `+0x24C` | rotation row 1 (up, ~world Z) | `(-0.008420, -0.054101, 0.998500)` |
| `+0x258` | rotation row 2 | `(0.147026, 0.987616, 0.054751)` |
| `+0x264` | position | `(792.0701, 1568.1400, 17.1000)` |
| `+0x270` | frustum L, R, B, T | `-0.17321, 0.17321, -0.09743, 0.09743` |
| `+0x280` | near / far | `0.1000 / 8000.0` |
| `+0x2E8` | 4x4 restatement of the same rotation, no translation | — |

The rotation was verified orthonormal: all three row norms within `1e-3` of 1 and all three
pairwise dot products within `1e-3` of 0. Row 1 being the up axis matches CryEngine's forward=Y,
up=Z camera convention.

The frustum is internally consistent: `R/T = 1.77778` exactly equals the block's own aspect field,
and it is symmetric (`L = -R`, `B = -T`). Treating the values as near-plane extents gives a 120.000
degree horizontal and 88.507 degree vertical field of view, which is consistent with a maximum
in-game FOV setting. The setting itself was not read back, so the FOV figure is a derivation, not a
confirmed configuration value.

## Liveness proof

Sampled while the player walked:

```text
  #  frames slot        pos.x        pos.y        pos.z     rot[0]
  1  515160   0     793.4382    1573.6426     17.1005   0.947947
  2  515177   1     793.4530    1573.6479     17.1000   0.947947
  3  515194   0     793.4531    1573.6481     17.1000   0.947947
  4  515216   0     793.4531    1573.6481     17.1000   0.947948
  5  515236   0     793.2983    1573.5841     17.1086   0.947948
  6  515260   0     792.8151    1573.3898     17.0986   0.947948
```

Position moved `0.67244` world units over 100 frames. The Z column varies around `17.10` by roughly
`0.01` — head bob, which only the live player view would show. Across the wider session the
rotation also tracked: `rot[0]` was `0.999929` before the player turned and `0.947947` after.

Protocol step 7 therefore passes: this block is the live render view, not a stale copy.

## Decision: exactly one view

The orthonormal-camera census over all `0x328` bytes of each slot returned **1 match in slot 0 and
1 match in slot 1**, both at `+0x240`, in both the stationary and moving states.

The two slots are frame double-buffers, not eyes. The discriminator is decisive: while the player
was stationary the two slots were **byte-identical**, separation `0.00000`. Left and right eye views
must differ by a constant lateral IPD offset at all times, including when stationary. Identical
slots cannot be an eye pair.

**H-001's multi-view half resolves negative for the probed resident structure.** Combined with the
confirmed stereo negative, this justifies committing to a mod-owned per-eye route.

**Scope correction (2026-08-07).** This capture originally concluded "H-001 closes fully negative".
That overstated what the session showed. The observations above are unchanged and correct: the
resident R-026 frame slots carry one camera each, and identical slots cannot be an eye pair. But a
census of resident per-frame state cannot prove the absence of an internally constructed or
dynamically created multi-view route. The narrower claim — no exposed stock stereo/HMD control
surface, and no resident eye pair — is what the evidence supports, and is what
[`../../docs/HYPOTHESES.md`](../../docs/HYPOTHESES.md) and
[`../../docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md) now record.

The `e_ArkLookingGlass` second-scene finding, recorded later in
[`../../docs/VR_MECHANICS_RISK.md`](../../docs/VR_MECHANICS_RISK.md), is a concrete candidate for
exactly the kind of route this correction leaves open. It is tracked as H-007.

## Unplanned positive finding

The frustum is stored as **four independent edge values** rather than a single FOV scalar. OpenXR
requires per-eye asymmetric projection, where `L != -R`. Because this block already expresses each
edge separately, an asymmetric per-eye frustum is directly representable here without restructuring
the camera. The adjacent near/far pair is likewise the field VR needs for near-plane control.

Together with the R-009 `ArkPlayerCamera::UpdateView` callback seam, this gives the project two
distinct candidate pose-injection points: the gameplay camera and the renderer's own view block.
Neither has been written to, and this session did not establish which is authoritative.

## Safety

Read-only throughout. No memory written, no breakpoints set, no code modified, and no field in
`PreyRendererFrameProbe` was used as a write target. Prey remained running and responsive; the
session ended with the process untouched.
