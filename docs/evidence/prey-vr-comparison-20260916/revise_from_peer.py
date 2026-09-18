"""Create a corrected report without changing the frozen first-review artifacts."""
from pathlib import Path

root = Path(__file__).resolve().parents[3]
original = root / 'docs/PREY-VR-COMPARISON-2026-09-16.md'
target = root / 'docs/PREY-VR-COMPARISON-2026-09-16-v2.md'
s = original.read_text(encoding='utf-8')
s = s.replace('# Prey VR implementation comparison and integration plan', '# Prey VR implementation comparison and integration plan — revision 2', 1)
s = s.replace('## Decision', '''This revision supersedes the first review after checking Claude's supplied comparison against the same source trees. The original report and its hashed receipt remain unchanged for provenance. This revision changes the plan, not either mod's implementation. In particular, the FOV defect below is **identified, not fixed in code**.

## Corrections accepted from the peer review

- **Missed P0 defect:** our `SubmitStereoPair` falls back to runtime FOV when the native camera FOV is unavailable; the checking function logs/counts disagreement rather than preventing submission. This contradicts the fail-closed comment. Add a real publication gate before optimization, alongside the existing frame-provenance task. Static code proves the fallback exists, not that it was exercised in a player's session.
- **Understated head-tracking differences:** Jordi's `ApplyHeadLook` extracts yaw and pitch but does not feed tracked roll to `CView_Update_Hook`. His rendered eye offset uses the `vr_ipd` CVar (default 0.064 m), while submission uses runtime eye poses. Configurable does not mean runtime-derived. My original equal head-readiness scores obscured these gaps.
- **Missed HUD-space option:** Jordi has a VIEW-space head-locked quad, whereas our gameplay HUD is re-positioned in LOCAL space using a sampled head pose. Prefer a VIEW-space option for intentionally head-fixed status UI; preserve world locking for inventory and world-projected targeting. Do not simply move the whole captured HUD, including aim reticle, into VIEW space without checking the targeting geometry. No exact latency reduction was measured.
- **Insufficient locomotion detail:** our live move lane shapes and posts raw stick axes; it does not implement the explicit head-to-body rotation Jordi has. Verify the native movement basis, then offer head/body/controller-relative policy explicitly.
- **Missing backlog rows:** add compositor depth submission, floor/seated/standing calibration, settings UX, alpha interpretation, dynamic resolution, per-build relocation validation, crash dumps and CI/document freshness.
- **Melee nuance:** neither has a physical swing-detection/damage system, so the physical-melee score remains zero. Our August 1 native wrench query experiment did move contact by 0.127165 world units; that is valuable prior evidence for the future consumer, not a shipped physical-melee feature.
- **Allocation correction:** move XR haptic output and finger-input plumbing to the P team, which owns the selected action/lifecycle infrastructure. Jordi supplies weapon-event integration. Pose history is needed for velocity-based melee, not a prerequisite for firing haptics or grip-driven finger animation.

## Decision''', 1)
s = s.replace('| Head 6DoF / reference space | 3/4 | 3/3 |', '| Head 6DoF / reference space | 3/4 | 2/3 |')
s = s.replace("J's rigid tracking-to-world mapping is clear. Neither establishes", "J's rigid tracking-to-world hand mapping is clear, but tracked head roll is omitted. Neither establishes")
s = s.replace('| Physical melee / weapon collision | 0/0 | 0/0 | Wrench rendering, attack-button forwarding and aim raycasts are not swept physical melee or weapon-volume collision.', '| Physical melee / weapon collision | 0/0 | 0/0 | P has a historical native directional-contact experiment; neither implements swept physical melee or weapon-volume collision.')
s = s.replace('## Concrete findings that affect the merge', '''Additional feature decisions after cross-review:

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
''', 1)
s = s.replace('    D --> E[Correct AER baseline', '    D --> V[Reject missing or mismatched rendered FOV]\n    V --> E[Correct AER baseline')
s = s.replace('    L --> N[Inventory depth and live-world background work]', '    L --> U[Optional VIEW-space status HUD with separate targeting policy]\n    L --> N[Inventory depth and live-world background work]')
s = s.replace('    F --> G[Evaluate independent eye swapchains and fitted projection]', '    F --> G[Evaluate independent eye swapchains and fitted projection]\n    F --> Z[Identify valid per-eye scene depth before depth submission]')
s = s.replace('    M --> R\n', '    M --> R\n    U --> R\n    Z --> R\n')
s = s.replace('| P0: deterministic image/pose/FOV identity', '| P0: reject missing/mismatched native FOV | **P team** | Owns submission and contract tests. **High** | Unknown FOV never labels new world pixels with runtime FOV; held-pair or zero-layer fallback is explicit and tested. |\n| P0: deterministic image/pose/FOV identity', 1)
s = s.replace('| P2: event-driven haptics and physical interactions | **Jordi** combat events; P team XR output/lifecycle | Closest hooks are J weapon consumers; neither has a found haptic pipeline. **Low/medium** |', '| P2: event-driven haptics and finger-input plumbing | **P team** XR/actions/rig lead; Jordi combat events | Selected host already owns actions, lifecycle and rig; donor supplies event seams. **Medium** |')
s = s.replace('Physical reloads, object grabbing/throwing, ladder gestures and teleport locomotion', '''Additional shared gaps omitted from revision 1:

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

Physical reloads, object grabbing/throwing, ladder gestures and teleport locomotion''', 1)
s = s.replace('2. **Fix/prove stereo identity and coverage.**', '2. **Fix the missing-FOV fallback and prove stereo identity and coverage.**')
s += '''

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
'''
s = s.replace('</D:/', '<D:/')
target.write_text(s, encoding='utf-8')
print(target)
