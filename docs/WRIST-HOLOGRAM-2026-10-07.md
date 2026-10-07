# Health, suit and psi as a hologram on the left forearm

Branch `jordi/polishing`. Measured on the mock (xr runtime simulator, Steam `PreyDll.dll`), not yet in a
headset. It builds on Funk's native wrist work: the filtered replay of the HUD's status subtree
(`NativeHudIsolation.h`), the GPU fitter and the wrist quad. Everything is switchable at runtime:
`wrist.anchor 0` brings back the grip-anchored card, `wrist.face 0` puts the meters back in front of the eyes,
and the VR option WRIST STATUS turns the whole feature off.
Evidence: [`evidence/wrist-hologram-2026-10-07/`](evidence/wrist-hologram-2026-10-07/)
(`report.txt`, in git; `wrist_hologram_sheet.jpg`, nine cases, on disk only: images are git-ignored).

## Why the wrist never showed anything

The card never appeared on the mock (`wristLayerFrames=0`). The reason is in the game, not the replay:

- The profile has `Option.hud_showPlayerState=0`, which is the dynamic player state. With it, the HUD
  **closes** health/suit/psi a few seconds after each change. The meters are not faded out: the clips are
  hidden. Live, with the HUD idle: `_root.safe.quadrant_SW.health_mc._visible = false`,
  `status_effects._visible = false`, `_alpha` 100.
- A hidden clip is never displayed, so the filtered replay never traversed a meter. Coverage failed on every
  frame: `nativeWrist={captured=0 refused=1135 fault=0}`. The fallback "retry on a future frame" was correct
  and never succeeded.
- `hud.call healthOpen` opened health and suit (psi has its own `psiOpen`). The replay then captured, but
  `healthClose` closes the whole quadrant again and `healthStatic 1` does not prevent it.

## What changed

### 1. The native status, presented for the wrist (HudLayer)

GFx `SetVariable` is the movie root's slot `+0x80` = `0x18ABCC0`, right before `GetVariable` `+0x88` =
`0x18ABE10`. It takes `(root, path, const GFxValue*, SetVarType)`, converts the value with `0x183F4F0` and
logs an error for a null path. The `GFxValue` layout matches `NativeHudValue`: number = type 3 with a
double payload, bool = type 2.

Inside `CaptureNativeStatus`, around the one filtered replay (same callback, same native locks as `Resolve`),
`StatusOverride` works like this:

- **Reads** first, and accepts only plain numbers and bools (nothing to release, safe to write back). The
  values are the quadrant's `_xrotation`, `_yrotation` (authored -15/-15, `Health`'s init), `_xscale` and
  `_yscale` (50 here, the widget scale), and `_visible` of `health_mc`, `psi_mc`, `armor_mc`, `bg_mc`,
  `bg2_mc` and `line_mc`.
- **Writes**: rotations 0 (flat), scale x4 (`wrist.native <percent>`, default 400; the eye target renders it
  at about 1:1 for the card), the three meters visible, and the backdrop and top line hidden (the glass
  replaces them; `wrist.native 400 1` keeps them).
- **Draws** the replay through Funk's filter.
- **Restores** every written value in reverse order. The normal draw has already happened, so neither the
  gameplay HUD nor the movie's own logic ever sees the change. Live: `health_mc._visible` stays `false`
  across thousands of replays (`layout=N/0`, no refusal).

`status_effects` keeps its native visibility, so an effect shows on the wrist only while the game shows it.

### 2. The meters leave the face (HudLayer)

While the wrist owns the status (`WristMovesStatusOffFace()`: the option is on and `wrist.face 1`, the
default), the gameplay HUD's normal draw goes through the same sprite hook with a per-draw filter. It
excludes `health_mc`, `psi_mc`, `armor_mc`, `bg_mc`, `bg2_mc`, `line_mc` and `status_effects`. The handles
are resolved for that one draw and released after it. If any path fails to resolve, the HUD draws unchanged.
`climb_mc`, `flashlight_mc`, `stealth`, `pickup`, `power_mc` and the other quadrants stay on the face.

