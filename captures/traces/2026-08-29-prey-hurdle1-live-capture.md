# Prey supported-host load and live capture — Hurdle 1

First load of the lifecycle-hardened `0.3.0` artifact into a running Prey, plus Captures A and B.
**Hurdle 1 passed on every acceptance criterion**, and the capture caught a static-analysis error of
ours that had been shipped in both docs and code.

## Identity

- Host: `Prey.exe` PID `50832`, foreground, x64
- Engine: `PreyDll.dll` base `0x7FFD13B50000`, size `0x2E20000`
- Artifact: `PreyVR.dll` SHA-256 `A37D4C004808322F24B3C660DA42CA7C679FD34906F407CF8FB765567989D8C1`,
  loaded at `0x7FFDB8840000`
- Injection: **Frida** `LoadLibraryW`, `lastError=0`. See the failure note on x64dbg below.
- Log: `C:\Users\meise\OneDrive\Documents\PreyVR\PreyVR.log` — Documents is OneDrive-redirected, so
  the documented `Documents\PreyVR\` path is wrong on this machine. Set `PREYVR_LOG_PATH` next time.

## Hurdle 1 acceptance — all criteria met

```
preyvr_smoke_result status=verified landmarks=30 hooks=compiled_default_off observer=ready
                    module=pinned openxr=preflight_only openxrStatus=ready
