# H-011 investigation: the near view loses eye translation

**2026-09-05. Static evidence against the supported Steam PreyDll.dll.**
SHA-256: `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`.

**Result:** Prey has a translation-free view matrix specifically feeding its near
view-projection. The near projection already consumes all four camera asymmetry
fields, scaled by `0.03 / camera.nearPlane`. Changing the full camera's position
cannot survive the explicit translation clear. The best isolated stereo
experiment is to add the eye-relative translation to the finished **near
view-projection**, leaving the other matrix fields intact.

This establishes the binary mechanism, **not yet the cause of the observed
weapon pixels or a tested fix**. A live draw/constant-buffer correlation is still
required. The user reserved the running game for another agent during this
investigation. Before that instruction, this investigation only enumerated the
process/modules and read memory (including near FOV = 55 and candidate
prologues). No hooks, game-memory writes, VR controls, or rendering experiments
were performed. All subsequent work was static.

## Exact route through the binary

All addresses below are **Steam RVAs in PreyDll.dll**, not EGS addresses.

| RVA | Role established by code | Useful observation |
|---|---|---|
| `0xFB1670` | Builds a 0x2D0-byte view-info object, selecting the source | RCX = pipeline-like owner, RDX = output. Owner `+0x2E0` is the selected `CRenderView*`; null chooses renderer-state fallback. |
| `0xFB2AC0` | Builds view-info from a `CRenderView`'s derived camera and current/previous `CCamera` | Called with view `+0x1620`, `+0x11A0`, `+0x13E0`. Computes near VP at output `+0xA0`. |
| `0xFB0B70` | Builds full view, translation-free view and inverse view from a camera | **Clears the fourth argument's translation at `0xFB1037` and `0xFB103B`.** |
| `0xFB1530` | Builds the nearest projection | Near = 0.03; FOV override if 1 < nearFov < 179; rescales all four asymmetry values. |
| `0xFB4280` | Renderer-state fallback for the same view-info layout | Near VP = renderer `+0x130` (zero view) multiplied by nearest projection. |
| `0xFB57A0` | Packs/transposes view-info into a constant-buffer update | Input near VP `+0xA0` becomes payload `+0x90`; calls `0x107EE50` with 0x3A0 bytes. |
| `0xF43D70` | `CD3D9Renderer::UpdateNearestChange`, separate camera-switch path | Saves camera at renderer `+0x5620`, applies near frustum, calls `SetCamera`, changes depth range. |
| `0xF7FE70` | `CD3D9Renderer::SetCamera` (existing R-066) | Also builds a zero view at renderer `+0x49C8 + slot*0x328`. |
| `0xF42C00` | `CD3D9Renderer::RT_SetCameraInfo` | Fetches zero view into renderer `+0x130` and computes zero VP at `+0x230`. Renderer vtable `+0x930`. |
| `0xF39E00` | `FX_ObjectChange`-equivalent flag gate | Render object `+0x40`, bit 23 (`0x800000`), selects nearest; translates to pipeline bit `0x10000`. |

Names absent from Ghidra are descriptive. The `UpdateNearestChange`,
`SetCamera`, and `RT_SetCameraInfo` identifications also match the locally
held CryEngine source and Chairloader headers. Offsets above were read from
**Prey's bytes**, not transplanted from those sources.

### The instruction that removes the stereo offset

At `0xFB0B70`, Win64 arguments are:

- RCX: view-info / flags owner.
- RDX: input `CCamera*`.
- R8: full view matrix output.
- R9: translation-free view matrix output.
- Fifth argument: inverse-view output.

It constructs the full camera inverse using position, copies it to R9, and ends:

```asm
; RCX was cleared; the preceding instructions copy the full matrix from R8.
PreyDll+0xFB1037  mov qword ptr [r9+0x30], rcx
PreyDll+0xFB103B  mov dword ptr [r9+0x38], ecx
PreyDll+0xFB103F  mov rsp, r11
PreyDll+0xFB1042  ret
```

These are `m30/m31/m32`, the translation row of the untransposed Matrix44.
The fourth output retains the full view's rotation but has zero translation.

