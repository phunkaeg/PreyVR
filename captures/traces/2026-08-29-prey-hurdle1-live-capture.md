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

---

# Exhaustive harvest — second half of the session

Everything below was captured live in the same session, after Hurdle 1 passed, at the user's
instruction to gather everything needed now or later. Prey PID `50832`, `PreyDll.dll` base
`0x7FFD13B50000`, all reads through Frida on a running host. No writes to game memory.

## The rate discrepancy: resolved, and the fault was in the measurement

Measuring all four counters **in one window** instead of separate ones:

| counter | count / 3.001 s | per second |
| --- | ---: | ---: |
| `RT_EndFrame` (via the observer export) | 432 | 144 |
| `IDXGISwapChain::Present` | 432 | 144 |
| `CRenderView::SetCamera` | 432 | 144 |
| `C3DEngine::RenderWorld` | 432 | 144 |

**Exactly 1:1 — all four run once per frame.** The earlier "33/s" figures came from sampling
*different windows* while the framerate moved. Comparing rates across separate windows is invalid
when the underlying rate is not stationary; the fix is to measure them simultaneously. `sys_MaxFPS
= 144` and `r_VSync = 1` account for the cap. This supersedes the "Unresolved: the rate
discrepancy" paragraph above — the structural findings there were unaffected, but the numbers in
that table are wrong and the ones here are right.

## The frustum is stable — no jitter reaches `CRenderCamera`

16 consecutive `SetCamera` calls sampled at `onLeave`, reading the resulting `CRenderCamera`:

- **1 distinct frustum across all 16** — `-0.173205093, 0.173205093, -0.097427860, 0.097427860`
- **residual `9.519862e-9`, min == max** — identical every frame under the corrected formula
- alternated 8/8 between the two Default views

So the **live rounding floor is 9.52e-9**, and `kRenderCameraResidualLimit = 1e-4` sits ~10,000x
above it. Any TAA jitter is applied downstream of `CRenderCamera`, not in the frustum.
`r_AntialiasingMode = 3` and `r_MotionBlur = 2` confirm temporal techniques *are* active, so where
the jitter is applied stays open — it is just not here.

## `CRenderView` header fields — found by diffing a Default view against a Recursive one

