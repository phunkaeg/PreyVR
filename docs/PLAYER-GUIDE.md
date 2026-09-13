# Prey VR player preview

Start your headset connection and its OpenXR runtime, then double-click **Start Prey VR.cmd** in the extracted package. For Quest with Virtual Desktop, connect to the PC first and use your configured OpenXR runtime. Steam must be running. The launcher finds the Steam installation or asks you to select `Prey.exe` once.

The launcher starts Prey, loads the mod, waits for the requested render size, and enables VR. Put on the headset when prompted. Press **A** at the title screen, select **Continue** or **Load Game**, and press **A** at the loading-screen prompt. No desktop mouse is needed for this route.

## Controls

| Control | Gameplay | Menu / inventory |
| --- | --- | --- |
| Left stick | Existing locomotion | — |
| Right stick | Existing turning | Navigate |
| Right A / B | Jump / crouch | Select / back |
| Left X | Open inventory | X action shown by Prey |
| Left Y | — | Y action shown by Prey |
| Right grip | Native use / reload button | Next tab, on release |
| Left grip | — | Previous tab, on release |
| Triggers | Right: fire | Previous / next page (LT / RT) |
| Left menu button | Pause | Pause / resume |
| Both grips together | Reset VR view | Reset and bring the panel in front of you |
| F11 | Enable VR or retry setup | Enable VR or retry setup |
| F12 | Reset VR view | Reset and reposition the panel |

Release a held stick, trigger, or button after closing a menu before using it in gameplay. This prevents an inventory input from immediately moving, turning, firing, or jumping.

To equip a weapon, open inventory with **X**, highlight it with the right stick, press **X** for Prey's Equip action, then **B** to return to the game. Follow the native button prompts for other items. B backs out one level at a time in nested screens.

The Touch profile has been tested in the injected game through xr-sim; physical controller acceptance remains. Index bindings are included: left A/B correspond to X/Y, and **left stick click** opens pause. Other controller profiles do not yet have dedicated mappings. Headset rendering uses OpenXR; the panel fits both runtime eye frusta, including their asymmetry.

## Display

Menus and inventory open on a panel about **2 metres** away, anchored where you opened them. Reset view to bring a panel back in front of you. A separate card shows the menu controls. The combined layout stays within conservative limits of **60° horizontally and 40° vertically**, shrinking further for narrower runtime frusta.

The native gameplay HUD is captured once into a transparent target and displayed at its own depth. Its graphics and interaction prompts stay Prey's own. The reticle is remapped to the smaller HUD canvas; when aiming beyond the panel it clips out instead of marking its edge. This is a HUD reticle at a fixed depth, not a world-surface hit marker. World markers and subtitles remain in the scene.

The default render size is **2016 × 2160 per eye**, independent of monitor DPI. This restores the tall aspect used in earlier headset testing; **2688 × 2880** offers more detail at the same aspect and higher GPU cost. Edit `Width` and `Height` in `PreyVR.json`, then restart Prey. Menus can have unused space within their floating panel; do not switch the world render to 16:9 just to fill that panel. This is a starting preset, not automatic headset-resolution detection or a guarantee of complete FOV coverage on every headset.

The launcher migrates the old, unversioned **2560 × 1440** default once, backing up `PreyVR.json` first. Custom sizes remain unchanged. To intentionally retain that old size, add `"ResolutionDefaultsVersion": 1` to the JSON, or launch with both `-Width 2560 -Height 1440`. A single command-line dimension is rejected. `HudLayer` remains `1` for floating HUD or `0` for the native scene HUD.

## Recovery and support

If startup fails, the launcher names the failed stage and the diagnostic folder under `%LOCALAPPDATA%\PreyVR\runs`. Resolve the headset/runtime issue, return focus to Prey, and press F11 to retry. Reconnect the runtime before retrying a lost XR session. F12 and the grip chord need a fresh tracked headset pose.

This package supports the verified **Steam Prey 2017 x64 build** only. The launcher checks `PreyDll.dll` before loading anything; it does not patch game files. Keep the package files together. It also refuses to launch over another detected fleet game.

This is a preview. The new interface has simulator and in-game checks; headset comfort, every weapon, and every in-world terminal still need player acceptance. Existing alternate-eye rendering and its performance limits remain.
