# Scene reticle and controller feedback

Source work on `codex/scene-reticle-haptics`, based on the pending `codex/vr-options` branch. No game session was started for this work. Native integration and headset feel remain unaccepted.

## Implemented

The new **Feedback** page in VR Options contains:

| Setting | Default | Behaviour |
|---|---|---|
| Scene Reticle (Experimental) | Off | Query the scene along the installed weapon aim ray and project the hit with each render eye. |
| Reticle Fallback Distance | 10 m | Existing convergence distance when scene targeting is off, unavailable or misses. Supports 0.5–200 m. |
| Controller Feedback | On | Brief pulses for a changed VR option and a successful support-hand attachment. |
| Feedback Strength | 70% | Scales pulse amplitude; zero is silent. |

All four persist in the existing options file. The scene ray changes the reticle's projection point only. It does not steer shots, change weapon damage, add physical collisions or replace special-weapon aim consumers. A ray along the installed aim is not a swept weapon volume or a ballistic prediction.

The hit distance lives inside `aim::Sample`. A new miss or refused call publishes no hit, and existing equip/reference/age checks reject the hit with its ray. The render thread makes no physics calls. A hit is not smoothed against an older world point, so aiming across a foreground edge does not drag the reticle across an unrelated surface.

Haptics use OpenXR output actions bound for both hands on Touch and Index. Menu changes pulse the selecting hand; foregrip attachment pulses the support hand. Pulses last 18/35 ms, with bounded amplitude and a 40 ms per-hand minimum interval. Requests expire after 100 ms and carry tracking epoch, reference generation and settings generation. Focus loss, recenter, disable and teardown stop or discard pending feedback. Failed OpenXR requests are consumed once, not retried every frame. No firing/impact haptics: trigger travel and a ray hit do not establish a successful shot or impact.

## Steam query contract

Target: Steam x64 `PreyDll.dll`, SHA-256 `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.

Question: can the known R-127 dispatch be connected to a concrete synchronous query, private output storage and a proven player self-filter?

| Contract | Steam proof |
|---|---|
| Physics receiver | `base+0x224D9C8`; environment equivalence already established in the September 9 takeover audit. |
| Concrete receiver vtable | Constructor at `0xB9E7B9` installs `base+0x1D82E70`. Slot `+0x118` points to `base+0xCCF250`. |
| Query ABI | `int(this, const SRWIParams*, const char*, int caller)`, native consumers use caller 4. |
| Private parameters | `0x70` bytes; origin `+0x18`, delta `+0x24`, types `+0x30=0x11F`, flags `+0x34=0xF`, hit pointer `+0x38`, capacity `+0x40=1`, skip count/list `+0x50/+0x58`. |
| Remaining fields | Zeroed foreign data, callback, cache, collision type and trailing flags. `+0x64=0x400000` follows the native builder with no additional caller exclusions. This is separate from geometry flags. |
| Player self-filter | Native melee calls `ArkPlayer::GetInstance` at `0x157C990`, reads entity `player+0x38`, calls entity vtable `+0x228`. Concrete Steam accessor is `0x9042B0`, 160 bytes. |
| Output | `ray_hit` stride `0x50`; distance float at zero. Query and cell/heightfield consumers initialize, fill and copy these records. Only a returned count of one and finite distance within 0–200 m is accepted. A miss never interprets buffer contents as a hit. |
| Thread boundary | Mod calls only on its established engine input-drain/player-producer thread, never XR/render. Native synchronous caller path takes an internal interlocked caller lock at world `+0x5774+4*caller`. |

The adapter checks the current player, concrete entity callee, world vtable and query slot. Complete query/helper body hashes gate first use. The query has split unwind entries: its first 143-byte unwind record is **not** its full body; the verified body is 5121 bytes. Native ABI/field faults disarm this feature for the process. An exception inside an engine lock cannot be assumed recoverable; restart after a fault rather than treating the latch as an engine-recovery guarantee.

The EGS symbol corpus supplied names and candidate bodies. Capstone comparison normalizes relative branch/RIP operands, so similarity alone is not a target-address proof. Direct Steam bytes, constructor/slot evidence and the native melee call site are retained alongside it. The four remaining main-body differences are indexed references to axis permutation tables: Steam `0x22614A8/0x22614B8`, EGS `0x2255288/0x2255298`; both contain the same 32 bytes. Other referenced globals and calls were not all exhaustively mapped between builds. No EGS RVA is called.

Reproduce the offline byte checks:

```powershell
python -E -B tools/re/verify_scene_query.py
```

The verifier checks the exact binary SHA, seven complete native bodies, constructor/slot/player-accessor witnesses and a one-bit corruption negative control for every body. `SceneQueryTests` tests ABI offsets, reserved bytes, private storage, miss/malformed output and aim provenance. `HapticsTests` calls the production adapter against mocked OpenXR functions, including focus, epoch, reference, disable and failure cases. Menu tests exercise the new page through the production settings adapter.

Release DLL build passed. **52/52 offline CTest checks passed**; see [test output](evidence/scene-query-2026-09-29/ctest.txt). The [production Feedback page raster](evidence/scene-query-2026-09-29/feedback-options.png) was inspected for clipping and layout. Capstone 5.0.7 produced the retained instruction listings. No native query or real controller vibration was exercised.

The project graph refresh wrapper refuses this integration worktree because only the canonical `PreyVR` root is registered in the fleet. No fleet configuration was changed; refresh the canonical graph after integration. The project-owned receipt can be validated against this worktree; fleet submission likewise requires its artifacts to reach the configured project root.

## Brief acceptance sequence for CosmicTangerine

1. Keep Scene Reticle off initially. Confirm the existing weapon, HUD and menu behaviour remains usable. Change a VR option and attach the support hand to a long weapon; confirm the expected controller pulses. Disable feedback and verify silence.
2. Enable Scene Reticle in Feedback, or send `aim.scene 1`. Read `aim.scene` for `hits`, `misses`, `refused`, `error`, `fault`. The `report` command's reticle projection record includes `rpDistance` and `rpSceneHit` for the projected sample.
3. Aim at a nearby wall, then a farther wall and open space. Compare scene mode on/off; the reticle should meet the nearer surface rather than stay at the fixed-distance projection. Check both eyes, foreground edges, off-angle hands, weapon changes and pause/resume. A counter increment is not visual acceptance.
4. Recenter and change weapons while aiming. No prior target or haptic event should replay. Check a glass surface, living target and held weapon for unwanted filters/self-hits. Only the native player physics object is skipped; equipped-object behaviour needs this live check.
5. Compare frame timing with scene mode off/on in complex geometry. This adds a synchronous 200 m ray at gameplay producer cadence. Its cost and filter suitability have not been measured. Leave it off if it is unstable or expensive.

Diagnostics: `haptics [0|1] [strengthPercent]` reports submitted/failed feedback requests; submitted means OpenXR accepted the call, not that vibration was felt. No release archive was published by this batch.

Still separate: native HUD/wrist visual acceptance, simulator composition correction, inventory layer stereo acceptance, weapon-volume collisions, physical melee and reliable native shot/impact events.
