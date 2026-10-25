# Zero-g surface grabbing and push-off — 26 October 2026

Implemented behind **VR Options → Flight → Zero-G Surface Grab**, off by default.
Requires **World → Zero-G Flight**. This first version uses the **free left
hand**, one fixed surface at a time. It is built and checked with static Steam
evidence and offline fixtures. **No game, injection, xr-sim or headset run was
performed. Native acceptance, surface coverage and comfort remain unmeasured.**

## Controls

Release the left grip, bring it within about 10 cm of a wall, then squeeze.
A short haptic marks acquisition. Move the held hand to pull your body in the
opposite direction. Release while physically pushing against the surface to
request a bounded outward or tangential push; a still hand adds no release push.

The Flight tab saves grab reach (5–18 cm), push strength (0–150%) and release-speed
limit (50–300 cm/s). Defaults are 10 cm, 100%, 150 cm/s. Strength zero keeps
anchoring/pulling but disables the extra release request. Pull speed is capped
at 125 cm/s. Grabs shorter than 60 ms do not launch. This is a desired velocity
request through native movement, **not an additive impulse**.

Existing left-hand use/carry, medkit, psychoscope and weapon foregrip ownership
take priority. The right weapon remains usable. Anchoring suppresses ordinary
thrust, altitude, boost and artificial turning. Native input cleanup must finish
before an override is accepted; return sticks/buttons to neutral afterward.

Menus, tracking loss, recenter, changed player/weapon/session/settings, native
movement restrictions, excessive hand jumps, anchor stretch or lost geometry
cancel the anchor without authorizing a push. Existing game inertia can remain
after cancellation. Let go before grabbing again. Props and living bodies cannot
anchor; moving supports and bimanual anchors are not implemented.

Session-only diagnostics (menu changes are the persistent preference path):

```text
flight.controls 1
flight.grab 1
flight.grab.reach 10
flight.push.strength 100
flight.push.limit 150
flight.grab
```

The report distinguishes hook/settings errors, owner errors, acquisitions,
proposed pushes, native reads, successful submissions, cancelled anchors and
rejected colliders. A successful native call means submission, not measured
physical movement. A fault disarms this lane for the process instead of retrying
the same faulty native transaction.

## Static contract

Supported Steam x64 `PreyDll.dll` SHA-256:
`7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`.
Image base `0x180000000`; all addresses below are RVAs. Ghidra preflight found
another project's System Shock 2 session, which was left alone. Local EGS/PDB
exports supplied names and search leads; masked code matches located Steam
candidates. Steam instructions, concrete tables and producer/consumer flow
establish the implemented contract. No fixed EGS-to-Steam delta was used.

| Witness | Steam evidence |
| --- | --- |
| Ordinary zero-g request | `0x126FCC0` stores literal request type 4 at `0x1270EE3`; velocity XYZ is request `+4/+8/+C` |
| Native caller | `0x1270F98`: player vslot `+0x298`; `0x1270FA8`: returned animated receiver's vslot `+0x130`, RDX points at caller-owned request; return `0x1270FAF` |
| Animated receiver | Player getter `0x157C6D0` returns `[player+0x1808]`; constructor `0x4F5941` produces primary table `0x1CDDB60`; its slot `+0x130` is `0x4F5D00` |
| Interface distinction | Constructor also installs secondary table `0x1CDDED0` at `this+0x40`; the hook uses the primary interface, not that secondary base |
| Synchronous copy | `0x4F5E4D` copies type to animated `+0x178`, velocity to `+0x17C/+0x184`; later copies rotation/prediction/flags through request `+0x48` |
| Units | Preparation `0x4FBF5C` copies velocity and `0x4FBF8D` multiplies XYZ by frame time; native physical movement `0x4FC8D6` computes `1/dt`, then `0x4FC9EF` submits the resulting action through the existing physics receiver |
| Restrictions | Native movement input reads FSM `+0x613`; native status path passes player `+0x678` through `0x1587CA0`, then predicate `0x148F9B0` with literal 6. No unproved gameplay name is assigned to status 6 |
| Parent exclusion | Native parent getter `0x904130` reads entity `+0xA0`, then `+0x18` if present. Parented players are excluded |
| Fixed collider | Constructor `0xB7670D` produces base physical table `0x1D82188`; concrete type slot `+8` is `0x1A5A880`, returning 1. A constructor-zeroed word at `+0x28` must also remain zero as a conservative extra check; its field name is not needed by the implementation |

The detour accepts only the exact caller, type 4, current player/entity/animated
receiver, state 9, fresh matching XR/reference/menu/weapon/settings and enabled
flight session. It changes **only 12 velocity bytes** in the native caller's
request, calls the original once, then restores those bytes even on SEH. It
neither copies a guessed full request layout nor teleports the player, replays
physics, or issues a second physics action. Normal collision processing remains
in the native path; its observed behavior still needs a game test.

The existing synchronous, player-skipping scene query supplies contacts. A new
squeeze samples six short axes around the grip and chooses the nearest eligible
front-facing fixed hit. The nearest prop is rejected rather than raycasting
through it to a wall. Held anchors get one short local re-probe each input drain.
The collider is retained as an identity token only; only a fresh hit is classified
under a guarded read. Missing geometry, changed collider/part, point drift above
3 cm or changed normal cancels. Sub-tolerance script motion is not an exhaustive
moving-support detector, and general surface coverage is not established offline.

## Motion and delivery

Physical velocity comes from timestamped **raw tracking-space grip positions**,
rotated into the established gameplay basis. Player/camera movement is never
differentiated to create a launch. The world-space anchor error supplies a small
bounded correction while held. Changing the play-space orientation by more than
about 5.7 degrees cancels instead of rotating an old anchor into a new basis.
The initial hand position is the hold target, avoiding a snap across the reach
distance. Push-off removes the component into the wall and caps the result.

Held commands expire after 100 ms; physical samples over 60 ms apart, jumps
above 20 cm, speed above 5 m/s and stretch above 45 cm cancel. A release command
has one serial, one atomic reservation and one acknowledgement. Waiting does
not refresh its lease or produce a new serial. Re-gripping, stale tracking,
menus or context changes revoke it. A pending command is marked delivered only
after the original native consumer returns.

## Validation and next game test

`tools/re/verify_surface_grab.py` checks full target identity, independent native
instruction/table/constant witnesses and all runtime hash gates. Its negative
controls corrupt the concrete callee, request type, collider word and runtime
hash. Offline tests cover the actual production command adapter with named
boundary substitutions, pure numerical motion/context cases, settings/disk
roundtrip, flight handoff and the actual production 12-byte transaction with
normal and exception restoration. This does not execute the hook against Prey.

Release build and **85/85 offline tests pass**. Evidence and reproduction are in
[`evidence/surface-grab-2026-10-26`](evidence/surface-grab-2026-10-26/README.md).

When a game test is authorized, use an ordinary zero-g save and a fixed flat wall:

1. Start with the option off; confirm existing native flight still works.
2. Enable both flight options. Release controls, squeeze the free left grip by
   the wall, and compare `captures` with `submitted`; missing submissions need
   owner/cleanup/native-read diagnostics before tuning the motion.
3. Pull gently, release a still hand, then perform a small push away. Check
   collisions and the speed cap. Compare strength zero with 100%.
4. While anchored, test menu, recenter, weapon change and tracking interruption:
   no launch should result. Check that use/carry and two-hand grips still win.
5. Check several wall orientations and surfaces; record rejected collider types
   before extending the fixed-support whitelist. Then assess headset comfort.
