# Hypotheses queue

**H-005C static advance, 2026-09-05:** RenderCHR's near flag is computable at
entry from params `+0x80 & 0x800000` OR character `+0xAC8 & 2`. The camera getter
returns `CSystem+0x788`, but only the ordinary near matrix branch subtracts its
position. With the current near-VP patch, conversion is
`inverse(M) * (P - Ceye + nearDelta)`; cyclops is the simplification only when
the actual deltas match. The conditional keep-head-rotation path skips the
whole camera restore, and RenderFrame's valid flag does not protect concurrent
matrix copies. These must be accounted for in the matched publication.
Prey's 56-byte SInputEvent and native analog movement route are mapped, including
player handlers `0x158FD20/0x158FD80`; active bindings and neutralization remain
live proofs. [H-005C report](RE-H005C-SELECTION-ORIGIN-INPUT-2026-09-05.md),
19 static landmarks and 9 synthetic fixtures; no new runtime acceptance claim.

**H-005B static advance, 2026-09-05:** R-084/R-085/R-087 have since confirmed
the earlier consumer route and controller translation in a headset. The next
static contracts are now located: render object `+0` is the complete model
matrix, with camera-position-relative translation on the near entity route;
native two-bone solve is `0x871CA0`; bone-attachment SetAbs is `0x828D70`.
Before integration, account for the current marker following job dispatch,
both relative/absolute arrays being writable by IK, and attachment bind/current
pose compensation. The main modifier queue's first-entry repetition is also
byte-verified. [H-005B findings and bounded live proofs](RE-H005B-MODEL-FRAME-ARM-CHAIN-2026-09-05.md).
These are static refinements to the accepted hand takeover, not new headset results.

**H-005 static advance, 2026-09-05:** the finished-pose consumer and GPU/software
handoff are located: **`0x82EE10`** accepts a character's absolute joint poses,
builds skinning dual quaternions and publishes them. A private input substitution
is the proposed per-hand seam. Joint candidates 45/72 follow from correcting
R-077's RSI byte-offset interpretation; mesh ownership still needs live proof.
[Exact contract, register corrections and next experiment](RE-H005-SKINNING-CONSUMER-2026-09-05.md).
The historical H-005 producer findings below do not supersede this consumer route.

| ID | Question | Why it matters | Cheapest discriminating evidence | State |
| --- | --- | --- | --- | --- |
| H-001 | Does the shipping renderer expose or retain a stereo/multi-view rendering path? | Engine scene re-entry or native multi-view can avoid per-draw replay. | Search `PreyDll.dll` for renderer/stereo strings, interfaces, and call paths; validate with a bounded runtime trace. | **Closed negative for the stock/exposed route.** The image-wide search found zero `r_Stereo*` cvars, no `IHmdDevice`/`HMD` layer, and no CryVR plugin. A live read-only R-026 probe found one orthonormal camera per frame slot rather than a resident eye pair. This justifies a mod-owned per-eye path; it does not rule out every possible scene re-entry or dynamically constructed multi-view seam. |
| H-002 | Which D3D11/DXGI device, swapchain, and Present path own the final desktop frame? | A dynamic-loader string cluster establishes the API family, not the active device or hook seam. | Resolve the loader consumer, then record a read-only runtime Present/device observation. | Reproduced: the live swapchain/device are R-006/R-007 on the R-005 renderer singleton, Present is dispatched from `EndRendererScene`, and the loader consumer R-025 at RVA `0xF50000` is promoted into the 22-landmark gate. Remaining: classify the second consumer at `0xD87710`. |
| H-003 | Where are main camera pose, projection, FoV, and culling inputs constructed? | Required for correct positional 6DoF and peripheral visibility. | Renderer/camera string xrefs and static call-graph anchors, then live validation. | Partial: `ArkPlayerCamera::UpdateView(SViewParams&)` and its native custom-view callback are mapped and live; projection/culling ownership remains. |
| H-004 | Where can gameplay aim be changed without rotating the head/world camera? | This is the critical detached-aim gate that blocked the earlier fholger prototype. | Apply a small, reversible synthetic yaw/pitch offset and prove a native endpoint changes while the camera remains fixed. | Reproduced: a fixed-camera reticle probe changed the cached ray, and the stack-local wrench A0b moved a native wall contact `0.127165` units laterally on the same collider with effectively unchanged depth. |
| H-005 | **REPRODUCED 2026-09-05 -- independent per-hand control works.** See R-086. | Which transform owns the rendered first-person arms, held item, and muzzle? | The visual weapon must follow the controller independently of both camera and gameplay aim. | Trace viewmodel/weapon transform construction, then apply a reversible visual-only offset and verify shot impact is unchanged. | Partial/static: Steam `CArkWeapon::AttachToHand` consumes the resolved `IAttachment*` at weapon `+0x2B0` and installs the weapon binding. **Producers named live 2026-09-05 (R-081)** -- 7 write sites caught by hardware watchpoint on a live IK target, two clusters, the main one sharing R-077's region; the stores are float and Vec3 writes bracketed by `addss` blends, and their addressing independently confirms the R-078 QuatT layout. **CORRECTED -- see R-082's correction and R-083.** The original answer below was measured at the wrong offset and names the rarer of two paths; the steady-state last writer is `0x87BBA0`. More importantly, writing there reliably changes nothing, so the QuatT is a per-cycle scratch destination refreshed by a memcpy, not the consumed target. R-078's caveat is retired as a negative. ~~Answered 2026-09-05 (R-082): `0x87BC36`.~~ Reading the capture ring in order rather than folding it shows two cycle shapes, both ending on that site, replicated across two targets on two skeletons. It writes a whole Vec3 in three consecutive float stores, so the last writer is also the complete writer. `0xF97DC1DC` is explained as an out-of-module helper called from `0x87C849`/`0x87C92C`, not a producer. Still open: the containing function is unidentified, the update is multi-threaded, and nothing is hooked. **Animation-system reconnaissance 2026-08-23, prompted by a cross-engine lineage tip.** Prey's `PreyDll.dll` implements CryEngine 3's `IAnimationPoseModifier` architecture: 21 modifiers are registered by name, including `AnimationPoseModifier_LimbIk`, `AnimationPoseModifier_Ik2Segments` (the two-bone solver), `_IKTorsoAim`, `_PoseAlignerChain`, `_ConstraintAim` and `_Recoil`. **Directly relevant to hands: `IKLIMB_LEFTHAND` and `IKLIMB_RIGHTHAND` are named limb identifiers, alongside a `CreateIKLimb` entry point and `IKLimbs` / `LimbIK_Definition` / `IK_Definition` CHRPARAMS keys.** PoseAligner is runtime-controllable through `a_poseAligner*` cvars, and `CPoseModifierSetup` is a serialised, data-driven stack. This is a **named, native limb-IK facility** — the same 'override the engine already honours' shape as R-009's custom-view callback, and worth exhausting before considering any bone-transform hook. All strings-only so far: no address, no call site, nothing traced or executed. **Why the older vocabulary was absent, clarified by the peer 2026-08-23:** the FC1 solver belongs to the CryEngine 1 generation, and Arkane's CryEngine is far past it — so the miss was generational, not a naming quirk. The same tip still applies to Dunia-derived targets, which forked at that earlier era. The transferable lesson is that the *shape* carried across the gap while every specific name failed. |
| H-006 | What engine pathway turns the detached aim pose into weapon, use, melee, and interaction actions? | Motion controls should drive native gameplay systems instead of maintaining a parallel simulation. | Correlate camera-independent aim candidates with hitscan/projectile/melee/interaction call paths, then validate each action family at runtime. | Partial/reproduced: changed-melee-impact and changed-use-target both pass using transient copies of the shared detached ray. The native selector committed usable entity `0x1117` instead of keypad `0xFDE0` after a bounded -15-degree edit; projectile runtime proof remains. **Route identified 2026-08-30 (R-070).** Prey did not replace CryEngine's input layer: `CActionMapManager`/`CActionMap` are present with their diagnostic strings intact, `CMouse` runs on DirectInput, the `i_xinput*` and `i_forcefeedback` cvars are registered, and named actions including `attack1`, `firemode`, `reload` and `Crouch` exist in the binary. So the engine's own `IInput::PostInputEvent` feeds the same pipeline the keyboard and gamepad do, and a synthesised action reaches every consumer a real button does — which is exactly the "drive native systems rather than maintain a parallel simulation" property this hypothesis asks for. Still open: `gEnv`'s `pInput` slot and the `PostInputEvent` vtable index, both of which want a live process rather than a guess. The controller-side geometry that would feed it is built and tested in `preyvr::controller`. **Both open items closed 2026-09-05 (R-080).** `gEnv->pInput` was never actually open -- `SYSTEM_VTABLE.md` already recorded it at `gEnv+0x58` as one of the 24 verified accessors. `PostInputEvent` is vtable **slot 12, RVA `0x9D6D30`**, confirmed by two independent setter/getter pairs on `+0x48` and `+0x78` in a live disassembly rather than counted off a header. Nothing has been called yet: the address is confirmed, the `SInputEvent` layout and calling convention are not. |

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


