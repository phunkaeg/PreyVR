# Prey gameplay mechanics: VR difficulty ranking

## Purpose

Identify which of Prey's mechanics are hardest to bring into VR, so that the ones needing long-lead
design work are started before the engineering lanes reach them. Difficulty here means *design and
architecture risk*, not implementation hours.

## Method and evidence status

Every mechanic below was checked against the shipping build's string table rather than recalled from
play. Quoted identifiers are real strings in `PreyDll.dll` SHA-256
`7D6E322F…B05311A7`, located after the full Ghidra analysis pass.

Two distinct claim types appear here and should not be conflated:

- **Evidence** — a named string or cvar exists in the module. This proves the system is present and
  hints at its shape. It does not prove how the system drives the camera at runtime.
- **Judgment** — the VR difficulty assessment. This is reasoning from the evidence plus general VR
  constraints, and is not yet validated against a running game.

No runtime tracing of any system below has been done. Treat the ranking as a planning instrument to
be revised as lanes reach each system.

## Ranking

| # | Mechanic | Core problem | Tier |
| --- | --- | --- | --- |
| 1 | Zero-G traversal | Engine-owned roll and an arbitrary up-vector | Hardest |
| 2 | Cinematic camera takeover | Forced camera motion, conflicts with pose injection | Hardest |
| 3 | Mimic Matter | Player becomes an object; no body, no scale, no hands | Hardest |
| 4 | Camera shake and head bob | Engine writes view motion the head did not make | Hard |
| 5 | GLOO gun and GLOO climbing | Camera-rate modifier; improvised vertical traversal | Hard |
| 6 | Hacking minigame | Full-screen procedural 2D UI | Moderate |
| 7 | Psychoscope and HUD | Screen-space helmet overlay | Moderate |
| 8 | Leverage carrying | Object held in view space, not hand space | Moderate |
| 9 | Psi powers generally | Targeting, mostly reuses a proven seam | Tractable |
| 10 | Weapons, use, melee | Already proven steerable | Solved in principle |

---

## Tier 1 — start design work now

### 1. Zero-G traversal (GUTS, exterior EVA)

**Evidence.** 77 matching strings. The decisive ones:

```text
RollSpeedZeroG          MoveSpeedZeroG        SprintSpeedZeroG
AimSpeedZeroG           RecoilForceZeroG      zeroGUpAddition
DefaultCameraZeroGOffset   AttackCameraZeroGOffset   DefaultCameraOffsetZeroG
ZeroG_Velocity_LR       ZeroG_Velocity_FB     ZeroG_Angle_Pitch
onEnterZeroG            onExitZeroG           ZeroGTransitionOut
CanBeUsedInZeroG        ShotgunDispersionRateZeroG
```

**Why it is the hardest.** Three compounding problems:

1. `RollSpeedZeroG` means the engine rolls the player's view. Roll that the neck did not perform is
   the single most reliable way to make a VR user ill, and unlike yaw there is no accepted comfort
   mitigation equivalent to snap-turn.
2. `zeroGUpAddition` indicates the up vector is modified rather than fixed. Every VR convention that
   assumes a world up — comfort horizon, snap-turn axis, teleport arc, seated recentre — loses its
   reference.
3. Three separate zero-G camera offsets exist, so the camera system already branches on zero-G
   state. Any pose injection must handle both branches rather than one.

**Direct consequence for existing PreyVR code.** `MakeBoundedYawProbe` in
[`../src/common/AimState.cpp`](../src/common/AimState.cpp) constructs a **Z-up yaw** and bounds it to
15 degrees. That is correct only while world up is meaningful. In zero-G the player basis is
arbitrary, so the detached-aim math the wrench and interaction proofs rest on is not valid there.
This does not invalidate those proofs — both were captured in normal gravity — but it does mean the
aim lane needs a basis-independent formulation before zero-G is playable.

**Scope.** Not optional content. GUTS is a mandatory hub and exterior EVA is required repeatedly.

