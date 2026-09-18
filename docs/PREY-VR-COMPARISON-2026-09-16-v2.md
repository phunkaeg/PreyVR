# Prey VR implementation comparison and integration plan — revision 2

Date: 2026-09-16. Read-only source review; no game launch, deployment, or new headset test. Recommendations are for a combined implementation, not a claim that either current build is complete.

This revision supersedes the first review after checking Claude's supplied comparison against the same source trees. The original report and its hashed receipt remain unchanged for provenance. This revision changes the plan, not either mod's implementation. In particular, the FOV defect below is **identified, not fixed in code**.

## Corrections accepted from the peer review

- **Missed P0 defect:** our `SubmitStereoPair` falls back to runtime FOV when the native camera FOV is unavailable; the checking function logs/counts disagreement rather than preventing submission. This contradicts the fail-closed comment. Add a real publication gate before optimization, alongside the existing frame-provenance task. Static code proves the fallback exists, not that it was exercised in a player's session.
- **Understated head-tracking differences:** Jordi's `ApplyHeadLook` extracts yaw and pitch but does not feed tracked roll to `CView_Update_Hook`. His rendered eye offset uses the `vr_ipd` CVar (default 0.064 m), while submission uses runtime eye poses. Configurable does not mean runtime-derived. My original equal head-readiness scores obscured these gaps.
- **Missed HUD-space option:** Jordi has a VIEW-space head-locked quad, whereas our gameplay HUD is re-positioned in LOCAL space using a sampled head pose. Prefer a VIEW-space option for intentionally head-fixed status UI; preserve world locking for inventory and world-projected targeting. Do not simply move the whole captured HUD, including aim reticle, into VIEW space without checking the targeting geometry. No exact latency reduction was measured.
- **Insufficient locomotion detail:** our live move lane shapes and posts raw stick axes; it does not implement the explicit head-to-body rotation Jordi has. Verify the native movement basis, then offer head/body/controller-relative policy explicitly.
- **Missing backlog rows:** add compositor depth submission, floor/seated/standing calibration, settings UX, alpha interpretation, dynamic resolution, per-build relocation validation, crash dumps and CI/document freshness.
- **Melee nuance:** neither has a physical swing-detection/damage system, so the physical-melee score remains zero. Our August 1 native wrench query experiment did move contact by 0.127165 world units; that is valuable prior evidence for the future consumer, not a shipped physical-melee feature.
- **Allocation correction:** move XR haptic output and finger-input plumbing to the P team, which owns the selected action/lifecycle infrastructure. Jordi supplies weapon-event integration. Pose history is needed for velocity-based melee, not a prerequisite for firing haptics or grip-driven finger animation.

## Decision

**Use phunkaeg/PreyVR as the integration base. Retain its original arm/weapon rig, coherent gameplay pose sampling, two-handed solver, isolated HUD/PDA capture, controller-ray UI, and launch/diagnostic framework. Adapt Jordi's locomotion corrections, snap-turn policy, physical-crouch state machine, native focus-wheel selection, psi targeting seams, and weapon-specific consumer knowledge.**

Do not combine the DLLs or install both hook stacks. Establish one owner for camera, input, rendering, and each gameplay consumer. Neither implementation supplies a finished full-rate stereo renderer, physical melee, or a fully spatial inventory with correct internal head-motion parallax.

The highest-value contribution from Jordi's code is gameplay integration and the engine consumers it identifies. The highest-value contribution from ours is the rig/UI infrastructure and testable contracts. Jordi's independent-eye swapchains and symmetric coverage projection are promising performance techniques, but require corrections and measurement before adoption.

## Scope, identity, and attribution

| Label | Inspected checkout | Identity |
|---|---|---|
| **P** | `D:/Dev Debug/PreyVR` | `1ac2d6103d97f1321f991c46f2ccae8bbae6a115` **plus current uncommitted changes**, including September 13 inventory and September 14 two-hand work |
| **J** | `D:/Dev Debug/Other VR Mods/prey-vr` | Clean `master`, `b680f8bcb70d2deb6910c497d38e54d26683f933`; local `origin/stage-6` points to the same commit |

J's root README describes an older flat-screen build. Its latest HANDOFF, actual source, and git history supersede that description. Older remote branch tips were inventoried, not treated as additional current products. P's public 0.5.0 is not the same as the inspected development tree. Source hashes and checkout state are recorded in [the audit manifest](evidence/prey-vr-comparison-20260916/manifest.json).

Git authors are `phunkaeg` for P's committed history and `jordicalsina` for J's. Below, **P team** means the maintainers of our implementation, and **Jordi** means the maintainer represented by that second history. This is a proposed division of work based on demonstrated code, not proof of individual authorship of every line, a ranking of people, or an assignment already communicated to anyone. P includes mixed Codex/Claude-assisted and uncommitted work.

P explicitly targets Steam PreyDll SHA-256 `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`. J's documented installation patches Steam's DLL to Chairloader's EGS-compatible build. J's local dependency/binary bundle is not included here; its exact target binary hash was not independently established. **Offsets, layouts and member-call signatures must be re-proved for the chosen target.** Source-level names are valuable leads, not portable ABI guarantees.

J's README says its project license is TBD. Agree reuse/attribution terms before copying or redistributing its code. This review does not copy implementation code or modify the donor repository. No new download, public upload, or publication was performed.

## Ranking method