## H-005 progress, 2026-09-04: the right targets, the wrong end of the frame

A long live session with Frida on a vanilla Prey. Several claims made and
falsified within minutes of each other; what survives is below.

### Established

**These are the targets the viewmodel uses.** An asynchronous write produced a
visible one-frame flicker *in the weapon model*. Not a dead field.

**They are not the third-person skeleton.** The wearer checked their own shadow
during the same write: no flicker. That was the next hypothesis and it was
falsified before it could be proposed.

**They are not static.** R-078's claim was disproved by writing to them -- the
game restores the value within seconds. The original claim came from sampling a
stationary player.

**The count tracks characters on screen.** Forty targets appeared during combat and
six with no enemies nearby: every NPC skeleton carries the same limbs. Earlier
samples were not measuring "the player's four targets" at all.

**Four limb ids exist**, not two: `0x4EC` and `0x7E0` (the rifle and weapon
positions, ~1130 hits each in 4 s) plus `0xB1` and `0xB2` (~5 hits, rare).

### The finding that redirects the work

| write style | result |
| --- | --- |
| asynchronous hammer, random timing, ~72% present | **one-frame flicker** |
| deterministic, at the log site, 10 s, 5732 hits, 80 cm offset | **nothing at all** |

**Writing at the log site never works; writing at random times occasionally does.**

### The instrument for the next step, built 2026-09-04 (R-079)

The conclusion above -- write at the log site and nothing happens -- is what makes
the producer the gate. It is now hunted with hardware breakpoints rather than more
hooks, because neither address involved is a function entry: the log site is
`0xBF4` bytes into its function in a region Ghidra has not analysed, and the
producer's address is the unknown being solved for.

An **execute** watch at `0x878744` reports `r12` (a live target), `rsi` (limb),
`r13` (owner) and `rdi` (skeleton). A **write** watch on `r12+0x10` then reports
the faulting `rip`, which is the producer. No bytes are patched; the only written
state is the one-byte log gate at `0x2257810`, saved and restored.

**Not yet run against the game.** This is the instrument, not a result.

So `0x878744` sits *downstream* of the point where the solver consumes the target.
Our write there is always too late -- the value is recomputed before its next use --
while the async hammer occasionally landed in the window between the producer
writing and the solver reading.

We have been hooking the wrong end of the frame. **The log site is useful for
reading and useless for writing.**

### Next

Find the **producer**: set a hardware write watchpoint on one target address and
catch what writes it. That is one hit, not a hot path, so it is the case where a
debugger beats an interceptor -- the reverse of the earlier lesson, and for the
same reason: match the tool to whether the site is hot.

The wearer's own suggestion -- suppress the idle animations so nothing fights the
write -- remains a good fallback if the producer proves awkward to hook, and is
worth trying via the `ca_` animation cvars.

### Method note

Three separate claims this session were measured under conditions that could not
show the thing being claimed: six captures at an even frame stride, 277 "healthy"
syncs that were all unfocused, and "static" IK targets sampled from a stationary
player. Every one was cheap to falsify once someone tried. **The wearer falsified
two of them faster than the instrumentation did.**


## unitsPerMetre MEASURED, 2026-09-04: Prey's units are metres

No longer an assumption. Measured live on a **vanilla** Prey by sampling
`CSystem::m_ViewCamera`'s world height at 50 Hz while the player crouched and
stood repeatedly in one spot.

| | |
| --- | --- |
| samples | 584 over 12 s |
| eye height range | 488.5 to 489.2 |
| **crouch delta** | **0.700** |
| horizontal travel | 0.09, 0.10 |

**0.7 units for a crouch is a human crouch in metres** -- standing eye height about
1.65 m, crouched about 0.95 m. Centimetres would have given 70. The near-zero
horizontal travel is what makes it a crouch rather than a walk downhill or a
staircase, which is the confound that would otherwise make this number meaningless.

**So `unitsPerMetre = 1`.** The stereo eye offset of 0.064 m, chosen by the wearer
from a live sweep, is therefore a true 64 mm and not a coincidence -- which
retroactively explains why a *physical* IPD felt right when SS2VR's symptom triad
predicts a wrong scale shows up as needing an eye separation far from the physical
value.

### What this unblocks

M3's exit criterion no longer needs a calibration hunt. SS2VR's framing -- one
shared calibration must make leaning amplitude and stereo scale believable together
-- can now be tested against a *known* scale rather than a fitted one, so a
disagreement means something is wrong rather than that the constant needs tuning.

### Why it took this long, which is the useful part

The measurement is twelve seconds of work and needed only a live process. It stayed
open all session because it was recorded as an assumption and never converted into
a question anyone could answer cheaply. **Two earlier notes even described the
evidence for it** -- human-scale IK target heights, and a comfortable physical IPD --
without either being turned into a measurement.

## H-010 -- Prey has a full first-person body rig, and there are two skeletons

**Raised 2026-09-05 from a wearer's observation**, once 6DoF head tracking made it
possible to look down: *"it's been clear from looking around in VR that we HAVE a
full body rig underneath us."*

That matters more than a curiosity. Most flat games give the first-person camera
floating arms and nothing else; a real body makes VRIK-style full-body presence a
possibility rather than a fantasy, and it changes what a hand takeover is *for*.