**Direction to explore.** Lock roll entirely and expose it as a snap or comfort-framed rotation;
derive the movement basis from a controller rather than the head; consider a static cockpit or
vignette reference frame. This is a design decision that should be made before the camera-ownership
lane hardens, because it changes what the pose injection has to support.

### 2. Cinematic camera takeover

**Evidence.**

```text
ArkPlayer:Cinematic     Actor:PlayerCinematicControl    cinematicMode
OnCinematicPlay         OnCinematicStop                 cinematicFlags
CinematicVTOLUpdate     g_STAPCameraAnimation           CinematicMissing
```

**Why it is hard.** A cinematic system that owns the player camera is in direct conflict with HMD
pose injection: two writers, one transform. `CinematicVTOLUpdate` corresponds to the opening
helicopter sequence, so this is not an edge case — it gates the first minutes of the game. Prey also
uses camera-driven sequences for neuromod installation, several deaths, and scripted reveals.

**Why it ranks above Mimic Matter.** It is unavoidable and it arrives first. A player cannot reach
any other mechanic without passing through it.

**Direction to explore.** Decide per-sequence between three policies: let the cinematic drive
position while the head owns rotation; replace the sequence with a fixed comfortable vantage; or
suppress it. `cinematicFlags` suggests per-sequence configuration already exists, which may make a
policy table feasible rather than case-by-case patching.

### 3. Mimic Matter

**Evidence.** The player power is present and distinct from enemy mimicry:

```text
ArkPsiPowerMimicMorphInObject     ArkPsiPowerMimicPhysicalizeObject
ArkPsiPowerMimicUniqueProperties  ArkPsiPowerMimicLevelProperties
ArkPsiPowerMimicEffectsProperties ArkPsiPowerMimicEventSystem
```

(The separate 58-string `breakMimicry*` / `ArkAiTreeMimicryNode` cluster is the *enemy* Mimic AI, a
different system.)

**Why it is hard.** The player becomes a physical object. Three VR invariants break at once: eye
height collapses to object scale, so roomscale tracking is meaningless; there is no body or hand for
the controllers to drive; and the view transform is owned by the morph rather than by locomotion.
Related powers `ArkPsiPowerSmokeForm` and `ArkPsiPowerShift` have the same shape in weaker form.

**Why it ranks below the first two.** It is a Typhon-branch power the player can decline, so it does
not block a first playable build — but it needs a decided answer before the power lane is called
complete.

---

## Tier 2 — hard, but the shape of the solution is known

### 4. Camera shake and head bob

**Evidence.** `ArkCameraShake_Procedural`, `ArkCameraShake_Animated`, `ArkCameraShakeChannel`,
`cameraShake_footstepCameraShake`, `cameraShake_explosionCameraShake`, `repelBlastCameraShakeId`,
`Ark:Camera:TriggerCameraShake`.

**Already observed live.** The R-026 probe measured the camera Z oscillating roughly `0.01` units
around `17.10` while walking. That is footstep shake, and in VR it is view motion the neck did not
produce.

**Note.** The channel system implies shakes can be suppressed selectively rather than all-or-nothing,
which matters because some shakes are gameplay feedback rather than flourish.

### 5. GLOO gun and GLOO climbing

**Evidence.** `ArkWeaponGooGun`, `GooGunCameraSpeedMultiplier`, `GooGunWalkSpeedMultiplier`,
`GooGunZeroGTime`, `ArkGooed`, plus surface classification `climbableHeight`,
`climbableInclineGradient`, `climbableStepRatio`, `gcc_unclimbable`.

**Why it is hard.** `GooGunCameraSpeedMultiplier` slows the camera's turn rate while the gun is
active — a modifier that cannot be applied to a head. Separately, GLOO's signature use is building
improvised climbable geometry, so it turns arbitrary vertical traversal into a core mechanic, which
is a locomotion-comfort problem rather than an aiming one. `ArkGooed` also means the player can be
immobilised, which is a restraint on a body VR cannot restrain.

### 6. Hacking minigame

**Evidence.** `ArkHackingUI`, `HackingLibrary`, `HackingConfig`, `hackingLevel`, and generator
diagnostics including `HackingUI: Obstacle maxWidth must be greater than or equal to minWidth` and
`Hacking distances are two restrictive - unable to find a viable position to place a circle!`.

