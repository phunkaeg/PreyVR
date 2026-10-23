# The body follows the player: it turns with the head and walks with them

Branch `jordi/polishing`. Measured on the mock (simulated runtime, Steam `PreyDll.dll`), not yet in a headset.
New: `src/dll/BodyFollow.{h,cpp}`, pure rules in `include/preyvr/BodyFollow.h` + `src/common/BodyFollow.cpp`
(tests: `tests/BodyFollowTests.cpp`). One VR option, **BODY FOLLOWS YOU** (COMFORT, fourth row): OFF / TURN ONLY /
TURN AND WALK (default). Commands: `body.yaw 0|1`, `body.room 0|1`, `body.report`.
Evidence: [`evidence/body-follow-2026-10-16/report.txt`](evidence/body-follow-2026-10-16/report.txt).

## The problem (first Quest 3 session, 2026-10-16)

- **The camera turned, the character did not.** The view is `game yaw - reference yaw + head`, and only stick
  turning changed the game yaw. Turning the head left the body facing the old way: the arms twisted toward the
  controllers, and looking down showed a torso turned away. It also left the engine culling against the old
  facing (HEAD_TRACKING_SCOPE.md: culling follows the character's aim, not our camera).
- **Moving in the room left the body behind.** The head offset is applied on top of the game's eye point, so a
  step or a lean took the camera out of the character. A step back showed the character from behind, headless
  (`shots/looks`: `back30`/`back50`). This was the "neck without a head".

## The approach: change the body, never the view

Every change is made to the character and then taken out of the reference the view is composed with, so the
camera keeps following the head exactly and only the body moves. What is taken out is what the game actually
did, read back, not what was asked for. The old Chairloader mod's body-follow failed in a headset because it was
open loop (mouse counts assumed to turn the body by a known angle).

### Yaw: a servo with deferred, verified absorption

- **Where:** a detour on `ArkPlayerCamera::PrePhysicsUpdate` (Steam `0x148A250`), after the game has integrated
  its own look input. The player's look rotation is `ArkPlayerCamera+0x150` (= `ArkPlayer+0x13F0`, what
  `ArkPlayer::GetViewRotation` returns).
- **Turn:** the error between the head's heading and the body (in tracking space) is turned into the rotation,
  then passed through `ArkPlayerMovementFSM::LimitRotation` (Steam `0x159D4C0`), so the game's own limits apply.
  The heading uses the whole head and not only its forward axis: the crown takes over as the face pitches, so
  looking straight down still has a heading (unit-tested against `RecenterYawFromHeadPose` when level).
- **Absorption, one frame later:** measured frame order is `PrePhysicsUpdate(N) -> UpdateView(N)`. A rotation
  written in `PrePhysicsUpdate(N)` is not visible to `UpdateView(N)`; it reaches the view at `UpdateView(N+1)`.
  So the write is verified at the start of `PrePhysicsUpdate(N+1)` (kept, reverted or replaced by the game) and
  only a kept write is subtracted from the reference yaw at `UpdateView(N+1)`. The play yaw (`game - reference`)
  therefore never changes because of the body.
- **No new generation:** `AdjustHeadTrackingRoom` moves the reference without bumping
  `HeadTrackingReferenceGeneration`, so nothing calibrated against the reference (IK calibration, aim, haptics)
  sees a recentre. The aim lane's play yaw is `camera yaw - head yaw`, which this keeps constant by construction.
- Gated to the free-turning movement states (ground, jump, fall, slide), gameplay input (no menu), no cinematic
  gate, a fresh tracked head and a reference.

### Position: the body walks under the neck

- **The neck, not the eyes:** the body follows a neck point 7.5 cm below and 8 cm behind the eyes in the head's
  frame. Nodding or turning the head about the neck moves the eyes but not the body (measured: zero moves in the
  nod case). Following the eyes would slide the body forward every time the player looked down.
- **Turning about the neck:** a body turn swings the neck round the eye anchor; each yaw write is paired with
  the move that undoes that swing, every frame and without a deadzone. Without it the pivot hopped by 2 cm while
  the head turned.