**The evidence was already in hand and half-read.** The first head-tracking attempt
was reported as *"we could see the entire player body rotating in front of us"*.
That was recorded purely as a composition bug -- which it was -- and the other half
of what it proved was missed: you cannot watch a body rotate unless there is one.

### What the IK capture shows, and what it does not

Every capture finds **two skeletons** under one owner (`ArkPlayer`-side object,
constant across runs), each carrying the same limb pair `0x4EC`/`0x7E0`, with
similar-but-offset positions. That is the shape of a viewmodel rig and a body rig
posed from the same animation, which is the obvious reading -- and it is not yet
tested.

**It cannot be tested with the current capture.** The ring **saturates**: a 14 s
window returns byte-identical structure to a 6 s one -- four rows, exactly 16 hits
each, 72 hits against a 64-slot ring. It fills in a fraction of a second and stops.

So the earlier observation of rare limb ids `0xB1`/`0xB2` (~5 hits against ~1130)
cannot be reproduced or refuted here, and **their absence from these captures is
not evidence of absence**. Same failure shape as the stationary-player sample and
the even-frame capture stride: conditions that cannot show the thing being asked.

### The test that would settle it

Raise the capture cap, or sample by limb id rather than first-come, so rare limbs
are not crowded out by the two common ones. If a limb set -- legs, spine -- belongs
to only one of the two skeletons, that names it as the body and the other as the
viewmodel. That distinction decides which rig a hand takeover should drive, so it
is worth settling before anything is hooked.

### H-010 bump results, 2026-09-05 -- suggestive, not confirmed

Six bump attempts, writing the position of both hand IK targets at ~643 writes/s
for 12 s each, while a wearer watched. Amplitudes from 0.5 m sine to 2.5 m square.

| skeleton | stimulus | reported |
| --- | --- | --- |
| `0x…9f80` | 0.7 m sine | "flicker in the shadow… twice" |
| `0x…9f80` | 2.5 m square | left then right hand flickered, **shadow only**; **gun shadow did not move** |
| `0x…d680` | 0.5/0.7 m sine | nothing |
| `0x…d680` | 2.5 m square | "flicker from the model hands", unprepared |
| `0x…d680` | 2.5 m square, repeat | "left hand flicker away from its position for a frame" |

**Reading, held loosely:** `…d680` drives the visible first-person body and
`…9f80` drives the shadow proxy -- which is the wearer's own theory, arrived at
from noticing the body mesh has no head while its shadow does.

**Why this is not yet a finding.** Every observation is a **single-frame glimpse**
by a human who, by the third run, knew what to expect. That is the weakest evidence
class this project accepts, and it is exactly the shape that produced the
"IK targets are static" error. It is recorded as a lead, and the honest status is
that the mapping is *consistent with* the observations rather than demonstrated by
them.

**A method error worth keeping.** The first `…d680` runs used a third of the
amplitude and a sine rather than a square, so "nothing on d680" was compared
against a 2.5 m square result on `…9f80`. That comparison was invalid and nearly
became the conclusion. Both were re-run under identical stimulus before anything
was written down.

**Blinding was not possible.** The tester can see the tool calls, so the skeleton
under test is always visible to them. Repetition under a stated prediction is the
substitute, and it is weaker.

### The mechanism question is, however, settled

Racing the animator does not work, and now has six negative-to-marginal attempts
behind it rather than one. `0x87BC36` (R-082) overwrites every cycle, so a write
survives only if it lands in the narrow window before consumption -- which at 643
writes/s buys a frame or two in twelve seconds.

**So the next step is not another bump.** Hooking `0x87BC36` and applying the
offset after the engine's final write makes the displacement *hold*, which turns
both open questions -- which mesh each skeleton drives, and whether the weapon
follows the hands -- into things that can simply be looked at.

The most interesting observation of the session points the same way: **the gun's
shadow did not move when the hand shadows did.** If that survives a test that
holds still, the weapon is not parented to the hands, and the bone names agree --
`RHand2RiflePos_IKTarget` drives the hand *to* the rifle. That would make a weapon
takeover the primary lane and hand IK a follower, which is a materially different
design from the one H-005 assumed.

## H-011 -- the near/viewmodel pass receives no per-eye offset

**2026-09-05 static follow-up:** the translation-free near-matrix mechanism is
now located in Prey's binary. `0xFB0B70` explicitly clears view translation,
`0xFB2AC0` builds the near VP at view-info `+0xA0`, and `0xFB57A0` packs it at
constant-buffer payload `+0x90`. The near projection already rescales all four
asymmetry values. See [the exact route and test](RE-H011-NEAR-PASS-STEREO-2026-09-05.md).
The link to the observed weapon draw and the fix remain unconfirmed live.

**Found in a headset 2026-09-05, by a test I would not have thought to run.** With
stereo, head tracking and positional tracking all live, the wearer lined up
overlapping detail between the left and right eye images and reported:

* the **weapon model has no per-eye offset** -- identical in both eyes, zero parallax;
* the **shadows cast by the weapon do** separate correctly per eye.

That pair is the whole diagnosis. World geometry and shadow rendering receive the
per-eye camera offset; the near pass does not.

### Mechanism -- `INFERENCE`, not established

CryEngine draws first-person/near geometry with a `FOB_NEAREST`-style path that
transforms relative to the camera. Geometry rendered that way is **invariant to
camera translation** by construction, so a per-eye offset applied to the camera
moves the world and moves the viewmodel with it -- net zero parallax -- while
shadows, computed in world space, separate normally.

That explains both halves exactly, and **it is still inference.** A string search
for `CameraSpace`, `camera space` and `FOB_NEAREST` found nothing; CryEngine flags
this in code rather than in a string. Do not record it as fact until the near-pass
transform is actually read.

### `i_offset_front` / `_right` / `_up` are vestigial -- closed negative

Registered since the R-076 survey with no consumer ever found. Driven live for the
first time on 2026-09-05 at 0.25 m and then **1.0 m on all three axes**, with a
wearer watching: **no movement at any magnitude.** They join `g_detachCamera`
(R-056) as registered Crysis-era GameSDK leftovers that Prey does not consume.

The lead is closed rather than open, which is worth as much as a positive here --
it was the obvious route to per-eye weapon separation and it does not exist.

### Why this matters more than the hand work

Even a perfect bone-level hand takeover leaves the **weapon flat in stereo**. This
is upstream of H-005: until the near pass receives a per-eye offset, the weapon
cannot sit at a believable depth however its bones are driven.

### The next hunt, and it is well motivated

~~The five `GetCVar` string references were the recommended route.~~ **Superseded 2026-09-05.** Two of them manage the FOV *setting*, not the near render camera. The real consumers hang off the per-frame latch at `renderer+0x95B4` -- **which R-069 had already recorded** -- and are `0xF43D70` (near camera switching), `0xFB4280` (view-info construction) and `0xF054F0` (deferred shadow setup); `0xF7D710` writes the latch. Same lesson as `gEnv->pInput` earlier the same day: check this project's own registry before sending anyone down a static path.


## H-012 -- Prey's release build keeps cvars whose implementations were stripped

**Established 2026-09-05 by four consecutive live negatives.** A cvar existing in
the shipped image -- as a name string, with help text, and accepted by the engine --
says **nothing** about whether anything still consumes it.