preyvr_snapshot result=complete
preyvr_snapshot identity system_vtable=yes engine_vtable=yes render_world_slot=yes renderer_agrees=yes
30 landmark lines, 0 mismatches
```

Pre-injection cross-check: R-003's prologue was read at `base+0xF7E210` **before** loading anything
and matched `40 57 48 83 EC 60 83 B9 F8 AE 00 00 00 48 8B F9` byte-for-byte.

## Registry entries promoted from `static-only`

| Entry | Predicted | Observed |
| --- | --- | --- |
| R-044 `gEnv` | `+0x224D980` | `0x7FFD15D9D980` |
| R-039 `gEnv->pSystem` `+0xE0` | resolves | `0x1AF1D8A5380` |
| R-043 `CSystem` vtable `+0x1D9B9C8` | matches | `system_vtable=yes` |
| R-054 `RenderWorld` at `IProcess` slot 3 (`+0x18`) | `0x21F520` | `render_world_slot=yes` |
| R-005 renderer singleton `+0x2B3E8E0` | == `gEnv->pRenderer` | both `0x7FFD16674E80` |
| R-040/R-048 `m_ViewCamera` at `CSystem+0x788` | decodes as a camera | plausible, sane values |
| R-053 `CRenderView` vtable `+0x1DCAE00` | matches all pooled views | `0x7FFD1591AE00` |
| R-006/R-007 swapchain/device + mirrors | mirrors hold same pointer | confirmed |
| R-012 cached reticle ray | unit direction vector | length `0.99999997` |

Live view camera: pos `331.0835, 756.6542, 481.7031`, fov `1.5447413` rad (88.5 deg), `2560x1440`,
ratio `1.7777778`, near `0.1`, far `8000`. **Asymmetry baseline `0,0,0,0`** — so Hurdle 3 may
overwrite rather than compose, which was an open question.

## The correction: `fW*` are near-plane coordinates, not tangents

`RenderCameraResidual` returned **1.5588** against a limit of `1e-4`. Logging it as a number rather
than a verdict is what made the cause findable — a bool would only have said "no".

```
tan(fov/2)        = 0.974278578
actual fWT        = 0.097427860
fWT / tan(fov/2)  = 0.100000002   <- exactly the near plane
```

The engine multiplies by the near plane, which we had dropped:

```
t' = tan(fov/2) * near                     <- the missing factor
fWL = asymL - t'*ratio    fWB = asymB - t'
fWR = t'*ratio + asymR    fWT = t' + asymT
```

Residual under the corrected formula: **1e-9**. This matches `CRenderCamera::Frustum(l, r, b, t,
Ndist, Fdist)`, whose arguments are glFrustum-style near-plane coordinates. The error was a misreading
of our own decompilation — the line `fVar8 = fVar8 * fVar1` (with `fVar1` = near) was present in the
output we quoted and was dropped when the formula was written down.

**This also establishes the live rounding floor at ~1e-8**, the number `LIVE_CAPTURE_PLAN` said the
capture existed to learn. The `1e-4` limit sits four orders of magnitude above it.

## R-033: the pooled render views — acceptance test passed

All four `m_pRenderViews[2][2]` entries non-null, all four vtables `0x7FFD1591AE00` (= R-053), and
`[t][1]` distinct from `[t][0]`.

| slot | pointer | camera |
| --- | --- | --- |
| `[0][0]` | `0x1AF1FBB4EF0` | live: 2560x1440, fov 1.5447, near 0.1, far 8000 |
| `[0][1]` | `0x1AF1FAB5620` | idle default: 640x480, fov 0.9599, fW `-1,1,-1,1`, near 1.4142, far 10 |
| `[1][0]` | `0x1AF1FAFA8A0` | live, identical to `[0][0]` |
| `[1][1]` | `0x1AF1FAFDBC0` | idle default |

The recursive slots are permanently allocated but **never given a real camera** — consistent with
R-032's "permanent pool" finding, and it tells us they are idle rather than in use.

## Threading and call sites — the design-critical result

Sampled with Frida `Interceptor` over 2 s each, hooks detached afterwards.

| Function | Rate | Thread | Caller sites |
| --- | --- | --- | --- |
| `CRenderView::SetCamera` (R-030) | 33/s | **44208** | **exactly one**, `PreyDll+0x2110E5` |
| `C3DEngine::RenderWorld` (R-054) | 33/s | **44208** | **exactly one**, `PreyDll+0xE0BC62` |
| `IDXGISwapChain::Present` | 144/s | **54600** | — |

`RenderWorld`'s `szDebugName` was `"CSystem::Render"` on 66/66 calls, with `nRenderFlags = 0xf`.
`SetCamera` alternates between the two `[t][0]` views, which is the MT/RT double-buffer in motion.

**Both seams have exactly one caller each.** That makes the cross-engine playbook's deny-by-default
return-RVA gate trivially implementable, and it is recorded against H-009 as the recommended gate.

**Two distinct threads** — camera and world-render setup on the game thread, presentation on the
render thread — which is precisely the shape XR-005 prescribes: cache on the game thread, submit from
the render thread at `RT_EndFrame`, where the observer already sits.

**Unresolved: the rate discrepancy.** `RT_EndFrame` measured 144/s in one window (216 callbacks in
1500 ms) and ~34/s in another (17 in 500 ms), while `Present` held steady at 144/s and
`RenderWorld`/`SetCamera` at 33/s. The structural findings above do not depend on the rates, but the
rates themselves are inconsistent between samples and need a controlled re-measurement before any
pacing decision rests on them. Recorded as unexplained rather than reconciled.

## Hurdle 2 — the blocking questions, answered

**Swapchain** (`IDXGISwapChain::GetDesc`): `2560x1440`, format **28 = `DXGI_FORMAT_R8G8B8A8_UNORM`**
(not sRGB), `SampleDesc.Count = 1` (no MSAA), `BufferCount = 2`, `Windowed = 1`,
`SwapEffect = 0` (`DISCARD`, not a flip model), `Flags = 0x2` (`ALLOW_MODE_SWITCH`).
Present called with `SyncInterval = 1` on all 288 samples — vsync on.

**Device**: feature level `0xB000` (`D3D_FEATURE_LEVEL_11_0`), creation flags `0x80`
(`PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY`). Notably **not** `SINGLETHREADED`.

**`ID3D11Multithread` is available but protection is OFF** — `QueryInterface` succeeded and
`GetMultithreadProtected()` returned `0`. Submitting from a thread the engine does not own would be
unsafe without either enabling it or submitting from the render thread. Since `RT_EndFrame` *is* the
render thread, the XR-005 design already avoids the problem; this is a confirmation, not a blocker.

**Adapters — and this overturns an earlier assumption.** The device is on
`NVIDIA GeForce RTX 5070 Ti`, LUID `{0x15533, 0}`, 15995 MB, which is `EnumAdapters1` index 0.
But enumeration returns **five** adapters:

| idx | description | LUID |
| ---: | --- | --- |
| 0 | NVIDIA GeForce RTX 5070 Ti | `0x15533` (the game's) |
| 1 | NVIDIA GeForce RTX 5070 Ti | `0x25DE7` |
| 2 | NVIDIA GeForce RTX 5070 Ti | `0x22F3F` |
| 3 | NVIDIA GeForce RTX 5070 Ti | `0x1EB8E` |
| 4 | Microsoft Basic Render Driver | `0x16AE6` (software) |

Four entries are **indistinguishable by description or VRAM** and differ only by LUID. The earlier
assessment that adapter mismatch was "unlikely on a single-GPU machine" was wrong. Selecting the
right adapter here *requires* LUID comparison, which makes the LUID-to-index translation for
`r_overrideDXGIAdapter` (R-052) necessary rather than precautionary.

Still outstanding: the LUID `xrGetD3D11GraphicsRequirementsKHR` returns. That needs an XR instance
and can be answered out of process, without touching Prey.

## Other live facts

- `ArkPlayer` instance `0x1AFDE0923D0` via R-008.
- **R-009's custom-view callback slot at `ArkPlayerCamera+0x148` is `NULL`** — the override the
  engine already honours is unoccupied and available, confirming it is a free seam rather than one
  already in use.
- OpenXR preflight `status=ready`, runtime = Virtual Desktop Streamer.
- Full `CCamera` 0x240 raw dump archived in the session transcript.

## Failures and tooling notes

**x64dbg `loadlib` freezes Prey.** It hijacks a thread to call `LoadLibraryA` and left that thread
suspended; the game froze and `pause` then failed with *"The active thread is suspended, switch to a
running thread to pause the process"*. The DLL never loaded. Recovery required resuming the thread by
id, and the process was ultimately restarted. **Frida injected cleanly on the first attempt** with no
pause and no hijack. Recorded as F-008.

**Two DLL exports fault under Frida.** `PreyVR_CaptureRenderViews` and
`PreyVR_SetFrameObserverEnabled` both raise `system error` when called through a Frida
`NativeFunction`. The observer's *enable* took effect anyway (status 1 -> 2, frames counted), so the
error is raised around a call that partially or fully completes — consistent with Frida's exception
handler reacting to MinHook's `VirtualProtect`/write into `PreyDll`'s `.text`. `CaptureRenderViews`
produced no log output, so it faulted before logging; its data was gathered directly in Frida JS
instead, which is why the walk above exists.

**The observer's disable path did not complete.** `PreyVR_SetFrameObserverEnabled(0)` raised the same
error twice, status stayed `2`, and the prologue at `+0xF7E210` remained
`E9 BF 2D 07 FF ...` (MinHook's `JMP rel32`) rather than being restored. The hook is our own,
functioning, and the module is pinned until process exit by design, so this is stable rather than
dangerous — but **the documented "disable restores the target prologue" behaviour was not observed**
and remains unproven on a live host.
