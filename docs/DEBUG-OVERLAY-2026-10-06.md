# In-game debug overlay (`dbg.draw`) — 2026-10-06

A testing aid drawn **inside the game's own eye images**: each hand's ray as the game uses it,
the holster and medkit body slots, the wrist card's visibility test and the two-hand foregrip
region, with labels and a status panel. Off by default. Implemented and checked in Prey on the
headless OpenXR mock (1280 × 1440 per eye, Quest 3 field of view), **not yet in a headset**.

![Body slots, rays, wrist card and status panel](evidence/debug-overlay-2026-10-06/final_slots_eye0.png)

## How it is drawn

`SubmitStereoPair` copies Prey's backbuffer into the held eye image together with the render
contract that produced it (eye pose in the app's OpenXR space and the frustum tangents). The
overlay draws into that image right after the copy, projecting every primitive with that same
pose and frustum. Consequences:

- A point drawn at an OpenXR position appears at that physical position in the headset, in
  stereo, whatever the engine's cameras do. It needs no engine debug renderer (`IRenderAuxGeom`
  is still unmapped, see `ReticleFollow.h`).
- With alternate-eye rendering each held eye keeps the overlay of its own frame, so overlay and
  pixels never belong to different poses.
- Any capture of the submitted images (the mock, xr-tape) shows exactly what the wearer sees.

The held eye images are now created with `D3D11_BIND_RENDER_TARGET` (fallback without it; the
overlay then refuses and logs once). The renderer saves and restores all pipeline state it
touches (IA/VS/PS/GS/HS/DS, constant buffers, SRV/sampler, RS, OM, predication). Text uses a
GDI-rasterised Consolas atlas; lines, discs and glyphs are anti-aliased in one pixel shader.

## What is shown

| Layer (bit) | Content |
|---|---|
| Rays (1) | Right (amber): from the controller's aim pose to where the **game's** aim ray ends. The installed ray starts at the native eye (`aim.origin 0`), so this is the converging shot path; the marker is filled on a scene hit, hollow at the reticle fallback distance. Faint, labelled `NO WEAPON`, when nothing is equipped. Left (cyan): the left aim ray; violet with `psi.target 2`. Hidden while the foregrip is held. |
| Raw (2) | Grip axes of both controllers; the raw right aim ray, dashed. |
| Body (4) | Holster slots (right hip, left chest) and the medkit slot as spheres where **their own gestures** measure them (`HolsterGesture::ZoneCentre`, the gesture's torso yaw and radius). Colour: white idle, yellow hand inside, orange inside with the grip already held (the gesture refuses on purpose), green owned/success, red refused, blue dot = weapon stored. Tether and distance in cm; label says what a squeeze does next (`SQUEEZE: STORE`, `SQUEEZE: DRAW/STOW`, `SQUEEZE: SELECT`, `TRIGGER: USE`). |
| Wrist (8) | The wrist card rectangle (`WristPose`, configured size) and its face normal. Green when the layer shows it, orange when hidden by the pose test, grey dashed when gated (menu, two-hand, tracking). Label: facing/viewing cosines against the thresholds `WristVisible` uses. |
| Foregrip (16) | The weapon's published support region as a capsule; yellow when `ClosestGrip` accepts the left hand, green with the socket when held. |
| Labels (32) | Text next to each primitive. Kept inside the runtime's displayed frustum, stacked when they collide, hidden when their anchor is out of view. |
| Status (64) | Head-locked panel about 25° below the gaze: both rays, every slot, wrist metrics, foregrip, query count. |
| Hits (128) | One extra scene query per hand and frame on the input-drain thread (`QueryDebugRay`), sharing the scene query's gates and its process-wide fault latch. Never changes the reticle. |

## Controls

- `dbg.draw` reports; `dbg.draw all|on|off|<mask>` sets the layers. `dbg.report` gives counters
  and the status text; `dbg.marks` prints every primitive in OpenXR space with its projected
  pixel in the last drawn eye (for automated checks).
- **VR Options → Setup → Debug Overlay (Testing)**, persisted as `debug_overlay` in
  `vr-options.ini`. A saved "off" never overrides an overlay enabled by command or launcher.
- `Start-PreyVR.ps1 -DebugOverlay 255` (not persisted).

## Validation (mock, Prey Steam build, `PreyDll.dll` 7d6e322f…)

- Left ray drawn by the mod lies on the mock runtime's own aim ray (independent drawing).
- Right ray pointed 60° down hits at y = −0.013 m in the STAGE space: the physics hit lands on
  the tracked floor, so the world → OpenXR conversion is right.
- Holster: hand inside → yellow; squeeze → `holsterDispatched=1`, green `OK`, blue stored dot;
  squeeze again → draw (`holsterDispatched=2`). Medkit: `READY` → trigger → `medkitUsed=1`, `OK`.
  Foregrip: `SQUEEZE` → `aim.twohand held=1`, green with socket. Wrist gated while held.
- Stereo: both eyes consistent; snap turn and recenter leave nothing stale; menus suppress it.
- Cost: render-thread service p50 361 µs with the overlay vs 238 µs without, frame p50
  unchanged (8.2 vs 8.3 ms) on this machine. ≈3–6 k vertices per eye.
- CTest 60/60, including the new `debug_draw` (projection, near clipping, pixel floors, dash and
  vertex budgets, exact sphere silhouettes, label placement, slot colours, the drawn slot centre
  selecting that slot in the real gesture, wrist metrics reproducing `WristVisible`).

## Findings the overlay already exposed (mock grip estimate, to confirm on a Touch)

- With the left controller simply pointing forward the wrist card measures facing 0.89 and is
  reported **visible**, also while reaching for the foregrip. With real Touch grip/aim poses this
  may differ; the overlay's wrist label gives the numbers directly in the headset.
- `wristLayerFrames` stayed 0 although the wrist decision was visible: the native wrist capture
  produced no card on the mock. Not investigated here.
- Holster slots disappear while the left grip is squeezed (`l.squeezeValue < .45` gate), and all
  slots follow the head down when crouching (they are head-relative).

## Limits

- No depth test: lines draw over geometry (as in the Dishonored VR overlay).
- Labels are clamped to the frustum the runtime requests for that eye, ignoring the small
  rotation between render and display pose.
- Not seen in a headset; text size (min 17–20 px at 1440 px height) needs a wearer's verdict.