| cvar | intended effect | observed |
| --- | --- | --- |
| `g_detachCamera` (R-056) | detach the camera | no consumer found; inert |
| `i_offset_front` / `_right` / `_up` | move the item viewmodel | **nothing at 0.25 m or 1.0 m on all three axes** |
| `ca_DebugADIKTargets` | draw the animation-driven IK targets | **nothing drawn** |
| `ca_useADIKTargets 0` | stop animation driving the IK targets | **hands still animating** |

All four were registered, all four reached the engine, none did anything.

### The measurement trap this creates

Our console bridge reports `lastResult=0` when **our queue submitted the string**,
not when the engine implemented the behaviour. So the success path of an inert
cvar is indistinguishable from the success path of a working one. Every one of the
four above reported success.

**A cvar test is only meaningful with an observable**, and the observable has to be
named before running it. That is why each row above has an "observed" column
recorded by a person looking at the screen rather than a counter.

### What still works, for contrast

`r_DrawNearFoV`, `r_MotionBlur`, `r_AntialiasingMode` and `t_Scale` all demonstrably
work. So this is not "Prey ignores cvars" -- it is specifically the **GameSDK and
animation-debug surface** that is vestigial, while the renderer surface is live.
That split is the useful prior: expect `r_*` to work and `ca_*`/`a_*`/`g_*` to be
dead until shown otherwise.

### Consequence for H-005

The wrench is not available through cvars. Freezing or bypassing Prey's animation
would have to be done by hooking, which is far more invasive than a console
setting -- and that cost should be weighed against simply accepting the pipeline
and hooking the consumer instead.


## H-011 -- CONFIRMED live 2026-09-05: the near pass now has a per-eye viewpoint

The weapon has depth. A wearer's verdict: *"weapon had depth for sure."*

### The protocol, in the order that made it falsifiable

| step | result |
| --- | --- |
| Stereo up, native projection on | `fovDiverged 0`, `lastEye` publishing |
| **Zero-delta control** -- every path armed, displacement zero | 30,796 applied, 0 refused, **pixel-identical** |
| Real delta, half-IPD 32 mm | 1,036,783 applied, 0 refused |
| Sign per eye | `lastDelta` alternating **-32000 / +32000 um** |

The control is what makes this worth recording: hook, snapshot, matrix arithmetic
and restore all ran with a zero displacement and produced **no visible change**,
so the machinery was proven inert before a real offset was layered on it.

### What it fixes

The near view-projection is built from a **translation-free** view (`0xFB0B70`
zeroes the row at `0xFB1037`), so the viewmodel was camera-relative by
construction and could not receive stereo separation from moving the camera --
which is exactly why the wearer saw the weapon identical in both eyes while its
shadows separated correctly. The edit adds the eye displacement to the finished
near VP only, leaving world and shadow paths untouched.

### A separate bug found in the same session, and it is instructive

`r_DrawNearFoV` was set to **104.254** twice, live, and the wearer reported the
weapon FOV looked wrong. It did. The declared-FOV export returns
`[0, tanLeft, tanRight, tanUp]` -- **offset by one field** -- and indices 2 and 3
were read as the vertical pair, mixing a horizontal tangent with a vertical one.
Read correctly the frustum is `120 x 88.507`, precisely the value recorded weeks
earlier and replaced by a "derived" one that was worse.

**The frustum guard did not catch it**, and the reason generalises: that assert
watches the *submission* path, comparing declared against rendered. This number
reached the engine through a **console cvar**. A check only covers the path it is
on, and a second path into the same subsystem is a second place to be wrong.

### Acceptance test passed in a headset, 2026-09-05

Run with the full stack live at once -- stereo, native projection, head rotation,
positional 6DoF, corrected viewmodel FOV -- and a **valid recenter taken in
gameplay** rather than at a menu. Health across 9,472 frames: `fovDiverged 0`,
`posRefused 0`, `nearRefused 0`, `nearNoEye 0`, delta alternating per eye.

Wearer's verdict: **"weapon depth looks correct, nothing else off."**

The second half is the part that was genuinely open. The packer at `0xFB57A0` is
shared -- 1.43 M applies in one session is far more than one per frame, so many
view-info builds pass through it, and the projection-shape guard admits all of
them. The concern was that a secondary view, HUD or scope would gain disparity
too. **Checked by eye across a play session: it did not.**

That is an *absence of observed error*, not a filter, and it is worth saying which
one it is. If a future scene introduces a near-flagged secondary view, this would
bite without warning, and owner or view-type filtering is the fix. But the
acceptance test as specified has passed, and the lane is closed rather than open.


## H-013 — a synthesised SInputEvent cannot reach Prey's front end

**Status:** open, handed to a static investigation
([`HANDOVER-H013-INPUT-CONSUMER.md`](HANDOVER-H013-INPUT-CONSUMER.md)).

**Claim.** The title screen's listener rejects or never sees a synthesised event
for a reason on the *consumer* side, not on ours.

**What supports it.** R-090 decoded the whole path and found it permissive: the
posting gate, the `keyId != -1` test, the blocking query (empty list, returns 0
immediately) and the null `pSymbol` tolerance all pass, so normal listeners are
walked with our event. Five live attempts across two devices, both digital
states, the UI state and `force=true` were each delivered and each ignored.

**What would falsify it.** Finding a check on our side that all five attempts
happened to fail together — the most plausible being a non-null `pSymbol`
requirement inside a listener rather than in `PostInputEvent`.

**The most valuable outcome is arguably a negative:** if the front end reads the
Windows message loop rather than `IInput`, no synthesised `SInputEvent` will ever
drive it, and the menu lane needs a different seam. That also bounds what
locomotion can expect from the same route.

## H-014 — the controller pose does not reach the hand lane under xr-sim

**Status: CLOSED, and it was not a defect. Operator error.** The hand lane was
armed at `hand.mode 1`, which the header defines as
`0 off, 1 passthrough, 2 apply` — passthrough is the deliberate control that
copies the joints and changes nothing. The entire drive block, `ControllerWorld`
included, sits inside `if (mode == 2)`, so it never executed.

Re-run at `hand.mode 2`, same commanded controller move:

```text
handMode=2 handDriveArmed=1 handCalibrated=1 handRightJoint=45
handApplied=560  handSubtree=21  handRightMm=1633  handNoPose=217
```

560 applications and a **21-joint subtree** — the wrist and its descendants, so
fingers travel with the hand rather than being left behind, which is the failure
the header warns about.

**The instrument was the real defect, and it is fixed.** In passthrough,
`handApplied`, `handSubtree`, `handRightMm` and `handNoPose` are all zero *by
design*, which is indistinguishable from mode 2 with a dead controller. I read
`handNoPose=0` as positive evidence that a controller pose had arrived, when the
code that would have incremented it had not run. `report` now carries
`handMode`, `handDriveArmed`, `handCalibrated`, `handRightJoint` and
`handLeftJoint`, and `hand.mode` prints
`mode=1(passthrough_changes_nothing)` rather than a bare `1` that reads like
"on". This is the third instrument in one day to permit a wrong conclusion, after
`xr.start result=1` and `inputPosted`.