Each feature gets **Q/C** scores from 0–5:

- **Q: implementation completeness/correctness readiness**, based on inspected paths and explicit known defects. 0 absent, 1 exploratory, 2 partial/major gap, 3 usable with material limitations, 4 strong scoped implementation, 5 broadly accepted production implementation. These are not measured visual-quality scores.
- **C: code robustness/testability**, considering ownership, lifecycle, explicit error handling, coherent state, test separation and maintainability. 0 absent; 1 fragile prototype; 2 substantial unguarded assumptions; 3 reasonable but incomplete; 4 explicit contracts/tests; 5 broad regression and portability evidence. Tests do not establish target ABI correctness by themselves.
- **Performance** is a source-based cost assessment or **unknown**, not invented FPS. No matched GPU/headset/scene benchmark was run. No weighted overall score: stereo correctness is a gate, not something good menus can average away.

HMD reports in J's documents remain **author-reported** here. P's earlier captures/receipts are scenario-specific historical evidence, not a fresh run of every current file. P's September 14 receipt explicitly records an unresolved baseline rendering band and a simulator per-hand float-action limitation. It does not certify projectile accuracy or headset comfort.

## Feature rankings and selections

| Feature | P Q/C | J Q/C | Performance / quality distinction | Selection |
|---|---:|---:|---|---|
| World stereo | 3/3 | 2/2 | Both normal paths alternate eyes: each eye refreshes every other game render. P retains two images and copies both into an array; J updates one eye swapchain and reuses the other's last released image. J has lower copy-count potential, but documented pairing starvation remains. | **P baseline; J independent-eye storage design after frame-contract work.** |
| Projection / resolution | 3/3 | 3/2 | P declares native rendered tangents and uses a taller launcher backbuffer; runtime-frustum path is opt-in. J fits symmetric coverage with pixel aspect, then crops native eye FOV, potentially spending more pixels on visible content. Neither is a universal headset solution yet. | **Hybrid:** retain P coverage diagnostics; evaluate J coverage fit with corrected crop/FOV math. |
| Full-rate stereo | 1/2 | 1/2 | Both contain experiments and documented render-thread deadlocks. A double render also raises GPU scene cost. | **Neither ready.** Separate research branch; keep working AER fallback. |
| Head 6DoF / reference space | 3/4 | 2/3 | Both map tracked translation and decouple aim. P uses freshness/reference generations; J's rigid tracking-to-world hand mapping is clear, but tracked head roll is omitted. Neither establishes collision-safe room-scale body following. | **P ownership/sampling, with J's rigid-transform formulation as a cross-check.** |
| Original arm + weapon rig | 4/4 | 2/2 | P drives original wrists and authored weapon/barrel alignment. J's main path spawns a separate 1P model entity and hides the rig; source shows no equivalent native animation-state synchronization. Rig CPU cost needs measurement. | **P.** J separate entity only as an explicit fallback for unsupported rigs. |
| Two-handed aim | 3/4 | 0/0 | P has support-region acquisition, release hysteresis, owner resets, shared aim correction and scoped GLOO evidence. J's “two-handed grips” comment rotates a native left offset; it is not a second-controller grip solver. | **P**, then validate every supported long weapon in headset. |
| Shot / muzzle / special weapons | 3/3 | 3/2 | P edits the player's cached native ray and has optional calibrated muzzle origin. J identifies projectile, tracer, muzzle and continuous Q-beam consumers, but uses a common 0.25 m muzzle offset and several global-camera swaps. Q-beam remains incomplete in its own backlog. | **P aim contract + J consumer coverage**, ported with explicit player/weapon ownership. |
| Reticle / targeting feedback | 3/4 | 3/2 | P preserves native reticle dispatch but projects a default 10 m aim point. J raycasts the scene and projects actual hit points, then draws simple markers; it does not preserve Prey's rich reticle semantics. Raycasts add a small, unmeasured cost. | **P native reticles + J scene-query idea**. One shared hit query per aim sample. |
| HUD isolation | 4/4 | 2/2 | P identifies movies and redirects their target, including rebind interception. J uses generic Flash/backbuffer guards and shared-RT experiments; source/docs acknowledge incomplete capture. | **P.** Do not revive broad target-size heuristics as identity proof. |
| Menus / inventory presentation | 4/4 | 2/2 | P has separate PDA swapchain, quad/cylinder presentation, fallback and capture lease. J uses flat-window fallback; optional world-locked full-backbuffer quad duplicates UI according to current source. | **P.** Retain a simple flat fallback, not duplicate presentation. |
| Motion-controller UI interaction | 4/4 | 3/3 | P intersects controller rays with the displayed panel and manages pointer capture. J's main UI path is thumbstick-driven virtual mouse plus trigger, not a laser pointer. | **P primary; J stick cursor as optional accessibility fallback.** |
| Deep inventory stereo | 2/4 | 0/0 | P double-displays the original movie with per-eye clip-space depth adjustment, preserving one outer callback. Adds UI rendering/copy cost. It is not full 6DoF internal geometry. | **P prototype**, not a finished hologram. |
| Locomotion / body-facing | 3/4 | 4/3 | P posts native axes with modal release/neutralization. J additionally compensates engine strafe/backward scales and measures entity travel vs requested heading. That can intentionally change flat-game speed balance. | **J policies/telemetry, P input ownership**; make uniform speed optional. |
| Physical crouch | 1/3 | 3/3 | P translates the camera and exposes crouch input, but no automatic physical stance transition was found. J has threshold/hysteresis, UI gating and native B-toggle fallback; engine camera drop still stacks with real crouch. | **J**, after removing the double camera drop and testing ceilings/stance transitions. |
| Comfort turning / recenter | 3/4 | 4/3 | P's live lane is smooth native turn; pure SnapTurn helper is not live wiring. J has actual snap/smooth selection and hysteresis. Both recenter; J both-grips gesture conflicts with two-hand grip. | **J snap policy + P recenter/input arbitration**. Keep artificial pitch opt-in. |
| Weapon / focus wheel | 3/4 | 4/3 | P opens wheel via right-stick click. J additionally selects native focus-wheel slices directly from stick angle and supports consumable/tab routing. Negligible expected GPU difference. | **P requested click binding + J native selection mechanism**, re-proved on Steam. |
| Left-hand psi powers | 0/0 | 3/2 | J hooks psi update/activation and swaps camera direction to left-hand aim; it retains camera origin. This is not complete left-hand origin/collision semantics. | **J consumer knowledge**, driven from the shared pose/ray contract. |
| Physical melee / weapon collision | 0/0 | 0/0 | P has a historical native directional-contact experiment; neither implements swept physical melee or weapon-volume collision. No integrated sweep/damage ownership system found. | **New work.** P rig lead, Jordi combat-consumer review. |
| Post effects / colour | 3/3 | 3/3 | Both disable temporal AA/motion blur and prefer sRGB in normal VR activation. P has near-pass stereo-specific work; neither proves per-eye AO/SSR/shadow/history isolation across the game. | **P near-pass handling + shared explicit colour/history policy**. Validate rather than copying a blanket format choice. |
| Startup / distribution / recovery | 4/4 | 3/2 | P offers launcher plus F11/F12 lifecycle and target checks. J has Chairloader integration/CVars but documented one-shot HMD discovery and extra setup dependencies. Different deployment tradeoffs; not an FPS issue. | **P user flow**, optional Chairloader adapter if maintaining that target is desired. |
| Testing / diagnostics / maintenance | 4/4 | 3/3 | P has pure CTest contracts and frozen receipts (last recorded run 40/40). J has focused mock-runtime gameplay scripts, useful travel telemetry and direct engine types, but no comparable unit-test targets found in inspected CMake. Both have oversized central modules and stale comments/docs. | **P regression framework + J gameplay scenario/telemetry coverage.** |

