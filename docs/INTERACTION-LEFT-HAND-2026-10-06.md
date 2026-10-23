# World interaction from the left hand (phase 1) — 2026-10-06

The left hand points at what to use and its grip uses it; the right grip only reloads. Everything after the
press is Prey's own: the prompt, the highlight, pick-up, carry, drag, search, quick loot, hold-to-use. The mod
chooses only **which ray the native selector looks along** and **when the native use button is down**.

Phase 1 is deliberately only that. Objects still float in front of the camera when carried (native carry);
moving them into the hand, throwing from hand velocity and Alyx-style pulls are later phases
(`PHYSICAL-GRAB-SEAMS-2026-10-04.md`).

Code: `include/preyvr/InteractionUse.h` + `src/common/InteractionUse.cpp` (pure: the button, the filter, the
geometry; `tests/InteractionUseTests.cpp`), `src/dll/InteractionLane.{h,cpp}` (native seam and observers),
`src/dll/MoveLane.cpp` (the button), `src/dll/DebugOverlay.cpp` + `src/common/DebugOverlayScene.cpp` (the
pointer). Everything is switchable: `use.hand 0` restores the previous layout exactly.

## Why the left hand, and why the right grip had to change

- The free hand interacts and the weapon hand keeps aiming. This is how Half-Life: Alyx splits the hands and
  the hand Funk's grab research planned for. The user chose it (2026-10-06).
- **Prey has one gamepad button for use and reload (X)**: with something selected it uses it, with nothing it
  reloads. Once the selector looks along the left hand, an X from the right grip would use whatever the *left*
  hand points at. So the right grip first makes the selector find nothing (two frames), then holds X: a reload,
  whatever the left hand points at. Keyboard E/R were not used because posting keyboard keys switches the
  game's prompts to keyboard glyphs.

## The native seam

`ArkPlayerTargetSelector::UpdateCandidates` (R-022, `0x159A660`; `RCX` = selector = `ArkPlayer+0xAC8+0x190`)
runs with the player's cached reticle ray (`ArkPlayer+0x17D4/+0x17E0`) set to the left hand's ray and
restored in a `__finally`. A getter probe (`aim.shotprobe`) found exactly **three per-frame readers** of that
ray: this selector (return `0x159A70A`), the HUD markers (`0x15A7C10`, `markerUpdate...`) and the HUD reticle
(`0x1667D50`, `reticleDisplay`/`interactIconDisplay`). Only the selector is steered: the reticle and markers
keep the weapon's ray. The write is scoped to one native call on the game thread; nothing stays edited.

The selector itself (disassembly of `0x159A660` and its per-entity evaluator `0x1598A90`, Chairloader layout):

| Field | Offset | Value in the test save |
|---|---|---|
| `m_bIsHoovering` | +0x01 | 0 |
| `m_innerAimDistance` (a cosine) | +0x04 | 0.980 (11.5°) |
| `m_outerAimDistance` (a cosine) | +0x08 | 0.960 (16.3°) |
| `m_interactDistance` | +0x0C | 2.50 m |
| `m_forceSelectEntity` | +0x1C | 0 |
| candidates `std::vector<ArkPlayerTarget>` (0x70 each: priority, aim cosine, 4 × `ArkInteractionInfo`, id at +0x68) | +0x20 | |

It first takes a direct hit along the ray (`interactDistance` long); otherwise every interactable entity in
an `origin ± distance` box whose interaction point is within its distance and **inside the 16.3° cone** of
the ray, with line of sight. Items outrank props (`m_itemTypePriorityMap`: an item scores ~1000, a prop ~2).
So the native code already has the aim assist the Dishonored VR mod first tried to add by hand; the mod adds
none. The ray starts 10 cm behind the controller (`use.back`), so a prop the hand touches is still in front of
it.