**Not visually confirmed.** The `+map Campaign/Research/Lobby` spawn carries no
weapon and draws no first-person arms, so nothing was on screen to check the
counters against. Visual confirmation needs a spawn or save with visible hands.

### Original report, kept for the record

**What was verified on the outside.** xr-sim had the right controller at the
commanded position — `handR pos [0.45, 1.55, -0.25], valid: true,
followsHead: false` — with `sessionState: FOCUSED` and `actionsAttached: true`.
The mod had `preyvr_xr_input result=0 detail=input_created
profile=oculus/touch_controller`, `controller_drive value=1`, a completed
`calibrated`, a selected character and `right_joint value=45`.

**What the mod computed.** `handRightMm=0`, `handApplied=0`, `handSubtree=0`,
across a 20 cm commanded move. `handMatched` climbed to 3216, so the skinning
hook is firing and the character restriction works — `handSkipped=1228` while
restricted, which is the two-instance filter doing its job.

So the displacement is zero at the point it is computed, not refused after the
fact. The break is **between `XrInput`'s located pose and the hand lane's read of
it**, inside our own code. This is a source investigation, not another live run.

Worth checking first: whether `UpdateXrInput` is actually called on the frames
that matter, and whether the hand lane samples the same publication it writes.
`xrFrames` was advancing (39,531), so the frame service itself was alive.

## H-015 — stereo submission may block the in-level prompt from being dismissed

**Status:** open, and deliberately not claimed as established.

Twice, a posted keypress dismissed a gate while stereo submission was off and
did nothing while it was on. The second time, `xr.submit 0` plus four posted keys
cleared the `+map` prompt and gameplay appeared at luma 90.6.

**Two variables moved together** — submission was disabled *and* four different
keys were tried instead of one — so which mattered is not isolated. The clean
experiment is one key, twice, with submission the only difference. Do that before
building anything on it.

It matters because if submission does interfere with the game's UI input, that is
a shipping defect and not a testing inconvenience.


## H-016 — the hand displacement magnitude is ~4.4x the commanded controller move

**Status: CLOSED. Not a bug — a broken test procedure, mine.**

`hand r follow off` in xr-sim does not freeze the controller where it is. It
drops it to the **origin**: with follow off, `state.json` read
`handR pos [0.0000, 0.0000, 0.0000], valid: true`. So the calibration zero of
`(0,0,0)` was correct, the mod was reading the controller correctly, and the
"367 mm move" was really a 1633 mm move from the origin. The 4.45 factor was the
ratio between the move I thought I commanded and the one I actually did.

Re-run with an explicit rest position instead of `follow off`:

```text
calibrated at rest:  zero=349,-199,1300   world=349,-199,1300   handRightMm=0
after a commanded 367 mm move:
                     zero=349,-199,1300   world=249,-449,1550   handRightMm=367
                     handApplied=5294  handSubtree=21
                     handCalibYawMdeg=-89999  handLastYawMdeg=-89999
```

`delta = (-100,-250,250)`, `|delta| = 367.4 mm`. **The lane is exact to the
millimetre.** The yaw hypothesis is dead too: both samples were taken at the same
yaw, so the reference frame was never the problem.

Also verified in passing: the OpenXR to engine conversion. OpenXR
`(0.20, 1.30, -0.35)` became engine `(349, -199, 1300)` mm — the `(x, -z, y)`
relabelling followed by the play-space yaw of -90 degrees. Magnitude is preserved
across the conversion, which is the property that actually matters.

### What this and H-014 have in common

Two consecutive "defects" that were both **operator error**, one after the other:
mode 1 instead of mode 2, then a test fixture that teleported the controller.
Both were found in minutes once the instrument reported its *inputs* rather than
only its output. The code was right both times; the procedure was the weak link,
and the counters could not tell me so until they carried the operands.

### Original report, kept for the record

The right controller was moved from its rest pose `[0.20, 1.30, -0.35]` to
`[0.45, 1.55, -0.25]` — a delta of `(0.25, 0.25, 0.10)`, magnitude **367 mm**.
The lane reported `handRightMm=1633`, a factor of **4.45**.

A rotation cannot change a magnitude, so `WorldDeltaToModel`'s yaw is not it, and
`unitsPerMetre = 1` is corroborated twice. The leading suspect is that the
calibration zero and the live sample were taken through **different camera yaws**:
`ControllerWorld` builds its reference frame from the live camera each call, so if
the body yaw moved between calibrating and reading, the difference of the two
world positions is not a pure controller delta.

Cheap test: calibrate and read with the game camera held still, then repeat while
deliberately yawing the camera between the two. If the error tracks the yaw
change, the fix is to capture the reference frame once at calibration rather than
per sample.

**This matters more than it looks.** A hand that moves 4.4x too far is not a
scale nuisance — it is the difference between a hand that tracks and a hand that
flies off, and it would be read in a headset as "the takeover is broken".

## H-017 — `SetAttAbsoluteDefault` is the wrong seam for continuous tracking

**Status:** strongly indicated by elimination; one confirming run outstanding.

The rotation lane was driven in a live save with the GLOO cannon attached: aim
calibrated, `weapon.rotate 1` armed, a 45 degree controller yaw commanded. Two
captures either side are **pixel-identical** — the weapon did not move.

### Why the write almost certainly happened anyway

`UpdateWeaponMountFromController` can only skip `WriteMount` through paths that
increment one of two counters, and both are accounted for:

| gate | evidence |
|---|---|
| `gRotationDrive` | logged `rotation_drive value=1` |
| `gRotationCalibrated` | logged `rotation_calibrated` |
| `gHaveBaseline` | set in the **same success block** as `gAttachment`, which read `0x2750D7395D0` |
| `attachment != nullptr` | same |
| `ControllerAimRotation` fails | increments `gRotationNoPose`; **`weaponAimUsable=1` measured live** |
| non-finite composition | increments `gRefused`; `weaponRefused=0` |
| `WriteMount` returns false | increments `gRefused`; `weaponRefused=0` |

Every escape either fires a counter that read zero or depends on the aim pose,
which was separately measured usable in a level with no weapon at all. So the
composed rotation reached `SetAttAbsoluteDefault` and the engine did not render
it.

### Which is exactly what R-094 warned

The offset lane behaved the same way: `weaponApplied` went to **1** and stopped,
because `SetAttAbsoluteDefault` writes an attachment *default* rather than a
per-frame transform. R-094 recorded the consequence and this is it arriving —
**a mount written after attach does not move the drawn weapon.** The visible
150 mm shift in R-094 worked because the value was in place when the attachment
was next evaluated, not because the write animates anything.

**Consequence for the product:** a controller cannot own the weapon through this
seam. The rotation lane needs a per-frame transform — the render matrix captured
at `RenderCHR` entry (R-089/R-093) is the obvious candidate, since it is already
hooked, already per-frame, and already knows which instance is the near one.

### The confirming run

With a weapon attached, `weaponRotApplied` should climb continuously while the
weapon stays put. That is now observable: `weaponRotApplied`, `weaponRotNoPose`,
`weaponRotDrive`, `weaponRotCalib`, `weaponBaseline` and `weaponAimUsable` are
all in `report`. If `weaponRotApplied` instead stays at zero, the elimination
above is wrong somewhere and the gate it names should be believed over this note.