All scores are review judgments scoped to these revisions. A 4 for HUD does not establish every PDA screen or HMD has passed. A 0 means no implementation was located across the source inventory and relevant entry paths, not proof no private branch exists elsewhere.

Additional feature decisions after cross-review:

| Feature | P Q/C | J Q/C | Selection and limitation |
|---|---:|---:|---|
| Runtime IPD / eye geometry | 3/4 | 2/3 | P obtains runtime separation and bounds it; J defaults to scalar 64 mm. Keep P, but arbitrary canted/asymmetric eye transforms still need validation beyond a scalar IPD. |
| Intentionally head-fixed status HUD | 3/3 | 3/3 | J VIEW-space placement option with P capture/fit; keep reticle/world targets and world-locked inventory under their own spatial policy. Source advantage, not a measured frame of latency saved. |
| Compositor depth submission | 1/3 | 0/0 | P has extension detection, idle fields and `BuildRange` tests; no connected per-eye depth submission. Build on P, but obtaining correct scene depth is the main missing work. |
| Reproducible headless setup | 3/4 | 3/2 | P has external xr-sim/xr-tape; J has an in-tree mock tied to J action names. Pin/bootstrap a tested runtime and fixtures rather than require wholesale replacement. |
| Human-facing runtime settings | 3/3 | 4/3 | J ImGui/CVar presentation is a useful desktop UX lead; P has native-console access and commands. Build a headset-accessible settings panel using P pointer/UI infrastructure. Neither desktop UI is already that panel. |

## Concrete findings that affect the merge

### 0. Enforce a valid FOV contract instead of a logging-only check

`src/dll/XrSessionHost.cpp:952` falls back to `views[target].fov` when `DeclaredFovFromLiveCamera()` returns empty. `AssertDeclaredMatchesRendered` at line 791 returns without proof on unavailable/nonfinite data and logs divergence without rejecting the image. Therefore the “fails closed” comment is false for this path. The second fallback around line 1586 also exists, but its effect depends on whether projection or panel layers are actually submitted; it is not evidence every menu has wrong depth.

Required behavior: only publish a newly rendered world image with a valid **matching render record**. On missing FOV/identity, use an explicitly valid held-image policy or end with zero world layers; do not attach a wished-for runtime frustum. Test absent camera, nonfinite tangents, bad coverage and transitions. This is separate from, and complementary to, carrying the correct pose/time through the renderer.


### 1. Render provenance must precede performance work

P's `SubmitStereoPair` stores `views[target].pose` after copying a completed backbuffer. Those views were just obtained by `ServiceXrFrame`. The camera was rendered earlier from the gameplay/render pipeline. Its FIFO carries an eye index, not that source pose, FOV, display time, and reference generation together.

J similarly calls `BeginFrame` from its present hook, locates views, then stores those poses with the already-rendered image in `SubmitFrame`. Its pose channels are individual atomics, so an atomic float is not a coherent multi-component pose transaction. Both correctly try to retain the older eye's metadata; neither inspected publication path establishes that the initially attached metadata describes the actual render.