- **The move:** `CEntity::SetPos` (Steam `0x90AF40`) on the player entity (`ArkPlayer+0x38`, position `+0x38`),
  at most 5 cm per frame, with hysteresis (starts at 15 mm of neck offset, stops at 3 mm). Before moving, rays
  through `RayWorldIntersection` (the scene query's own contract, now also `QueryBodySegment`) stand in for the
  capsule: just above the highest step (22 cm), the waist and chest, plus two at the flanks. A hit slides the
  step along the wall once. The floor is found under the destination; a step up or down of more than 3 cm and
  at most 22 cm is taken only onto a real tread (the floor half a radius further on is at the same height), and
  anything higher or lower is refused, so leaning over a ledge never walks the player off it.
- **Closed loop:** rays slip between geometry (a table top between two heights), and the physics then refuses
  or deflects the move in the same frame. So the absorption at `UpdateView` uses the pivot's actual horizontal
  change since the last view: all of it while our moves are settling (3 frames) and the stick is idle on the
  ground, and otherwise only the part of this frame's move that was achieved along its direction. Vertical
  change is never absorbed: a step or a fall is the game's to show. A refused move holds off walking that way
  for 12 frames, so the body does not shiver against the obstacle.
- **The game's eye swings ~1 cm** ahead of the pivot in the body's frame (`viewParams` position rotates with the
  body). While the body follows, horizontal changes of the game's eye are absorbed too, so the view does not
  slide ~1 cm per 60 degrees of head turn.

### The head in a wall

The body's eye is always somewhere the capsule could stand. A segment from it to the camera that hits geometry
means the head went where the body could not follow, so the view fades to black, from 10 cm off the surface to
black at it. It is drawn as a premultiplied black quad 0.2 m in front of the eyes, over the scene and under every
panel and HUD layer (XrSessionHost). It eases in over ~0.1 s and out over ~0.2 s, and a stale target fades out.
`InventorySwapchain` refuses textures under 640x480, so the quad is 640x480.

## Measured (mock; `build/jordi-mock/BodyCases.ps1`, `bodytrace.py`)

The mock now generates smooth head motion from the display clock (spins, sweeps, steps, rocking, circles, nods),
turning the head about a neck as a real one does, and can carry the controllers with the body. Every frame of
the composed camera is traced (`body.tracestart` / `body.trace`).

The key metric is the **world anchor**: camera minus the head's own placement in the world. It must stay
constant while only the player moves. Horizontally it held to **0.22 mm** (float precision) in every case:
two full turns at 90 deg/s, looking around at +-80 deg, a 450 deg/s head shake, nodding, steps forward, back
and sideways, rocking, a circle walk, and walking into a cabinet and an obstacle. The play yaw drifted by
**0.000-0.001 deg**.

| case | body vs head yaw | body moved / head moved | notes |
|---|---|---|---|
| spin 720 deg | max 1.9, end 0.000 deg | 0.006 m / 0 | nothing accumulates |
| shake 450 deg/s | p95 3.4, max 5.6 deg | | the body follows one frame behind |
| step fwd/back/side | 0 | 0.59-0.60 / 0.6 m | neck gap ends at 1-13 mm |
| nod +-60 deg | 0 | 0 / 0 | the body never walks |
| into a cabinet | 0 | 1.44 / 2.4 m | head goes on, view black (fade 1.0) |
| walking with the stick, head turned | | | travel vs head: 0.03-1.5 deg in open floor |
| snap 45 / smooth turn | 0 | | view turns exactly with the play yaw |
| menu open while turning 60 | 60 while open, 0 after | | the body catches up when play resumes |
| quickload | | | invariants hold after the load |

**Drawn hands vs their controllers** (in the controller's frame, controllers carried): 8-30 mm, the same as with
the body not following (9-37 mm). That residual is the IK's own latency, and following does not add to it.

## Not done / open

- **Crouching for real.** A physical crouch lowers the camera inside a standing body (crouch40 in
  `shots/looks`). Mapping it onto the game's crouch stance, with its eye-height change absorbed, is the natural
  next step.
- Ladders, cinematics and zero-G are gated off by movement state and the cinematic input gate, but were not
  exercised on the mock save.
- A tall living capsule (~0.3 m radius here) is wider than the default probe radius (0.25 m). The closed loop
  makes that harmless. `body.radius` tunes it.

## Reverse engineering note: EGS names to Steam addresses

`build/jordi-mock/re/egs2steam.py` rebuilds the EGS `PreyDll.dll` from Chairloader's BinDiff
(`Versions/7d6e322f61b28331.diff.gz`), then signature-matches an EGS function's first instructions in the Steam
image. Rel32 branch targets and RIP-relative displacements are wildcarded and the signature grows until it is
unique. It reproduces exactly the four published pairs in `RE-EGS-SYMBOL-CORPUS-2026-09-10.md` and
`ArkPlayerCamera::UpdateView`, the view hook's own address (`--selftest`). The diff itself is not an address map: its matcher searches only +-64 KB and the code moved
~0x2D000. `edis.py` disassembles the EGS image with call targets named from Chairloader's headers.