**Suppression** (the right grip's reload) runs the selector looking at an empty point 20 km above the eye: no
foreign call, the native code runs normally and simply finds nothing.

## The button (`preyvr::use::UseButton`)

- Left grip, fresh press, **something selected** → X down, and **held every frame** (`kStateDown`) while the
  grip is held: hold-to-carry and the native tap/hold distinction need it (the fire lane's lesson). Released
  with the grip. Pressed with nothing selected → nothing is sent (it would reload).
- While **carrying**, the left grip is the button (it drops the object); the right grip does nothing.
- Right grip, fresh press → selection suppressed → X down after two frames with no selection → held while
  the grip is → released, then three more suppressed frames so the release cannot land on a target. Gives up
  (sends nothing) if a selection never clears in 12 frames.
- The left grip's other owners win: medkit zone (inside its sphere, owned or not), the long weapon's foregrip
  region, a two-hand hold, the recentre chord, and the psychoscope gesture zone when that option is on. The
  right grip's: holster, two-hand hold, recentre chord. Both grips in the same frame do nothing.
- A hand acts only on a press after it has been seen released (a grip held across a menu does nothing).

## The pointer

A One Euro filter (1 Hz, β 2) then a **0.75° dead band** (backlash): the ray the selector sees does not move
until the hand has turned further than that, then follows at that distance. Measured on the mock with a
physiological tremor (`handJit`) aimed exactly at the edge between two props (`UseTremor.ps1`):

| Tremor | Raw ray | One Euro only | One Euro + dead band |
|---|---|---|---|
| 0.4° | 40–48 selection changes / 8 s | 22–25 | 0–3 (most runs 0) |
| 0.8° | 36–44 | — | 0–2 |

Both rays are drawn in gameplay by default while interaction is tested (`dbg.draw` rays + hits; the VR options'
Debug overlay switch turns them off). The left ray is the use pointer: faint cyan while it selects nothing
(ending at the selector's reach or the first surface), bright and ending **on the selected object's bounds**
when the game selected one (with a bend to it when the pick came from the cone rather than the line), green
while the button is held, grey while the right grip reloads. `dbg.draw all` adds the object's name and action,
the other candidates and a status line.

## Measured on the mock (test save `Campaign2\manual4`, `docs/TEST-SAVES.md`)

`UseScan.ps1` sweeps the left controller over a 10° grid: all 17 interactables of the room are selectable,
behind (the corpse at ±180°), left (lockers at 60–100°), right (towels at −80/−110°), on the floor (shoes, the
alien organ at −70°). `UseCases.ps1` then runs every case end to end and judges it by what the **game** did
(`PerformInteraction`/`Interact` observers, the carried entity, the selection):

| Case | Native action (type/mode) | Result |
|---|---|---|
| towel, shoe, desk fan ("sculpture") | carry (6/0) on press | carried, dropped with the left grip |
| coffee mug (breakable) | carry (6/0) | carried, thrown with the right trigger |
| jerky, tubing, alien organ, mimic tumor, test tube | take (4/0) | into the inventory, gone from the world |
| trash can | search (3/0) on tap; carry (6/1) on hold | both |
| bench | carry with a 0.75 s hold (6/0, delay 0.75) | 0.4 s hold: nothing; 1.3 s: carried at 0.76 s |
| corpse (behind) | search (3/0) on release; drag (6/1) on hold | both; dropped with the left grip |
| both lockers | quick loot (3/0) per tap | one stack per tap, grid empty |
| right grip, left hand on a pickup | reload | pickup kept; GLOO fired 17, then a full clip (42) after |
| left grip on the foregrip / at the medkit slot / in the recentre chord, something selected | — | no press: two-hand hold, medkit grip, recentre |

Runs (`docs/evidence/interaction-2026-10-06/`): every case passed on fresh boots. When a case failed it was the
harness's own clutter: a carried prop it dropped lay in front of the next target, and the game used what was
really selected (`uc3`: the fan in front of the bench; `uc4`: the dropped bench over the shelf). The final
harness drops facing an empty sector, throws the big props away, and tests the bench last. During GLOO fire the
selection flickers while the goo stream crosses the left ray (line of sight): native, and the button is unaffected.

The native tap/hold split (~0.33 s) is Prey's own: a quick squeeze is the prompt's tap action, holding the grip
is its hold action (drag a body, eat, carry the bin).

## Commands

| Command | Effect |
|---|---|
| `use.report` | state, selection (name, class, the 4 interaction modes, angle/distance), candidates, counters, last events |
| `use.hand 1\|0` | left hand points and uses, right grip reloads (default) / previous layout (right grip, weapon ray) |
| `use.hold 1\|0` | post the held state every frame (default 1) |
| `use.filter on [minCutoffCentiHz 100] [betaHundredths 200] [backlashCentiDeg 75]` | pointer filter |
| `use.back <mm>` | ray origin behind the controller (default 100) |
| `use.trace 1` | log every selection change |
| `use.find <entityId>` | where an entity is, as the controller angles that point at it (test harness) |

Logs: `preyvr_use target=… name=… class=… m0=type:text…`, `button press|release owner=use|reload`,
`interact mode=… result=…`, `perform type=… mode=… name=…`, `carry entity=…`, `menu open|closed`.

Mock scripts (`build/jordi-mock/`): `UseScan.ps1`, `UseCases.ps1`, `UseTremor.ps1`, `UseArbitration.ps1`.

## Found, not fixed

- **The medkit slot / psychic targeting and `aim.shot` cannot both install.** Both hook
  `IArkPlayer::GetReticleViewPositionAndDir` (`0x157CBB0`). `PsiMedkit::Install` hashes the native bytes
  first, so once `aim.shot` (VR option *Shots from the muzzle*, or any `aim.shot` command) has hooked the
  getter, enabling the medkit slot or psychic targeting returns 193 (`ERROR_BAD_EXE_FORMAT`) and sets its
  fault. The other order costs `aim.shot` its getter hook (the crossbow/pistol pattern centring). A shared
  getter hook with per-thread scopes would serve both.
- The secondary **Y** actions Prey offers on some targets (*Search* on a container's full transfer screen,
  *Eat* on food) are not reachable in gameplay: left Y is unbound there. Taking items works with the grip.
- `EArkInteractionType` names are not in a readable table; known from observation: 3 search/loot,
  4 take, 6 carry, 12 the hold action on pickups.