This is a **source-demonstrated provenance gap**, not a measured latency/visual-error magnitude. Implement an immutable render record: game frame ID, render serial, eye, tracking sequence, reference/turn generation, actual engine camera, declared tangents, intended XR pose/time, target dimensions and viewport. Propagate the record with render work; reject unmatched images rather than guessing parity.

P returns unknown on an empty eye FIFO and holds the old pair. J returns its last eye; its HANDOFF explicitly records loading presents without matching RenderEnd events. Both need lifecycle/overflow tests; a constant queue depth alone does not prove image identity.

### 2. J's projection technique is useful; its crop function is not ready to import unchanged

`MatchedCropRect` clamps source coverage and truncates floating boundaries to unsigned integers. Submission still declares the original runtime FOV. The actual integer rectangle therefore differs from the declared angular interval; insufficient source coverage can be silently stretched. Recompute FOV from outward-rounded bounds, or choose projection dimensions that make the intended interval exact. Missing rendered coverage must be an explicit failure/fallback, not a clamp that changes magnification.

Its source comment that the engine “cannot render asymmetric projections” records this implementation's experience, not a proof of a universal CryEngine/Steam limitation. Prefer the cheapest demonstrated correct route; validate near pass, culling and post effects alongside any projection change.

The bounded [arithmetic controls](evidence/prey-vr-comparison-20260916/checks.json) reproduce this horizontal crop formula: an exactly aligned case has zero error; a 1511-pixel case has a subpixel angular discrepancy; an intentionally insufficient source interval silently clamps away 0.3 tangent units while retaining the wider declared interval. This is a Python reproduction of the inspected formula, not execution of the donor DLL or evidence that those exact inputs occurred in a headset.

### 3. Our optional muzzle-origin work is not normal-startup behavior

P initializes `gOriginMode` to 0 (native origin). `VrMode` enables aim and rig alignment but does not call `SetAimOriginFromHand(2)`; the command channel is the located setter. A calibrated muzzle option existing in code is not evidence every player boots into it. Close the consumer/origin contract and fallback policy before enabling it by default. Neither a hand-directed cached ray nor a mesh aligned to that ray alone proves every projectile/beam/impact agrees.

P's reticle convergence defaults to a fixed 10 m. J supplies a useful live hit-query pattern, but its simple marker should not replace Prey's native lock-on, interaction and weapon-specific symbols.

### 4. J's broad weapon overrides need narrower ownership

`SpawnProjectile` checks player ownership plus proximity, but `UpdateLaser_Hook` uses a distance-to-head heuristic without equivalent beam ownership. Nearby non-player lasers or secondary events are counterexamples to treating proximity as identity. `GetReticleInfoForFiring_Hook` and `FireWeapon_Hook` lack the `PlayerWeapon()` equality guard used in other hooks. Whether shared call sites actually exercise NPC cases is a runtime question; the source does not enforce exclusion.

Adapt the identified consumers, not these broad override conditions. Pass a player-owned shot context with weapon generation, attack instance, origin/direction and native spread policy. Distinguish initial shots from GLOO splits, ricochets, reflected beam segments and grenades. Preserve spread instead of replacing every projectile direction with one identical ray.

### 5. UI and comfort must have a single input owner

J's focus-wheel angle selector is more useful than importing its entire button mapping. Preserve the user's requested right-click wheel binding; add selection behavior beneath it. P's recenter chord intentionally avoids consuming a normal two-hand hold. J's both-grips recenter must not replace it.

Keep inventory/pause behavior explicit. J's `vr_pda_pause` hook is a useful seam, but unpausing does not itself restore world rendering behind a PDA or remove the movie's opaque background. Its `UpdatePdaPanel` still sources the full backbuffer, and its own comments warn about duplication. Neither “world keeps running” nor a frozen background is a live stereo world behind a transparent hologram.

### 6. Performance winner is unresolved, but avoidable costs are visible

In the normal AER world path, P performs one hold-image copy plus two array-slice copies per update. J copies only the newly rendered eye into its own swapchain and reuses the other released image. **J wins the static image-copy-count comparison**, not a measured frame-time comparison: it also creates/releases an RTV and clears each updated image. Cache those RTVs and avoid redundant clears for full-coverage copies.

Both render one world eye per game render in the default stereo path. J's projection-fit approach may reduce wasted rasterization at equal angular resolution; P's taller aspect improves coverage but does not eliminate overscan. Benchmark equal useful pixels per degree, graphics settings, scene, runtime and refresh rate, recording GPU scene time, CPU render time, XR wait time, copy time, eye age/skew and p95/p99 frame times. Do not treat reduced XR wait as a GPU speedup. Do not compare the donor's RTX 3060 laptop reports with this machine's 5070 Ti as a code benchmark.

## Fundamental integration flowchart

