# `CCamera` layout

`sizeof(CCamera) == 0x240`. Derived from `CryMath/Cry_Camera.h` in the Chairloader PDB headers and
then checked against the installed Steam `PreyDll.dll`, which is what makes it usable rather than
merely plausible.

The live instance that matters is **`CSystem::m_ViewCamera` at `CSystem+0x788`** (R-040) — the camera
`ISystem::GetViewCamera()` returns, and the one both R-011 and R-016 read. See [H-008](HYPOTHESES.md).

## Why this layout is trusted

The header is for the EGS build, so its field order is evidence, not proof. Eleven offsets observed
independently in the *Steam* binary each land exactly on a member boundary, and they are spread from
the first field to the last with no slack anywhere:

| Source | Offsets |
| --- | --- |
| R-039, from decompiling R-011's reads | `+0x30` `m_fov`, `+0x38` `m_Width`, `+0x3C` `m_Height`, `+0x40` `m_ProjectionRatio`, `+0x48` `m_edge_nlt` (`.y` at `+0x4C`), `+0x60` `m_edge_flt` (`.y` at `+0x64`) |
| R-042, from the copy widths in `CCamera::operator=` | `+0x218` `m_pPortal` and `+0x228` `m_pMultiCamera` copied as **qwords** (both pointers), `+0x220` `m_ScissorInfo` as a qword, `+0x230` `m_OccPosition` as a qword plus a trailing dword |
| R-042, from the final instructions | `+0x23C` merged with masks `& 1` and `& 0xE` — exactly a 1-bit field followed by a 3-bit field, matching `m_JustActivated : 1; m_sceneMaskFilter : 3` |

The computed size lands on `0x240`, matching the size R-042 measured from where the copy stops. Head,
tail and total all agree.

## What this means for VR

- **Asymmetric projection is first-class, and the path is live.** `m_asymL/R/B/T` at `+0x6C`/`+0x70`/
  `+0x74`/`+0x78` are four independent frustum shifts. These offsets are not merely header-derived:
  `CRenderView::SetCamera` (R-030) reads all four and folds them into the render view's frustum
  tangents as `fWL = m_asymL - t*ratio`, `fWR = t*ratio + m_asymR`, `fWB = m_asymB - t`,
  `fWT = t + m_asymT`, where `t = tanf(m_fov*0.5)`. Those tangents are the same parameterisation
  OpenXR's `XrFovf` uses, so an eye FOV maps on by taking `tan` of each angle. A per-eye asymmetric
  projection is therefore four float writes on the camera handed to `SetCamera` — no matrix
  injection, no restructuring.
- **But the header carries a warning worth heeding**: the asymmetry fields are annotated *"not used
  for culling atm"*. Setting them should therefore be expected to change what is **rendered** without
  changing what is **culled**. For a modest per-eye IPD shift that is likely invisible; it becomes a
  correctness question at wide asymmetry, where geometry just inside an eye's true frustum could be
  culled against the symmetric one. Treat "does culling follow the asymmetry?" as an open live test,
  not a settled fact.
- **A camera is bit-copyable.** `operator=` is a plain memberwise copy with no recomputation (R-042),
  so snapshot/restore around an eye render is exact. That is what makes the H-008 fallback sound in
  principle.
- **Frustum state is cached inside the object.** `m_fp`, the `m_id*` index arrays and the twelve
  cached corner vertices are all derived data living in the struct. Writing `m_Matrix` or `m_fov`
  directly does **not** refresh them — go through the engine's own setters, or accept that culling
  and any consumer reading cached corners will use stale values.

## Layout

