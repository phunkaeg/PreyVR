# Draw-near particles at the weapon's depth, and the Q-Beam (2026-10-07)

Measured on the headless OpenXR mock (Prey Steam, `PreyDll.dll` 7d6e322f…), **not yet in a
headset**. Evidence: `docs/evidence/near-particles-2026-10-07/` (PNGs are not in git).

## 1. Draw-near particles had twice the weapon's parallax (`near.fx`, default on)

**Symptom.** The Disruptor's charge glow floated in front of the coil, toward the face
(~105 px more crossed disparity than the coil, docs/SHOT-RAY-2026-10-06.md).

**Cause (observed static, then live).** Particle effects authored `DrawNear="true"` are the
broken ones; the same effect's non-DrawNear children (the prong flare and arcs) were correct.
In `StunGun.FullyCharged_00` the coil glow is `MeshGlow_00` (geometry) and `InnerElectrical_00`
(sprites), both DrawNear. The near pass draws camera-space content: the arms and weapon are
posed relative to the camera and the pass's view-projection has no camera translation, so
NearViewStereo gives them parallax by adding the eye offset. A DrawNear particle is simulated
in world space and the engine makes it camera-relative itself, with the **pass camera**, which
in alternating stereo is the eye camera:

- geometry: `CParticle::RenderGeometry` `0x1B13A0`, at `0x1B1A7F`:
  `if (params.bDrawNear /*+0x4C8*/) pos -= context.m_vCamPos /*+0x18*/` (CryEngine 5.1's
  "DrawNear is now rendered in camera space"); `bDrawNear`'s offset is from the type-info table
  (`0x18D173`);
- sprites: written in world space (`CParticle::SetVertices` `0x1B2B90` → `GetRenderMatrix`
  `0x1AF210`, vT = the vertex) and made camera-relative later by the renderer, again with the
  eye camera.

So the particle arrives in camera space already shifted by the eye offset, and NearViewStereo
shifts it once more. `tests/NearParticlesTests.cpp` reproduces it: twice the weapon's
disparity, i.e. seen at half its distance.

**Fix** (`src/dll/NearParticleStereo.cpp`, pure part `include/preyvr/NearParticles.h`): a DrawNear
particle is made camera-relative to the **cyclops** the eye was offset from — the weapon's own
convention — and NearViewStereo then gives it exactly the weapon's parallax.

- geometry: `SParticleVertexContext::Init` `0x1AFF70` (every vertex-context builder copies the pass
  camera into `m_vCamPos` and calls it); for the geometry builder (return `0x1B1EFC`) and a DrawNear
  effect the context camera becomes the cyclops;
- sprites: `GetRenderMatrix` from `SetVertices`/`SetTailVertices` (returns `0x1B2D69`/`0x1B279E`)
  moves the world vertex by the eye offset, so the renderer's `vertex − eye` is `world − cyclops`.

The eye and its cyclops come from CameraEditHook's `BuiltEye` ring, which now also records
`centre` (the camera before the eye offset); a pass whose camera is not a stereo eye (shadows,
cubemaps, a mono frame) matches no record and is untouched. Both hooks are prologue-gated and
fail closed; `near.fx 0` leaves them counting only (A/B), `PREYVR_NEAR_FX=0` does not install them.