`0xFB2AC0` invokes the helper twice:

| Call RVA | Return RVA | Camera |
|---|---|---|
| `0xFB2E48` | `0xFB2E4D` | Current, supplied from view `+0x11A0` |
| `0xFB2E6D` | `0xFB2E72` | Previous, supplied from view `+0x13E0` |

A blanket helper patch would affect other zero-view products as well as the
near pass. The current near-only candidate is the **finished view-info field**,
after its builder returns, or a copied input immediately before packing it.

### Near projection is already asymmetric

`0xFB1530` derives:

```text
nNear = 0.03
q = nNear / camera.near
h = tan(nearFovRadians / 2) * nNear
w = h * camera.projectionRatio

left   = -w + q * camera.asymL
right  = +w + q * camera.asymR
bottom = -h + q * camera.asymB
top    = +h + q * camera.asymT
```

It selects normal or reverse depth from view-info flags `+0x2C8`, bit 0;
bit 2 applies jitter to projection `m20/m21`. The camera-switch route
`0xF43D70` independently implements the same near-plane/asymmetry scaling.

This proves the code supports the asymmetry; it does not prove that the live
near draw receives the intended camera values. A different near FOV also means
a different optical projection from the world. Preserve it for the first
translation discriminator, then check the complete near projection against the
intended headset framing before calling the feature correct.

### View-info and upload fields

Both view-info builders use this layout:

| View-info offset | Matrix | Packed payload offset |
|---|---|---|
| `+0x20` | Translation-free view × world projection | `+0x00` |
| `+0x60` | Full view × world projection | `+0x50` |
| **`+0xA0`** | **Translation-free view × near projection** | **`+0x90`** |
| `+0xE0` | World projection | Not the near projection |
| `+0x120` | Full current view | Separate field |
| `+0x160` | Inverse full world VP | Separate field |
| `+0x1A0` | Inverse current view | Separate field |
| `+0x260` | Near projection | `+0x150` |
| `+0x2C8` | Flags | Not a matrix |

The payload matrices are **transposed**. `+0x90` is relative to the data passed
to the 0x3A0-byte update, not a verified D3D11 binding slot or buffer identity.
The shader consuming the observed weapon has not yet been matched to it.

A concrete downstream chain is:

```text
0xFAFB00
  -> 0xFB1670(owner, owner+0x350, ...)
       -> 0xFB2AC0   if owner+0x2E0 is non-null
       -> 0xFB4280   otherwise
  -> 0xFB57A0(owner, owner+0x350, owner+0x2A8)
       -> 0x107EE50(buffer, packedData, 0x3A0, 1)
```

Other callers build temporary view-info and use the same packer. Do not infer
main-scene provenance from the packer's address alone.

## Concrete experiment for the agent owning the live process

**Hypothesis:** the weapon consumes the nearest VP above, whose rotation follows
the selected eye but whose translation is discarded.

**Control:** matched gameplay scene; no edit, then zero-delta edit, then a small
signed eye delta. Confirm frame progress and the equipped weapon are present.
Use world geometry and the weapon's shadow as image controls.

**Variable:** only the current main-view near VP `viewInfo+0xA0`.
First trace `0xFB1670` / `0xFB57A0` with owner, view type, source camera,
frame/eye identity, and the three VP matrices. Confirm the actual path before
choosing a hook. Avoid a global eye toggle: render-thread work may consume a
previously queued frame. Carry the eye delta with that render view/frame.

For the existing row-vector layout, let `M0` be the original near VP and
`deltaWorld` the engine-space **eye-minus-cyclops displacement** for that
rendered frame. The candidate transform is:

```text
M = Translation(-deltaWorld) * M0

M[3][j] = M0[3][j]
        - deltaWorld.x * M0[0][j]
        - deltaWorld.y * M0[1][j]
        - deltaWorld.z * M0[2][j]       for j = 0..3
```

Rows 0..2 remain unchanged. Compute from an original copy each time, not an
already edited matrix. Use the mod's established world-units-per-metre scale.
Do not use the full world camera position: near vertices already have a
camera-relative origin. The eye-only delta preserves that origin while giving
the two eyes different viewpoints. The actual near-object anchor still needs
confirmation before extending this to roomscale translation.

