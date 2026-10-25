# Native zero-g flight controls — 25 October 2026

This first phase adds native flight controls. Surface grabs and push-off are not
implemented here. Source/static and offline fixture evidence are separate from
native acceptance: **no game, xr-sim session or headset run was performed**.

The optional follow-up [surface-grab implementation](ZERO-G-SURFACE-GRAB-2026-10-26.md)
uses this flight mode as a prerequisite. Its scoped velocity requests and limits
are described separately; the native-controls-only behavior below is unchanged.

## Enable and use

Enable **VR Options → World → Zero-G Flight (Experimental)**. It is off by
default, saved with the other VR preferences. `flight.controls 1` enables it
for diagnostics; `flight.controls 0` disables it. `flight.controls` reports
enabled/active, native movement state, movement mode, face-button ownership,
cleanup debt, owner/pose/readiness gates, last error, delivered edges and applied
steering reads. Mode 0 is the ordinary path, 1 waits/cleans up, and 2 is armed
flight movement. An owner error distinguishes a refused native owner read from
waiting for neutral controls or a fresh pose.

In ordinary zero-g flight:

| Controller input | Request |
| --- | --- |
| Left stick | Forward/back and sideways thrust |
| Right A | Ascend along the flight basis's up axis |
| Right B | Descend along that axis |
| A and B together | Native braking; cancels ascent/descent and zeros stick thrust |
| Hold left Y | Native boost request; release clears the request |
| Right stick | Existing snap/smooth turning |
| Menu button | Existing pause/VR options |
| Both grips and left Y | Existing recenter; cancels boost while the chord is held |

Release the stick, A/B and Y on entry or after a context change. With
**Comfort → Movement Direction → Head Relative**, thrust follows the full
headset orientation, including looking up/down. This rotates the native
acceleration basis, not the headset camera or world. **Body Relative** retains
the native entity flight basis. No controller-driven pitch or roll is added.
The existing headset-owned view rotation remains in place.

The game retains acceleration, inertia, drag, collisions, native speed/boost
limits, restrictions and other flight physics. No velocity is assigned or
teleported by this feature. Braking requests native deceleration rather than an
instant stop. A/B use the shipped native jump/sneak input bindings; custom
action-map remaps may change what those keys do.

## Static native contract

Target: supported **Steam x64 PreyDll.dll**, SHA-256
`7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`,
image base `0x180000000`. Addresses below are RVAs.

EGS/PDB names supplied navigation leads. Masked function-prefix matches located
Steam candidates; the actual Steam instructions, table entries, producers and
consumers below establish the compiled contract. No EGS RVA was transferred by
a fixed offset. Ghidra had no running instance; the investigation used the
existing local exports, PE reader and Capstone offline.

### State and receiver

- Native `CheckZeroG` stores **9** at the FSM receiver (`0x159C45F`), then calls
  zero-g entry. The FSM update reads its first dword; jump-table entry 8
  (state minus one) selects `0x159EAF8`, which calls `0x126FCC0` with
  `this=FSM+0x310`, `RDX=FSM`, `XMM2=frameTime`. Gravity shafts use a different
  dispatch and are excluded from this first phase.
- The native jump action obtains the complete player from input `+0x70`, then
  passes player `+0x58` to the FSM setter. The native movement-input path
  produces input `player+0x8E0`. The adapter uses the existing proven
  `ArkPlayer::GetInstance` route (`0x157C990`), checks that back-pointer and
  requires the exact native entity vtable before using the owner.
- Native ascent setter/getter use FSM `+0x611`; descent uses `+0x610`.
  Zero-g update calls those getters and adds **+1 / -1** to its local vertical
  input. Installed base and overriding patch profiles bind jump/sneak to A/B.

### Direction without camera/physics replay

Zero-g update loads the player entity from complete player `+0x38`, asks that
entity for its world rotation at `0x127064B` (vtable byte slot **+0x150**), then
rotates its local acceleration with the resulting quaternion.

The Steam CEntity constructor at `0x9018C2` produces vtable `0x1D30BD0`;
that concrete table's `+0x150` slot is **`0x904D30`**, the world-rotation getter.
The caller passes a 16-byte return buffer in RDX. Its native layout is
**w,x,y,z**, unlike the mod's x,y,z,w. The original getter fills that buffer
and returns it in RAX.

