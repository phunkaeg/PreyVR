# Weapon-switch alignment and independent HUD sizing

## Result

The reported persistent angular offset is explained by the current rotation
calibration contract. After an equip/reference/tracking change, a previously
calibrated hand is automatically captured again. The new offset preserves the
animated wrist at whatever controller angle exists at that moment. It does not
establish agreement between the barrel and the firing/reticle ray.

Independent reticle and interaction-prompt sizing is a feasible UI change, but
the current mod has no verified independent size controls. It does not require
the larger project of extracting the HUD into an OpenXR composition layer.
The movie subelements and their scale/animation ownership still need to be
identified before implementing a working size control.

Scope: static source/native investigation and an offline C++ reproduction.
No production behavior, DLL, game state, launch or injection was changed.

## Why a weapon switch locks in the wrong angle

Reviewed source: `da1ad4e` (full revision in the evidence directory).

- `CalibrationState::Bind` in `include/preyvr/AnimIk.h:63` invalidates offsets
  when equip generation, reference generation or tracking epoch changes.
  Hands with completed calibration are scheduled for another capture.
- `kSettleFrames=90` counts matching animation callbacks. `Tick()` runs after
  rig identification, before the later drive/gate checks. It is not proof of
  90 distinct XR samples, a completed equip animation, a settled wrist, or
  alignment between the physical controller and native barrel.
- `AnimIkTakeover.cpp:407` captures
  `offset = inverse(controllerModelAtCapture) * animatedWristAtCapture`.
  It then writes `controllerModelNow * offset` as the wrist target.
- `AimTakeover.cpp:233` obtains firing direction independently from the right
  controller's **aim pose**. The IK wrist uses its **grip pose**. The firing
  direction does not consume the captured wrist offset.
- `tests/AnimIkTests.cpp:124` explicitly asserts preservation of the animated
  wrist at calibration. That test verifies the implemented delta-following
  behavior; it does not verify barrel/ray alignment.

At the capture instant the multiplication cancels the controller completely:

```text
newWrist = controller * inverse(controller) * animatedWrist
         = animatedWrist
```

So if the native weapon points ahead while the controller aims 45 degrees to
the side, calibration preserves that disagreement. Controller movement then
rotates the model relative to this captured mismatch. More delay does not fix
the contract. Repeating this process after a recenter/focus transition can
reintroduce the same problem without changing weapons.

The standalone harness compiled the unmodified math and calibration lifecycle
from the pinned source. Its controlled identity-wrist/yaw fixture reports:

```text
existing_calibration: callback_delay=90 capture_error_deg=45.000 returned_to_neutral_error_deg=45.000
proposed_contract: synthetic_asset_pairs=2 noncommuting_pose_root_cases=16 max_barrel_error_deg=0.0000
PASS: source calibration reproduction and synthetic design controls; no shipped fix or headset acceptance
```

This establishes the source mechanism. It does not measure the exact angular
error of a particular Prey weapon or exclude additional downstream animation.

`aim.calibratebarrel` does not repair this rotation error: its `BarrelOffset`
stores a **position vector in grip space**, plus equip generation and validity.
There is no barrel orientation in that record. The older `weapon.rotate` lane
also follows a captured delta and is explicitly excluded while IK mode 2 owns
the rig; enabling another writer is not the correction.

## Reliable pose and aim ownership

Use a single generic solve with authored per-weapon data where assets differ.
An equip event selects/revalidates data and the live owner. It must not treat
the player's current pointing direction as the new zero.

1. **Independent head/world reference.** Fix the reticle-derived shared anchor
   described in [the IK crosstalk report](RE-IK-CROSSTALK-2026-09-09.md).
   Publish a cyclops head anchor and matching tracking/reference epoch upstream
   of reticle feedback and per-eye translation. This is separate from the
   angular recapture defect. Reach mapping also needs the stable reference
   discussed there.
2. **Authored model transforms.** Resolve the equipped weapon's rigid mount,
   wrist/socket relationship, local muzzle position, and full local barrel
   basis. A firing-position point alone cannot establish a direction; one axis
   alone does not determine roll. Prefer a proven authored helper/bind transform.
   Where assets lack one, use validated data keyed by stable weapon/asset
   identity, never a transient pointer or the angle at equip time. The current
   investigation has not recovered those full bases for all Prey weapons.
3. **One aiming policy.** Use the runtime aim pose for pointing and grip pose
   for placement/hand contact, with an explicit conversion between them. Do
   not assume the two orientations are interchangeable. Apply any user trim
   once in a named local frame and share it with the barrel, ray and reticle.
4. **Solve the weapon and wrist together.** With quaternion products mapping
   local frames into parent frames, let `B` map barrel frame into weapon frame
   and `M` map weapon frame into wrist frame. Then:

   ```text
   weaponWorld = aimWorld * inverse(B)
   wristWorld  = weaponWorld * inverse(M)
   wristModel  = inverse(characterWorldRotation) * wristWorld
   ```

   This closes `characterWorldRotation * wristModel * M * B = aimWorld`.
   Position must likewise preserve the authored grip contact/lever arm when
   rotating the weapon. The harness proves the rotation algebra for synthetic
   authored transforms, not that these transforms have been extracted from
   Prey's assets or that no later native writer changes the result.
5. **Use the resulting aim for all consumers.** Publish equip/owner identity,
   tracking/reference epoch, sample time, weapon pose, final muzzle origin,
   direction and validity as one immutable record. Project the same selected
   world aiming point into each eye. If Prey's native firing policy converges
   from the muzzle to a selected point, use that target consistently; account
   for the muzzle after IK reach/clamping. Preserve native spread, recoil,
   projectile trajectories and weapon-specific targeting. A reticle is not
   automatically an exact impact prediction for every projectile.
