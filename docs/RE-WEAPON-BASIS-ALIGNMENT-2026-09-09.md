# Authored weapon-to-aim alignment — 2026-09-09

Implemented an opt-in replacement for the right hand's equip-angle rotation
capture. It resolves the selected weapon's authored ammo-helper frame and solves
the wrist orientation from the current controller **aim** pose. Grip position and
the left hand retain their existing paths. This is a static/offline result, not
headset acceptance or a claim of coverage for every weapon.

## Build and use

Versioned package: `build/packages/20260909-weapon-basis/`.

- `PreyVR.dll`: 831,488 bytes; SHA-256
  `983ff48a4914751c3be4a86b4625b0a3bc2506834201ac3ae624e8d53c95fae0`.
- `openxr_loader.dll`: 655,360 bytes; SHA-256
  `cd5b35fed75714c68a0910e9f94fc2140cf2d564493185f147890c5647f0f1c5`.
- Includes the previous takeover's corrected render-thread near-FOV override.
- Release build and **32/32 offline CTest tests passed**. The new test contains
  **108** native attachment-chain/reticle comparisons and rejection fixtures.
- The startup script passed PowerShell parsing. It was not executed.
- Nothing was deployed or injected; Prey was not launched or instrumented.

During the next authorized live session, enable through
`Invoke-PreyVRStartup.ps1 -Controls -WeaponAimAlignment` (plus the normal session
arguments). For an already configured IK session, `ik.align 1` opts in;
bare `ik.align` reports without changing state. `ik.align 0` restores the legacy
rotation route; explicitly run `ik.calibrate` again for its right-hand zero.

With alignment enabled, `ik.calibrate` applies only to the left hand. Right-hand
`ikCalR=0` is expected; use `ik.align` instead. Its report includes enabled state,
status, basis source, helper name, socket, equip generation, tracking sequence,
successful target-write count and refusal count. `basis_ready` describes a solved
candidate; `applied` means the target write completed, **not** that the engine or
headset confirmed the resulting barrel direction.

Unsupported binding/helper/rig cases refuse the right-hand goal for that callback.
They do not fall back silently to equip-angle capture. The left-hand path remains
independent. Defaults retain the existing behaviour until this option is selected.

## Question and decision rule

Can the native weapon mount and authored muzzle frame supply the wrist-to-barrel
rotation without capturing the controller's arbitrary equip-time orientation?
Control: reproduce the old 45-degree residual after capture and return to neutral.
Variable: substitute the verified attachment chain and authored helper frame for
that captured offset. Offline acceptance: reconstruct the resulting helper's full
world orientation through the native chain and compare all three axes with the
controller aim frame across assets/equip angles. This does not require a world raycast.

## Target and native evidence

