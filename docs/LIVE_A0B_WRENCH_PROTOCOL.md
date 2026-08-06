# Live A0b wrench protocol

**Status:** Completed successfully on 2026-08-01. The same wall collider remained at effectively identical depth while the +10-degree stack-local direction moved the returned contact `0.127165` world units. See [`../captures/traces/2026-08-01-prey-wrench-a0b.md`](../captures/traces/2026-08-01-prey-wrench-a0b.md).

This is the next bounded runtime experiment. Its only question is whether changing the direction used by one native wrench query changes the returned collision contacts without changing the camera.

## Why the stack-local seam is preferred

`ArkWrenchComponent::GetHits` begins at Steam RVA `0x13BD620`. Static analysis resolves this sequence:

| RVA | Operation |
| ---: | --- |
| `0x13BD69F` | Load the local player's `IArkPlayer` subobject and prepare its virtual ray getter. |
| `0x13BD6AF` | Set the six-float getter destination to `[RSP+0x58]`. |
| `0x13BD6F3` | Call `IArkPlayer::GetReticleViewPositionAndDir`. |
| `0x13BD6F5` | First instruction after the copy; safe one-shot direction-edit stop. |

At `0x13BD6F5`, the local copy is:

| Stack address | Value |
| --- | --- |
| `[RSP+0x58]` | origin X |
| `[RSP+0x5C]` | origin Y |
| `[RSP+0x60]` | origin Z |
| `[RSP+0x64]` | direction X |
| `[RSP+0x68]` | direction Y |
| `[RSP+0x6C]` | direction Z |

Only the twelve direction bytes at `[RSP+0x64]` are eligible for the synthetic pass. This memory belongs to the current `GetHits` stack frame. It is not the ArkPlayer cache, camera, code, or heap, so the edit naturally disappears on return and does not require a persistent-state restoration write.

The caller invokes `GetHits` at RVA `0x13BFA96`. At RVA `0x13BFAA9`, the returned vector has been loaded as `R15=begin` and `RBX=end`, but damage/effect iteration has not begun. This is the result-capture stop.

## Preconditions

- Use the exact supported Steam `PreyDll.dll` hash and let the build doctor pass first.
- Load a save with the wrench equipped and face a broad, static wall or other large non-destructible surface.
- Keep the mouse/controller and player position still between the two swings.
- Preflight x64dbg, connect to the actual Prey PID, and confirm there are no stale breakpoints.
- Resolve every stop as `PreyDll.dll base + RVA`; never reuse absolute addresses from an earlier process.

## Pass 1 — baseline

1. Arm single-shot hardware execute stops at `0x13BD6F5` and `0x13BFAA9`.
2. Request one wrench swing.
3. At `0x13BD6F5`, capture the 24 ray bytes at `[RSP+0x58]` without modification, then resume.
4. At `0x13BFAA9`, verify `(RBX-R15) % 0x50 == 0`, capture the result bytes, clear both stops, and resume.

## Pass 2 — synthetic yaw

1. Re-arm the same two single-shot hardware execute stops.
2. Request one wrench swing without moving the view or player.
3. At `0x13BD6F5`, capture the local ray, create a `+10.0` degree Z-up yaw using `MakeBoundedYawProbe`, and write only its twelve little-endian direction bytes to `[RSP+0x64]`.
4. Read those twelve bytes back immediately and decode them. Abort rather than resume if they differ or the direction is not normalized.
5. Resume to `0x13BFAA9`, capture the result vector, clear both stops, and resume normally.

The headless policy rejects a yaw whose magnitude exceeds 15 degrees. For the captured downward-pitched wrench ray, a 10-degree horizontal yaw produces a `9.0643`-degree full 3D ray separation and separates equal-distance endpoints at the observed `2.75` range by about `0.4346` world units. The precise 3D angle varies with pitch, and actual wall-contact displacement also depends on wall geometry.

## Pass condition

The experiment passes when:

- baseline and synthetic local rays share the same origin within capture tolerance;
- the synthetic direction is normalized, its horizontal azimuth differs by approximately 10 degrees, and its separately reported 3D angular delta is plausible for the ray pitch;
- the camera and UI reticle were never written;
- the returned nearest contact or hit set changes consistently with the yaw direction;
- all breakpoints are removed and Prey resumes normally.

A no-hit synthetic result is useful but weaker evidence; repeat against a broader surface before interpreting it as failure. A crash, malformed vector, unexpected module/hash, failed read-back, or inability to clear a stop is an immediate stop condition.

## Evidence to preserve

Store both 24-byte local rays, both raw result vectors, module base/PID, RVAs, decoded contacts, and the exact written twelve bytes. Promote them into the existing aim/wrench headless fixtures before beginning the renderer observer.