The detour calls the original exactly once. It can replace that returned buffer
only for return address **`0x1270651`**, the current player's matching entity,
state 9, enabled/active flight, fresh matching pose/epoch/reference/menu/settings
and an armed neutral-entry context. All other callers retain the native result.
It changes no entity transform and runs no extra native simulation update.
Ground locomotion's yaw remap is skipped while this flight basis owns movement,
preventing the head heading from being applied twice.

### Boost and brake

Native gamepad `OnActionSprint` is press-latched: a synthetic key release does
not clear its flag. Instead, this feature uses the verified native
**`ProcessSprintInput(bool)` at `0x1591120`** on the owned player input object.
Its flag at input `+0xAC` is consumed by zero-g update. False clears it without
relying on the current keyboard/gamepad scheme; true makes the normal native
stance request. Boost remains subject to the native movement state.

Native lean handlers set input `+0xB5/+0xB6` then run
**`ProcessLeanInput` at `0x15910B0`**. That leaf calculates left=-1, right=+1,
stores their sum as lean and sets input `+0xB1` when both are held. Zero-g
consumes that braking flag at complete player `+0x991`.

The brake adapter sets those adjacent flags together before calling the native
leaf, so braking has zero net lean. It does not post two separate shoulder
events that could fail halfway and leave a roll direction held. Native cinematic
clearing in the leaf remains effective. A setter fault after a possible write
creates explicit cleanup debt; best-effort rollback and later retries block new
acquisition until it clears. These calls require the input-drain thread.

## Integration and cancellation

The input drain prepares one coherent tracking/owner context, updates ordinary
movement, releases the old ground A/B owner, then delivers flight edges. It
requires those old releases to finish before acquiring flight controls. Held
flight buttons do not become ground jump/crouch when flight ends. Hacking and
queued scoped menu presses wait for flight cleanup; native Start remains usable.

Menus, flight/steering-mode changes, recenter, player replacement, focus/tracking loss,
stale publications and VR stop withdraw flight requests and require neutral
controls before reacquisition. A replacement player never receives a setter
write addressed to the old player's input object. The basis uses a single
prepared pose and refuses a changed tracking/reference context immediately.
Native movement-state changes remain the game's responsibility.
The live native input cinematic flag is checked in addition to the movement
lane's last observed gate, so an old analog observation cannot enable flight
requests during a cinematic.

## Verification and next acceptance test

`tools/re/verify_native_flight.py` checks the exact Steam identity, native
instruction witnesses, numerical ascent/descent signs, state dispatch,
constructor-to-vtable-to-callee flow, actual C++ runtime-gate bytes and the
installed base/patch bindings. It rejects a corrupted concrete callee and a
wrong input-field gate. Full decoded game assets are not redistributed.

The offline fixtures exercise the production flight adapter with native and
publication boundaries substituted, plus the pure control/basis code. They
cover held entry, release acknowledgements, stale/changed contexts, menu and
options handoff, player replacement, recenter versus boost, a setter fault after
write with refused rollback, and the exact versus unrelated getter caller and
entity. VR options tests cover high-bit application and preference persistence.
No fixture establishes the hook executing or inputs being accepted in Prey.
The combined Release build passes **82/82 offline tests**. Frozen build, static
contracts, production World-page raster and reproduction commands are in
[the evidence folder](evidence/native-flight-2026-10-25/REPRODUCE.md).

Next game/headset test, in a save near GUTS or exterior EVA:

1. Leave the option off and confirm ordinary movement/menus, then enable it and
   enter zero-g with controls neutral. Check diagnostics show state 9/mode 2.
2. Move forward/sideways while looking straight, sideways, up and down. Check
   headset steering once, without an unexpected roll or doubled yaw. Repeat
   with Body Relative selected to compare the native basis.
3. Test A, B, A+B braking and hold/release Y. Check brake decelerates without
   roll, boost ends on release, and weapons/menus still operate.
4. Pause, open VR options, recenter and interrupt tracking while boosting or
   ascending. Check cleanup completes and held controls cannot restart flight.
5. Land while holding A/B, re-enter flight, load a save, and transition from a
   crouched posture. Check no accidental jump/crouch or inherited boost, and
   confirm camera height and hand alignment across the transitions.

Surface grabbing/push-off and controller-directed flight are later work.
