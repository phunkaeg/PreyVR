# Architecture and proof ladder

The project is aiming for a native, engine-owned 6DoF path. This is deliberately staged so desktop rendering remains usable and each transition answers one question.

```text
bootstrap → default-off engine frame observation → OpenXR instance/system bootstrap
          → flat stereo bridge → private eye targets → engine-owned per-eye scene route
          → camera/culling ownership → native controller aim/actions → UI/viewmodel/comfort lanes
```

## Ownership lanes

- **Lifecycle:** module identity, safe attach/detach, logs, build gating.
- **Graphics/XR:** D3D11 device/context state, OpenXR session/frame timing/swapchains, target freshness, desktop presentation.
- **Engine view:** camera transform, projection, FoV/near plane, visibility/culling, scene re-entry or a verified native stereo path.
- **Input/gameplay:** controller poses converted into the engine’s own aiming, interaction, locomotion, and haptic semantics.
- **Presentation:** screen-space passes, HUD, menus, viewmodels, gamma/tonemapping, and compositing—not assumed to share world-space ownership.
- **Diagnostics:** logs, capture manifests, state counters, reversible A/B switches, and exact build identity.

## Three systems that must be separated

Prey currently treats looking, presenting the first-person weapon, and aiming gameplay as one coupled desktop action. A usable motion-control mod must establish independent ownership of all three systems:

1. **Head / world camera** — OpenXR HMD position and orientation drive the rendered view, projection, and culling. Moving the head must not rotate the weapon or change the gameplay aim ray unless an explicit fallback mode requests it.
2. **Visual hands and weapon** — the first-person arms, held item, muzzle, and animation root follow a controller-relative pose. This is a presentation transform; moving it alone must not be accepted as proof that shots or interactions follow it.
3. **Gameplay aim and actions** — hitscan rays, projectile direction, melee traces, interaction/use rays, targeting, recoil, and relevant animation parameters originate from the controller-owned aim pose through Prey's native gameplay pathways. The mapped use lane is now `cached reticle ray -> ArkPlayerTargetSelector::UpdateCandidates -> ArkPlayerInteraction::Interact`; the mod should steer the transient ray/candidate input and leave native interaction execution intact.

The systems should be proved independently. A temporary synthetic pose is acceptable for discovering a seam: for example, offset gameplay yaw without moving the camera, or offset the weapon model without changing where shots land. These diagnostic splits are milestones, not shipping behaviour.

### Dependency order

```text
rudimentary OpenXR output
  -> HMD camera ownership
  -> synthetic camera/aim separation proof
  -> controller-owned gameplay aim
  -> controller-owned weapon/viewmodel
  -> alignment, animations, interactions, recoil, and polish
```

The critical detached-aim gate is now reproduced end to end. A reversible fixed-camera probe moved Prey's cached world aim ray; a +10-degree stack-local wrench edit moved the native contact `0.127165` units laterally across the same wall; and a -15-degree stack-local interaction edit changed both selected and usable entity from keypad `0xFDE0` to `0x1117`. In both native consumers the camera and persistent ray remained unchanged. PDB-mapped `CArkWeapon` functions consume the same ray for firearm queries; the projectile runtime test remains before controller-owned gameplay aim is complete. Substantial viewmodel work remains downstream of rudimentary OpenXR output and HMD camera ownership.

The bootstrap/build-gating rung is now `PreyVR.dll` version `0.3.0-lifecycle-hardening`. It starts work outside the loader-lock callback, validates 30 exact in-memory landmarks, plans a default-off `RT_EndFrame` observer, and performs a no-call OpenXR runtime/loader preflight. Once supported, the module is pinned until process exit and observer disable retains its inactive trampoline, closing worker/callback unload races. The callback performs only atomic telemetry and original-function forwarding; its summary is written from the control thread on disable. The current artifact is headless/on-disk verified and awaits its first supported-host run. The earlier 21-landmark artifact counted 301 live callbacks, which proves the seam but not the current binary. The contract is in [`FRAME_OBSERVER_BOOTSTRAP.md`](FRAME_OBSERVER_BOOTSTRAP.md).

## Non-negotiable checks

- No exposed shipping stereo/HMD control surface was found: the image-wide search found no `r_Stereo*` cvar, `IHmdDevice`/`HMD` layer, or CryVR plugin, and the probed R-026 frame slots contained one camera rather than a resident eye pair. H-001 is closed only for that stock/exposed route. It does not prove that every possible scene re-entry or internally constructed multi-view route is absent; the mod should own the per-eye path unless later dynamic evidence identifies one. See the [stereo-path reconnaissance](../captures/traces/2026-08-01-prey-stereo-path-recon.md) and [multi-view probe](../captures/traces/2026-08-01-prey-multiview-probe.md).
- Two candidate HMD pose-injection points now exist: the R-009 `ArkPlayerCamera::UpdateView` gameplay-camera callback and the R-026 renderer view block. `CRenderView::SetCamera` (R-030) is the currently preferred seam: it copies the camera **by value** into the render view's own `m_camera` (R-049), so it cannot alias `CSystem::m_ViewCamera`, and it derives the full per-eye projection from the camera it is handed. The *static* chain to the renderer is now traced — `SetCamera` writes the `CRenderCamera` block (R-050) that the renderer consumes for view and frustum — so the open question is narrower than it was: not "which one reaches the renderer" but **whether the gameplay camera from R-009 ends up feeding `SetCamera`, and what calls `SetCamera` per frame**. Both are live questions. Neither seam has been written to. R-026's frustum stores its four edges independently, so OpenXR's required asymmetric per-eye projection is representable without restructuring the camera. **This is now proven at the binary level rather than inferred**: `CRenderView::SetCamera` (R-030) reads `CCamera::m_asymL/R/B/T` and folds them into the render view's frustum tangents `fWL/fWR/fWB/fWT` (R-050) — the same tangent parameterisation OpenXR's `XrFovf` uses. Prey's asymmetric-frustum path is live in the render pipeline. Two limits travel with it: the engine header marks the asymmetry *"not used for culling atm"*, so expect it to change what is rendered without changing what is culled; and `CCamera` caches its frustum planes and corner vertices inside the struct, so writing fields directly leaves that derived state stale.
- Distinct eye images alone are not native 6DoF; the engine must own camera/culling changes for translation to reveal new geometry.
- Changing one projection matrix is not enough. Audit dependent camera constants and screen-space producer passes together.
- Prefer a verified engine scene-render/re-entry/multi-view seam over indiscriminate draw duplication.
- OpenXR's `XR_KHR_D3D11_enable` requires the app's D3D11 device to sit on the adapter LUID the runtime names. Prey picks its own
  adapter, but honours `r_overrideDXGIAdapter` (R-052) as an `EnumAdapters1` index inside the real device-creation path (R-025),
  so adapter agreement is reachable natively with no hook. It is an index rather than a LUID, and it is read once at device
  creation, so the mod must translate LUID to index itself and set it before the renderer initialises.
- All engine pointers, vtables, and signatures are build-gated and fail closed.
