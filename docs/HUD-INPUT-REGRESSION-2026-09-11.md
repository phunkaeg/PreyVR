# HUD duplication and unsolicited weapon wheel: candidate fixes

The wearer reports both symptoms before opening inventory. Inventory close is
therefore not a required trigger. This step changes owned mod code and tests it
offline; it does not establish the cause of the headset symptoms by replay.

## Evidence from the reported session

`player-20260911-112538/PreyVR.log` records the main swapchain as format 29 (sRGB)
and a successful separate inventory submission. The supplied counters show
1766 PDA captures and 1766 inventory layer frames, zero capture refusals and a
live consumer. The wearer reports inventory absent on desktop but visible in VR.
That supports working redirection/submission. Counter equality is not a full
per-frame trace, and a black desktop alone does not prove a native render gate.

The available log does not record wheel-source presses, so it cannot identify
whether the reported wheel originated from the controller binding, a delayed
menu event, native input or another path.

## Changes

- HUD textures are drained at each render-frame boundary, including skipped XR
  frames. The former freshness allowance could reuse a capture after native HUD
  rendering resumed.
- Each retained world eye records whether its HUD was captured separately.
  A separate binocular HUD is submitted only with a current capture and two
  world images that had their HUD removed. This closes the startup/fallback case
  where one eye still contains a baked-in reticle and then gets another overlay.
- Controller-menu taps carry the producing menu epoch in queue metadata, never
  in native event padding. The main-thread drain rejects stale presses after a
  menu closes or closes/reopens. This applies to startup menus as well as PDA.
  Menu Y and D-pad taps otherwise reach gameplay's weapon-selection consumers.
  Releases still pass if their presses were actually delivered, even when the
  press itself closed the menu. Orphan releases are discarded. Pause and explicit
  diagnostic commands remain unscoped.
- Successful wheel press/release posts log `detail=wheel_input`, the key and
  `source=right_stick_click`. Interaction reports include release counters;
  status includes `menuDiscarded` for expired scoped events.

No controller remapping, deadzone change, native reticle suppression or world
rendering experiment was made. The known right-stick click binding is retained.
These are confirmed code-path defects and guards, not a confirmed explanation
for every instance of either reported symptom.

## Verification and next trial

Logs and hashes: `evidence/hud-input-regression-2026-09-11/`.
Regression cases cover one old eye containing a HUD, both cleaned eyes, capture
loss/recovery, mono capture freshness, queued Y/D-pad after a context change,
the release of a menu-closing press, orphan-release rejection and preservation
of pair metadata across ring wrap. The full CTest run also retains real WARP
inventory-copy and linear/sRGB tests.

Use a fresh boot of the candidate, first without opening inventory. If the
reticle doubles, check each eye separately: two images in a single eye supports
duplicate drawing; one per eye with poor fusion points to stereo/depth alignment.
Temporarily compare `hud.layer 0` against `hud.layer 1` to isolate the separate
HUD contribution. This toggle needs subsequent eye frames to refresh.

If the wheel reappears, preserve the run log and the `move.act` report. A logged
mod wheel press narrows the source to the sampled stick-click route. No such
post while the wheel appears rules out that route for that observation, but
does not by itself identify the alternative. Compare `menu.nav 0`/`menu.nav 1`
while in gameplay to isolate queued controller navigation; pointer selection
and locomotion are separate routes. Do not infer a hardware fault without this
source evidence.
