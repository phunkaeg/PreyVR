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
| `Width`, `Height` | `2560`, `1440` | Per-eye render size, independent of desktop DPI. Keep 16:9 for complete native menu framing. |
| `UiCurveDegrees` | `35` | `0` for flat; `1`–`60` for curvature when the runtime supports it. |
| `PointerHand` | `1` | `0` left beam, `1` right beam, `2` button navigation only. In button-only mode both triggers retain page actions. |
| `HudLayer` | `1` | `1` floating native gameplay HUD; `0` native scene HUD. |

Restart Prey after changing these settings. Lower render sizes such as 1920×1080 reduce GPU cost; 3200×1800 or 3840×2160 increase detail and cost.

The gameplay HUD stays on its own transparent, flat canvas. Health, ammo, interaction prompts and reticle remain Prey's own graphics. The reticle lies at the HUD's fixed depth; it is not a marker painted onto world geometry. Subtitles and world markers remain in the scene. In-world computer terminals have a separate native interaction path; this menu beam does not replace that path yet.

## Recovery and current limits

If startup fails, the launcher reports the stage and a diagnostic folder under `%LOCALAPPDATA%\PreyVR\runs`. Resolve the headset/runtime issue, return focus to Prey, and press **F11** to retry. Reconnect a lost runtime before retrying. Recenter needs a fresh tracked headset pose.

This preview supports the verified **Steam Prey 2017 x64 build**. The launcher checks `PreyDll.dll` before loading and does not patch game files. Keep the package together. It refuses to start over another detected fleet game.

Read **VALIDATION.md** in this package for tested behaviour and outstanding checks. Physical headset comfort, controller feel and full campaign coverage still require player acceptance. Existing alternate-eye rendering and its performance limits remain.