**Why it is moderate rather than hard.** It is a procedurally generated 2D puzzle in UI space. The
solution shape is well understood — reproject to a world-space panel and drive it with controller
pointing — but the procedural generator works in screen coordinates, so the panel needs a real
coordinate mapping rather than a naive quad.

### 7. Psychoscope and HUD

**Evidence.** `ArkPsychoscopeMod`, `attachmenteffects_PsychoscopeBlurEffect`, `PsychoscopeHideTime`,
`psychoscope3p_open` / `psychoscope3p_close`, `@i_psychoscope`.

**Why it is moderate.** Prey's HUD is diegetically a helmet visor, which is unusually friendly to VR
— it justifies a head-locked layer narratively. The technical work is the usual screen-space
reprojection, and `attachmenteffects_PsychoscopeBlurEffect` is a full-screen effect that will need
per-eye handling.

**Possible lever.** `sys_flash_stereo_maxparallax` survives in this build (see the stereo
reconnaissance capture). Scaleform retains some stereo-3D parallax awareness even though the renderer
stereo layer was stripped. Whether it is still functional is unknown and worth one cheap test before
building UI reprojection from scratch.

### 8. Leverage carrying

**Evidence.** `CarryLeverage1SpeedScale` through `3`, `fLeverage0_ImpulseScale` through `3`,
`pushLeverageMax`, `leverageI` / `leverageII` / `leverageIII`.

**Why it is moderate.** Carried objects are held in view space in the flat game. VR expects
hand-space attachment with physical placement. The impulse scaling is per-level, so the physics
response is already parameterised, which helps.

---

## Tier 3 — tractable on seams already proven

**Psi powers generally.** `ArkPsiPowerTargetingComponentProperties` indicates psi powers share a
targeting component. Since the detached-aim proofs already steer both the melee query
(`ArkWrenchComponent::GetHits`) and the native use selector
(`ArkPlayerTargetSelector::UpdateCandidates`) from a transient ray, psi targeting is likely to fall
out of the same seam. Worth confirming early because it would convert a large roster —
`KineticBlast`, `Lift`, `Cyberkinesis`, `ElectrostaticBurst`, `Hypnosis`, `CreatePhantom`,
`CombatFocus` — from unknown to routine.

One exception: `ArkPsiPowerCombatFocus` is time dilation. A compositor runs on real time regardless
of game time scale, so slow-motion needs its own handling.

**Weapons, use, and melee.** Already demonstrated steerable independently of the camera by the
completed A0b and interaction A0 proofs.

---

## One opportunity, not a problem

`e_ArkLookingGlass` carries this help text:

```text
Looking Glass mode:
Usage: e_ArkLookingGlass [0/1/2/3]
0 disabled (renders normally)
1 use Scene Windows to look into the other scene
2 show only the main scene
3 show only the second scene
```

Prey's Looking Glass screens render **a second scene** through in-world windows. The project's
committed per-eye route is mod-owned scene re-entry, and this is evidence that the renderer already
contains machinery for rendering an additional scene view in the same frame.

This does not contradict the H-001 closure. H-001 established that no *stereo* control surface
survives and that the R-026 per-frame view block carries exactly one camera. A second-scene path for
in-world windows is a different mechanism. But if that machinery is general enough to drive from a
second camera, it could materially reduce the cost of the per-eye route.

**This is the single highest-value follow-up on this page** and is cheap to start: `e_ArkLookingGlass`
is a live cvar with four documented modes, so its behaviour can be observed without writing anything.

---

## Suggested ordering

1. Investigate `e_ArkLookingGlass` — potential large saving on the committed route.
2. Decide the zero-G comfort model, since it constrains camera ownership design.
3. Decide the cinematic policy, since it gates the opening.
4. Reformulate the detached-aim math to be basis-independent, removing the Z-up assumption.
5. Confirm psi targeting rides the proven selector seam.
6. Leave Mimic Matter, hacking UI, and Leverage until their lanes are reached.
