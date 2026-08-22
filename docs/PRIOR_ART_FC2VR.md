# Prior art: FC2VR (Far Cry 2 / Dunia)

Assessment of `FC2VR_DISCORD_TEST_v1.0.1R4` as prior art for PreyVR. Read-only inspection of the
shipped release; no code was run and nothing was copied.

## What it actually is

The release is described as achieving "native stereo". That phrase means something specific and it is
worth pinning down, because it is **not** an engine stereo mode of the kind H-001 went looking for.

- `NATIVE_VIEW_BEGIN` / `NATIVE_VIEW_END` bracket **one eye being rendered through the engine's own
  camera and view-builder path**, as opposed to reprojecting or post-processing a mono image.
- "Native stereo" means both eyes are produced that way and submitted as an
  `XrCompositionLayerProjection`. The failure mode they fought — "Projection/Quad flapping" — is the
  host falling back to `XrCompositionLayerQuad`, a flat panel in VR.

So FC2VR is **an existence proof of the mod-owned per-eye route**, the same route PreyVR committed to
after H-001 closed negative. It is not evidence that any engine retained a stereo switch.

That makes it relevant, and the lineage helps: Dunia is a CryEngine 1 fork, Prey is CryEngine 3.x-era.
Same ancestry, different generation.

## Architecture

```text
game process (32-bit)                         separate process (64-bit)
+-----------------------------+               +--------------------------+
| Dunia.dll                   |               | fc2vr-host.exe           |
|   renderer vtable hooks     |               |   OpenXR session/frames  |
|     +0x14 PrepareFrameGraph |               |   swapchains             |
|     +0x18 WorldExec         |  shared mem   |   Projection vs Quad     |
|   CameraCopy / ViewBuilder  | <===========> |   input, haptics         |
|   CameraRebuild             |               +--------------------------+
+-------------^---------------+                          |
              |                                   openxr_loader.dll (64-bit)
   d3d9_gog.dll / d3d9_uplay.dll   <- engine-specific "outer"
   d3d9_fc2vr.dll                  <- engine-agnostic bridge, also the d3d9 proxy
```

The bridge exports a clean per-eye API: `FearVr_GetRenderRequest`, `FearVr_WaitForNewRenderRequest`,
`FearVr_BeginEye`, `FearVr_CaptureEye`, `FearVr_EndStereoFrame`, plus `GetInputState`,
`SubmitHapticRequest`, `SetStereoEnabled`, `SetRenderScalePercent`, `SetFovScalePercent`,
`RequestRecenter`, `SetMenuActive`, `SetComfortModeEnabled` and `InstallIatHook`.

The `FearVr_` prefix indicates the bridge predates this mod and was carried over from a F.E.A.R.
project, so it has already crossed at least one engine boundary. Everything engine-specific lives in
the thin `outer`; the whole OpenXR lane lives behind the bridge.

## The blocker for reuse

| Component | Machine | Reusable by PreyVR? |
| --- | --- | --- |
| `d3d9_fc2vr.dll` (bridge) | **x86** | **No** — cannot load into 64-bit Prey |
| `d3d9_gog.dll` / `d3d9_uplay.dll` (outers) | **x86** | No, and they are Dunia-specific anyway |
| `fc2vr-host.exe` | **x64** | In principle — it is out-of-process |
| `openxr_loader.dll` | x64 | Already have our own pinned 1.1.60 |

The in-process components are 32-bit because Far Cry 2 is. The host is deliberately a separate 64-bit
process so a 32-bit game can reach a 64-bit OpenXR runtime. Prey is 64-bit, so the bridge as shipped
cannot be loaded; it would need a 64-bit build, and only binaries are present here, no source. The
shared-memory protocol between bridge and host is not documented in the release either.

**Verdict on reuse: not directly usable.** The value is in the design and the hard-won failure
analysis, not in the binaries.

## What transfers, in order of value

### 1. Primary-baseline contamination (their R2) — a bug PreyVR will hit

Their causal evidence is precise. While the LEFT eye was being built, the game's own camera observer
kept running, so LEFT's transient `CameraRebuild`/`ViewBuilder` camera **overwrote the stored primary
camera reference**. RIGHT then compared the correctly-restored stock camera against that contaminated
reference and rejected it. They proved it by bit-exact match: RIGHT's "expected primary" equalled
LEFT's transient value, not the real pre-LEFT value.

