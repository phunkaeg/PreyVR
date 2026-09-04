# Hypotheses queue

| ID | Question | Why it matters | Cheapest discriminating evidence | State |
| --- | --- | --- | --- | --- |
| H-001 | Does the shipping renderer expose or retain a stereo/multi-view rendering path? | Engine scene re-entry or native multi-view can avoid per-draw replay. | Search `PreyDll.dll` for renderer/stereo strings, interfaces, and call paths; validate with a bounded runtime trace. | **Closed negative for the stock/exposed route.** The image-wide search found zero `r_Stereo*` cvars, no `IHmdDevice`/`HMD` layer, and no CryVR plugin. A live read-only R-026 probe found one orthonormal camera per frame slot rather than a resident eye pair. This justifies a mod-owned per-eye path; it does not rule out every possible scene re-entry or dynamically constructed multi-view seam. |
| H-002 | Which D3D11/DXGI device, swapchain, and Present path own the final desktop frame? | A dynamic-loader string cluster establishes the API family, not the active device or hook seam. | Resolve the loader consumer, then record a read-only runtime Present/device observation. | Reproduced: the live swapchain/device are R-006/R-007 on the R-005 renderer singleton, Present is dispatched from `EndRendererScene`, and the loader consumer R-025 at RVA `0xF50000` is promoted into the 22-landmark gate. Remaining: classify the second consumer at `0xD87710`. |
| H-003 | Where are main camera pose, projection, FoV, and culling inputs constructed? | Required for correct positional 6DoF and peripheral visibility. | Renderer/camera string xrefs and static call-graph anchors, then live validation. | Partial: `ArkPlayerCamera::UpdateView(SViewParams&)` and its native custom-view callback are mapped and live; projection/culling ownership remains. |
| H-004 | Where can gameplay aim be changed without rotating the head/world camera? | This is the critical detached-aim gate that blocked the earlier fholger prototype. | Apply a small, reversible synthetic yaw/pitch offset and prove a native endpoint changes while the camera remains fixed. | Reproduced: a fixed-camera reticle probe changed the cached ray, and the stack-local wrench A0b moved a native wall contact `0.127165` units laterally on the same collider with effectively unchanged depth. |
| H-005 | Which transform owns the rendered first-person arms, held item, and muzzle? | The visual weapon must follow the controller independently of both camera and gameplay aim. | Trace viewmodel/weapon transform construction, then apply a reversible visual-only offset and verify shot impact is unchanged. | Partial/static: Steam `CArkWeapon::AttachToHand` consumes the resolved `IAttachment*` at weapon `+0x2B0` and installs the weapon binding. The exact late-frame transform writer and safe reversible override are still open. **Animation-system reconnaissance 2026-08-23, prompted by a cross-engine lineage tip.** Prey's `PreyDll.dll` implements CryEngine 3's `IAnimationPoseModifier` architecture: 21 modifiers are registered by name, including `AnimationPoseModifier_LimbIk`, `AnimationPoseModifier_Ik2Segments` (the two-bone solver), `_IKTorsoAim`, `_PoseAlignerChain`, `_ConstraintAim` and `_Recoil`. **Directly relevant to hands: `IKLIMB_LEFTHAND` and `IKLIMB_RIGHTHAND` are named limb identifiers, alongside a `CreateIKLimb` entry point and `IKLimbs` / `LimbIK_Definition` / `IK_Definition` CHRPARAMS keys.** PoseAligner is runtime-controllable through `a_poseAligner*` cvars, and `CPoseModifierSetup` is a serialised, data-driven stack. This is a **named, native limb-IK facility** — the same 'override the engine already honours' shape as R-009's custom-view callback, and worth exhausting before considering any bone-transform hook. All strings-only so far: no address, no call site, nothing traced or executed. **Why the older vocabulary was absent, clarified by the peer 2026-08-23:** the FC1 solver belongs to the CryEngine 1 generation, and Arkane's CryEngine is far past it — so the miss was generational, not a naming quirk. The same tip still applies to Dunia-derived targets, which forked at that earlier era. The transferable lesson is that the *shape* carried across the gap while every specific name failed. |
| H-006 | What engine pathway turns the detached aim pose into weapon, use, melee, and interaction actions? | Motion controls should drive native gameplay systems instead of maintaining a parallel simulation. | Correlate camera-independent aim candidates with hitscan/projectile/melee/interaction call paths, then validate each action family at runtime. | Partial/reproduced: changed-melee-impact and changed-use-target both pass using transient copies of the shared detached ray. The native selector committed usable entity `0x1117` instead of keypad `0xFDE0` after a bounded -15-degree edit; projectile runtime proof remains. **Route identified 2026-08-30 (R-070).** Prey did not replace CryEngine's input layer: `CActionMapManager`/`CActionMap` are present with their diagnostic strings intact, `CMouse` runs on DirectInput, the `i_xinput*` and `i_forcefeedback` cvars are registered, and named actions including `attack1`, `firemode`, `reload` and `Crouch` exist in the binary. So the engine's own `IInput::PostInputEvent` feeds the same pipeline the keyboard and gamepad do, and a synthesised action reaches every consumer a real button does — which is exactly the "drive native systems rather than maintain a parallel simulation" property this hypothesis asks for. Still open: `gEnv`'s `pInput` slot and the `PostInputEvent` vtable index, both of which want a live process rather than a guess. The controller-side geometry that would feed it is built and tested in `preyvr::controller`. |