```mermaid
flowchart TD
    A[Pin revisions and reuse terms] --> B[Choose Steam target first and define verified engine adapter]
    B --> C[One XR session, pose snapshot and input owner]
    C --> D[Render-frame records: eye, camera, FOV, time, target and generation]
    D --> V[Reject missing or mismatched rendered FOV]
    V --> E[Correct AER baseline through menu, loading, recenter and focus transitions]
    E --> F[Projection coverage, near pass, colour and post-effect validation]
    F --> G[Evaluate independent eye swapchains and fitted projection]
    F --> Z[Identify valid per-eye scene depth before depth submission]
    C --> H[Shared gameplay pose and player-owned aim context]
    H --> I[P original rig, authored barrel and two-hand support]
    I --> J[J firing and psi consumers through verified adapter]
    J --> K[One scene query feeds reticle, muzzle obstruction and effects]
    C --> L[P UI capture, inventory lease and ray pointer]
    L --> M[J wheel selection and optional stick cursor]
    L --> U[Optional VIEW-space status HUD with separate targeting policy]
    L --> N[Inventory depth and live-world background work]
    C --> O[J snap, locomotion and crouch policies]
    O --> P[Remove camera double motion and arbitrate input modes]
    K --> Q[Swept weapon collision and physical melee]
    G --> R[Endurance and matched performance tests]
    K --> R
    M --> R
    U --> R
    Z --> R
    N --> R
    P --> R
    Q --> R
    R --> S[Headset acceptance, clean install and release]
    E -. isolated research .-> T[Full-rate stereo with independent per-eye state]
    T -. only after endurance proof .-> R
```

The branches describe dependencies, not authorization to edit or start other developers' work. Full-rate stereo is not required to ship incremental interaction improvements; correct AER identity is required for their visual evaluation.

## Missing or unfinished across both codebases: proposed owners

These are the gaps in the requested VR feature areas, not an exhaustive list of every possible VR enhancement. “Lead” is the closest demonstrated implementation; low-confidence allocations explicitly lack a direct precedent.

| Priority / gap | Proposed lead; supporting owner | Why this lead / confidence | Acceptance condition |
|---|---|---|---|
| P0: reject missing/mismatched native FOV | **P team** | Owns submission and contract tests. **High** | Unknown FOV never labels new world pixels with runtime FOV; held-pair or zero-layer fallback is explicit and tested. |
| P0: deterministic image/pose/FOV identity across threads and transitions | **P team**; Jordi supplies RenderEnd/present scenarios | Existing FrameContract, generation snapshots, eye diagnostics. **High** | Camera-derived record travels with each image; injected missing/duplicate frames cannot relabel an eye; repeated loads/recenter/focus changes preserve identity. |
| P0: one build-specific engine adapter and combined startup | **P team**; Jordi maps Chairloader names/signatures | Target hash/prologue gates and staged launch lifecycle. **High** | Steam ABI proof, unsupported build refusal, no simultaneous hook owners; clean enable/disable/retry. EGS support is a separate adapter. |
| P0: complete all-weapon origin/direction/impact contract | **Jordi**; P team owns shared pose, rig and ABI port | Broadest actual projectile/tracer/beam consumer coverage. **Medium**: current overrides need ownership fixes | Pistol, shotgun spread, GLOO and secondary blobs, Huntress, disruptor, Q-beam/reflections, NPCs all preserve intended behavior while head and hands diverge. |
| P0: robust projection/coverage for arbitrary supported headset FOVs | **P team**; Jordi contributes projection-fit approach | P has numerical frustum/coverage tests; J has efficiency technique. **Medium** | Signed tangents match integer image bounds; canted/asymmetric views and odd dimensions; culling/near effects agree; no visible borders on target HMDs. |
| P1: scene-hit-aware native reticle and interaction targeting | **Jordi** query lead; P team native HUD/selector lead | J has hit raycasts; P has rich reticle dispatch. **High** for combination, not current completion | Ray, muzzle, hit point, native symbol and interact target agree at several depths; blocked muzzle handled separately from sight line. |
| P1: independent-eye swapchain optimization | **Jordi**; P team contract/error tests | Existing one-eye-update implementation. **Medium** | Equal image quality/coverage; lower measured copy cost; safe stale-image reuse, resize, loss and shutdown; no pose relabeling. |
| P1: original rig support for every weapon / equip transition | **P team**; Jordi supplies weapon coverage scenarios | Native rig basis and owner generations, two-hand solver. **High** | Each weapon's grip/barrel/foregrip, reload and recoil remain correct; no inherited offset when switching while held off-axis. |
| P1: physical melee and swept weapon collision | **P team** spatial/rig lead; **Jordi** damage consumer lead | Closest combination is P continuous weapon pose + J native combat hooks. **Medium/low**: neither has physical melee | Sweep weapon volume between time-stamped poses; prevent tunneling/repeated stationary hits; ownership, velocity thresholds, cooldowns, damage/stamina and tracking-loss behavior; haptic contact. |
| P1: full comfort policy, including physical crouch without double drop | **Jordi**; P team reference-space/input integration | Existing live snap policy, physical stance hysteresis and movement telemetry. **High** | Height moves once; stand blocked under ceilings; no snap while UI owns stick; real crouch, button crouch, seated mode and recenter coexist. |
| P1: room-scale body/collision reconciliation | **Jordi**; P team coherent head/hand mapping | Stronger body-follow and movement-FSM experience, but no completed room-scale solver. **Medium** | Physical lean/walk cannot grant through-wall vision/shots; body catch-up doesn't double-translate hands or camera; tested on stairs and moving platforms. |
| P1: complete menu/PDA/wheel input arbitration | **P team**; Jordi native wheel behavior | Existing ray capture, modal gating, owned releases. **High** | Click/drag, tabs, maps, examination, dialogs and wheel selection; no click-through firing or stuck action after tracking/focus loss. |
| P1: full left-hand psi behavior | **Jordi**; P team pose and control ownership | Existing psi update/activation hooks and marker. **High** for seam, **medium** for completion | Correct origin/direction/range per power, no camera leak; left support grip and psi targeting have explicit priority. |
| P1: stereo-correct AO/reflections/near effects and temporal history | **P team**; Jordi supplies AA/colour regression scenarios | Near-view provenance and renderer diagnostics. **Medium** | Per-pass captures at nonzero IPD; no offset mirage, doubled weapons or opposite-eye history; settings restore on exit. |
| P2: live world behind inventory | **P team**; Jordi advises PDA pause seam | P already owns isolated movie/swapchain, prerequisite for composition. **High** | Actual world renders and tracks behind one inventory layer; opacity policy explicit; scene updates are not confused with a frozen snapshot. |
| P2: deep hologram with internal 6DoF parallax and correct pointing | **P team** | Existing matrix/depth investigation and binocular UI prototype. **High** | Head translation changes internal layer projection correctly; paired state and ordering preserved; depth-aware interaction and comfortable depth limits. |
| P2: event-driven haptics and finger-input plumbing | **P team** XR/actions/rig lead; Jordi combat events | Selected host already owns actions, lifecycle and rig; donor supplies event seams. **Medium** | Per-hand fire/contact/selection feedback, cancellation on focus loss, sensible strength controls; no uncontrolled repeated buzz. |
| P2: handedness/accessibility and locomotion vignette | **Jordi** policy; P team UI/runtime implementation | J has comfort controls; neither has a finished vignette/left-primary two-hand path. **Medium/low** | Left/right primary roles consistently swap; seated reach/calibration; configurable comfort mask without obscuring UI; artificial pitch defaults off. |
| Research: full-rate stereo without lockstep deadlocks | **P team** renderer lead; Jordi contributes failed RenderEnd experiment | P has more isolated render probes/contracts; neither has succeeded. **Low/medium** | Same simulation state, two correct eyes, once-only side effects; per-eye histories/resources; long complex-scene/load soak and measured cost. |
| Release gate: shared scenario suite and long-session recovery | **P team**; Jordi gameplay telemetry/scenarios | P receipts/pure contracts; J native traversal/input scenarios. **High** | Frozen builds and evidence, independent controller inputs in simulator, repeated load/equip/menu cycles, clean shutdown, multi-runtime headset acceptance and reproducible package. |

