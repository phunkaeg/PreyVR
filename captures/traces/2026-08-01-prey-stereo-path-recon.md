# Prey retained-stereo / multi-view reconnaissance

## Scope

Answer H-001: does the shipping Prey renderer expose or retain a usable stereo or multi-view
rendering path? This is a read-only static pass over the installed Steam engine module. No process
was attached, no memory was written, and no Ghidra annotation was saved.

## Identity

- Engine target: `PreyDll.dll`, SHA-256 `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`
- Ghidra program: `/Prey/PreyDll.dll`, image base `0x180000000`, 79,306 functions, 48,332,552 bytes mapped
- Method: exhaustive ASCII byte-pattern search across the whole image

## Method note

`PreyDll.dll` has defined strings only in the PE export-name region around `0x182243D0C`; the ASCII
analyzer never swept the image. Both `search_strings`/`list_strings` and `get_xrefs_to` therefore
return nothing for engine literals — the same limitation already recorded for the R-001 DXGI
loader cluster. This pass used raw byte-pattern search instead, which does read the entire image.

The search is not silently capped: a control query for `"Enables "` returned 152 distinct hits
with no truncation, so the small result counts below are real totals, not a truncated head.

### Update after the full analysis pass

The database has since been fully auto-analysed and the string index now works. The method note
above describes the database state at the time of this pass; it is no longer a live limitation.

The two methods were re-run against each other and **agree exactly**. `search_strings` now returns
8 stereo-bearing literals, which reconcile 1:1 with the 10 raw byte hits enumerated below — two
byte hits fall inside `Stereo:ReadStereoParameters` and two inside `Stereo:StereoParameters`. A
single combined query for `r_Stereo`, `StereoMode`, `Oculus`, `OpenVR`, `OSVR`, `SteamVR`,
`HmdDevice`, and `IHmd` returns zero matches, matching the confirmed-absence table below.

The conclusions in this capture therefore rest on two independent methods rather than one.

## Exhaustive enumeration

Every occurrence of `Stereo` (7 total) in the entire 48 MB image:

| Address | Content | Classification |
| --- | --- | --- |
| `0x181CD4F38` | `Stereo:ReadStereoParameters` | Flow Graph node name |
| `0x181CD4F43` | second `Stereo` inside the same literal | — |
| `0x181CD4F90` | `Stereo:StereoParameters` | Flow Graph node name |
| `0x181CD4F97` | second `Stereo` inside the same literal | — |
| `0x181DC9FB2` | `r_VolumetricCloudsStereoReprojection` | renderer cvar |
| `0x181EFE910` | `Stereo` | audio channel configuration |
| `0x181EFEA29` | `Auro_222_Stereo` | Auro-3D audio channel configuration |

Every occurrence of lowercase `stereo` (3 total):

| Address | Content | Classification |
| --- | --- | --- |
| `0x181D97247` | `...ewing Flash content in stereo 3D` | cvar help text |
| `0x181D97262` | `sys_flash_stereo_maxparallax` | Scaleform cvar |
| `0x181DC9F38` | `Enables stereoscopic reprojection for procedural volumetric clouds...` | help text for the cvar above |

## Confirmed absences

Each of the following returned zero matches across the whole image:

| Pattern | Meaning if present |
| --- | --- |
| `r_Ster` | any member of CryEngine's `r_Stereo*` cvar family |
| `StereoMode` | `CD3DStereo`'s primary mode selector |
| `Oculus` | CryOculusVR plugin |
| `OpenVR` | CryOpenVR plugin |
| `OSVR` | CryOSVR plugin |
| `SteamVR`, `Vive` | runtime/device identifiers |
| `HMD`, `Hmd` | `IHmdDevice` / `IHmdRenderer` interface layer |
| `LeftEye`, `RightEye` | per-eye render target or view naming |

`RenderView` appears exactly once, in the job name `JobRenderViewPostWrite` at `0x181DCAF7B`,
confirming the `CRenderView` job architecture is present but revealing nothing about view count.

## The decisive contradiction

The two surviving Flow Graph nodes sit in a shared 7-pointer descriptor record followed by an
inline name string. Their port names are adjacent in `.rdata` at `0x181CD4E8C` onward:

```text
CurrentEyeDistance
CurrentScreenDistance
CurrentHUDDistance
TimeLeft
```

In stock CryEngine those three ports write `r_StereoEyeDist`, `r_StereoScreenDist`, and
`r_StereoHudScreenDist`. **None of those cvars exist in this image** — `r_Ster` has zero matches.
The node registration survived; its cvar targets did not.

## Interpretation

Arkane shipped Prey with CryEngine's stereo *device and output* layer removed. What remains is
residue in subsystems that register their own cvars independently of `CD3DStereo`:

- volumetric-cloud stereo reprojection (`r_VolumetricCloudsStereoReprojection`);
- Scaleform stereo 3D parallax (`sys_flash_stereo_maxparallax`);
- two orphaned Flow Graph nodes whose cvar targets are gone;
- audio channel configurations, which are unrelated to rendering.

There is no `IHmdDevice`/`IHmdRenderer` interface layer, no CryVR plugin, no per-eye naming, and no
`r_Stereo*` control surface. **H-001 resolves to a negative for the stereo half.** The mod cannot
enable a retained engine stereo mode, because no such mode is present to enable.

The multi-view half is not resolved by this pass. `CRenderView` exists, but whether it retains more
than one `SRenderViewInfo` cannot be decided from strings — that requires structural analysis of the
render-view object, not literal search.

## Consequence for the proof ladder

The `flat stereo bridge -> private eye targets -> engine-owned per-eye scene route` rungs must be
built on a mod-owned per-eye route. Two candidates remain, and neither is a retained engine switch:

1. **Scene re-entry** — drive the engine's own scene submission twice per frame with a swapped
   camera, at a boundary paired with `BeginRendererScene`/`EndRendererScene` (R-002/R-003).
2. **View-info ownership** — if `CRenderView` proves to carry more than one view info, populate the
   second directly. This is the cheaper route if it exists, and it is the next thing to test.

## Safety

Read-only. No process attached, no memory written, no Ghidra annotation created or saved, and the
installed game was not modified.
