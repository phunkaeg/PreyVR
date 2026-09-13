# Prey VR hologram preview

Connect your headset to the PC and start its OpenXR runtime, then double-click **Start Prey VR.cmd** in the extracted package. With Quest and Virtual Desktop, connect Virtual Desktop first and use your configured OpenXR runtime. Steam must be running. The launcher finds Prey or asks you to select `Prey.exe` once, then starts the game, loads the mod and enables VR.

Press **A** at the title screen. Point the right controller at **Continue** or **Load Game** and squeeze its trigger to click. Press **A** at the loading-screen prompt. The right stick and A/B remain available for navigating menus.

## Screens and controls

Menus and inventory appear on a screen about **2 metres away**, anchored where you opened them. A cyan beam and dot show where you are pointing. Squeeze the beam hand's trigger to click; hold it while moving to drag, then release. Moving outside the screen cancels an inventory drag and stops hovering. Release the trigger before trying again. **B** backs out one screen at a time.

The screen has a gentle curve when the runtime supports OpenXR cylinder layers and uses a flat panel otherwise. The menu and its control card fit inside both runtime eye frusta with room around the edges. **Both grips together**, or **F12**, reset the VR view and bring the screen back in front of you.

| Control | Gameplay | Menu / inventory |
| --- | --- | --- |
| Left stick | Locomotion | — |
| Right stick | Turning | Navigate |
| Right stick click | Open Favorites / weapon wheel | — |
| Right A / B | Jump / crouch | Select / back |
| Left X | Open inventory | Prey's X action, including Equip |
| Left Y | — | Prey's Y action |
| Right grip | Use / reload | Next tab, on release |
| Left grip | — | Previous tab, on release |
| Beam-hand trigger | Right hand: fire | Click / hold to drag |
| Other trigger | Existing gameplay mapping | Its native LT / RT page action |
| Left menu button | Pause | Pause / resume |
| Both grips together | Reset view | Reset view and screen position |
| F11 | Enable VR or retry startup | Enable VR or retry startup |
| F12 | Reset view | Reset view and screen position |

Open inventory with **X**. You can point at an item, or navigate with the right stick. For the native button route, highlight a weapon, press **X** to equip, then **B** to return to gameplay. The control card shows the VR bindings even when Prey changes its own prompts between mouse and gamepad icons.

Right-stick click sends Prey’s native middle-mouse Favorites Wheel shortcut. The wheel is part of the gameplay HUD; beam selection for this particular wheel has not been established. The new opening binding still needs a live check.

Release held controls after closing a menu before using them in gameplay. The mod waits for this neutral state to prevent an inventory input from immediately firing or moving. A trigger already held when a menu opens must also be released before it can click.

Touch controllers are the primary mapping. Index bindings are included: left A/B correspond to X/Y and left stick click opens pause. Other controller profiles do not yet have dedicated bindings.

## Display settings

After the first launch, edit `PreyVR.json` beside the launcher:

| Setting | Default | Choices |
| --- | --- | --- |
| `Width`, `Height` | `2016`, `2160` | Per-eye render size, independent of desktop DPI. `2688`, `2880` gives more detail at the same aspect. |
| `UiCurveDegrees` | `35` | `0` for flat; `1`–`60` for curvature when the runtime supports it. |
| `PointerHand` | `1` | `0` left beam, `1` right beam, `2` button navigation only. In button-only mode both triggers retain page actions. |
| `HudLayer` | `1` | `1` floating native gameplay HUD; `0` native scene HUD. |
| `UiGuide` | `0` | The control card under the menu. `0` hides it and gives its height back to the menu; `1` shows it, which is worth setting when someone is using the mod for the first time. |
| `UiScalePercent` | `100` | Panel size for the HUD and menus, `20`-`200`. Raise it if the UI sits too small in the headset, lower it if the edges fall outside your view. |

Restart Prey after changing these settings. Scale both render dimensions together to retain vertical view coverage. The default restores the tall aspect used in earlier headset tests; it is a starting preset, not automatic headset detection. Native menus may have unused space within the floating screen. World rendering should not be narrowed to 16:9 to remove that menu space.

Unversioned configurations containing the old `2560`, `1440` default are backed up and migrated once. Custom sizes are preserved. To deliberately keep 2560×1440, add `"ResolutionDefaultsVersion": 1` to `PreyVR.json`, or pass both `-Width 2560 -Height 1440` to the launcher. Command-line overrides must provide both dimensions.

The gameplay HUD stays on its own transparent, flat canvas. Health, ammo, interaction prompts and reticle remain Prey's own graphics. The reticle lies at the HUD's fixed depth; it is not a marker painted onto world geometry. Subtitles and world markers remain in the scene. In-world computer terminals have a separate native interaction path; this menu beam does not replace that path yet.

## Changing things while you play

Prey 2017 has no user console, so this mod is driven by a small file the game
watches. **`Tune Prey VR.cmd`**, beside the launcher, does that for you: run it
while the game is up, type a command, and the reply prints back.

```
preyvr> ui.scale 130
  ui.scale result=0
```

`help` lists the common ones. The two that decide panel size are `ui.margin`
(30-100, default 72) and `ui.guide`. `ui.margin` is the real ceiling: the panel
is shrunk until it sits inside that fraction of the frustum the headset reports,
so once it binds, raising `ui.scale` does nothing at all. Raising it is a trade
-- the reported frustum is already wider than the lenses show, so at high values
the corners go where you cannot see them. `ui.guide 0` is free by comparison and
gives back about a sixth of the panel.

The useful one for fit is `ui.scale` (20-200);
once you find your number, put it in `PreyVR.json` as `UiScalePercent` and every
later launch starts there. `vr.recenter` re-centres, and so do F12 and squeezing
both grips -- the grips are the ones that work with the headset on, since F12
needs the Prey desktop window focused.

You can also pass a command straight in: `Tune Prey VR.cmd ui.scale 130`.

If it answers `(no reply ...)`, the mod is not loaded in the running game --
start it with the launcher rather than from Steam.

## Recovery and current limits

If startup fails, the launcher reports the stage and a diagnostic folder under `%LOCALAPPDATA%\PreyVR\runs`. Resolve the headset/runtime issue, return focus to Prey, and press **F11** to retry. Reconnect a lost runtime before retrying. Recenter needs a fresh tracked headset pose.

This preview supports the verified **Steam Prey 2017 x64 build**. The launcher checks `PreyDll.dll` before loading and does not patch game files. Keep the package together. It refuses to start over another detected fleet game.

Read **VALIDATION.md** in this package for tested behaviour and outstanding checks. Physical headset comfort, controller feel and full campaign coverage still require player acceptance. Existing alternate-eye rendering and its performance limits remain.