6. **Maintain one writer and explicit transitions.** Rebind the actual owning
   character/attachment, invalidate stale frame data, and reload the asset
   transforms on equip/load. Recenter refreshes the world mapping, not the
   authored grip geometry. During unequip/reload/tracking loss, use an explicit
   transition/fallback. Blend into the deterministic goal if desired; never
   capture a new zero to conceal a discontinuity. Keep the free left hand on its
   own controller; only an explicit two-hand-grip policy couples it to a weapon.

A native-neutral-frame derivation could avoid some per-asset authoring, but it
must prove the native barrel basis and wrist/mount correspondence at the same
instant. Assuming native wrist orientation equals camera/reticle orientation
merely relocates the current calibration error.

### Acceptance that catches this defect

For each supported weapon, switch away/back while holding the controller at
centre, yaw +/-45, pitch +/-45 and rolled orientations. Return to the same
tracked pose: the resulting weapon transform must be independent of the pose
held during equip. Include A -> B -> A, recenter, focus recovery and reload.
Compare final rendered barrel direction against the authoritative ray in world
space, and reticle centre against the projected target separately. Track the
actual post-IK/render muzzle, not merely the target that was written.

## Reticle and interaction-prompt size

These should be separate user settings. Proposed names such as
`reticle.scale` and `interaction.scale` are **design placeholders, not existing
commands**. Keep a master HUD scale separate if added later.

Scale the visual child around its registration point, leaving its projected
position/interaction target unchanged. For the reticle, decide explicitly
whether the preference shrinks only the central artwork/stroke or the whole
widget: reducing spread-ring radius changes the apparent spread information.
Preserve native weapon styles, charge/lock/hit animations and visibility.

For interaction popups, scale the relevant text, background and button glyph
as a group about a stable anchor. Verify long/localized text, wrapped lines,
hold progress, multiple actions, and controller glyph changes. Distinguish
this group from scanner/objective/enemy markers and other contextual widgets.

### What the current evidence supports

- `ReticleFollow.cpp` dispatches `reticleXOffset` and `reticleYOffset`. The
  canvas and stage controls change coordinate conversion; they are not artwork
  size controls.
- `HudBridge.h` documents **F-017**: changing HUD constraint `bMax` changed
  readback and `ScreenToFlash` but did not change the visible HUD. Do not use
  `hud.fit` as a size fix. Changing only conversion can misalign the reticle.
- The native `DanielleHUD` path has `reticleDisplay`, `reticleDispersion`,
  `interactPrompt`, `interactFormatPrompts`, `interactPromptArray`,
  `interactDisplay` and `interactIconDisplay` names. These identify UI content
  routes, not necessarily display-object paths.
- At VA `0x18159543A` in `ArkPlayerInteraction_Update`, `interactPrompt` is
  dispatched after building text/state arguments. The small function at RVA
  `0x1592FD0` calls `interactDisplay` with zero/one visibility state. Neither
  inspected boundary establishes a size argument; do not pass a guessed scale
  through these calls.
- A defined-string search found `hud_onScreenNearSize`/`hud_onScreenFarSize`.
  Their returned direct xrefs were in the large name-list function
  `0x181727AB0`; no active sizing consumer was established. Their names alone
  are insufficient to recommend them. `MinSizeForCrosshair` is read in the
  `ArkPsiTargetingData::Initialize` path at RVA `0x15B8340`, alongside targeting
  angle, collision radius and particle effects. It is not established as an
  ordinary weapon-reticle artwork setting.
- `GameData.pak` and `Scripts.pak` failed a standard ZIP-format check, consistent
  with the existing project findings. Their movie assets were not decoded by
  this audit. A defined-string search of the DLL is not exhaustive coverage of
  ActionScript or dynamically constructed member paths.

### Bounded implementation route

Recover the actual `DanielleHUD` movie and relevant display-object hierarchy
through a supported asset extraction route or the runtime owner's bounded
movie/member inspection. Trace the known native function names to their movie
implementations to identify the reticle art and interaction group. Prefer an
existing scale function/variable if present; otherwise add an explicit wrapper
transform in the movie or use a target-verified member/display-info API.

Keep engine `Advance`/animation updates once per game frame and apply the scale
at the correct owning thread before render. Cache by movie instance/generation;
reacquire after reload, and preserve original values for disable/restore.
Never repeatedly multiply the already-scaled value. If animation owns a scale,
use a separate wrapper or combine with the fresh authored value so the preference
does not compound or get reset. Verify pixels/bounds at 1.0, a smaller setting,
then restored 1.0, with off-centre reticle and prompt anchor staying fixed.

This can improve the baked-in HUD immediately once the subelement contract is
closed. Independent depth/curvature is a separate composition-layer project.

## Evidence and limits

Native module: Steam `PreyDll.dll`, x64, Windows ABI, base `0x180000000`,
SHA-256 `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
Ghidra import SHA and the disk hash matched. No new native call was implemented.
UI observations above are static leads/decompiled paths, not newly measured
visual behavior. F-017 is attributed to the existing runtime log.

The project graph (3110 nodes, last written 2026-09-08) supplied architecture
leads but predates current changes; source and pinned evidence take precedence.
Ghidra xref tools were discovered/loaded, but the session wrapper did not expose
them after loading. Read-only xrefs used the same host's documented `/mcp/schema`
GET API with explicit `/Prey/PreyDll.dll` selection instead. No security settings
or target selection were changed.

Reproduction commands, source snapshots/excerpts, Ghidra results and the
standalone harness are in `docs/evidence/equip-alignment-hud-2026-09-09/`.
The shipping fix still requires validated authored weapon transforms and native
UI object ownership, followed by runtime and wearer acceptance.