- The HUD capture is wanted for this filter even when the HUD layer is off.
- The sprite hook is installed as soon as the wrist owns the status, not only at the first look.
- Measured with the meters opened by the game: the face HUD quad has **0** non-transparent pixels.
  With `wrist.face 0` it has 19,990.

### 3. The card on the drawn forearm (WristLane, new; pure rules in `WristHolo.h`, tested)

- **Animation thread.** `CaptureMarks` publishes `WristArmFrame` from the same marks as `pose.marks`, in
  OpenXR space. The left wrist joint, the forearm joint (elbow) and the drawn hand's palm normal place the
  card. The right wrist, grip point, aim and weapon/free-arms are used for occlusion. Frames are keyed by
  `tracking.displayTime` in a ring of 16.
- **XR thread.** `DecideWrist` takes the arm of **the frame being submitted**, keyed by
  `eyeDisplayTime[0]`. So the card is composited with the same arm pose the eye images were rendered with,
  and it does not slide over the sleeve when the arm moves. Measured: 6,559 of 6,581 frames exact, 22 one
  frame older, `armLagMs=0.0`.
- **Placement** (`wrist.place along hover width tilt`; defaults 75 mm, 42 mm, 125 mm, 40 deg):
  - The centre is 7.5 cm from the wrist joint towards the elbow and 4.2 cm out through the back of the
    forearm. The back of the forearm is `-palm`, made perpendicular to the forearm (the forearm's twist
    follows the hand's).
  - The face turns about the forearm towards the eye, at most 40 deg.
  - Reading direction: along the forearm while that is within 25 deg of the viewer's horizontal, rolled in
    the card's plane towards it beyond that, fully level from 60 deg. With the arm held straight out, the
    text is level (`roll=-88`) instead of reading sideways.
- **Look test** (`wrist.gate`):
  - Enter: the back of the forearm faces the eye within 60 deg (`facing>=0.50`), the card is within 35 deg of
    the gaze (`viewing>=0.82`), the distance is 0.12-0.80 m, the hand is slower than 1.2 m/s, and all of this
    holds for 80 ms.
  - Exit (hysteresis): 77 deg and 52 deg.
  - Fades in over 140 ms while unfolding from 90 % to 100 % size, and fades out over 200 ms.
  - Occlusion or the gameplay gate closing removes it in 70 ms or at once.
  - It ticks the left controller once on opening (`haptics::Event::WristOpened`, 0.16 x strength, 14 ms;
    `wrist.haptic 0` turns it off).
- **Host gate** (unchanged conditions, now one bit each in `wrist.report gate=`): 1 option, 2 views,
  4 rendered, 8 no menu panel, 16 no VR options, 32 gameplay input, 64 not two-handed, 128 no recentre in
  flight, 256 head and left hand tracked.
- **Occlusion** (`wrist.occlusion` mask):
  - 1, scene: while the card is a candidate, the game thread runs three `QuerySegment`s from the camera to
    the card's centre and both ends (5 cm out of the eye, 1.5 cm short of the card).
  - 2, own hand: the eye-to-card segment against the drawn right wrist (5 cm) and grip (6 cm), plus, with a
    weapon drawn, a capsule from 12 cm behind the right grip to 40 cm ahead along the right aim (5 cm).

### 4. The glass (NativeWristTexture)

- **Texture.** It is now 1216 x 480 (the meters' strip; `InventorySwapchain` needs at least 640 x 480).
- **Framing is held.** The alpha bounds grow at once but shrink only after 60 frames inside, so the meters do
  not breathe with every animation. The new pass `HOLD_BOUNDS` runs as one thread.
- **Panel** (hologram only). It is a premultiplied rounded rectangle under the meters: dark glass (alpha 0.62
  at the top to 0.50 at the bottom, a faint scan), a thin bright rim, and a fixed margin in output pixels.
  Minification uses 4 taps. `alpha` fades all four channels.
- **Missed captures.** A frame without a new capture refits the retained source (`Refit`) for up to 250 ms
  instead of blinking out. The HUD's draw and the XR frame are not lock-stepped.
- **Tests.** `NativeWristTests` adds the panel and fade checks: transparent corner, glass alpha, the meters
  over the glass, half fade exact to 1 LSB, and refit with a new fade over the same pixels.

### 5. Option default and the debug overlay

- WRIST STATUS now **defaults on** (`VrOptions.cpp`, `VrOptionsRuntime.cpp`; tests updated). Its text
  describes the watch gesture.
- `dbg.draw` (layer 8) outlines the hologram's own card with its live look test
  (`face 0.86/0.22 view 0.91/0.62`: value / threshold now) instead of the grip card.

## Measured on the mock

| Case | Result |
|---|---|
| Watch check (forearm across the chest, back up, looked at) | shown; face 0.86, view 0.91, 0.35 m, tilt 26, roll -10 |
| Look ahead (head pitch 0) | hidden (view 0.43) |
| Wrist rolled: back sideways / -60 / -30 deg | hidden (facing -0.57) / shown (0.53) / hidden (-0.12) |
| Arm held straight out | shown, text level (roll -88) |
| Carrying a towel in the left hand | shown, on the back; the towel is behind it (gate 511) |
| Right hand between the eye and the card | hidden (`handOcc=1`), back when moved away |
| Forearm pushed into a locker | hidden (`sceneOcc=1`; 144 / 341 segment hits); `wrist.occlusion 2` shows it stuck to the locker |
| GLOO held two-handed, wrist turned and looked at | hidden (`eligible=0`); released and looked at: shown |
| Inventory open | hidden (`gate=471`: menu panel, no gameplay input); closed: back |
| Lateral sweep past the gaze, 0.25 s (mean 4.4 m/s) | 0 openings; the same pass in 2 s: 1 |
| Game closes the meters, then `healthUpdate 27` | the wrist shows 27 (`health_mc._visible` still false) |
| Face HUD with the meters opened by the game | 0 px of status (A/B `wrist.face 0`: 19,990 px) |
| Cost, visible vs hidden | frame P50 7.10 vs 6.98 ms; XR service P50 258 vs 231 us |
| `wrist.anchor 0` | the old grip card, flattened meters, no glass |

CTest 66/66, including the new `wrist_holo` test and the extended `native_wrist` test.

## Commands

`wrist.report` (gate bits, look test, card, arm match, counters), `wrist.anchor 0|1`,
`wrist.place <along mm> <hover mm> <width mm> <tilt deg>`,
`wrist.gate <enterFacing> <exitFacing> <enterViewing> <exitViewing> [dwell ms]` (permille),
`wrist.native <scale %> [background 0|1]`, `wrist.face 0|1`, `wrist.occlusion <mask>`, `wrist.haptic 0|1`,
`wrist.force 1` (skip the look test; captures only).

Also new: `hud.var <path...>` / `hud.setvar <path> <number>`. They read or write ActionScript properties of
the gameplay HUD inside its display callback, and the report lists each value afterwards.

Mock: `WristTwoHand.ps1` (two-handed hold), `WristSweep.ps1` (sweeps), `wristshot.py` (crops the card
from a capture; renders the card texture over dark and light).

## Not verified / open

- **Headset.** The gesture thresholds, placement and size were tuned on the mock's arm poses with an
  estimated grip. The commands above adjust them live (`commands.txt`).
- **Gate closed once.** On one mock session, while carrying a towel after a two-handed test, the gate
  stayed closed for over a minute with no menu open. Every predicate checked afterwards was open, and it did
  not reproduce in four tries. The cause is unknown; `wrist.report gate=` now names the closed condition.
- **Status effects.** No effect was active in the test save, so their look on the wrist is unseen.
- **Other HUD option values.** With player state "always", the meters are visible anyway, and the override
  only flattens and enlarges them.
- **Left-handed play** is not considered: the hologram is on the left forearm only.
