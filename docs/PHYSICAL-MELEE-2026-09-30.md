# Physical wrench swings and weapon contact nudges

Status: implemented for the Steam target; **static and offline tests only**. No game, simulator, or headset run was performed for this batch. Both gameplay changes default off, under **VR Options → Physical**.

## Player-facing behaviour

- **Physical Wrench:** move the tracked wrench into a target to request a normal, uncharged native attack and apply its contact immediately. Prey still decides attack admission, fatigue cost, damage, material response and recovery. The later animation hit is suppressed, including duplicate events and events on another thread. Ordinary button attacks remain available after recovery.
- **Object Nudges:** tap movable rigid props with the displayed weapon. This applies an impulse, without invoking the melee damage function for guns. It supports the verified rigid-body class; living characters, articulated bodies and static geometry receive no generic impulse. Physics can still have gameplay consequences, which is why this is a separate opt-in.
- **Minimum Swing Speed:** 80–250 cm/s, default 120. A strike also needs 9 cm of accumulated weapon travel and physical controller motion. Rest for at least 150 ms between swings; there is also a 350 ms minimum contact cooldown and the native wrench's recovery.
- **Object Nudge Strength:** 0–100%, default 50%. Raw impulse is capped at 1.5 N·s before this percentage. The measured inverse mass and world inverse inertia further limit the induced linear/angular velocity to 1.5 m/s and 3 rad/s. These are bounds on this impulse, not on the object's total velocity after other physics effects.

When Physical Wrench is on, wrench contacts use the native strike policy; a nudge is not added on top. For damage-free wrench taps, turn Physical Wrench off and Object Nudges on. Contact feedback uses the existing global haptics enable/strength controls.

This is **sampled bounds contact**, not a solid mesh collider. Nine points—weapon bounds centre plus corners—are swept between poses. It can miss small features between samples or report contacts in empty corners of the bounds. It does not stop or push back the visible weapon at a wall, implement blocking/parrying, provide exact mesh collision, add physical grabbing, or replace the damage system. Those remain separate work.

## Geometry and ownership

The animation adapter reads the bound **weapon character's** local AABB through the field returned by its verified getter. It reconstructs the full attachment chain, including translation:

`inverse(native wrist) × native socket × relative attachment default × extra rotation`

The projected-attachment bit selects the native relative field; otherwise the bind-pose inverse derives it from the authored absolute default. The new wrist target and character location place those bounds in the world. This uses the same owning rig and authored transforms as the visual takeover. No camera-forward ray or guessed generic weapon length is used.

An immutable geometry sample carries owner generation, tracking sequence/time, session epoch, reference generation and settings generation. Both ends of each sweep are expressed in tracking reference space and mapped through one world transform. Locomotion and snap turning therefore do not create artificial sweep velocity. Controller translation/rotation must also establish physical intent: idle animation alone cannot strike. Duplicate samples, jumps, stale tracking, menus, equipment changes and recentering refuse/reset the gesture. A fresh rest interval is required after a reset.

Queries and native actions run only on the existing engine input-drain/player-producer thread. Animation only publishes geometry; XR only submits feedback. At most nine short synchronous queries run for a new qualifying sample. The earliest fractional contact across the samples wins. Failed batches cannot reuse a previous hit. Player physics is excluded using the established native scene-query adapter. Equipped-object self filtering still needs live verification.

## Native contracts proved on this target

Steam x64 `PreyDll.dll`, SHA-256 `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`; image base `0x180000000`. Addresses below are RVAs.

| Seam | Evidence and use |
|---|---|
| Wrench attack `0x16B1FD0` | Checks native fatigue before calling PlayAnim `0x16B2E20`; state at weapon `+0x520` changes from idle 0 to 1/2. Its bool return is **not** acceptance: the admitted idle case returns false. The concrete body does not consume the incoming entity ID, name or value. |
| Weapon hit `0x16B2BC0` | `this` in RCX, angle in XMM1; component is weapon `+0x4C8`. Computes native charge scale and calls component hit `0x13BF730`. We request only an idle, uncharged attack. |
| Component GetHits `0x13BD620` | Component RCX, hidden result vector RDX, weapon R8, angle XMM3. Replace only the exact active physical request, never other game queries. |
| Hit vector append `0x13BD250` | Native three-pointer vector; records have stride `0x50`. Engine append allocates the replacement record so the native consumer can free it on its own heap. No mod-CRT or stack vector storage escapes. |
| Wrench impulse wrapper `0x16AE410` | Called by the component hit. Negates its incoming direction; physical requests supply the negative swing direction while retaining native force/stat handling. Ordinary calls pass through unchanged. |
| Bound character AABB `0x82DD40` | Vtable `0x1D22200`, slot `+0x98`, returns `this+0x9F0`. Reads are bounded and reject malformed/implausible bounds. |
| Rigid action `0xBCF990` | Concrete vtable `0x1D83870`, slot `+0x38`; GetType at `+0x8` returns 2. Type-2 action validation reaches `0xB43870`. Only this class is admitted for generic nudges. |
| Impulse response fields | The action consumer multiplies impulse by inverse mass `+0x2E8`, and uses centre `+0x298` plus world inverse inertia `+0x328..+0x348` for contact torque. These also bound our nudge before dispatch. |
| Queued action lifetime | `0xB1B160` copies the action into engine storage. Runtime descriptor table entries for type 2 must report exactly 56 bytes and no pointer payloads. Stack action storage cannot be retained by this supported path. |