Equivalent formulation: put `-deltaWorld * cameraRotation` in the zero view's
translation, then multiply by the near projection. The finished-field edit is
more selective because it leaves `viewInfo+0x20` and world VP `+0x60` alone.

**Decision rule:** zero delta is unchanged; reversing the signed delta reverses
weapon disparity; doubling it doubles disparity approximately; details at
different weapon depths show different parallax. World geometry and weapon
shadow remain the controls. Save the pre/post view-info and packed bytes with
frame identity as a replayable fixture. A constant screen-space shift alone
does not prove stereoscopic depth.

**Still open:** which branch and buffer the live weapon consumes, correct
eye/frame ownership, secondary-view filtering, temporal/motion-vector
companions, any additional legacy nearest draws, and headset acceptance.
Do not globally patch the zero matrix or the two clearing instructions as a
production fix: that would also modify other camera-relative products.

## Corrections to the starting handover

The five `GetCVar("r_DrawNearFoV")` string references are not a guaranteed route
to near setup. R-069 already located the per-frame latch `renderer+0x95B4`.
A direct displacement scan found its three consuming functions:

- `0xF43D70`: near camera switching.
- `0xFB4280`: view-info construction.
- `0xF054F0`: deferred-shadow projection setup (matches Chairloader's
  `FX_DeferredShadowPassSetup(..., bool bNearest)` shape).

`0xF7D710` writes the latch. The string consumers examined at `0x1490D80`
and `0x17266D0` manage the FOV setting, not the near render camera.

The correct local source roots are siblings:
`D:\Dev Debug\source code\CryGame` and
`D:\Dev Debug\source code\CRYENGINE`. The latter contains the renderer
source used here; CryGame is the older GameDLL/header reference.

## Evidence files and reproducibility

Validation completed here: installed DLL hash matches the supported baseline;
Ghidra disassembly confirms the two translation-clearing stores and the
current/previous helper call sites. A synthetic row-vector matrix check passed
zero-delta identity, equality with `Translation(-delta)*M`, unchanged rows 0..2,
and halved disparity at twice the depth. With 64 mm IPD, 55-degree vertical FOV
and 16:9 aspect, the synthetic NDC disparities were 0.138310713 at 0.5 m and
0.069155357 at 1 m. These are analytic fixtures, not measurements of Prey pixels.

Raw Ghidra output and the read-only PE scanner are under
[`captures/re/h011/`](../captures/re/h011/static_scan.py).
This directory is gitignored; the conclusions and addresses are preserved in
this tracked report.

- [Matrix helper](../captures/re/h011/fb0b70-build-view-matrices.c)
- [View-info from CRenderView](../captures/re/h011/fb2ac0-viewinfo-from-render-view.c)
- [View-info source dispatch](../captures/re/h011/fb1670-viewinfo-dispatch.c)
- [Renderer fallback](../captures/re/h011/fb4280-viewinfo.c)
- [Near projection](../captures/re/h011/fb1530-nearest-projection.c)
- [Packing/upload](../captures/re/h011/fb57a0-upload-per-view.c)
- [Camera-switch route](../captures/re/h011/f43d70-update-nearest.c)
- [Static scan with exact module hash](../captures/re/h011/static_scan.json)

Ghidra base was `0x180000000`. PE scan results are raw candidate references;
the important calls and translation-clear instructions were independently read
in Ghidra disassembly. No compiled code or installed-game files were changed.

Source comparison:
`CRYENGINE/Code/CryEngine/RenderDll/XRenderD3D9/DriverD3D.cpp` (`SetCamera`);
`D3DRendPipeline.cpp` (`UpdateNearestChange`);
`GraphicsPipeline/StandardGraphicsPipeline.cpp` (nearest VP and cbuffer packing).
Chairloader `Common/Prey/CryRenderer/IRenderer.h:149` calls bit 23
`FOB_NEAREST` and describes camera-space rendering; Prey's `0xF39E00`
tests that exact bit at render object `+0x40`.