| offset | meaning | evidence |
| --- | --- | --- |
| `+0x10` | `EUsageMode` | tracks `SwitchUsageMode` exactly: 0 Undefined, 1 Reading, 2 ReadingDone, 3 Writing, 4 WritingDone |
| `+0x14` | **`EViewType`** | 0 Default, 1 Recursive, 2 Shadow — verified against a 16-view census |
| `+0x20` | pass rendering flags | `0x2E5DF` on Default (matches `CreateGeneralPassRenderingInfo`), `0xAE5DE` on Shadow |
| `+0x24` | `nRenderFlags` | `0xf` on Default (matches `RenderWorld`'s argument), `0x0` on Shadow |
| `+0x11A0` | `m_camera` (`CCamera`) | R-049, confirmed |
| `+0x1620` | `CRenderCamera` | R-050, confirmed |

## The render-view census — the `[2][2]` pool is not the whole set

**16 distinct `CRenderView` objects** are scheduled per frame, all carrying the R-053 vtable:

| `EViewType` | count | resolution | in the R-033 pool? |
| --- | ---: | --- | --- |
| Default (0) | 2 | 2560x1440, real frustum, flags `0x2E5DF`/`0xf` | yes — `[0][0]` and `[1][0]` |
| Shadow (2) | 14 | 640x480, untouched defaults, flags `0xAE5DE`/`0x0` | **no** |
| Recursive (1) | 2 | never scheduled at all | yes — `[0][1]` and `[1][1]` |

**R-033's pool holds only 4 of the 16 live views.** The 14 shadow views live outside it. This is a
correction to the implicit assumption that the pool was the complete set, and it matters for H-009:
a second world pass has to account for shadow views the pool does not enumerate.

The **recursive views are allocated, correctly typed, and completely idle** — `usage = Undefined`,
never passed to `SwitchUsageMode`, never given a camera. They are genuinely free.

## The per-frame lifecycle

`SwitchUsageMode` (slot 10) over 1 s: 72 of each mode, so every view is used every other frame,
matching a 144 Hz double buffer.

```
game thread 44208 :  Writing -> WritingDone      <- SetCamera happens inside this window
render thread 54600: Reading -> ReadingDone
```

`SetCamera` is **always immediately followed by `SetPreviousFrameCamera`** (slot 9) on the same
view, with a *different* camera. With `r_MotionBlur = 2` and `r_AntialiasingMode = 3` both active,
that previous-frame camera is load-bearing for motion vectors and temporal reprojection — **a
per-eye write must handle both, or the second eye carries the wrong reprojection history.** This is
a new constraint on Hurdle 3 that static analysis had not surfaced.

## Call sites — disassembled

`SetCamera` caller, at the return address `PreyDll+0x2110E5` (the call is the instruction before):

```
mov  rbx, [r14+0x20]      ; the render view
test rbx, rbx / je skip
mov  rax, [rbx]           ; vtable
lea  rdx, [rsp+0x60]      ; the camera
mov  rcx, rbx
call [rax+0x40]           ; SetCamera  -- slot 8, confirming R-053's header-derived index
>>> return lands here
lea  rdx, [rbp+0x1a0]     ; a DIFFERENT camera
call [rax+0x48]           ; SetPreviousFrameCamera -- slot 9
```

`RenderWorld` caller, `PreyDll+0xE0BC62`:

```
lea  r9,  [rip+0xf99fc5]  ; szDebugName = "CSystem::Render"
mov  r8,  rax             ; SRenderingPassInfo*
mov  edx, 0xf             ; nRenderFlags
mov  rcx, [rcx+8]         ; C3DEngine*
call [rbx+0x18]           ; RenderWorld -- IProcess slot 3, exactly as predicted
>>> return lands here
```

Both seams have **exactly one caller each**, which makes the cross-engine playbook's deny-by-default
return-RVA gate trivially implementable. The `mov rbx, [r14+0x20]` in the first is the same `+0x20`
field identified in `SRenderingPassInfo` below — the caller is pulling the view out of the pass.

## `SRenderingPassInfo` layout, from `RenderWorld`'s third argument

| offset | observed | meaning |
| --- | --- | --- |
| `+0x00` | `0` then `1` | thread/slot id, alternating |
| `+0x04` | `0x2E5DF` | rendering flags — the constant `CreateGeneralPassRenderingInfo` is called with |
| `+0x08` | `1.0f` | scale/zoom |
| `+0x0C` | `188783`, `188784` | frame id, incrementing by 1 |
| `+0x10` | `188782`, `188783` | previous frame id |
| `+0x20` | `0x1AF1FBB4EF0` / `0x1AF1FAFA8A0` | **`CRenderView*`** — exactly the two Default views |
| `+0x48` | `0xFFFFFFFFFFFFFFFF` | sentinel |

`+0x20` is the link between a pass and the view it renders into — precisely what a second per-eye
pass would need to populate.

## R-009 `ArkPlayerCamera::UpdateView` — live

- 41 calls in 0.3 s (~137/s, once per frame), thread **44208**, **single caller `PreyDll+0x3D94B0`**
- `customViewFn` at camera `+0x148` is **`0x0`** throughout — the override slot is unoccupied
- camera mode at `+0x1A4` is `0`
- `SViewParams` on **entry**: world position `325.83, 740.60, 482.79`, quaternion
  `-0.0527, -0.0581, 0.7380, 0.6702`, fov `1.5447413` at both `+0x30` and `+0x34`
- on **exit**: a *local* eye pose `-0.031, -0.003, 1.6999` (1.7 m eye height), same quaternion, same
  fov. So `UpdateView` converts world -> local eye offset, which is the shape a VR eye offset would
  compose with.

## Live cvar values

Read through `gEnv->pConsole` (`0x1AF1DA6EC50`), `GetCVar` at vtable `+0xB8`, `GetIVal` at `+0x10`.

| cvar | value | note |
| --- | ---: | --- |
| `r_overrideDXGIAdapter` | `-1` | R-052's default: auto-scan. **The cvar exists and is readable** |
| `g_detachCamera` | `0` | **R-056's cvar is registered and readable.** Whether it *does* what we think is still untested |
| `e_ArkLookingGlass` | `1` | H-007's cvar, currently enabled |
| `sys_MaxFPS` | `144` | explains the frame cap |
| `r_VSync` | `1` | matches `SyncInterval = 1` |
| `r_Width` / `r_Height` | `2560` / `1440` | matches the swapchain |
| `r_Fullscreen` | `0` | matches `Windowed = 1` |
| `r_MultiThreaded` | `1` | confirms the MT/RT split observed |
| `r_AntialiasingMode` | `3` | temporal AA active |
| `r_MotionBlur` | `2` | active — makes `SetPreviousFrameCamera` load-bearing |
| `r_DeferredShadingTiled` | `3` | deferred path confirmed |
| `e_ShadowsMaxTexRes` | `1024` | consistent with the 640x480 shadow views |
| `cl_fov` | `88` | matches fov `1.5447413` rad = 88.5 deg |
| `r_DrawNearFoV` | `54` | **the viewmodel is drawn with a separate FOV** — relevant to H-005 |
| `e_ViewDistRatio` | `100` | |

## Complete `CRenderView` vtable, live-resolved

All 24 slots matched the header-derived ordering exactly. Selected:

| slot | offset | name | address |
| ---: | --- | --- | --- |
| 8 | `+0x40` | `SetCamera` | `PreyDll+0xEE7E80` (R-030) |
| 9 | `+0x48` | `SetPreviousFrameCamera` | `PreyDll+0xEE82D0` |
| 10 | `+0x50` | `SwitchUsageMode` | `PreyDll+0xEE82F0` |
| 11 | `+0x58` | `GetWriteMutex` | `PreyDll+0xEE6350` |
| 23 | `+0xB8` | `EnableLookingGlass` | `PreyDll+0xEE4B00` (R-035) |

`C3DEngine`'s `IProcess` head also matched: slot 3 `RenderWorld` = `PreyDll+0x21F520`.

## Thread roles

| tid | role | pc while sampled |
| ---: | --- | --- |
| 44208 | game thread — `SetCamera`, `SetPreviousFrameCamera`, `RenderWorld`, `UpdateView`, view Writing | `win32u.dll+0x12e4` |
| 54600 | render thread — `Present`, view Reading | `PreyDll+0x102A3E2` |

This is exactly the shape XR-005 prescribes: cache on the game thread, submit from the render
thread at `RT_EndFrame`, where the observer already sits.

## Ghidra cross-check of the live call sites

The live census gave return addresses; Ghidra names the functions they sit in. Image base
`0x180000000`, `executable_path` = the same installed
`D:/SteamLibrary/steamapps/common/Prey/Binaries/Danielle/x64/Release/PreyDll.dll` the landmark gate
checks, so the RVA arithmetic and the target both line up.

| live return address | containing function | identified as |
| --- | --- | --- |
| `+0x2110E5` | `FUN_180210e10` (`+0x210E10`) | `C3DEngine::UpdateRenderingCamera` (R-057) |
| `+0xE0BC62` | `FUN_180e0ba30` (`+0xE0BA30`) | `CSystem::Render` (R-058) |
| `+0x3D94B0` | `FUN_1803d91c0` (`+0x3D91C0`) | sole caller of `ArkPlayerCamera::UpdateView` (R-065) |

`get_function_callers` on `FUN_180210e10` returns exactly `FUN_18021f520` — which is `RenderWorld`
(R-054, RVA `0x21F520`). So the whole chain is now named end to end:

```
CSystem::Render (R-058)
  -> C3DEngine::RenderWorld (R-054)          [via CSystem::m_pProcess at +0xAB0, vtable slot 3]
    -> C3DEngine::UpdateRenderingCamera (R-057)
      -> CRenderView::SetCamera (R-030)      [via SRenderingPassInfo+0x20]
      -> CRenderView::SetPreviousFrameCamera (R-064)
```

### `CSystem::Render` re-confirms four offsets from an independent site

Static analysis had established these from other evidence; this function uses all four together:

| in the decompilation | offset | our entry |
| --- | --- | --- |
| `param_1 + 0xf1` | `CSystem+0x788` | `m_ViewCamera` (R-040) |
| `param_1[5]` | `CSystem+0x28` | `gEnv` (R-039) |
| `*(longlong**)(param_1[5] + 8)` | `gEnv+0x08` | `p3DEngine` (R-044) |
| `DAT_18224daa0` | `gEnv+0x120` | `pRenderer` (R-005/R-044) |

and then builds the pass with `FUN_1801e5b30(local_58, &m_ViewCamera, 0x2e5df, 0)` —
`CreateGeneralPassRenderingInfo`. **`m_ViewCamera` is the single source feeding both the 3DEngine
camera update and the pass info**, which is the structural fact H-008 needed.

### The freeze path — an override the engine already honours

`UpdateRenderingCamera` has two camera paths:

```c
if ((cvars->e_CoverageBufferDebugFreeze == 0) && (cvars->e_CameraFreeze == 0)) {
    save C3DEngine+0x610 as prev;  store the pass camera into C3DEngine+0x610;
    view = passInfo->m_pRenderView;              // +0x20
    view->SetCamera(passCamera);                 // slot 8
    view->SetPreviousFrameCamera(prev);          // slot 9
} else {
    cam = gEnv->pSystem->GetViewCamera();        // pSystem vtable +0x388
    view->SetCamera(cam);                        // C3DEngine+0x610 is NOT written
    view->SetPreviousFrameCamera(&staticFrozenCamera);
}
```

The cvar binding was resolved live rather than guessed. Each name was looked up through
`pConsole->GetCVar`, and the backing pointer stored at `ICVar+0x48` was compared against the
predicted field address:

| CVars offset | cvar | live value |
| --- | --- | ---: |
| `+0x1CC` | `e_CameraFreeze` | `0` |
| `+0xD4` | `e_CoverageBufferDebugFreeze` | `0` |
| `+0x230` | `e_CameraRotationSpeed` (float) | `0` |
| `+0x3E0` | `e_StreamCgfDebug` | `0` |

`e_CameraGoto` and `e_Recursion` were run as **negative controls** and correctly matched nothing,
which is what makes the four positives meaningful rather than coincidental.

What this buys us: it is a shipped, engine-honoured path on which `CSystem::m_ViewCamera` reaches
`CRenderView::SetCamera` directly. What it does not buy us: it is **not a clean seam** — skipping the
write to `C3DEngine+0x610` freezes the culling camera, and that is a side effect rather than an
option. Recorded as evidence about the engine's own plumbing, not as a recommended mechanism.

`UpdateRenderingCamera` also applies a Z-rotation to the camera when `e_CameraRotationSpeed != 0`,
before either branch — so the engine already accepts that the camera it renders with is not
necessarily the camera it was handed.