Additional shared gaps omitted from revision 1:

| Work | Proposed lead / rationale | Dependency and acceptance |
|---|---|---|
| Compositor depth layer | **P team**: existing depth math and swapchain contracts | Prove actual scene-depth texture, frame/eye match, near/far/reverse-Z encoding, viewport/MSAA handling and near-weapon compatibility before acquisition/copy/submission. Idle fields do not make this nearly complete. |
| Floor / real-height / seated mode | **Jordi** policy, P reference-space implementation | Enumerate supported spaces; prefer floor space when usable, retain LOCAL calibration fallback. Rebase head, hands, panels and recenter generations together. A blind LOCAL-to-STAGE replacement is not a complete implementation. |
| In-headset settings | **Jordi** settings design, P UI integration | Controller-operable surface, saved settings, safe preview/revert and no modal click-through. Existing ImGui is a desktop lead. |
| Dynamic resolution | **Jordi** pixel-budget policy, P swapchain lifecycle | First implement safe resize/recreation and metadata changes; ours currently refuses a size mismatch. Measure equal angular quality and GPU time; crop alone is not dynamic resolution. |
| Alpha correctness / component HUD policies | **P team** capture ownership | Determine premultiplied/straight alpha from known translucent elements on contrasting backgrounds; avoid double blends; separate reticle and status spatial behavior. |
| Relocatable signatures / new-build support | **P team** binary contracts | Validate callee/layout/consumer ABI after relocation; finding similar bytes does not establish a safe new build. |
| CI / static analysis / formatting / document freshness | **P team**, Jordi contributes gameplay scripts | Reproducible clean checkout and pinned dependencies; separate pure tests from native/headset scenarios. Existing CTest count is configuration-specific. |
| Crash minidumps and recoverable diagnostics | **P team** lifecycle | P's DebugWatch has a vectored exception handler for probes; this is not a complete crash-report/minidump pipeline. Preserve useful crash evidence without recursive failure or UI stalls. |
| Optional lazy-follow status HUD | **Jordi** comfort policy; P UI | Implement explicit deadzone/settling behavior in world/local space as appropriate; VIEW-space hard lock alone cannot provide delayed following. |

Physical reloads, object grabbing/throwing, ladder gestures and teleport locomotion would be additional product scope, not prerequisites inferred from the request. Neither inspected tree establishes complete implementations of them. If selected later: P team leads spatial/rig interaction, Jordi leads game inventory/physics consumers; teleport would need fresh path/collision research, so no evidence-based expert winner yet.

## Recommended first work packages