## H-019 -- the GLOO cannon's ammo display separates between the eyes

**Observed by a wearer, 2026-09-07. Not diagnosed.** Recorded now because it is a
stereo correctness bug and those are the ones that cannot be seen on a monitor.

> "the display on the gloo gun sometimes renders offset from the gun itself.
> Depending on which way the gun is aiming, the blue ammo display offset in the
> left and right eye OR just renders in the correct position. I wonder if its a
> draw order thing and sometimes its just not syncing with the alternating eye"

### Why the wearer's reading is plausible

This mod produces stereo by **writing `CSystem::m_ViewCamera` per eye** and
letting the engine render twice. Anything that samples the view camera at a
*different point in the frame* than the geometry it annotates will be built
against the wrong eye's camera. A holographic readout carried on the weapon is
exactly that kind of element: it is a Flash/Scaleform surface positioned against
a projection, not a skinned part of the weapon mesh.

That also explains the aim dependence. A separation along the view axis is
invisible; the same error becomes visible as the weapon turns and the offset
rotates into the horizontal, which is the axis stereo disparity is read on. So
"sometimes correct" is what a *constant* frame error looks like from a moving
weapon -- it does not require the fault itself to be intermittent.

It is the FAIL-HAND-037 class again, one layer up: a mismatched camera/geometry
pair that stays plausible until something rotates.

### Two cheap discriminators, both one command in a headset

Neither needs a rebuild, and they separate our stereo from the engine's own
draw order:

* **`near.enable 0`** removes the near-pass eye delta `T(-d)`. If the display
  stops separating, the fault is that delta reaching this element inconsistently
  with the weapon it belongs to.
* **`xr.stereo 0 50`** collapses the IPD to zero. If the offset survives a zero
  IPD it is **not** our per-eye camera at all, and the search moves to the
  engine's own UI projection -- a much more valuable negative, because it would
  mean no camera-side fix can help.

Run the zero-IPD test first: it is the one that can eliminate an entire lane.

## H-020 -- native weapon animation has to be suppressed, and the cvar route is already dead

Raised 2026-09-07: *"we still need to supress some of the native weapon
animations."* Correct, and it needs saying that the obvious approach is
**already eliminated**.

**H-012 is a standing negative: four animation cvars reach the engine and do
nothing.** They exist, they accept values, and no behaviour changes. So this
cannot be solved by finding the right cvar, and a pass spent looking for one is a
pass already spent.

Two seams this project actually owns, in increasing order of reach:

* **The pose view in `HandRigTakeover`.** Both arrays are cloned (`+0x10`
  relative, `+0x18` absolute), so joint-level animation can be overridden per
  joint before the engine consumes the pose. This is the right seam for *bone*
  animation -- reload cycles, finger movement, anything that moves the rig.
* **The `RenderCHR` matrix override (R-095/R-100).** This wins on the whole
  object's placement every frame and the engine cannot reassert it, so it already
  defeats any animation expressed as a transform of the drawn object -- weapon
  sway and bob, if those are carried on the object rather than the bones.

**Which animations matter has not been established**, and that determines which
seam applies. The next step is a wearer naming the specific motions that fight
the controller, not a general suppression switch -- "some of the native
animations" is not yet a target, and building a blanket freeze risks killing
motion that should stay, such as the weapon's own firing action.

## H-021 -- absolute wrist, arm chain, weapon and aim share one native seam

**Static, 2026-09-07:** [full report](RE-H021-ABSOLUTE-WRIST-ARM-CHAIN-WEAPON-AIM-2026-09-07.md).

Prey drives its first-person arms with CryEngine's animation-driven IK:
`Prey_ProcessAnimationDrivenIK` `0x877B50` reads a target joint's absolute pose
and a weight joint's relative X per arm, solves the limb with the native 2BIK
leaf, slerps the wrist to the target rotation and re-propagates the fingers.
The hand rig carries the joints (`r_hand_spine_target` 38, `r_hand_spine_blend`
4, left 39/5), and Prey's own `Prey_ArkHandIKContextUpdate` `0x181801830` feeds
them every frame through an `AnimationPoseModifier_OperatorQueue` on layer 6.

Writing the controller's wrist into that target with weight 1 -- either at the
dispatcher's entry (Route A, one detour, exact model transform in its params) or
through the same OperatorQueue on layer 15 (Route B, engine API, `eOp 2`
converts from world for us) -- is items 1 and 2 together, with no `IKLimb`
fabricated. The weapon follows because it is a `CAttachmentBONE` on the arms rig
sampled on the main thread in `0x8297E0`, after the job; the current skinning
hook is after that, which is why the hand moved and the weapon did not.
Projectile origin follows the weapon (`0x1694BC0` reads the ammo spawn helper).
**Corrected by the integration audit:** moving the reticle ray origin to the
controller does *not* align shots with the barrel -- GLOO normalises reticle
hit minus the native firing origin, so `aim.origin 1` is a controller-point
diagnostic and per-weapon barrel alignment is a separate runtime gate.

**CONFIRMED in a headset, 2026-09-07 (R-104). H-021 is closed.** Every static
prediction was read live before anything was written: two ADIK entries
(`r_hand_spine_target` 38 / `r_hand_spine_blend` 4, left 39/5), `2BIK` limbs
`36/40/41/45` and `35/67/68/72` ending at the hand joints, both gates on, and the
GLOO cannon on bone 47 with its binding spring off. With a fixed goal and no
controller in the loop the hand and weapon rose together; with the controller
driving, the wearer reported *"hand and gun are moving in unity. The arm bends
correctly."*

So the arm chain is solved by the engine from two joint writes, and H-018 Gap 4's
`IKLimb` construction is not needed. Two defects the same session exposed are
fixed and awaiting retest: the hand yawed with the headset (the view camera
carries head tracking, so a head-relative offset needs the *body* yaw), and the
hand flickered between goal and animation (other characters' animation jobs won
the IK state lock about one frame in nine).

What H-021 did **not** establish: barrel alignment. A shot still converges from
the weapon's authored muzzle helper toward the reticle ray, so per-weapon
grip-to-barrel rotation is a separate lane with its own runtime gate.

## H-022 -- the weapon model flickers in stereo, occasionally

**Static review correction, 2026-09-08:** Read
[the H-022 eye/frame and pass audit](RE-H022-WEAPON-FLICKER-AND-GHOST-2026-09-08.md)
before treating the historical diagnoses below as conclusions. The near hook
uses `LastRenderedEye()`, which its own header explicitly marks observation-only
because it is a game-thread value. A valid value can belong to a newer frame;
`nearNoEye==0` does not exclude that. Wrong-eye sign produces the reported
outward jump in both eyes in an analytic projection check. Zero near delta also
collapses color/depth/effect disagreement and does not exclude postprocessing.
The remembered-row guard requires pointer equality, so its zero count cannot
exclude copies at other pointers. Static evidence confirms a separate renderer
zero-VP parameter-cache path at Steam RVA `0xF18970`; attribution to the visible
ghost remains open. No new runtime test or completed fix is claimed.

**Observed by a wearer, 2026-09-08, after the IK lane was working:** *"There is
still a minor stereo flicker of the weapon model occasionally - which seems to be
separate from the screen graphic misalignment (they arent a synchronised
offset)."*