**PreyVR has an exact analogue already mapped.** R-011 `ArkPlayer::UpdateCachedReticleViewPosAndDir`
recomputes a cached world ray from the camera every frame, into ArkPlayer `+0x17D4`/`+0x17E0`. If we
drive the camera once per eye, that cache takes whichever eye ran last — contaminating the very ray
that the A0b wrench proof and the interaction A0 proof depend on. R-016
`ArkPlayerMovementController::GetMovementState` writes view vectors too.

Their fix was to freeze primary camera/projection provenance across the whole two-eye transaction.
Worth designing in from the start rather than debugging later.

### 2. Pose-space validation instead of raw matrix comparison (their R4)

Their original guard compared all 16 raw View-matrix coefficients against a fixed `0.010` threshold.
World-space translation amplifies tiny rotation rounding by the player's map coordinates, producing
**yaw-dependent false rejects** — rejections near one world direction and again 180 degrees opposite.

The fix: validate the *rebuilt physical camera pose* rather than raw coefficients —
rotation max component error `< 0.0025`, position max component error `< 0.010` — and keep the raw
value as telemetry only.

**Directly applicable.** The R-026 camera was measured at roughly `(786, 1572, 17)`; Prey's map
coordinates are in the same range that caused this. Any validation we build around an injected camera
should validate pose, not matrix coefficients.

### 3. Separate persistent ownership from per-frame state (their R3)

One flag served as both "the mod owns stereo rendering" and "the frame just transferred was stereo". An
ordinary mono Present cleared it, so the host flipped to a flat Quad layer mid-stream. Their fix
suppressed only the generic mono clear while leaving explicit menu/comfort/disable transitions intact.

### 4. Hold the last complete pair (their R4)

After a rejected eye, a following mono Present could overwrite both eye slots with the same image while
the host stayed in Projection — stereo geometry replaced by duplicated mono, inside a stereo layer. The
fix holds the last coherent LEFT+RIGHT pair until a new complete pair arrives.

### 5. Transactional restore with visible fail-back (their R1)

Exact post-`CameraRebuild` restore, backbuffer restore on an aborted eye transaction, and a *visible*
failure rather than a silent one. This matches the discipline PreyVR already applies to stack-local
writes, and confirms it is the right posture for camera writes too.

### 6. Deterministic builds are achievable

Their static validation includes `GOG deterministic fresh rebuild` and `UPLAY deterministic fresh
rebuild`, both reproducing the shipped SHA-256 exactly. PreyVR currently cannot do this — see F-005.
It is an existence proof that the goal is reachable, and supports the `/Brepro` option already
recorded there.

## An injection vector PreyVR has not considered

FC2VR reaches the game with a **`d3d9.dll` proxy** in the game folder, not by injecting into a running
process.

R-025 established that `PreyDll.dll` calls `LoadLibraryA("dxgi.dll")` and `LoadLibraryA("d3d11.dll")`
with bare names. Bare-name `LoadLibrary` searches the application directory first, so **a `dxgi.dll`
proxy placed beside `Prey.exe` would be loaded by Prey itself**. apitrace's refusal to install a DXGI
wrapper (recorded in F-003) was an apitrace limitation, not a Windows one.

This is a genuinely different lifecycle from PreyVR's current manual injection: the proxy is present
from process start, before any device exists, which is exactly when a per-eye renderer needs to be in
place. It also writes to the game directory, which the project has so far avoided — a real tradeoff,
not a free win.

## What does not transfer

- **D3D9 versus D3D11.** FC2 is D3D9; Prey is D3D11. Device, swapchain and capture mechanics differ.
- **Every Dunia RVA** in `DUNIA_PORT_MAP.txt`. Different engine, different build.
- **The specific hook points.** `PrepareFrameGraph`, `WorldExec`, `CameraCopy`, `ViewBuilder`,
  `CameraRebuild` and `SkyCommon` are CryEngine-1-lineage names with no direct CryEngine 3.x
  equivalents, though `ViewBuilder` and `CameraRebuild` are conceptually close to what
  `CRenderView::SetCamera` (R-030) and `CCamera::UpdateFrustum` do on our side.

## Verification note

Everything above comes from reading the release's own documentation and from parsing the shipped PE
headers and export tables directly. The bitness table was measured, not taken from their
`STATIC_VALIDATION.txt`. No FC2VR component was executed.
