# What CryEngine's own source says, and how it redirects rung 1

Source trees at `D:\Dev Debug\source code\`. Four are CryEngine across three
generations; **`CryGame` is the CE3 Free SDK and the closest to Prey**, which is a
CE3/4-era Arkane fork. `CRYENGINE` and `CRYENGINE_Source` are 5.x and two
generations away.

**None of this is an offset.** It is semantics, sign conventions and which passes
consume what -- the class of question source answers well and a decompiler answers
badly. The rule that governs every use of it: **the version gap moves symbols, not
concepts. Search for the concept.** A missing enum is not a missing feature --
`EFQ_DrawNearFov` is gone from 5.x while its mechanism survives as `m_drawNearFov`
and `DRAW_NEAREST_MIN` in `D3DRendPipeline.cpp`.

---

## The redirection: Crytek traverses once and submits twice

`RenderDll/XRenderD3D9/D3DStereo.cpp`, `CD3DStereoRenderer::RenderScene`:

```cpp
const std::array<CCamera, 2> cameras = {{
    PrepareCamera(CCamera::eEye_Left,  camera),
    PrepareCamera(CCamera::eEye_Right, camera)
}};
pRenderView->SetCameras(cameras.data(), 2);
pRenderView->SetPreviousFrameCameras(m_previousCameras.data(), 2);
...
    auto eyeRenderScope = PrepareRenderingToEye(CCamera::eEye_Left);
    pRenderView->SetCurrentEye(CCamera::eEye_Left);
    gcpRendD3D->RT_RenderScene(pRenderView);
...
    auto eyeRenderScope = PrepareRenderingToEye(CCamera::eEye_Right);
    pRenderView->SetCurrentEye(CCamera::eEye_Right);
    gcpRendD3D->RT_RenderScene(pRenderView);
```

**There is ONE `CRenderView`, it carries BOTH cameras, and it is *submitted* twice.**
The world is traversed once. There is even a non-sequential path that renders both
eyes in a **single** `RT_RenderScene` call with `eEye_Both`.

**This is why every rung-1 attempt leaked.** The split is:

| stage | what it does | allocates per frame? |
| --- | --- | --- |
| `C3DEngine::RenderWorld` (R-054) | traversal and culling; fills the render view | **yes** -- `FUN_1802114D0`, the prepare |
| `CD3D9Renderer::RT_RenderScene(view)` | consumes the view and submits draws | no |

A3, A4, A6 and A7 all doubled the **traversal**. Crytek doubles the **submission**.
F-016's leak was not a bug to work around; it was the engine saying we were at the
wrong seam. The flag experiments were an attempt to make a second traversal cheap,
when the answer is not to traverse twice at all.

`PrepareRenderingToEye` returning a scope object (`auto eyeRenderScope = ...`) is
the per-eye setup/teardown as RAII -- the borrow/restore contract the playbook
describes, expressed in the engine's own code.

## What this means for Prey specifically

Prey is CE3/4, and the 5.x eye API does not exist here -- the version gap in
action:

| 5.x | Prey (measured) |
| --- | --- |
| `SetCameras(cameras, 2)` | `CRenderView::SetCamera(const CCamera&)` -- **one** camera (R-030, `0xEE7E80`, vtable slot `+0x40`) |
| `SetCurrentEye(eye)` | no equivalent found |
| `SetPreviousFrameCameras(..., 2)` | `SetPreviousFrameCamera` -- **one** (R-064, slot 9, `+0x48`) |

But the **concept** transfers, and we already hold every piece needed to express it:

- `CRenderView::m_camera` is a full `CCamera` **by value** at `+0x11A0` (R-049) --
  the view copies rather than references, so writing a second camera between two
  submissions is a bounded, restorable write.
- The derived block the renderer actually consumes is `CRenderCamera` at `+0x1620`
  (R-050), rebuilt by `SetCamera`. So swapping eyes means calling `SetCamera`, not
  patching a matrix.
- `SetPreviousFrameCamera` (R-064) is the per-eye temporal history hook, which is
  the TAA hazard the playbook flags.

**The shape for Prey therefore is:** let `RenderWorld` run **once**, then submit the
resulting view **twice**, calling `SetCamera` between with the second eye. Prepare
runs once, so there is no leak by construction.

**Our own research log flagged the seam on 2026-08-15 and it was never followed:**

> `CD3D9Renderer::RT_RenderScene(CRenderView*, int, SThreadInfo&, void(*)())` is
> the obvious next thread.

That is now the single highest-value target in the binary. It has no name string
and both `RT_BeginFrame` (`0xF7D710`) and `RT_EndFrame` (`0xF7E210`) are virtually
dispatched with no direct callers, so it needs vtable analysis rather than xrefs --
which is exactly what that log entry said, and why it was left.

---

## Three findings that close open items elsewhere

### 1. The asymmetry formula, from the engine rather than from ourselves

`DriverD3D.cpp:6051` computes the projection using `wL/wR/wB/wT` -- the same four
fields PreyVR reverse-engineered as `fWL/fWR/fWB/fWT`. They are **frustum-edge
offsets in near-plane units, not angles**, added to the computed edges.

This matters because we flagged our own asymmetry test as self-referential: it
recomputed with the same formula on the same inputs. The engine source is an
**independent** confirmation that a tangent-space inversion is the right shape.
See `SUBMISSION_CONTRACT.md`, which already insists on tangents.

### 2. The viewmodel answer: `fNearRatio`

The near/viewmodel pass **re-applies the asymmetry, rescaled**. Inject asymmetry
into the world camera without accounting for that ratio and the viewmodel's
asymmetry is wrong relative to the world by exactly that factor.

That is R-069 answered from the engine side. We knew the viewmodel renders at
`r_DrawNearFoV` 54 against the world's 88 and does not follow the per-eye camera;
this names the relationship rather than leaving it as an observation.

### 3. "Not used for culling" does not mean unused

Crytek's own comment on the asymmetry fields is first-party confirmation of the
hazard this fleet derived independently -- widening the projection does not widen
what is culled. But `ShadowUtils.cpp` derives a `vStereoShift` from `GetAsymL()`
and `GetAsymB()`, so **shadows consume the asymmetry while culling ignores it**.

That asymmetric behaviour is the shape behind edge-void reports, and it means a
per-eye asymmetric frustum will move shadows correctly and cull incorrectly. Worth
predicting before a headset session rather than diagnosing during one.

## Choosing a tree

For Prey, prefer **`CryGame`** (CE3) over the 5.x trees for anything structural.
Use 5.x for concepts and for subsystems CE3 lacked -- `D3DStereo.cpp` itself only
exists in 5.x, and its value here is architectural rather than literal.