| Offset | Type | Member | Notes |
| ---: | --- | --- | --- |
| `+0x000` | `Matrix34` | `m_Matrix` | World-space matrix. Row-major 3x4; translation is the 4th column. |
| `+0x030` | `f32` | `m_fov` | Vertical FOV in radians. |
| `+0x034` | `f32` | `m_fovBase` |  |
| `+0x038` | `int` | `m_Width` | Surface width. |
| `+0x03C` | `int` | `m_Height` | Surface height. |
| `+0x040` | `f32` | `m_ProjectionRatio` | Width/height of the view surface. |
| `+0x044` | `f32` | `m_PixelAspectRatio` | Non-square-pixel correction. |
| `+0x048` | `Vec3` | `m_edge_nlt` | Left/upper vertex of the near plane. `GetNearPlane()` returns `.y` at `+0x4C`. |
| `+0x054` | `Vec3` | `m_edge_plt` | Left/upper vertex of the projection plane. |
| `+0x060` | `Vec3` | `m_edge_flt` | Left/upper vertex of the far plane. `GetFarPlane()` returns `.y` at `+0x64`. |
| `+0x06C` | `f32` | `m_asymL` | **Asymmetric frustum shift, left.** |
| `+0x070` | `f32` | `m_asymR` | **Asymmetric frustum shift, right.** |
| `+0x074` | `f32` | `m_asymB` | **Asymmetric frustum shift, bottom.** |
| `+0x078` | `f32` | `m_asymT` | **Asymmetric frustum shift, top.** |
| `+0x07C` | `Vec3` | `m_cltp` | Projection-plane vertices in camera space. |
| `+0x088` | `Vec3` | `m_crtp` |  |
| `+0x094` | `Vec3` | `m_clbp` |  |
| `+0x0A0` | `Vec3` | `m_crbp` |  |
| `+0x0AC` | `Vec3` | `m_cltn` | Near-plane vertices in camera space. |
| `+0x0B8` | `Vec3` | `m_crtn` |  |
| `+0x0C4` | `Vec3` | `m_clbn` |  |
| `+0x0D0` | `Vec3` | `m_crbn` |  |
| `+0x0DC` | `Vec3` | `m_cltf` | Far-plane vertices in camera space. |
| `+0x0E8` | `Vec3` | `m_crtf` |  |
| `+0x0F4` | `Vec3` | `m_clbf` |  |
| `+0x100` | `Vec3` | `m_crbf` |  |
| `+0x10C` | `Plane[6]` | `m_fp` | Frustum planes; `Plane` is `Vec3 n` + `f32 d` = 16B. |
| `+0x16C` | `uint32[6]` | `m_idx1` | Sign-derived AABB test indices. |
| `+0x184` | `uint32[6]` | `m_idy1` |  |
| `+0x19C` | `uint32[6]` | `m_idz1` |  |
| `+0x1B4` | `uint32[6]` | `m_idx2` |  |
| `+0x1CC` | `uint32[6]` | `m_idy2` |  |
| `+0x1E4` | `uint32[6]` | `m_idz2` |  |
| `+0x1FC` | `float` | `m_zrangeMin` | Z-buffer near range for this camera. |
| `+0x200` | `float` | `m_zrangeMax` | Z-buffer far range. |
| `+0x204` | `int` | `m_nPosX` | Viewport. |
| `+0x208` | `int` | `m_nPosY` |  |
| `+0x20C` | `int` | `m_nSizeX` |  |
| `+0x210` | `int` | `m_nSizeY` |  |
| `+0x218` | `IVisArea*` | `m_pPortal` | Portal this camera was created from. |
| `+0x220` | `ScissorInfo` | `m_ScissorInfo` | 4 x `uint16` x1,y1,x2,y2. |
| `+0x228` | `void*` | `m_pMultiCamera` | Optional culling camera list. |
| `+0x230` | `Vec3` | `m_OccPosition` | Occlusion-test position. |
| `+0x23C` | `uint8 :1 / :3` | `m_JustActivated / m_sceneMaskFilter` | Bitfield: 1-bit then 3-bit. |

Total `0x240` bytes after 8-byte alignment (the two pointer members force 8-byte alignment).
