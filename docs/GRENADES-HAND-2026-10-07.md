# Grenades held in the right hand and thrown with it

Branch `jordi/polishing`. Measured on the mock (xr runtime simulator, Steam `PreyDll.dll`), not yet in a
headset. Everything is switchable at runtime: `grenade.hand 0` restores the game's own throw exactly.
Evidence: [`evidence/grenades-2026-10-07/`](evidence/grenades-2026-10-07/) (reports in git, images not).

## What a player saw before

- **The grenade did not follow the hand.** With a grenade weapon equipped (EMP charge, Recycler charge,
  Nullwave transmitter, Lure) the right hand stayed where Prey's first-person animation holds it, 10-20 cm
  off the controller (`palmErrCmR` 10.6 at rest, 21.2 with the controller moved;
  `g_emp_moved_eye0.png`): the IK's weapon alignment refused it (`ik.align status=missing_helper`, a grenade
  has no barrel) and returned without writing the hand.
- **The throw ignored the hand.** Holding the trigger charges, releasing plays the throw animation, and the
  grenade leaves 128-143 ms later from the **camera**, along the **view**, at
  `lerp(fMinSpeed 10, fMaxSpeed 35, charge / 1.5 s)` m/s.

## Native contracts (static, Steam build; no RTTI on these classes)

`CArkWeaponGrenade`, primary vtable `0x1E6F148`, secondary (at `+8`) `0x1E6F390`; one C++ class for all four
grenades (archetypes `ArkSpecialWeapons.{EMPGrenade,RecyclerGrenade,NullwaveTransmitter,LureGrenade}Weapon`).

| What | Where |
|---|---|
| Fire action handler (mode 1 press, 4 hold, 2 release) | `0x16A5C90` |
| Start charge: `+0x521 = 1`, timer `+0x524` (remaining) / `+0x528` (duration), charge fragment | `0x16A6150` |
| Release (vtable `+0xE8`): throw fragment, or the deploy fragment when `+0x4F9` | `0x16A6040` |
| Anim event handler: CRC("Throw") -> vtable `+0x118`, CRC("Deploy") -> `+0x120` | `0x169DD20` (call at `0x169DDA9`) |
| `ThrowGrenade` (vtable `+0x118`) | `0x16A6300` |
| `DeployGrenade` (vtable `+0x120`) | `0x16A4B20` |
| Projectile creator (archetype `[w+0x4F0]`, `&pos`, `&dir`, owner id, six more) | `0x168F2B0` (call at `0x16A6497`) |
| Per-frame update (secondary vtable `+0x98`, RCX = w+8): raycasts `[w+0x4C0]` (1.25 m) along the VIEW, sets `[w+0x4F9]` "deploy" on a surface | `0x16A67B0` |
| `fMinSpeed` / `fMaxSpeed` | `[w+0x4E0]` / `[w+0x4E4]` (`LoadCachedProperties` `0x16A5700`) |

`ThrowGrenade`: position = `GetFiringPosition` (`0x1694BC0`, the camera while the grenade idles: fallback
byte set), direction = to `GetReticlePosition` (vtable `+0x148`); after the creator, if the charge duration
is > 0, `pe_action_set_velocity` (queued) = normalized current velocity x lerp(min, max, elapsed/duration);
then ammo (`[w+0x2B0]` vtable `+0x88 (1)`) and vtable `+0x1F8`.

Test support only: `i_giveitem`'s handler is `0x146F2C0`; the give itself is `0x146E6D0` (item manager
`0x16FDDA0([0x2C16840])`, `&std::vector<EntityId>`, receiver `0x7777`, archetype from the entity system
`+0x1A0`, count); `grenade.give` calls it on the game thread and equips through `0x1274820`.

## What changed

### The hand holds it (AnimIkTakeover.cpp, one condition)

The weapon-alignment branch is skipped for a grenade (`GrenadeHeldByHand`: the player's weapon whose vtable
`+0x118` is `ThrowGrenade`, while `grenade.hand` is on). The right hand then takes the existing free-hand
path: anatomical wrist from the grip pose, palm grip point on the controller. The fingers stay native (the
finger plan is only for weapon-less hands), so the animation's own grip wraps the grenade.