Steam `PreyDll.dll`, SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`,
base `0x180000000`, x64 Windows. Explicit Ghidra program `/Prey/PreyDll.dll`,
Ghidra 12.1.2. Import identity agrees with disk. The offline byte verifier checks
11 captured spans and 12 concrete virtual targets against that installed PE.

| Target RVA / receiver | Observed contract |
| --- | --- |
| `0x16A29E0`, secondary receiver `weapon+0x190` | Reads entity at receiver-0x150 (=weapon+0x40), helper name at receiver+0x160 (=weapon+0x2F0). At `0x16A2BD9` calls helper TM getter, then normalizes matrix components 1/5/9: the **+Y column**. A later reticle convergence branch can replace that direction. This proves a native ammo-helper forward convention, not every projectile's final policy. |
| `0x11A5CF0`, helper TM getter | x64 hidden output pointer in RCX, entity RDX, slot R8d, name R9; world/relative flags on stack. Character route tries attachment by name before joint lookup. No call to this function was added to the mod. |
| `0x16914F0`, AttachToHand | Character binding vtable `0x1CB1328`, weapon character pointer at binding+8. Static object and entity bindings have different vtables and are refused by this implementation. |
| Binding virtual +0x10 -> `0x334EC0` | Tiny thunk loads binding+8 and tail-calls character virtual +0x198 -> `0x82E760`. That consumer obtains attachment world transform and writes the child character's location at +0xA90. The visible weapon's frame is this attachment-controlled character, not an assumed entity world matrix. |
| Character vtable `0x1D22200` | +0x48 -> `0x12EABB0` returns embedded manager at character+0x18; +0x58 -> `0x8CE820` returns skeleton pointer at character+0x10. Constructor `0x82BD50` installs manager vtable `0x1D22110`. |
| Manager `0x822FB0` / `0x8230A0` / `0x82DD50` | Case-insensitive name lookup; pointer array at manager+0x20; count at array-4 masked with 0x7fffffff. Manager+0x18 identifies its character. |
| Skeleton vtable `0x1D2A3E8` | Joint records at +8, stride 0xA8, name pointer +0, signed int16 parent +0x18. `0x8BC240` case-folds ASCII before its name hash lookup. Our exact case-insensitive name scan is conservative about hash aliases. |
| Skeleton default absolute getter `0x8BC0E0` | Embedded CPoseData at skeleton+0x18, absolute virtual +0x48 -> `0x87C6D0` reads pose+0x18. Therefore default absolute array pointer is skeleton+0x30, QuatT stride 0x1C. |
| Bone attachment vtable `0x1D212B8`, `0x7A2560` / `0x7A3390` | Socket index +0x15C; absolute default Q +0x114; relative default Q +0xF8; extra rotation Q +0x14C. When projected bit 0x4000 at +8 is clear, projection builds relative default = inverse(bind socket) * absolute default. Normal update consumes current socket * relative default * extra rotation. Simulation (+0x30) changes the formula and is refused, as is redirect (+0x33). |

Raw files are under `docs/evidence/weapon-basis-2026-09-09/`. Three tiny getters
were not recognized as functions by the decompile endpoint; their error outputs
are retained alongside successful raw-byte reads. `shotgun.c` is an exploratory
request that resolved inside `0x16AAB70`; it is **not** used as proof of shotgun
StartAttack or of this implementation's contracts.

## Implementation contract

For rotations in engine coordinates, let `C` be this animation callback's
character-world orientation; `W` its pre-IK wrist; `S` its current socket; `R` the
relative mount default that the next normal attachment consumer uses; `K` its
extra rotation; and `B` the authored barrel frame in the weapon model.

```
M = inverse(W) * S * R * K       # weapon frame relative to wrist
desiredWrist = inverse(C) * aimWorld * inverse(B) * inverse(M)
```

If projected, `R` is read directly from +0xF8, including later native adjustments.
Otherwise it is reconstructed as inverse(bind socket) * absolute default, matching
the pending native projection. The socket must be the wrist or its descendant;
a bounded signed-parent walk refuses an unrelated arm or cyclic hierarchy. The
existing native ADIK descendant propagation preserves this wrist-to-socket relation.

`B` comes from the weapon character's matching bone attachment absolute default
times its extra rotation, or its matching skeleton joint's bind orientation when
there is no attachment match. This is **authored** geometry. The code does not read
another animation job's partially updated weapon pose. It also rejects unknown
attachment types encountered during the lookup rather than guessing their layout.

The DLL uses the same `GameplayPoseFrame` for grip position and aim orientation,
checks pose validity, and rechecks equip/reference/epoch ownership before writing.
No calibration history or 90-frame settling delay enters the new orientation
calculation. Disabling it clears the right legacy offset so it cannot reuse an old
zero. The adapter performs bounded guarded reads and no native virtual calls.

## Validation and remaining work

`WeaponRigAlignmentTests.cpp` executes the production resolver and solver with
bounded engine-layout fixtures. It reconstructs the normal post-IK attachment
chain for 3 asset bases, 4 equip/wrist angles and 9 rolled aim poses, checking all
axes and comparing helper +Y with `AimFromController`'s actual ray. It covers
attachment priority, joint fallback, direct/descendant sockets, projected native
adjustments, ownership replacement, malformed arrays/names/quaternions, missing
helpers, simulation/redirect and unsupported bindings. Engine fixtures remain
byte-identical through reads and refusals. These tests do not execute the complete
injected ADIK callback or the native solver.

Reproduce offline from the repo with Python 3.12 + pefile running
`docs/evidence/weapon-basis-2026-09-09/verify_static.py`, then a configured Python
running the sibling `build_verify.py`. The build manifest pins source snapshots and
output hashes against source HEAD `5efd7c8f75923eec0f2553590d302e82743a2b37` plus
the recorded local changes.

Next live discriminator: equip each supported weapon with the controller at
different yaw/pitch/roll angles, then return to the same aim pose. Compare
`ik.align 1` against the legacy control and check `applied/refused`, helper and
generation while inspecting both eyes. Repeat through reload, recoil, recenter,
focus loss and weapon changes. Reject visual acceptance if the authored barrel
differs from the animated barrel or if the native solve/attachment schedule breaks
the reconstruction. No live asset coverage, frame-time budget or headset quality
was measured here.

Still separate: the shared reticle-origin positional anchor, reach-compression
coupling, muzzle-origin calibration, native shot convergence/obstruction policy,
static/entity weapon bindings, HUD scale and scene collision queries. This work
does not identify unknown physics fields or add a physics call.
