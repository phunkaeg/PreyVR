# Native wrist HUD test build — September 29

This is an experimental build from `codex/vr-options`, for the supported Steam
version of Prey (2017). It includes VR options, two-hand grip preferences,
optional psychoscope gestures, optional holsters and the native wrist HUD.

## Start and enable wrist status

1. Extract the ZIP into a new folder. Keep your previous working package.
2. Connect the headset and start your OpenXR runtime and Steam.
3. Double-click **Start Prey VR.cmd**. Select the game's `Prey.exe` if asked.
4. Continue/load a save and enter gameplay.
5. Hold the left controller's **Menu** button for about a second to open VR
   Options. Alternatively, pause the game and press **F10** on the keyboard.
6. Select **Equipment → Wrist Status → On**, using the controller beam and
   trigger or the stick/A controls. Close options and resume gameplay.
7. Raise your left hand, turn your palm toward your face, and look at the wrist.
   Adjust **Wrist Display Size** (80–140%) if needed.

Wrist Status, holsters and the psychoscope gesture default off. Settings persist
between sessions, including across packages. You can disable Wrist Status in
the same menu. F12 or both grips + left Y recenters the view.

## What to check

- Are the game's health, psi (when available) and suit widgets readable near
  the left wrist in both eyes? The normal forward HUD should remain available.
- Does the wrist display disappear when you lower/look away from your hand,
  open a menu, or hold a two-handed grip, and return afterward?
- Do the values/animations follow damage, healing, psi use and suit changes?
  Native widget visibility is preserved, so a meter the game hides can remain hidden.
- Is anything unrelated copied onto the wrist, such as the reticle or pickup prompts?
- Does it remain stable through weapon changes and save/load? Note any new
  stutter, flicker, unreadable tilt or large changes in apparent size.

Please report headset/runtime, whether each eye matches, which action caused
the problem, and a screenshot if practical. **Tune Prey VR.cmd** can send
`hud.layer` and `vr.options` for diagnostic counters; counters alone do not
confirm appearance. General controls are in **PLAYER-GUIDE.md**.

## Validation boundary

The exact DLL in this package passed 50 offline tests. A brief real-game run
through the headset simulator submitted 1,493 wrist-layer frames, with zero
native capture refusals/faults; lowering/raising and pause gating worked.
Simulator panel placement looked distorted, with a suspected compositor
matrix-layout issue. This build has **not passed headset visual acceptance**.
The wrist panel is not occluded by world geometry. Detailed results are in
**VALIDATION.md**. This test build does not replace the normal preview release.