Measured for all four (`gt1_report.txt`, `gt1_*_sheet.jpg`: the player's view, from the left, from above,
from the front, plain and with the controller axes and the game hand drawn): `palmErrCmR=0.0`,
`lenErrDegR=0.0`, `palmNormErrDegR=0.0`; the prop joint the grenade hangs on (`r_handProp_jnt`, the arms'
`weapon` attachment) sits 1.5-5.3 cm from the grip origin towards the palm (grip -X): EMP (-3.6, -2.1, -1.8) cm,
Recycler (-5.3, -1.2, -0.9), Nullwave (-5.2, -2.6, -0.4), Lure (-1.5, -2.1, -0.6). `pose.report` now prints
`propInGripCm`/`propX|Y|ZInGrip`, and `pose.marks` draws the prop joint's axes.

### The throw (GrenadeLane.cpp, GrenadeThrow.{h,cpp})

- **When**: at the trigger's release. The release hook (`0x16A6040`) calls `ThrowGrenade` right after the
  native release starts the throw fragment; that fragment's "Throw" event call is skipped (matched by the
  caller's return address `0x169DDAF` and the weapon, 3 s window). The input lane notes the edge *before*
  posting it, because posting runs the fire handler synchronously. Measured: release -> projectile 0.0-0.1 ms
  (native: 128-143 ms), one projectile per release, every event skipped (three in a row: 3/3/3).
- **From where**: the grenade in the fist (grip origin + 2 cm through the palm), or the native firing
  position when it is a real (non-fallback) position on the hand. If the segment head -> spawn is blocked,
  the spawn is pulled back 8 cm short of the hit. Hand 60 cm through a window: spawned 8 cm on the player's
  side, pulled back 0.63 m (`g_wall.jpg`).