The wearer's separation of the two is the useful part: this is **not** H-019, the
GLOO ammo display. Two faults, not one.

### Eliminated, by measurement rather than argument

* **A missed eye tag in the near pass.** Over 4 s of normal play: 77,002 near
  draws applied, `nearRefused +0`, `nearNoEye +0` -- a 0.000% miss rate. The near
  pass determines the eye on every draw.
* **The two eyes getting different wrist poses.** `ikMatched +379` against
  `observerFrames +379` in the same window: the ADIK pass runs exactly once per
  rendered frame for the owning rig, so both eyes compose against one pose. A
  per-eye resample would have shown `ikMatched` running ahead of the frame count.

### Not yet tested

* Whether the fault's magnitude scales with the near-pass half-IPD
  (`near.halfipd 20` vs `45`). If it does, it is in the near delta despite the
  eye tag being right; if not, the near delta is not involved at all.
* Whether `near.zero 1` removes it. **This is the test F-010's freeze prevented**
  -- the wearer was looking at a held frame at the moment the delta was zero, so
  that run produced no observation and must be repeated.

### Diagnosed, 2026-09-08, from the wearer's own description

> "the flicker in the left eye flickers the weapon model FURTHER to the left. and
> the flicker in the right eye flickers the weapon model FURTHER to the right.
> Almost like the offset is applying twice occasionally."

~~That is not a mis-tagged eye -- a wrong tag sends the weapon the *wrong* way.~~
**FALSE, and it was load-bearing.** Corrected 2026-09-08 by
[the H-022 static review](RE-H022-WEAPON-FLICKER-AND-GHOST-2026-09-08.md): with
projection proportional to `(vertexX - e) / depth`, using `+h` where `-h` belongs
moves a left-eye point **outward** by `2h/depth`, and the right eye mirrors it.
A wrong eye tag produces an outward jump in *both* eyes, which is precisely the
reported symptom. This wrong inference was used to dismiss eye mis-tagging in the
first minutes and shaped every explanation after it.

`PackViewInfo`'s near view-projection is a **shared buffer**. The hook snapshots
it, offsets it by the eye delta, calls the original and restores. **If the
original re-enters the hook with the same buffer**, the inner call snapshots a
matrix that is already offset and offsets it again: exactly twice the half-IPD,
in the correct direction for each eye, on whichever frames take the nested path.
The hook runs about 177 times per frame, so a rare nested path is entirely
consistent with "occasionally".

Guarded by a thread-local claim: a nested call that finds the buffer already
offset for this eye forwards it untouched, which is *correct* rather than a
compromise -- the matrix is already right for that eye. `nearReentered` counts
them.

### The permanent ghost is the same fault, and it is the better evidence

A second observation the same session, and it fits without needing a second
cause:

> "there is a shimmer of another wrench, like a mirage, to the right of the
> wrench in the right eye, (and conversely to the left of the wrench in the left
> eye) - this is the exact offset that the visible weapon model flickers to
> occasionally."

Under the nesting explanation the arithmetic lands exactly. A pass that *always*
takes the nested path renders at **2x** the half-IPD; the main model normally
renders at **1x**. Their separation is therefore **one delta** -- and the main
model, on the frames it takes the nested path too, jumps to 2x, which is the same
one delta. So a permanent ghost and an occasional jump, both of one delta, are a
single mechanism seen twice, and the wearer's "this is the exact offset" is the
measurement that ties them together.

It also explains why the ghost is *steady* rather than flickering: whatever draws
it takes the nested path on every frame, not occasionally. On a wrench as well as
the GLOO cannon, so it is not weapon-specific.

### Both explanations REFUTED, 2026-09-08, and the refutations narrow it sharply

**Nesting: dead.** `nearReentered` stayed at **0** across 74,213 near draws with
the weapon on screen and the flicker occurring. The hook never re-enters, so the
doubled offset is not one call editing another's buffer. The counter existed to
make this falsifiable and it did exactly that.

**An external effect: also dead.** With `near.zero 1` -- our delta at zero,
stereo still armed -- the wearer reported *"both locations have collapsed to the
same place, so there is no flicker or mirage."* Both artefacts scale with **our**
delta. Had the ghost come from an effect reprojecting with the world
view-projection, it would have remained separated when ours went to zero.

### The observation that named the mechanism

> "the flicker does appear to happen when facing the more complex areas of the
> level. When facing empty space its not flickering, though the offset mirage
> still occurs."

**Load-dependent means concurrency.** A busier scene runs more render jobs in
parallel; the hook runs about 190 times per rendered frame across them. Two
threads on the same shared view-projection race: A snapshots it clean and offsets
it, B snapshots **A's already-offset value** and offsets it again -- twice the
half-IPD, correct direction, frequency rising with scene complexity exactly as
described. The steady mirage is a pass that always runs alongside the main draw,
so it collides every frame regardless of scene.

This also explains the zero reading that refuted the first fix: the guard was
`thread_local`, and a cross-thread collision is structurally invisible to it. The
guard was not merely wrong about the cause -- it could not have detected this one.

**Fixed by idempotence, not by locking.** Serialising would mean holding a lock
across the original call, across real engine rendering, trading a cosmetic
artefact for a frame-rate one. Instead the hook remembers the exact translation
row it last wrote for each buffer and eye; a matrix arriving with that row
already in place is forwarded untouched, which is correct for the racing thread
because the buffer really is offset for this eye. It subsumes the copy theory
too: a copied matrix carries the same row and is caught the same way.
`nearAlreadyOffset` counts it, and **should rise with scene complexity** -- if it
stays at zero while the flicker persists, this is wrong as well.

### The exclusions were over-claimed, and two guards could not see their target

[The H-022 static review](RE-H022-WEAPON-FLICKER-AND-GHOST-2026-09-08.md) shows
the counters below cannot carry the conclusions drawn from them:

* **`nearAlreadyOffset == 0` does not rule out a copied matrix.** The check
  requires *pointer equality*, so a copy living at a different pointer cannot
  match, by construction. It was built to catch copies and was structurally
  incapable of it.
* **`nearReentered == 0` rules out only same-pointer, same-thread nesting**, the
  one condition it tested.
* **Packer invocations are not draw identities.** 99,744 of them prove volume,
  not that every weapon draw was covered; there is no shader, buffer, mask or
  pass attribution behind that number.
* **`ikMatched` equal to the frame count** counts work frequency. It does not
  compare the pose samples the two submitted eyes actually used.

Two guards in a row were aimed at a mechanism they could not detect. The
negatives stand as *observations*; the exclusions built on them do not.

### Fixed in the build, 2026-09-08 (not yet run)

The near pass no longer asks a global which eye is current. It reads the
view-info's own camera at `+0x08` and matches its position against records
published when each eye was built, so the eye and the right axis both come from
**the camera that produced this view**.

`viewInfo+0x08` was verified independently before being relied on: the builder
`0x180FB2AC0` opens with `param_1[1] = param_3`, and `0x180FB1670` calls it
passing `renderView + 0x11A0`. So `+0x08` is the render view's own current
CCamera, which is a by-value copy of what the game thread built -- matching it by
position identifies the eye from the data being rendered, and cannot go stale
because it *is* that data.