1. **Freeze two baselines and agree the contracts.** Shared render record, gameplay pose, aim/shot context, UI ownership; resolve reuse terms and supported target. Do not begin with a wholesale renderer swap.
2. **Fix the missing-FOV fallback and prove stereo identity and coverage.** Retain AER; test transitions and repair image metadata provenance. Explicitly close the known baseline band before declaring a combined visual baseline healthy.
3. **Bring over the small, valuable gameplay policies.** Snap turning, corrected physical crouch and native focus-wheel selection; retain P's controls and modal safety.
4. **Close shot and interaction correctness.** Native scene query, per-weapon consumers, muzzle/reticle/effects contract, left-hand psi. Then physical melee and collision have trustworthy geometry.
5. **Optimize and polish.** Independent swapchains and fitted projection with equal-quality benchmarks; inventory world/depth work; end-to-end headset acceptance.

## Source map

P paths are relative to `D:/Dev Debug/PreyVR`; J paths to `D:/Dev Debug/Other VR Mods/prey-vr`. Function names are included because local line numbers will change with integration.

| Area | P source | J source |
|---|---|---|
| Stereo / ownership | `src/dll/CameraEditHook.cpp` (`eyehandoff`, `BuildSyntheticEye`); `src/dll/XrSessionHost.cpp` (`ServiceXrFrame`, `SubmitStereoPair`) | `src/Src/XRRenderHook.cpp` (`PushEye`, `PopEye`, present path); `src/Src/XRSession.cpp` (`BeginFrame`, `SubmitFrame`); `src/Src/XRStereoRender.cpp` (`CSystem_RenderEnd_Hook`) |
| Projection / post effects | `src/common/StereoFrame.cpp`; `src/dll/NearViewStereo.cpp`; `src/dll/NearFovOverride.cpp`; `src/dll/VrMode.cpp` | `src/Src/XRCameraHook.cpp` (`CViewSystem_Update_Hook`); `src/Src/XRSession.cpp` (`MatchedCropRect`); `src/Src/ModMain.cpp` VR startup settings |
| Head / aim / rig | `src/dll/HeadTrackingHook.cpp`; `src/dll/AimTakeover.cpp`; `src/dll/AnimIkTakeover.cpp`; `src/common/WeaponRigAlignment.cpp`; `src/common/TwoHandedAim.cpp` | `src/Src/XRCameraHook.cpp`; `src/Src/XRWeaponRender.cpp` (`Update`); `src/Src/XRWeaponHook.cpp` (`MuzzleShot`, firing hooks) |
| Reticle / queries | `src/dll/ReticleFollow.cpp` (`kDefaultConvergenceMm`, `WriteReticleForCamera`) | `src/Src/ModMain.cpp` (`UpdateAimMarker`); `src/Src/XRWeaponHook.cpp` (`LaserBeamEnd_Hook`) |
| UI / depth | `src/dll/HudLayer.cpp` (`CaptureFlash`, `StereoDisplay`); `src/dll/InventorySwapchain.cpp`; `src/dll/UiPointer.cpp`; `src/common/UiPanel.cpp`; `src/common/InventoryDepth.cpp` | `src/Src/XRHudHook.cpp` (`RedirectGuard`); `src/Src/ModMain.cpp` (`UpdatePdaPanel`); `src/Src/XRSession.cpp` (`EndFrameWithLayers`) |
| Movement / controls | `src/dll/MoveLane.cpp`; `src/dll/XrInput.cpp`; `src/dll/UiPointer.cpp`; `src/dll/VrMode.cpp` | `src/Src/XRGameHooks.cpp` (`GroundMovementInput_Hook`, `GetMaxMovementSpeed_Hook`, psi/PDA hooks); `src/Src/XRGameInput.cpp`; `src/Src/ModMain.cpp` (`UpdateTurn`, `UpdatePhysicalCrouch`, `UpdateWheelPointing`) |
| Evidence / limitations | `receipts/20260914-twohand-native-grip.json`; `docs/TWO-HANDED-AIM-2026-09-14.md`; September 13 inventory/resolution reports; `tests/`; `CMakeLists.txt` | `docs/HANDOFF.md` latest sections; `docs/NEXT-STEPS.md` with stale items checked against source; `scripts/testing/`; `src/CMakeLists.txt`; `src/Src/CMakeLists.txt` |

The P knowledge graph was consulted (5,209 nodes, legacy ID warning), then leads checked in current source. No existing J graph was present in its listed root. Scoped source inspection was used rather than making a full graph rebuild a prerequisite. No FPS, GPU timings, new build success, headset acceptance, or binary ABI equivalence is claimed by this review.

Direct source entry points: [P submission](<D:/Dev Debug/PreyVR/src/dll/XrSessionHost.cpp:905>), [P aim](<D:/Dev Debug/PreyVR/src/dll/AimTakeover.cpp:68>), [P reticle](<D:/Dev Debug/PreyVR/src/dll/ReticleFollow.cpp:67>), [J crop](<D:/Dev Debug/Other VR Mods/prey-vr/src/Src/XRSession.cpp:502>), [J weapon consumers](<D:/Dev Debug/Other VR Mods/prey-vr/src/Src/XRWeaponHook.cpp:134>), [J physical crouch](<D:/Dev Debug/Other VR Mods/prey-vr/src/Src/ModMain.cpp:794>), [J locomotion](<D:/Dev Debug/Other VR Mods/prey-vr/src/Src/XRGameHooks.cpp:28>), [J panel](<D:/Dev Debug/Other VR Mods/prey-vr/src/Src/ModMain.cpp:867>).


## Corrections and qualifications to send back to Claude

The core recommendation agrees: P is the base, with selected J gameplay and presentation policies. The following statements need correction or tighter scope.