| H-007 | Can Prey's native Looking Glass second-scene path be driven from a second camera? | H-001 is closed only for the stock/exposed stereo route; it explicitly leaves internal scene re-entry open. The committed per-eye path is mod-owned scene re-entry, so a native mechanism would materially reduce its cost. | Write `e_ArkLookingGlass` directly — cvars are plain ints in a heap object, so Prey's missing console is not a blocker. Set mode 3 and look. | **Core question answered positive, 2026-08-15.** With mode 3 the display rendered the exterior scene alone, full-screen, with no apartment interior; mode 0 revealed the level's real geometry. So a native path renders a complete scene from a non-default viewpoint to the full framebuffer, and mode 1 composites both in one frame. **Still open:** the second scene is a different *environment*, not the main world from a second camera, and nothing shows its camera is settable by a mod. That generalisation is the next question. |

| H-008 | When the camera is driven once per eye, which engine consumers cache or observe it, and does that corrupt the detached-aim ray? | FC2VR hit exactly this: while building the LEFT eye, the game's own camera observer overwrote its stored primary-camera reference with LEFT's transient camera, so the RIGHT eye rejected a correctly-restored camera against a contaminated baseline. PreyVR's whole aim lane depends on a cached ray that is recomputed from the camera every frame. | Enumerate per-frame camera consumers and establish which camera each reads. | **Resolved 2026-08-22 — the risk is real and specific.** R-011 `UpdateCachedReticleViewPosAndDir` has exactly one caller, `CArkUIHUD::OnPreRender`, so the cached aim ray is rebuilt during *render preparation*. It unprojects using `ISystem::GetViewCamera()` (R-039), the **global view camera** — not ArkPlayer's own. So setting the system view camera per eye would feed R-011 an eye-specific camera and corrupt ArkPlayer `+0x17D4`/`+0x17E0`, the exact ray the A0b and interaction A0 proofs steer. **Mitigation:** inject at `CRenderView::SetCamera` (R-030) instead, which is downstream of `GetViewCamera()` and leaves R-011 untouched — an option F.E.A.R./FC2 did not have, since they wrote the game camera object directly. If the system view camera must also be set, bracket the eye loop with snapshot/restore and keep `OnPreRender` outside it. Either way ArkPlayer `+0x17D4`/`+0x17E0` is now a required before/after acceptance check. **R-016 closed 2026-08-22:** it reads the *same* `ISystem::GetViewCamera()`, but caches nothing — every write goes to the caller's `SMovementState&`, none to a global or to ArkPlayer. It is a pure on-demand query, so a per-eye camera can only affect a call made while an eye camera is set and nothing survives the restore. Its callers could not be enumerated (pure virtual on `IMovementController`, vtable-dispatched), so the conclusion rests on the absence of caching rather than on call frequency. **CORRECTION 2026-08-22 — the enumeration was far too narrow.** The claim that followed here, that both consumers were traced, was wrong in scope: R-011 and R-016 are the two consumers relevant to the *aim ray*, not the consumers of the view camera. A byte-level sweep for `CALL [reg+0x388]` finds **144 candidate sites in `.text`, of which 84 provably load `gEnv->pSystem` RIP-relative within 48 bytes** — and 84 is a conservative lower bound, since the window is arbitrary and some of the remaining 60 will be `ISystem` too. They span rendering, gameplay, UI and 3D-engine code. So the global view camera is read from dozens of places per frame, not two. This does not change H-008's conclusion; it **strengthens** it, and it is decisive for preferring a seam that never touches `CSystem::m_ViewCamera` at all. **Mechanism pinned 2026-08-22:** the global view camera is not a pointer but the member `CSystem::m_ViewCamera` at `+0x788` (R-040), and `ISystem::SetViewCamera` (R-041) is nothing but `ADD RCX,0x788; JMP CCamera::operator=` — a memberwise copy (R-042) that notifies nothing, invalidates no cache and never contacts the renderer. So the setter itself is provably side-effect-free, and **all** of the contamination risk lives in who *reads* `m_ViewCamera` between a set and its restore. That makes the snapshot/restore fallback sound in principle — `CCamera` copies are bit-exact — and reduces the open question to read ordering against `CArkUIHUD::OnPreRender`, which is a live-timing question, not a structural one. `CRenderView::SetCamera` remains the preferred seam because it avoids the ordering question entirely. The camera's full field layout is now verified (R-048): per-eye asymmetric projection is representable directly via `m_asymL/R/B/T`, though the header warns those shifts are *"not used for culling atm"*, and the cached frustum planes and corner vertices inside the struct are not refreshed by writing `m_Matrix` or `m_fov` directly. **The preferred seam is now fully characterised (R-030):** it copies the camera *by value* into `CRenderView::m_camera` at `+0x11A0` (R-049), so a camera written there provably cannot alias `CSystem::m_ViewCamera` — the contamination H-008 is about is structurally impossible on this path, not merely avoided by ordering. It then derives the render view's frustum tangents from `m_fov`, `m_ProjectionRatio` and all four asymmetry fields, so a per-eye OpenXR projection is expressed by setting four floats on the camera handed in. **Live + Ghidra 2026-08-29 — the consumer question is answered structurally for this path.** `CSystem::Render` (R-058) hands `m_ViewCamera` (R-040) to exactly two places: `p3DEngine` vtable `+0xA8`, and `CreateGeneralPassRenderingInfo`, which stores it at `SRenderingPassInfo+0x18` (R-061). `C3DEngine::UpdateRenderingCamera` (R-057) then copies it **by value** into a local before modifying anything, and `CRenderView::SetCamera` copies by value again. The FC2VR contamination shape requires some observer to retain a *reference* to the transient per-eye camera; along this path none is retained, so it cannot occur here. This narrows H-008 rather than closing it: the 84+ `GetViewCamera` call sites elsewhere in the binary are untouched by this finding and remain the open part. |
| H-009 | Can Prey render the world a second time per frame from a supplied camera — native stereo — rather than replaying draws or patching matrices downstream? | Native stereo gets correct culling, LOD, sky and fog per eye for free, which no downstream approach can buy. A cross-engine playbook reports two independent mods reaching it on D3D9 titles, and says the deciding property is whether the world-render is reachable as a vtable slot rather than inlined in the frame loop. | Static only, and answerable in Ghidra before any renderer code exists: is the world-render a vtable slot; is there a camera-accepting pass constructor; how many direct callers does it have. | **Static preconditions met, 2026-08-22 — viability is open, not established.** Both halves of the primitive exist as callable code. `C3DEngine::RenderWorld` (R-054) is virtual at `IProcess` slot 3 with **zero direct call sites**, so it is dispatched purely through `gEnv->p3DEngine`. `CreateGeneralPassRenderingInfo` (R-055) is a real function taking an **arbitrary `CCamera&`**, with 3 direct callers. The canonical pattern is visible in one of them. Corroborating: the `e_ArkLookingGlass` experiment already showed Prey rendering a *complete* scene from a non-default viewpoint. **What is not established:** nothing has been called or observed executing; render-target and `CRenderView` lifecycle behaviour under a second pass is unknown, and render views are pooled `[2][2]` (R-026), so a second pass may contend for them; `SwitchUsageMode` sequencing is untouched; and cost is unmeasured. Requires a live host. **Gating recommendation received 2026-08-23 from the cross-engine playbook, not yet adopted:** bioshock-trilogy-vr gates its second world pass **deny-by-default on the return RVA of a known gameplay caller**, with a census naming that site as the per-tick dispatcher, plus camera-silent, present-stall, teardown and poison gates — reported as observed refusing a foreign caller live. For Prey this is attractive precisely because `C3DEngine::RenderWorld` (R-054) is virtual with **zero direct call sites** (R-053-style dispatch), so any second pass we drive is indistinguishable from the engine's own by signature alone; who called us is the only thing that separates them. Recorded as a design input for when H-009 moves from preconditions to implementation. **Second-pass hazard class added 2026-08-27 from the cross-engine render-pass atlas, and it is broader than the render-target question this entry already lists.** Any *per-frame mutable state* touched by a pass we now run twice advances twice: SOMAVR hit this in two unrelated effects with identical shape — deferred SSAO advancing a temporal blur phase once per invocation, and tone mapping advancing exposure/white-cut/fade/grain together in one packet — so the second eye silently rendered at a different phase. The fix both times was to capture the pre-update packet and the committed result before eye one, replay that baseline for eye two, then restore eye one's result, so exactly one logical update persists per frame. **The test that classifies a resource is whether it is read before it is written within a frame** — carried state must be duplicated or replayed, intra-frame scratch must not be (duplicating ping-pong buffers costs memory and can itself cause eye-desync). Two further traps: fixing a producer does not move its downstream consumers (BioshockVR's shadow pass kept sampling stale centre depth after the colour pass was corrected), and a fix should be scoped to the narrowest predicate that reproduces rather than to its whole class. None of this is Prey-specific yet — no pass census exists — but it is the shape to expect the moment a second `RenderWorld` runs. **Live 2026-08-29 — one relaxation, two new constraints.** Relaxation: `UpdateRenderingCamera` (R-057) is the *only* caller of `SetCamera`, and is itself called only by `RenderWorld` (R-054), so the deny-by-default return-RVA gate is a single comparison rather than a set. Constraint 1: the R-033 pool holds only 4 of the **16** render views scheduled per frame — 14 are Shadow views living outside it (R-062) — so a second world pass must not assume the pool enumerates everything. Constraint 2: `SetPreviousFrameCamera` (R-064) fires immediately after every `SetCamera` with a different camera, and with `r_MotionBlur = 2` and `r_AntialiasingMode = 3` both live it is load-bearing; a per-eye write that ignores it hands the second eye the wrong reprojection history. The 2 Recursive pool entries are allocated, correctly typed, and never scheduled or given a camera, so they are genuinely free. Viability still open — nothing has been written yet. |

Promote an item to a research-log finding only after the specified evidence is captured.


## H-005 addendum, 2026-09-03 — a console-reachable weapon offset exists in Prey

Found by querying the rebuilt fleet graph, which pointed at FarCry2-vr's
`CURRENT_STATE.md`:

> **VIEWMODEL LEVER — FOUND.** Dunia has a built-in **weapon camera offset**, a
> float3 at **`weapon+0x6C`**, reached from its own `SetWeaponCameraOffsetX/Y/Z`
> console commands. **Moving the weapon needs no render-pass hook and no engine
> call. There is no second viewmodel camera.**

Dunia is CryEngine-1 lineage and Prey is CryEngine 3/4, so the obvious question is
whether Prey inherited it. **It did.** Present in `PreyDll.dll`:

| string | note |
| --- | --- |
| `SetWeaponCameraOffsetX` / `Y` / `Z` | help text: *"Set the offset in X for the First Person camera"* |
| `Game:SetWeaponCameraOffsetX(%%)` | a **script binding**, not only a console command |
| `i_offset_front`, `i_offset_right`, `i_offset_up` | CryEngine's standard viewmodel offset cvars |
| `g_weaponOffsetInput`, `g_weaponOffsetOutput`, `g_weaponOffsetToMannequin`, `g_debugWeaponOffset` | a whole offset family |

### CORRECTION 2026-09-03, before any test was spent on it

**FarCry2-vr already falsified this lever in a headset**, and the entry above
over-claimed by not checking their result first:

> `weapon+0x6C` float3 -- **moves the CAMERA, not the mesh.** Tested in a headset.
> Real, engine-native, per-frame writable -- but it is a *camera* knob.

Prey's own help text says the same thing and I read past it: *"Set the offset in X
for the **First Person camera**"*. `SetWeaponCameraOffset` names the weapon camera,
not the weapon. So this is very likely a camera knob here too, and **not** the
weapon-placement answer.

It is still worth the one cheap test, because a per-frame writable first-person
camera offset is genuinely useful for camera work -- but it should be filed under
camera, and the expectation corrected before it is run.

**Their bone finding matters more, and is also a negative result:**
`GetBoneWorldMatrix` is an attachment *query*, not a skeleton -- 92,298 calls
across 3 distinct (entity, bone) pairs, and no arms entity ever appears. Nine bones
on the weapon, only muzzle and shell-eject queried. *"There is no arm rig on this
path at all"*, and the final arm draw is almost certainly **pre-skinned**, with the
pose work upstream in a system not yet located. They recorded that it *"saved
building a write, a dirty-flag protocol and a composition"* on a dead path.

For H-005 that means the bone route needs the **skinning/pose** path, not a bone
getter -- and their eventual method was *"follow the getter down to its backing
store, because an engine that ships no bone setter leaves direct array writes as
the only mechanism. Not by hunting for a function called SetBone."*

**Why this matters more than the bone route.** H-005 has been scoped around
locating the late-frame weapon transform writer and driving limb IK. If these
offsets are live, weapon *position* is reachable **without a render-pass hook, a
bone API, or an engine call** — the same conclusion FarCry2-vr reached for Dunia.

**FarCry2-vr also deferred its arm rig, and said why:** *"No bone API confirmed
yet. SOMAVR's discipline is passive-probe first, and we cannot yet read a bone
transform, so building a bone lane would be guessing."* A sibling in a closer
engine lineage chose the offset lever over bones deliberately.

### The honest caveat, and it is a real one

**Strings existing is not the same as the lever being live.** F-007 records exactly
this trap on this binary: the search that found no consumer for `g_detachCamera`
also found none for `g_difficultyLevel`, which certainly is consumed. So a
registered name proves registration and nothing else.

Settling it is cheap and does not need a headset: set the offset and see whether
the weapon moves. Note that `SetWeaponCameraOffsetX` is not currently on the
console allowlist, which is fail-closed by design — widening it is a deliberate
decision for the project owner, not a convenience.

### What BioshockVR contributes: an acceptance test worth stealing

`bioshockvr.dll` **implements** a tracked viewmodel pose, gated behind
`WeaponViewmodelAbsoluteGripOrientation=1`, and its test procedure is exactly the
exit criterion M2 needs:

> Equip the weapon, then **hold the controller still while yawing, pitching and
> rolling only the HMD. The weapon should remain controller/room owned.** Then hold
> the HMD still and rotate the controller to confirm natural weapon rotation.

Health signals: `absoluteOrientationApplied=1`, a one-time
`absoluteOrientationBaselineCaptured=1`, and later
`absoluteOrientationReason=controller_basis_applied`.

That is a better exit test than the one in `SIXDOF_ROUTE.md`, because it separates
the two failure directions -- weapon following the head, and weapon not following
the hand -- instead of asking whether it "feels right".


## unitsPerMetre: first evidence, and a better test than "does it feel right"

From SS2VR's `H12` via the fleet graph concept `World and Unit Scaling`. Their
symptom triad for a wrong metres-to-engine scale:

- HMD positional movement feels **too small** unless a position scale is cranked up
- stereo is only comfortable at a **low raw eye separation**
- first-person weapons and nearby objects feel **about 2x too large**

Their resolution was `world_scale = 3.28` -- feet per metre. **SS2VR's engine units
are feet**, and one shared calibration then made leaning amplitude and stereo scale
believable together.

### Why this is evidence for us, weak but real

The second symptom is the one we can already check against a measurement we have.
The wearer swept the eye offset live across 0.045 to 0.085 and chose **0.064 m** as
most comfortable -- a *normal physical IPD*, not a suppressed one. Under SS2VR's
triad a wrong world scale shows up precisely as needing an eye separation far from
the physical value.

So `unitsPerMetre = 1` now has its first supporting observation rather than being
purely assumed. **It is still not measured**, and this does not close it: the sweep
was judged on a static view, and translation is where a scale error actually bites.
But the assumption has moved from unexamined to weakly supported, and by evidence
gathered before anyone was looking for it.

### The M3 test, replaced

`SIXDOF_ROUTE.md` gives M3's exit as leaning producing correct parallax, judged by
eye. SS2VR's framing is sharper and worth adopting:

> **one shared calibration must make leaning amplitude and stereo scale believable
> at the same time.**

If leaning needs one scale and stereo needs another, the scale is wrong -- and that
is a *comparison*, not an aesthetic judgement. It also fails loudly in the case
that matters: a value that happens to look right standing still and wrong the
moment the player moves.


## H-004 CONFIRMED LIVE with a motion controller, 2026-09-04

The gate that stopped the earlier prototype is open. Previously reproduced with a
synthetic probe and a stack-local wrench edit; now driven by a physical Touch
controller through the full path.

| | |
| --- | --- |
| native direction magnitude | **1000** exactly, sampled before every write |
| applied / compose-rejected, measured window | **911 / 0** |
| controllers located | 14,550 left, 15,447 right |
| restore failures | 0 |

**Confirmed by the wearer, all three:**

1. the gloo gun aims **predictably** along the controller
2. the aim **rotates with the mouse yaw** -- it carries with the player's facing
   rather than being left behind on a fixed bearing
3. pointing at an interactable raises the use-prompt where the **controller**
   points

The second is the one that distinguishes a working composition from a coincidence,
and it is what the fix on 2026-09-04 changed.

### What is proven, and what is not

**Proven:** a motion controller owns Prey's gameplay aim. Native consumers respond
-- weapon firing and `ArkPlayerTargetSelector::UpdateCandidates` both act on the
written ray. The write is direction-only, at `ArkPlayer+0x17E0`, after R-011 rebuilds
it and before its consumers read.

**Not proven, and not claimed:**

- The **weapon model does not follow** the controller. It never could from this
  seam -- the mesh is posed by the animation and attachment system, which is H-005
  and still open. An earlier instruction to "watch the weapon move" was asking for
  the one thing this lane cannot do.
- **Not yet tested against head tracking.** BioshockVR's stricter form -- hold the
  controller still, move only the HMD, the weapon must not follow -- cannot be run
  until the view seam is applied, because the head currently drives nothing. It
  remains M2's real exit criterion.
- The applied share over the whole armed period was about **half**, with the
  remainder rejected as no-pose and **zero** rejected on composition. The split
  counter attributes it to controller pose availability rather than the maths, and
  it is worth a look rather than an assumption.

### On the "impossible" claim

The earlier prototype's blocker was taking control of weapon models **and** aim.
Those are now clearly separate: **aim is done**, and weapon models remain open with
a narrower question than before -- FarCry2-vr established for the same engine family
that the weapon camera offset moves the *camera* rather than the mesh, that the bone
getter is an attachment query with no arm rig on it, and that the arm draw is
probably pre-skinned with the pose work upstream.
