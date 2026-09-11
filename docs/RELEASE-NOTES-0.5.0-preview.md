# PreyVR 0.5.0 preview

Pre-release. **This build has not had a full headset pass.** The 0.4.0 preview
had; this one's new inventory path was exercised once in a headset (every
captured frame submitted, zero refusals) and everything else was verified by
build, test suite and log, not by a wearer. Treat it as better-instrumented
rather than better-proven.

Supports the Steam Prey (2017) x64 build only. The launcher checks `PreyDll.dll`
before loading and never patches game files.

## Install

Unzip anywhere, double-click **`Start Prey VR.cmd`**. The first run asks where
`Prey.exe` is and writes `PreyVR.json` beside the launcher; edit that file to
change settings, no rebuild needed. Close the game before relaunching.

## What's new since 0.4.0

**You can adjust it while playing.** Prey has no user console, so the mod is
driven through a file it watches. **`Tune Prey VR.cmd`** does that for you:
run it while the game is up, type a command, read the reply.

- `ui.scale` (20–200) sets panel size and now persists in `PreyVR.json`.
- `ui.margin` (30–100, default 72) is the *real* size ceiling: the panel is
  shrunk until it fits inside that fraction of the frustum the headset reports,
  so past the point it binds, `ui.scale` does nothing. Raise it for a bigger
  panel at the cost of the corners nearing the lens edge.
- `ui.guide` shows or hides the control card under the menu (now off by
  default — it cost about a sixth of the panel).

**Launcher fixes.** Every double-click failed under Windows PowerShell 5.1
(`$PSScriptRoot` is empty in a param default there); the launcher said "next:
inject" and didn't; running it from `tools\` found no binaries. All fixed, and
the launcher now injects and enables VR itself.

**Hands.** Both hands are anchored at the camera centre instead of the reticle
ray origin — the right controller no longer drags the left hand.

**Prey's own console.** The console UI ships in the retail binary with no key
bound to it. `~` (the key below Esc) now opens it. **It draws its background but
no text** — see known issues.

**Separate inventory layer (experimental, off).** `ui.inventory 1` renders the
inventory through its own swapchain instead of the world images. Verified in
one headset run: 1766 captured, 1766 submitted, 0 refused. Side effect: the
desktop mirror loses the inventory while it's on, because the draw is
redirected out of the backbuffer. Nothing visible changes in the headset yet —
this is the foundation for a world-behind-inventory view, not the view itself.

**HUD input.** Fixed a duplicate reticle from stale HUD captures, and queued
menu inputs reaching gameplay after leaving a menu (the surprise weapon wheel).
`move.act` now reports wheel `pressed=`/`released=` counts.

**Diagnostics.** `hud.layer` reports per-movie capture counters; `ui.scale`
echoes what it stored plus the margin; refusals name why.

## Known issues

- **The console shows no text.** Background draws, glyphs don't. The likely
  cause is the retail build shipping without the engine's default font; not yet
  confirmed. `r_DisplayInfo 1` is the test — text there means the font is fine.
- No world visible behind or around the inventory; no stereo depth in the
  inventory. Both are the next steps and the plumbing for them is in this build.
- The quick-select wheel isn't steerable by the right stick, and no controller
  button confirms a selection.
- `F11`/`F12` need the Prey desktop window focused. Use the grips to recenter
  and `Tune Prey VR.cmd` for everything else.
- Prey renders one eye per frame; each submitted pair has one eye a frame stale.
  Performance is what it was.
- The launcher refuses to start over another running fleet game, by design.

## Verification on this build

Full test suite 39/39. Shipped `PreyVR.dll` SHA-256 begins `7F5727292A9E` and
is byte-identical to a clean rebuild of the tagged sources.