1. **The leading FOV finding is correct.** Call it a statically identified defect on the world-submission path, not an observed live failure. The assertion is telemetry only. Both reviews should prioritize this and render-pose provenance together.
2. **Test and resolution figures do not describe the current reviewed configuration.** `ctest --test-dir build/inventory-stereo -C Release -N` lists **40** tests (enumeration only, not a fresh pass). `tools/Start-PreyVR.ps1` defaults to **2016×2160**, not universally 2688×2880. J's **838×905** figure is from the older crop; commit b680f8b changes the projection fit expressly to reclaim that wasted coverage. Current per-eye dimensions depend on runtime FOV, source size and configuration. Raw source/assert counts were not independently reproduced and should not drive readiness rankings.
3. **`stage-6-hands` is not an unmerged branch relative to the reviewed HEAD.** `git merge-base --is-ancestor origin/stage-6-hands HEAD` returns **0**. Its tip is 613135d. Later changes may remove features; compare current code rather than treating that branch as an unseen future implementation. Native focus-wheel selection is present in `ModMain::UpdateWheelPointing` on HEAD. A historical snapshot feature is not a current live-world inventory solution.
4. **Do not make copying mockxr the Phase 0 prerequisite.** P already has working external-runtime workflows, though clean-clone bootstrapping and the documented xr-sim hand-action defect need work. J's mock hardcodes action names such as `grip_l`, `grip_r`, `trigger_r`, and `move`; action-space handedness is inferred from a name suffix. P uses shared `squeeze`, `trigger`, `thumbstick` actions and `subactionPath`. `xrSuggestInteractionProfileBindings` is a no-op in the mock. Unmodified, it is not a drop-in validator for P. Pin and test a supported harness, fixing bindings/subactions and validating independent hands. A mock cannot certify visual quality or GPU performance.
5. **Head-relative movement needs relative yaw, not recenter yaw alone.** Jordi rotates by additive view yaw relative to the body's movement basis. `HeadTrackingReferenceYaw()` is a reference calibration, not the current head-to-body angle. Prove the native basis and use current desired movement-heading minus body-heading, accounting for recenter and comfort turns exactly once.
6. **Snap turn is small work, not zero-cost wiring.** The pure helper is tested, but live edge/release hysteresis, modal ownership, reference-space updates, pose coherence and level/recenter behavior still need integration and validation.
7. **VIEW-space HUD is a useful option, not a universal layer replacement.** Status information, world-aim reticles and world-locked PDA have different requirements. “One frame of latency” was not measured. Lazy-follow behavior requires a different policy from hard head locking.
8. **Depth submission is not nearly finished because fields exist.** Extension detection and `BuildRange` are real scaffolding. Correct per-eye scene-depth capture, encoding, freshness, sample/layout compatibility and matching projection are the difficult missing work. It does not automatically turn AER into full-rate stereo or implement a custom second-eye reprojection renderer.
9. **Failed re-entry experiments do not prove AER and depth reprojection are the only possible architectures.** They show those attempted full-rate paths fail under tested conditions. A different safe render boundary, independent per-eye state or replay design remains research, not an established impossibility.
10. **Startup currently verifies command dispatch, not CVar readback.** `ConsoleBridgeWin32.cpp` invokes `ExecuteString` with deferred execution, then reports `queued_to_engine`. `VrMode` checks that result. It does not read back the final AA/motion-blur value there. Add readback if claiming settings are verified applied.
11. **His hooks are target-specific RVAs through `PreyFunction`, not simply absolute virtual addresses.** His shipped dependency implementation is absent from this checkout, so do not infer every SDK hook-install behavior from missing call-site checks. The portability/ABI risk is real; qualify “wrong build fails open” as lack of demonstrated in-mod validation. Chairloader compatibility checks are separate.
12. **No general 'never modify the game' user prohibition was established in this comparison.** Our chosen route avoids replacing PreyDll; that is a distribution/compatibility advantage and a recommendation, not an invented authorization rule. In-memory hooks modify running game code. Distinguish disk patching from injection.
13. **Physical crouch is not the only height-aware code in either repository.** It is the only automatic tracked-height-to-stance system located; P already uses vertical head position for 6DoF and recentering. Haptic events and grip-driven finger articulation do not require pose history. Keep those separate from swing-velocity melee.
14. **Native melee evidence is narrower than a complete aim consumer proof.** P's August 1 trace shows a +10° stack-local wrench direction edit changed contact by 0.127165 units while persistent ray/camera remained unchanged. This proves the local query responds; it is not a test that every current controller-driven firing/targeting/melee path behaves correctly.
15. **Numerical performance/5-of-5 fidelity ratings are judgments, not measurements.** Both reports are source-only. J's smaller copy count is observable, but actual FPS/latency/comfort, full colour correctness and cross-runtime reliability cannot be awarded as measured results. Twelve model-map tokens include aliases, not twelve distinct weapon implementations.

The peer review is right about the omitted head roll, configurable-vs-runtime IPD, absent live snap wiring, physical crouch, missing depth/haptics/vignette work, useful VIEW-space placement and our stale documentation. It also agrees with the first review's most important architecture choice. Its proposed post-effects ownership differs: this revision keeps P as lead because the existing near-pass implementation and render diagnostics are closer evidence than the quality of a narrative AA explanation; Jordi contributes that analysis and scenarios.

No donor code was copied, no implementation defect was silently patched during this comparison, and no new native/headset success is claimed.