Both of the review's points are addressed together: the eye no longer comes from
a mutable game-thread global, and the right axis no longer comes from CSystem's
global camera, which could be a newer head orientation than the view was built
with.

**It fails closed.** A view-info whose camera matches no published eye is
forwarded un-offset and counted in `nearNoProvenance`, rather than falling back
to the global. An un-offset near pass is a visible, smaller error than a
confidently wrong eye -- and this counter, unlike the previous three, reports the
*absence* of information rather than the absence of a mechanism it could not
detect. If it climbs, the eye records are not reaching the render thread; that
is a different fault from the offset being wrong, and it says so.

Whether this is the flicker's cause remains unproven. It is a defect in our own
source either way.

### The defect that survives all of it: eye/frame ownership

`NearViewStereo` chooses its offset from `LastRenderedEye()`, which reads a
mutable **game-thread** global, while the view-info it is packing can belong to a
render frame queued earlier -- and `CameraEditHook.h` says in its own words that
the two threads run about a frame apart. Submission already knows better: it
identifies the finished image through a queued value, not the latest global.

Every tag along that path is individually valid, which is why `nearNoEye`,
`nearRefused`, `nearReentered` and `nearAlreadyOffset` can all read zero while
the wrong eye is used. Scene complexity changes frame overlap and queue depth,
so the load correlation fits this at least as well as it fitted the race.

`CameraRightAxis()` compounds it by reading CSystem's *global* camera, so even a
corrected eye bit can be rotated by a newer head orientation than the view-info
was built with. Eye displacement, orientation, units, frame and view generation
have to travel as one immutable record.

### Third refutation, and an assumption I never checked

`nearAlreadyOffset` stayed at **0** across 99,744 near draws while the wearer
still saw the flicker. So no matrix arrives carrying the row we last wrote: not
nesting, not a copy, and not a cross-thread race. Three mechanisms proposed,
three refuted by their own counters.

**The assumption that survived all three unexamined:** that the main model is at
`1x` and the ghost at `2x`. Every observation fits **main at `0x`, ghost at `1x`**
just as well -- the weapon drawn twice, once through a path this hook never sees
and once through it. `near.zero 1` collapsing them is consistent with either
reading, because it removes the only difference in both.

That reframes the question from *"what applies our delta twice"* to **"which
weapon draws go through PackViewInfo and which do not"**, and it makes the
wearer's original instinct -- "some type of post processing effect like AO" --
the better description: a screen-space pass drawing the weapon with a
view-projection we never intercept would differ by exactly one delta, steadily,
and would touch no counter we have.

**Stop guessing; capture a frame.** RenderDoc lists every draw with its constant
buffers. One capture with the artefact visible answers directly how many times
the weapon is drawn and which view-projection each draw used. The MCP analyses an
existing capture but cannot take one, so this needs a capture from the wearer.
Three counters have now each cost a relaunch; a frame capture costs one keypress
and settles it.

### What survived the refutations, and led here

`ghost = main + exactly one delta`, both produced by our own edit, with no
nesting. The remaining mechanism is a **copy**: our hook edits a matrix, the
engine copies that already-offset matrix into a second view-info, and later in
the same frame the hook is handed the copy and offsets it again. The pointers
differ, so the re-entrancy guard cannot see it, and whatever renders from the
copy sits permanently at 2x -- with the main model occasionally routed through
the same copy, which is the flicker.

**This is a guess until the pointers are read.** `near.lineage 1` then
`near.dump` records the distinct view-info pointers edited per frame and the
translation row *found* on each before editing. A second pointer whose found row
already differs from the first by the half-IPD **is** the copy, named rather than
inferred. If every found row is clean, the copy theory dies too and the next
question is what else consumes the near view-projection between our edit and our
restore.

Scale worth carrying into that read: the hook runs about **190 times per rendered
frame**.

### Worth knowing before chasing it

The weapon is drawn in the near pass with its own half-IPD, separate from the
world stereo. So a weapon-only stereo fault is consistent with the world being
correct, and the near pass is the right place to look first -- but the eye tag,
the obvious suspect there, is already ruled out.

## H-023 -- locomotion through Prey's own analog handlers

**Built 2026-09-08, not yet run.** The last of the three pillars in the stated
end state -- HMD view, controller aim, stick locomotion -- and the only one that
had never been wired.

### The schema is the fleet's, not invented here

`docs/03-input-and-locomotion.md` in the VR Modding playbook is explicit, and
every point below is its rule rather than a local choice:

* **Puppeteer the engine's movement; do not reimplement it.** The engine already
  knows how to move a player correctly -- collision, gravity, ledges, AI
  awareness -- and a mod that writes positions fights all of it forever. R-070
  established Prey kept CryEngine's input layer, so a synthesised analog event
  reaches the same handlers a real stick does.
* **Smooth locomotion plus snap turn is what actually shipped.** Of six surveyed
  mods, none shipped working teleport: two contain zero occurrences of the word,
  two more have a config entry or action stub with nothing behind it.
* **Radial deadzone, not per-axis.** Per-axis leaves diagonals live while the
  cardinals are dead, and makes diagonal movement sqrt(2) times faster --
  "sprinting only when moving cornerwise". `StickAxis` already did this.
* **The double-driving trap.** Two paths driving one control never look like two
  inputs; they look like movement that is mysteriously too fast, jitter, or a
  deadzone that "doesn't work" -- subjective symptoms that get tuned around
  instead of diagnosed.

### What was built

The analog handlers `0x158FD20` (X) and `0x158FD80` (Y) are hooked, gated on 23
exact bytes each. Their prologue was read this session and confirms R-089's
decode, including something the registry did not record: **RCX is the input
object**, so the engine hands us the pointer and nothing is inferred about where
it lives on the player. The axes are at `+0x5C`/`+0x60` and the cinematic gate at
`+0x94`.

Every handler call is attributed. `InputPost` sets a thread-local flag while
inside a `PostInputEvent` we issued, so a handler firing with it set was driven
by us and one firing without it was driven by the player's real hardware.
**`moveNative` is the playbook's `engineLeaked`**: while this lane is applying,
it is zero or double-driving is happening.

`moveAxisMilli` reads `+0x5C`/`+0x60` back every call, so a posted value that
never lands is visible rather than assumed, and `moveCinematic` shows when the
engine is discarding movement itself -- which distinguishes "our lane failed"
from "the game is in a cutscene".

### Open before it can be judged

* **Not run at all.** `move.mode 1` observes without posting and should be the
  first thing tried: it proves the handlers fire and measures the player's own
  hardware before anything is added.
* **The drain is one event per frame**, deliberately, so a menu press and its
  release cannot collapse. Two axes therefore take two frames, about 22 ms at
  90 Hz. Acceptable for a first wiring; a per-axis latest-wins slot would remove
  it if the lag is felt.
* **Turn is not wired.** The playbook's rule is to turn through the engine's own
  heading channel so mesh, capsule, aim and movement direction cannot disagree.
  `xi_thumbrx` is `0x216` in the PDB-derived enum, and enum values have carried
  twice here -- but that is an oracle, not this build. Verify against the
  XInput symbol registration at `0x9DAA20` before posting it.
