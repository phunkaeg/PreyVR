# Touch panels and native hacking controls — 25 October 2026

Remote check: `git fetch origin` found no newer CosmicTangerine/Jordi branch
updates. Shared `collab/integration` was `00e04ca7d7fd13bb7d84a48fa5a46043809cb10b`;
the latest Jordi branch tips remain the 16–17 October updates already reviewed
and accounted for in integration.

## Player controls

Hold the left menu button, release it, and choose **VR Options → Input**.
**Touch Panels** and **Controller Hacking** are separate saved preferences,
both off by default. Ordinary rays, stick menu navigation and the game's
original puzzle mechanics remain available.

### Touch panels

- Enable Touch Panels. Menus, inventory and VR options move to a reachable
  distance: **65 cm**, adjustable from **45 to 90 cm**. Reset VR view to bring a
  panel back in front of you. Existing optical-frustum fitting remains active.
- Use the selected pointer controller (right by default). Approach from the
  front; the surface marker shows the touch location. Crossing about **2 mm**
  into the panel presses. Withdraw at least **15 mm** to release and rearm.
- The touchpoint is a **controller-derived virtual fingertip**, initially
  **80 mm along its aim pose**. Touchpoint Reach adjusts it from **30 to 150 mm**.
  This implementation does not query optical finger joints or add a native
  pointing-finger animation. Check alignment against your visible hand in VR.
- Flat panels and existing curved panels use their actual presented surface.
  Near-touch takes priority over the beam. Away from the surface, ray plus
  trigger resumes; a held trigger cannot turn withdrawal into a second click.
- Touching an inventory item uses the existing native mouse/drag path.
  Leaving the surface cancels its inventory capture before releasing. Native
  drag/drop behaviour still needs a game test. Stick navigation is the fallback.

New tracking/reference, panel/settings or modal-content contexts invalidate a
touch. Tracking loss, long sample gaps and implausible jumps cancel it. Moving
a panel over a stationary controller cannot create a press. At low game frame
rates a very quick poke may miss the required neutral hover and be refused;
withdraw and approach again. Only the selected pointer hand is active.

### Hacking minigame

Enable Controller Hacking before entering a puzzle. Release all controls on
entry; **left stick** moves through the puzzle and **X/Y/A/B** answer the native
prompts. X/Y are on the left controller, A/B on the right. Native Start/pause
remains available. Holding menu requests native pause before opening VR options,
so the settings panel does not hide an unpaused hacking timer.

The installed Steam base and patch profiles contain ten hacking actions: two
analog stick axes, four keyboard-only movement actions and four face-button
answers. There is no separate controller jump, boost or trigger action. The
mod forwards the six shipped Xbox bindings through the existing main-thread
input path; native action maps retain their press-only final-answer filtering.
It does not call the hacking handler directly or replace ball movement,
collision, stun, costs, timer or difficulty.

The validated native movie `DanielleHacking2` now counts as a modal screen and
uses the existing floating native-menu presentation. Pause/dialog/loading
overlays prevent it from acquiring controls. Hacking suppresses ordinary menu
navigation, gameplay input and mouse-pointer events while it owns input.
Both directions of the gameplay/hacking handoff wait for acknowledged cleanup,
including retries after refused zero/release events. A menu-content epoch
prevents queued events from one screen reaching a replacement screen.

This is controller-driven native hacking on a floating panel. Directly moving
the puzzle ball with your finger, changing puzzle physics, world-behind-inventory
rendering and new internal stereoscopic menu layers are separate work.

## Diagnostics

```text
ui.poke 1               # touch on (0 disables)
ui.poke.distance 65     # centimetres, 45..90
ui.poke.tip 80          # millimetres, 30..150
ui.pointer 1           # existing pointer selection: 0 left / 1 right / 2 off
ui.hacking 1           # controller hacking on (0 disables)
ui.hacking             # enabled, active, ready, cleanup, handoffBlocked, visible,
                       # axes, presses, releases, refused, stick
```

Values changed through VR options are saved in the normal VR preferences.
Diagnostic command changes take effect immediately; use the options menu to
persist preferences. A returned native post is delivery-path evidence, not
proof that a puzzle accepted the input. Custom user action-map remaps may change
which default Xbox keys reach the puzzle.

## Verification and next game test

The Steam binary/asset verifier checks the supported module SHA-256, concrete
instruction/string/token witnesses, base and overriding patch action maps,
archive member CRCs and a deliberately corrupted witness negative control.
See [static evidence](evidence/ui-hacking-2026-10-25/README.md). Full decoded game
assets are kept in ignored local build storage and are not redistributed.

Offline tests exercise flat/cylinder projection and crossing, stale samples,
panel-motion rejection, production native-pointer dispatch with stubbed engine
boundaries, VR options/settings, exact hacking movie ownership, native-key
delivery acknowledgement and both directions of input handoff. These are
source/harness checks. **No game, xr-sim session or headset test was run for
this change.** They do not prove visual comfort or puzzle acceptance.

In the next authorised game/headset test:

1. Leave both new preferences off; confirm ordinary menu rays and sticks still
   work, then enable Touch Panels. Check reachable placement and touchpoint
   alignment, including a curved menu edge and the flat inventory.
2. Hover, poke once, withdraw. Check one selection, inventory drag/drop and
   cancellation when leaving a panel. Try switching a screen while touching,
   holding trigger during withdrawal, recentering and temporarily losing tracking.
3. Enable Controller Hacking, enter a native hacking puzzle and release the
   controls. Check left-stick movement and every displayed X/Y/A/B prompt.
   Confirm no jump, fire, inventory opening or unintended mouse-mode prompt swap.
4. Pause during hacking, open/close VR options, then resume. Verify no stuck
   stick/button and that the native puzzle timer pauses and resumes as expected.
   Complete or fail a puzzle, and verify gameplay requires neutral controls
   before reacquiring input. Repeat with each feature disabled.