`pe_action_impulse` is 56 bytes: type `+0`, impulse `+4`, angular impulse `+0x10`, point `+0x1C`, part ID `+0x28`, part index `+0x2C`, apply time `+0x30`, source `+0x34`. We use type 2, explicit zero extra angular impulse, the measured contact point/part, native unused part-index sentinel, deferred apply time 2, source 2 and threadsafe dispatch 1. Source 2 retains the native wake path; source 1 takes a different capped branch that skips that wake handling.

Fifteen complete function bodies, vtable entries and call-site witnesses are verified against the exact Steam PE. Ghidra's explicit `/Prey/PreyDll.dll` program memory matched those same disk bytes. The EGS symbols supplied leads, not transferred RVAs. Runtime gates check the unpatched body hashes before installing hooks; native/ABI faults latch the feature off. This is not a guarantee that an exception inside the engine is recoverable—restart after a fault.

Delayed-hit ownership is retained per weapon, so changing weapon instances cannot release an older pending animation hit. The bounded registry holds 16 instances, reusing an entry when that weapon starts a new idle native attack. If it fills with abandoned instances, further physical attacks refuse rather than evict a pending hit; this conservative lifetime policy needs evaluation across level loads. Admission refusals consume the physical gesture too, avoiding repeated fatigue/UI requests every frame.

Reproduce the independent byte checks:

```powershell
python -E -B tools/re/verify_physical_interactions.py
```

The verifier checks SHA-256, all body hashes, corruption controls, absence of relocations within hashed bodies, concrete slots and key instructions. The source tests exercise motion rejection, translation/rotation swings, geometry composition, sweep ordering, bounded impulses, mocked production dispatch, delayed duplicate events on another thread, native admission refusal and the options adapter. Mock success is not native execution evidence.

The Release DLL builds, and **54/54 offline CTest checks passed**. See [test output](evidence/physical-melee-2026-09-30/ctest.txt), [native byte verification](evidence/physical-melee-2026-09-30/verification.json), and the [inspected Physical options raster](evidence/physical-melee-2026-09-30/physical-options.png).

## Brief acceptance pass for CosmicTangerine

1. Start with both switches off. Confirm normal button melee, gun firing, menus and weapon switching still work.
2. Enable Physical Wrench only. Rest the wrench, then swing into a movable prop and a valid target. Check visual contact placement, fatigue cost and recovery; count one hit per swing, with no second hit from the later animation. Test depleted stamina, a miss, rapid repeated movement and an ordinary button attack after recovery.
3. Hold the controller still while walking, snap turning, recentering and changing weapons. None should strike. Pause during a swing and resume; require fresh motion after a rest. Switch the option off before the delayed animation event and verify it does not produce a second hit.
4. Enable Object Nudges with Physical Wrench off. Tap a light prop with the wrench and a gun, including sideways contact while looking elsewhere. Compare a heavier prop. Confirm no explosive launch/spin, no motion of walls and no generic shove of characters. Check small/thin objects for limitations of the sampled bounds.
5. Check tracking loss/recovery and the frame-time difference in busy geometry. Contact queries, native hook installation, runtime queue metadata and physical feel have **not** been tested in Prey yet.

Read-only diagnostic command: `physical`. It reports options, `poses`, `sweeps`, `contactsFound`, `strikes`, `substituted`, `suppressed`, `nudges`, `admissionRefused`, `refused`, `fault` and `error`. A strike counter means an admitted native attack/hit call, not confirmed health damage; `nudges` means accepted/queued, not observed object motion. Also read `aim.scene` if scene-query faults are suspected, since the query adapter is shared.

Build/test evidence and the inspected Physical page raster live under `docs/evidence/physical-melee-2026-09-30/`. Fleet receipt submission and graph refresh remain pending until the branch artifacts reach the fleet-registered canonical checkout; this integration worktree is not registered. No release package is created by this batch.
