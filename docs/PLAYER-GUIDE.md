# Prey VR player preview

This guide describes the integration branch, including changes after the released
v0.5.1 preview. New comfort settings below require an integration build.

Start your headset connection and its OpenXR runtime, then double-click **Start Prey VR.cmd** in the extracted package. For Quest with Virtual Desktop, connect to the PC first and use your configured OpenXR runtime. Steam must be running. The launcher finds the Steam installation or asks you to select `Prey.exe` once.

The launcher starts Prey, loads the mod, waits for the requested render size, and enables VR. Put on the headset when prompted. Press **A** at the title screen, select **Continue** or **Load Game**, and press **A** at the loading-screen prompt. No desktop mouse is needed for this route.

## Controls

| Control | Gameplay | Menu / inventory |
| --- | --- | --- |
| Left stick | Head-relative locomotion | — |
| Right stick | 45-degree snap turn (default) | Navigate |
| Right stick click | Quick weapon wheel | — |
| Right A / B | Jump / crouch | Select / back |
| Left X | Open inventory | X action shown by Prey |
| Left Y | — | Y action shown by Prey |
| Right grip | Tap: reload (never uses what the left hand points at). Hold: holster the weapon. With the weapon holstered: draw it again | Next tab, on release |
| Left grip | Use what the left hand points at (tap or hold, as Prey's prompt says); keep it closed to hold what you picked up, open it to drop or throw; hold a long weapon's support grip | Previous tab, on release |
| Triggers | Right: fire (hold for automatic fire and to charge the Disruptor); with a grenade: hold it, let go during a throwing motion to throw it | Previous / next page (LT / RT) |
| Left menu button | Tap: pause. Hold: VR options | Tap: pause / resume. Hold: VR options |
| Both grips + left Y | Reset VR view | Reset and bring the panel in front of you |
| F11 | Enable VR or retry setup | Enable VR or retry setup |
| F12 | Reset VR view | Reset and reposition the panel |
| F10 | — | Open / close VR options |

Release a held stick, trigger, or button after closing a menu before using it in gameplay. This prevents an inventory input from immediately moving, turning, firing, or jumping.

For two-handed aiming, bring your left hand to the weapon's foregrip and squeeze
the left grip. Keep it squeezed while aiming with both hands; release to return
smoothly to one hand. A squeeze away from the weapon does nothing. Changing
weapons, opening a menu or losing tracking requires releasing and grabbing again.
To recenter, hold both grips, then press left Y; F12 also works.

To use something, point at it with the left hand: the cyan ray ends on what Prey selected (it bends to it when
Prey picked it from near the line) and Prey shows its usual prompt. Squeeze the left grip briefly for the
prompt's tap action (take, carry, search, take the highlighted item of a locker) or keep it squeezed for the
hold action (drag a body, carry a heavy object). What you pick up stays in the left hand for as long as the grip
is closed: a light object flies to the hand and is held by the side you pointed at, a heavy one floats ahead and
follows the hand, a body is dragged from it. Open the hand to drop it, or open it during a throwing motion to
throw it with the hand's speed; the right trigger still throws it the game's way. `carry.hold 0` restores
"squeeze again to drop".

**Grenades** (EMP, Recycler, Nullwave, Lure) sit in the right hand. Hold the right trigger, throw with your
arm and let go of the trigger: the grenade leaves at that instant with your hand's speed and direction, and comes
down about where a real ball thrown the same way would. Let go with the hand still and it drops at your feet.
Looking at a nearby surface no longer sticks the grenade to it. `grenade.hand 0` restores the game's own
charged throw along the view.

With no weapon drawn -- holstered, or while carrying something -- both hands stay visible at the controllers
(open; the left one closes around what it carries). Hold the right grip to holster the weapon, squeeze it again
to draw it. `arms.free 0` lets the game lower the arms out of view again. A squeeze on the foregrip, at the left-hip medkit slot or in the recenter chord never uses
anything. `use.hand 0` restores the previous layout (right grip uses and reloads along the weapon's aim).
`aim.twohand 0` disables support aiming; `aim.twohand 1` enables it (default).

To equip a weapon, open inventory with **X**, highlight it with the right stick, press **X** for Prey's Equip action, then **B** to return to the game. Follow the native button prompts for other items. B backs out one level at a time in nested screens.

The Touch profile has been tested in the injected game through xr-sim; physical controller acceptance remains. Index bindings are included: left A/B correspond to X/Y, and **left stick click** opens pause. Other controller profiles do not yet have dedicated mappings. Headset rendering uses OpenXR; the panel fits both runtime eye frusta, including their asymmetry.

## Comfort settings

### In-headset VR options (integration build)

Hold the **left menu button for about 0.65 seconds**, then release it. In gameplay,
the mod first requests the native pause menu and waits until it is observed.
Point at the options panel and pull the trigger, or use the **right stick** to
select rows and adjust values. **A** changes the selected value, **grips** change
tabs, and **B** returns to the native menu. Releasing the controls is required
before they can operate the screen behind it. F10 is a desktop shortcut while a
native menu is already open; `vr.options 1` is the diagnostic equivalent.

- **Comfort:** snap angle / smooth turning, head-relative movement, smooth-turn
  speed, and reset VR view.
- **Hands:** two-hand aim on/off, hold/toggle foregrip, snapped/free support hand,
  and the optional psychoscope gesture.
- **Interface:** size, visible-area limit, panel curvature and controls guide.
- **Equipment:** optional body holsters, wrist status, wrist-card size and clear
  holster assignments. Both new features default off.
- **Abilities:** psychic targeting (Original / Head / Left Controller) and the
  optional left-hip medkit slot. Original targeting and slot off are the defaults.
- **Setup:** calibrate posture, reset VR view, and **Debug Overlay (Testing)**,
  which draws each hand's ray and target, the body slots, the wrist test and the
  foregrip region in the world. Off by default; see
  [the overlay note](DEBUG-OVERLAY-2026-10-06.md).
  **Shots from the Muzzle (Experimental)** makes every shot leave the weapon's
  muzzle along your controller's aim, so it lands where the barrel points at any
  distance, also from the hip or around a corner (otherwise the shot is aimed from
  your eyes and misses by up to several degrees close by). Off by default;
  `Start-PreyVR.ps1 -MuzzleAim` turns it on for a session. See
  [the shot note](SHOT-RAY-2026-10-06.md).

**Psychic targeting:** equip a power using Prey's normal quick wheel. Choose Head
or Left Controller on the Abilities tab, then hold **left trigger** to target and
release it to cast. Head follows where you look; Left Controller follows the
left controller's aim axis. Release the support grip before targeting. The game
keeps its native power costs, cooldowns, target filters and unlock restrictions.
Menus, recentering, tracking loss and long frame gaps cancel a pending cast;
release the trigger before trying again. Original restores the existing controls.

**Medkit slot:** once enabled, bring the left controller to your left hip,
release grip, then squeeze **left grip**. A short pulse confirms selection.
Keep grip held and press **left trigger** to request one medkit use; you may
move the hand away from the hip first. Release grip before another use. Prey
checks the native inventory and whether the item can be consumed, so an empty
inventory or full health does not force a heal. The slot uses an estimated body
position and currently has no visible medkit prop. This feature and psychic
targeting have offline/static checks; native and headset acceptance remain.
See [implementation and acceptance checks](PSI-MEDKIT-2026-09-30.md).

**Holsters:** with a weapon equipped, bring the right controller to your right
hip or across to your left chest, release grip, then squeeze. An empty slot stores
that weapon and requests its normal stow animation. Squeeze the same slot again
to draw it. If its weapon is already equipped, the squeeze stows it. Drawing a
different slot switches through Prey's native equipment transition. Release
between actions; drifting into a slot with grip held does nothing. Two-handed
aiming, menus and recenter take priority. Clear assignments on the Equipment tab
to bind different weapons; weapons remain in your inventory. Assignments are
session-local, not saved. This first version uses inferred hip/chest positions,
not tracked torso/hip hardware, and has no visible holstered weapon props.

**Wrist status (native HUD candidate):** turn the left palm up and look at the wrist.
The September 28 candidate captures the game's health, psi and suit widgets onto
a transparent panel, preserving their native artwork and visibility. Size adjusts
from 80% to 140%. It hides in menus, during two-handed aiming, on tracking loss
and when looking away. It supplements the forward HUD; reticles, prompts and
native status widgets remain unchanged. Off by default; implemented and tested
offline, awaiting in-game and headset verification.
See [implementation and test details](NATIVE-WRIST-IMPLEMENTATION-2026-09-28.md).

**Toggle foregrip:** squeeze near the foregrip to attach; release the grip button
and squeeze again to detach. **Free support hand** keeps its visual position on
the controller while both hands still steer the weapon. Weapon changes, menus,
tracking loss and recenter invalidate the attachment.

**Psychoscope gesture is off by default.** Once enabled, bring the left hand to
your forehead, squeeze, and pull down at least about 14 cm. Release before the
next gesture. A lift from near the eyes requests the same native toggle to put it
away. Each deliberate stroke requests one toggle; the game retains its unlock,
cinematic and movement restrictions. This does not add a visible visor prop.
Gesture feel and native acceptance still need headset testing.

Menu choices save automatically to **`%LOCALAPPDATA%\PreyVR\vr-options.ini`**.
They take precedence over launcher defaults for the settings listed above.
To return those settings to launcher defaults, rename that file with Prey closed.
Resolution, runtime and reference-space selection remain launcher settings.

This batch has offline tests and a production-rendered menu preview, but **has
not been run in Prey or accepted in a headset**. See
[implementation and validation](VR-OPTIONS-2026-09-27.md).

### Launcher settings

The launcher saves these options in `PreyVR.json` beside the launcher. Edit the
existing fields with Prey closed, or pass the corresponding PowerShell parameter
to `Start-PreyVR.ps1`. Explicit parameters override saved values.

| Setting | Default | Accepted values |
| --- | --- | --- |
| `SnapTurnDegrees` | `45` | `15`..`90`; `0` selects smooth turning |
| `HeadRelativeMovement` | `1` | `1` follows headset yaw; `0` uses native body axes |
| `ReferenceSpace` | `"auto"` | `"auto"` prefers LOCAL_FLOOR, then STAGE, then LOCAL; `"local"` forces LOCAL |

For this session only, use `move.snap 30` or `move.headrelative 0` in
**Tune Prey VR.cmd**. Their bare commands report the current setting. Runtime
commands do not update the JSON. Return the stick to neutral after changing modes.

`view.height` reports the chosen reference space and calibrated height.
`view.calibrate` sets a new standing/seated baseline from the current tracked
head position. Ordinary recenter preserves that height baseline. Floor tracking
does not yet implement physical crouch, avatar-height matching or a vignette.

## Display

Menus and inventory open on a panel about **2 metres** away, anchored where you opened them. Reset view to bring a panel back in front of you. An optional card shows the menu controls (`UiGuide: 1`; hidden by default). The combined layout stays within conservative limits of **60° horizontally and 40° vertically**, shrinking further for narrower runtime frusta.

The native gameplay HUD is captured once into a transparent target and displayed at its own depth. Its graphics and interaction prompts stay Prey's own. The reticle is remapped to the smaller HUD canvas; when aiming beyond the panel it clips out instead of marking its edge. This is a HUD reticle at a fixed depth, not a world-surface hit marker. World markers and subtitles remain in the scene.

The default render size is **2016 × 2160 per eye**, independent of monitor DPI. This restores the tall aspect used in earlier headset testing; **2688 × 2880** offers more detail at the same aspect and higher GPU cost. Edit `Width` and `Height` in `PreyVR.json`, then restart Prey. Menus can have unused space within their floating panel; do not switch the world render to 16:9 just to fill that panel. This is a starting preset, not automatic headset-resolution detection or a guarantee of complete FOV coverage on every headset.

The launcher migrates the old, unversioned **2560 × 1440** default once, backing up `PreyVR.json` first. Custom sizes remain unchanged. To intentionally retain that old size, add `"ResolutionDefaultsVersion": 1` to the JSON, or launch with both `-Width 2560 -Height 1440`. A single command-line dimension is rejected. `HudLayer` remains `1` for floating HUD or `0` for the native scene HUD.

## Recovery and support

If startup fails, the launcher names the failed stage and the diagnostic folder under `%LOCALAPPDATA%\PreyVR\runs`. Resolve the headset/runtime issue, return focus to Prey, and press F11 to retry. Reconnect the runtime before retrying a lost XR session. F12 and the grip chord need a fresh tracked headset pose.

This package supports the verified **Steam Prey 2017 x64 build** only. The launcher checks `PreyDll.dll` before loading anything; it does not patch game files. Keep the package files together. It also refuses to launch over another detected fleet game.

This is a preview. The new interface has simulator and in-game checks; headset comfort, every weapon, and every in-world terminal still need player acceptance. Existing alternate-eye rendering and its performance limits remain.