- **How fast**: the right grip's motion as of the release (`CarryLane`'s history: the peak of the last
  0.12 s, the runtime's `XrSpaceVelocity` when it reports one), plus `w x r` for the grenade's offset from the
  grip origin (a wrist flick), plus the player's velocity. Spin = the hand's (capped 20 rad/s).
  - Below 0.8 m/s it is a **drop**: the hand's own velocity, unscaled (a still hand drops it at the feet:
    rest 0.02-0.35 m from the release point). 0.8-2.0 m/s eases into the throw.
  - A throw is solved for **parity with a real ball**: the speed along the hand's direction such that the
    grenade comes down on the floor where a real ball (9.81 m/s^2, no drag) thrown with the hand's velocity
    x 1.3 (the usual VR under-throw factor; SteamVR's Throwable uses 1.1-1.5) plus the body's would. Needed
    because a Prey grenade is not a ball: its own `pe_simulation_params` (read from the projectile,
    `dampingFreefall`/`gravityFreefall`, physics vtable `+0x28`) are gravity 13.0 and damping 0.85/s, the
    same for all four; fitted frame by frame from the flight: 12.9 and 0.849. The resulting gain is
    1.7-3 depending on speed and elevation (1.84 for a 4.7 m/s, 21 deg throw).
  - Limited softly (tanh knee from 80 %) to 1.15 x `fMaxSpeed` (40 m/s): never beyond the game's own range.
  - The velocity is set (`pe_action_set_velocity`, queued) after `ThrowGrenade` returns, so after the
    native charged set; the creator is handed the throw direction so the projectile faces it.
- **Deploy** (stick the charge to a surface the VIEW is on, on press) is off while `grenade.hand` is on: the
  per-frame update runs with the deploy range at 0 when the flag is clear (it is cleared only by a raycast that
  misses, so a set flag is left to that). `grenade.deploy 1` restores it.
- Right-hand haptic pulse on a throw (not on a drop): 4 pulses for 4 throws on the mock.

## Measured (GrenadeCases.ps1, scripted throws on the display clock, trigger let go at the gesture's peak)

| Case | Hand m/s | Left at (m/s) | Frame-2 vs asked | Landed | Model | Real ball |
|---|---|---|---|---|---|---|
| Drop, hand still | 0.00 | 0.00 | - | at the feet (0.02-0.35 m) | | |
| Gentle toss | 1.12 | 1.27 (ease) | 0.20 | 0.48 m | 0.48 | (ease: shorter by design) |
| Underhand lob, 45 deg | 2.98 | 5.56 | 0.25 | 2.40 m | 2.39 | 2.42 |
| Medium, 21 deg | 4.67 | 8.51-8.59 | 0.23 | 4.49-4.55 m | 4.46-4.47 | 4.53-4.54 |
| 30 deg left | 4.67 | 8.52 | 0.24 | 4.51 m, heading error 0.00 deg, lateral 0.000 | 4.47 | 4.54 |
| Hard, 12 deg | 9.32 | 17.16 | 0.30 | end wall of the corridor (~10 m) | 9.77 | 9.93 |
| Overhand arc about the shoulder | 3.37 | 5.71 | 0.20 | 2.23 m | 2.22 | 2.26 |
| Wrist flick | 1.30 | 1.60 | 0.16 | thrown (not dropped) | | |
| Walking backwards 3.65 m/s | 4.67 | 7.13 + player | 0.22 | 1.57 m | 1.59 | 1.62 |
| Tracking lost at the release | - | 0.00 | - | dropped | | |
| `grenade.hand 0` | | native, 136 ms after the release | | | | |
| `grenade.early 0` | 4.64 | 8.58, at the event (131 ms) | 0.26 | 4.61 m | 4.52 | 4.60 |

"Frame-2 vs asked" is |v(frame 2) - v(asked)| in m/s: one physics frame of gravity and damping (~0.1 m/s and
~0.7 %). All four grenade types, medium throw: landed 4.49-4.55 m against a real ball's 4.53-4.55, lateral 0.

On the move the parity is solved on the world velocity (body + arm): unit test `TestParityOnTheMove`
(forward, backwards, sideways at 3-4 m/s) lands within 5 cm of the real ball; on the mock, walking backwards
3.65 m/s: 1.57 m against 1.62.

The scripts' angle helper had clamped with `[math]::Min(1, x)`, which PowerShell resolves to the integer
overload, so every angle read 0: the carry lane's "0.0 deg" (2026-10-07) is really 0.7-1.0 deg, one
physics frame of gravity (recomputed from those logs). Fixed in `CarryCases.ps1` and `GrenadeLib.ps1`.

## Commands

| Command | |
|---|---|
| `grenade.report` | state, equipped grenade (speeds, deploy range, firing position against the grip), last throw, last flight |
| `grenade.hand 0/1` | off = the game's throw exactly (default 1 when VR starts) |
| `grenade.early 0/1` | 1 = leaves at the release; 0 = at the animation event (VR velocity still) |
| `grenade.spawn 0/1/2` | the drawn grenade (fist fallback) / the fist / native |
| `grenade.deploy 0/1` | native deploy-to-surface |
| `grenade.throw <vrFactor% 130> <parity 1> <dropCm/s 80> <throwCm/s 200> <maxOverNative% 115> <lever 1> <fixedGain% 150>` | tuning |
| `grenade.physics <g cm/s2 1270> <damping %/s 85>` | the fallback when the projectile's own cannot be read |
| `grenade.trace 1` | per-frame flight path (`preyvr_grenade path`) |
| `grenade.give <emp|recycler|nullwave|lure|archetype> [count] [equip]` | test support (more than one stack leaves loose entries in the inventory) |

Log lines: `preyvr_grenade throw|flight|contact|rest|gone|event_skipped|give`.

## Not done / open

- Headset: the feel of the 1.3 factor and of the hand pose with real Touch controllers (grip != aim).
- The throw animation still plays after the grenade has left (fingers open; the arm is the IK's). The next
  grenade appears ~1 s later, as natively.
- When a stack runs out the game puts the weapon away; a `grenade.give` afterwards could not re-equip it
  (`Equip` refuses) on the mock. Not a player path (the wheel), only the test harness.