**Measured** (Disruptor at full charge, floor behind, full-resolution captures, `build/jordi-mock/fxdisp2.py`:
effect = brightness added over an uncharged capture, weapon disparity by block matching, so the
compositor's per-eye crop cancels):

| | glow − coil disparity |
|---|---|
| `near.fx 0` (native) | +86 / +90 px (box around coil and prongs) |
| geometry fixed only | sparks still +33 px (half resolution) |
| `near.fx 1` | **−2.5 / −1.7 / +3.8 px**, vertical 0 |

With the fix the cyan glow sits inside the coil's gaps in both eyes (`disruptor_coil_ab.png`).
Counters on a charge: geometry contexts 826 corrected / 0 without an eye, sprite vertices 6678
moved / 0. The same fix covers every DrawNear effect: Q-Beam start/warm-up/cool-down and impacts,
shotgun muzzle flash, weapon malfunctions, wrench trail, blood impacts, psi screen effects.

## 2. Q-Beam: the inner beam hung under the gun (`near.fxbridge`, default on)

**Symptom.** Firing the Q-Beam, a thick green streak ran from the muzzle back along the
barrel, through the gun and down past the hand: "the beam comes out of the bottom of the
weapon" (`qbeam_innerbeam_front_ab.png`, `qbeam_innerbeam_side_ab.png`, left halves).

**Cause.** It is not a sprite. `InstaLaser_Start` (and `Warm_Up_00`, `Cool_Down_00`,
`malfunction.MuzzleMalfunction_00`) has a DrawNear **geometry** child, `InnerBeam_00` /
`InnerBeamStart_00` (PlayerWeapons.xml): a plane with the inner-laser material, authored
`PositionOffset y=-0.4` (−0.27, −0.6 in the siblings), *behind* the emitter along its +Y,
the barrel. In the flat game the near pass draws the weapon at `r_DrawNearFoV`, so the nozzle
is low on screen while the world beam starts higher up at the muzzle helper; the stretch of
camera space behind the helper is exactly what projects between the two, so the plane is a
bridge (`qbeam_flat_reference.png`). In VR the near pass projects like the world, nozzle and
muzzle coincide, and the bridge lies back along the gun.

**Fix.** A DrawNear geometry particle authored at least 0.15 m behind its emitter is mirrored
through the emitter along the barrel (`nearfx::BridgeShift`): same plane, same material, now
leaving the muzzle and running forward over the start of the beam. It is applied where the
geometry fix already edits the vertex context: `RenderGeometry` draws `position - m_vCamPos`,
so the context camera moves back by the shift. The emitter's +Y is the aim lane's direction
(the Q-Beam's helper +Y is on the aim, 0.000 deg, section 3). `vPositionOffset` is
`ResourceParticleParams+0x6C` (type-info registration 0x18A400). In the whole particle library
only the four Q-Beam inner beams match; every other negative-Y DrawNear is a sprite on the
weapon itself. Flat frames and non-stereo passes are untouched (no eye record).

Measured: the streak is gone in both views; the core sits on the beam in both eyes. Soak
(6 min, 37 fire/grab/steer cycles): 5768 geometry contexts mirrored, 0 without an axis; a
malfunction during the soak mirrored `InnerBeamStart_00` (1.2 m shift).

The earlier sprite rule (`TrimRearHalf`, "keep the front half of a long streak along the
view") never fired: the long DrawNear sprites at the muzzle are camera-facing flares
(`Continous_01`, 1.39 m x 0.2 horizontal, and two round glows), which flat shows the same. It
was removed.

## 3. Q-Beam shooting: measured exact (`aim.beam`)

New passive observer (ShotRay.cpp): the beam update `0x16A29E0` raycasts damage with
`0x16AD790` (from 0.7 m behind the helper, call 0x16A2E49, hit point at out+0x3C) and draws
the beam with `CArkLaserBeam::UpdateLaser` `0x1671690` (`this` = weapon+0x520; calls
0x16A3302/0x16A332C). Each draw is paired with its raycast and compared with the drawn muzzle,
the aim and the aim lane's scene hit. Owner checked (weapon entity, beam object).

| Case | beam start - muzzle | beam vs aim | damage ray vs aim | hit off barrel line |
|---|---|---|---|---|
| one hand, 4 postures | 0.0 mm | 0.000 deg | 0.000 deg | 0.0 mm |
| two hands, steered by 10-15 cm | 0.0 mm | 0.000 deg | 0.000 deg | 0.0-0.1 mm |
| soak, 3427 beams | 0.0 mm | 0.000 deg | - | - |

`0x16A29E0` asks `GetReticleInfoForFiring` only when a player stat differs from its default
(`0x1272950` on player+0x678 -> +0xE8); not met in this save. If it is, `aim.shot` steers that
query like the other routes (return 0x16A2CC8). `move.firehold` makes no difference.

## 4. Q-Beam two-handed: held where an arm can reach it

**The native foregrip is out of reach.** It sits 0.58 m ahead of the trigger wrist, 0.36 m
beside, 0.23 m below. With an adult's shoulder (18.5 cm out, 23 cm down, 9 cm behind the eye)
and the right hand anywhere natural it is 0.73-0.92 m from the left shoulder; a palm reaches
~0.60 m. The drawn left hand stopped 31 cm short in mid-air
(`qbeam_twohand_left_short_before.png`). Flat viewmodels are posed for a camera; VR games keep
a real-size foregrip within reach and snap the support hand onto it from a grab radius.

**Fix (`ik.longreach`, default on; pure part `twohand::HoldBack/SupportRegion`).** A weapon
whose foregrip lies more than 0.45 m ahead of the trigger wrist is held back along the barrel
by the difference, at most 0.20 m: the drawn right hand stays on its own grip and both move
(Q-Beam 0.165 m; GLOO, foregrip 0.424 m, untouched). Its grab region reaches 0.10 m behind
the foregrip (plus the 10 cm radius); the drawn left hand still goes to the native hold on the
handle with the native finger pose. While held two-handed the drawn torso turns 30 deg (a
bladed stance, left shoulder forward), eased by the hold's blend, so the drawn arm reaches.
Muzzle, beam and every aim consumer follow the drawn weapon (section 3).

| Right hand (mock, OpenXR m) | real left grip - shoulder | drawn left arm |
|---|---|---|
| chest (0.18, 1.30, -0.20) | 0.60 m at the region's back end, ~0.50 m with the radius | reaches, x1.06 |
| high (0.17, 1.45, -0.25) | 0.61 / ~0.51 m | reaches, x1.08 |
| tucked (0.17, 1.40, -0.08) | 0.45 m | reaches, x1.00 |
| chest, `ik.longreach 0` | 0.85 m (impossible) | straight at x1.10, short |

Captures: `qbeam_longreach_grid.png` (right hand only / two hands), `qbeam_longreach_right_ab.png`,
`qbeam_twohand_left_on_handle.png`; reports `qlr_*_report.txt`.

**The hold let go while firing (fixed, `ik.supportlatch`, default on).** The region was read
from the native left palm every frame; a few seconds into a sustained beam the firing
animation takes that palm off the handle (375,616,198 mm at rest; 580,763,168 / 129,473,96 /
280,647,433 mm firing), the region left its bounds, was withdrawn, and the held foregrip was
released. The foregrip does not move on the weapon: one settled socket per weapon
(`twohand::RegionLatch`, 20 in-bounds samples within 1 cm), never moved or withdrawn by an
animated palm; a re-equip starts over. Measured: held through 5 s of beam while steering;
soak 36/37 cycles held.

Earlier fixes kept: wide acceptance (`ik.widesupport`) and the region turned with `pose.roll`.
The "gate 64" (HUD) seen before was a mock artefact: `-Shot` captures freeze frames, the HUD
menu state goes stale (>200 ms) and the hold releases by design. Measure holds with
`aim.twohand`/`aim.beam`, not across a capture.

## Commands

| Command | Effect |
|---|---|
| `near.fx [0\|1]` | Bare: report. 1 (default) draw-near particles at the weapon's parallax; 0 native. |
| `near.fxbridge [0\|1]` | 1 (default) the Q-Beam inner beam leaves the muzzle forward; 0 native (behind it). |
| `near.fxhide [1\|0]` | Research: moves every DrawNear particle far away. |
| `aim.beam` | Q-Beam: last beam against drawn muzzle, aim, barrel line and amber marker. |
| `ik.longreach [on comfortMm maxBackMm supportBackMm twistDeg]` | Long weapons held back (default 1 450 200 100 30). |
| `ik.supportlatch [0\|1]` | One settled foregrip per weapon (default 1); 0 = raw animated palm. |
| `ik.helpers` | Logs the held weapon's attachments, joints and bounds. |
| `ik.widesupport [0\|1]` | Wider two-handed foregrip acceptance (default 1). |
| `ik.align` | Also `supportLatch/latchedSupportMm`, `holdBackMm`, `weaponVtable` (Q-Beam 0x1E6E9F0, GLOO 0x1E6E390). |

## Tests and limits

`tests/NearParticlesTests.cpp` (`near_particles`: BridgeShift), `tests/TwoHandedAimTests.cpp`
(`two_handed_aim`: RegionLatch, HoldBack, SupportRegion); full CTest 62/62 (`input_queue` failed
once and passed on three reruns; not touched here). Mock only, not in a headset. Regression
`ShotSmoke.ps1`: 12 shots <= 0.001 deg. The intermittent hang seen once did not recur in a
6-minute soak with every hook on (`QbeamSoak.ps1`). Anatomy is an average adult; real reach
and comfort need the headset.
