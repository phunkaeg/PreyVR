# Viewmodel Tweaks & Ironsights (Prey 2017)

A [Chairloader](https://github.com/thelivingdiamond/Chairloader) mod that adds aim-down-sights to Prey and
lets you reposition, rotate and re-FOV the first-person weapon - all live, from an in-game window.

**Features**

* **Ironsights.** Hold (or toggle) the aim key and the weapon comes up to the sights, exactly aligned with the
  crosshair, with a camera zoom, its own weapon FOV and a configurable look-sensitivity while aiming. Tuned
  poses for every stock weapon are built in; each one can be adjusted with sliders or nudge keys.
* **Weapon feel.** Head-bob coupling (still sights or a little lag), the game's procedural recoil and the
  fire animation's own kick while aiming (both per weapon), a per-shot coupling boost, and bullet-spread
  multipliers for aiming vs hip fire on the pistol and shotgun (the only weapons with spread).
* **Feel.** A sprint pose (the weapon drops and tilts while you run, with extra sway; optionally no aiming
  while sprinting; off in zero-G unless you want the thruster boost to count), sway and settle while aiming (a slow figure-eight that is large when the sights come
  up, after sprinting or while moving, and calms down as you hold still; a hold-breath key), and GoldenEye
  style view drag (the weapon follows your turns on a spring, leading or lagging).
* **Viewmodel.** Global standing and crouch offsets, per-weapon hip offsets, weapon FOV (the stock 55 is
  fixed in vanilla), world FOV, sprint look-sensitivity.
* **Hip fire helpers.** Weapon convergence (the barrel points at what the crosshair is over), wall pull-back
  (the weapon slides towards you near walls, more for longer weapons), no aiming while the muzzle would be
  in a wall, while reloading or while switching weapons; the offsets ease out during reloads so the support
  hand meets the shells.
* **Reticle.** Centred / custom position, weapon reticle / dot / hidden, hide while aiming. A hidden reticle
  still comes back as the cursor while you use an in-world screen.

Nothing is stored in save games; uninstalling leaves your saves untouched.

## Requirements

* Prey (2017) on PC with **Chairloader 1.3.x or newer** installed and the game patched by ChairManager
  (the mod hooks the game build Chairloader supports; on another build it disables itself with a warning).

## Installation

1. In ChairManager: **Install Mod** and pick `Vee.ViewmodelTweaks-<version>.zip` (or unzip it so that
   `Prey/Mods/Vee.ViewmodelTweaks/ModInfo.xml` exists).
2. Enable **Viewmodel Tweaks & Ironsights** in the mod list and **Deploy**.
3. In the game, go to **Options -> Controls** and move *Select equipped power* off the right mouse button
   (the mod uses RMB to aim and blocks the game's action on it while enabled). Or pick another aim key in
   the mod's Quick Settings.

Updating: install the new zip the same way. Your settings live in `Prey/Mods/config/` and are kept.

## Usage

Press **F1** in game for the Chairloader GUI; the *Viewmodel Tweaks* window opens (toggle it from the
*Viewmodel Tweaks* menu in the top bar). While the window is open the cursor is shown and the camera is
frozen so you can drag sliders with the game running; press F1 again to play.

* **Quick Settings** - the essentials: offsets on/off, weapon FOV, world FOV, sprint sensitivity, aiming
  (key, hold/toggle, ironsights FOV, camera zoom, sensitivity), convergence, wall pull-back, reticle.
* **Global** - standing / crouch offsets for all weapons, convergence and wall pull-back tuning.
* **Weapon** - the equipped weapon: hip offset, aim pose, allow aiming, wall pull-back distance, firing
  feel, bullet spread. Built-in defaults exist for every stock weapon; "Reset to built-in" gets them back.
* **Feel** - sprint pose, sway & settle while aiming (with the hold-breath key), view drag.
* **Aim** - key binding, transition time, zoom, ironsight motion (head-bob coupling, recoil, sway), sensitivity.
* **FOV**, **Nudge keys**, **Options**.

### Tuning a weapon's aim pose

1. Equip the weapon, open the window, tick *Toggle instead of hold* in Aim, aim (RMB), then open **Weapon**.
2. *Forward* moves the weapon towards/away from you, *Up / Down* raises the sights onto the crosshair;
   *Right / Left* should stay 0 for a centred weapon. Small *Pitch* fixes a barrel that does not point
   straight; the sights should sit on the crosshair at any distance.
3. Or turn on **Nudge keys** and use I/J/K/L/U/O (hold H to rotate, N for slow) without the window.

Poses are saved per weapon automatically (`Prey/Mods/config/Vee.ViewmodelTweaks.weapons.xml`).

### Before / after comparisons

*Vanilla viewmodel* (top of the window, also in Quick Settings, `vm_bypass`) switches every viewmodel feature off at
once - offsets, ironsights, feel, weapon FOV, convergence, wall pull-back, spread - and keeps only the reticle,
world FOV and sprint-sensitivity settings. Your values are kept; untick to get everything back.

### Sway and the reticle

The aim sway's rotation moves the sights off the crosshair. With *Hide reticle while aiming* on (the default)
that is real inaccuracy: shots go where the camera points, so time them for the calm part of the sway or hold
your breath. With the reticle visible the sway is only cosmetic.

### Console

Everything is also a `vm_*` cvar in the Chairloader console (`vm_fov`, `vm_aim_key`, `vm_aim_bob`,
`vm_converge`, `vm_wall_push`, `vm_reticle_mode`, ...). `vm_show_advanced 1` reveals diagnostics and
experimental features in the window.

## Known limitations

* During very fast leans the hands can slip a millimetre or two on the grip for a frame; the sights stay put.
* Bullet spread multipliers affect the pistol and the shotgun only - no other weapon has spread.
* The aim pose is relative to the camera, so it survives FOV changes, but the weapon's apparent size in
  ironsights depends on the weapon FOV; adjust *Ironsights FOV* to taste.
* Grenades, the wrench and the Nullwave transmitter have aiming disabled by default; enable per weapon if
  you want it.

## Building from source

Standard Chairloader DLL-mod workflow (Visual Studio 2022 + CMake + vcpkg), see
<https://github.com/thelivingdiamond/Chairloader/wiki/DLL-Modding-%E2%80%90-Mod-Project-Setup>:

```
cmake -S . -B _build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake ^
  -DCHAIRLOADER_COMMON_PATH=<Chairloader repo>/Common ^
  -DMOD_DLL_PATH=<Prey>/Mods/Vee.ViewmodelTweaks
cmake --build _build --config Release
```

`Src/ModMain.cpp` is the whole mod; `DEVNOTES.md` has the reverse-engineering notes. `CommonMod/` and
`CMake/` are the unmodified Chairloader mod SDK glue. The experimental Terraria-style death rework that
used to ship inside this mod is now its own mod, *Vee.Mediumcore*.

## Credits

* thelivingdiamond & tmp64 - Chairloader and the PDB-derived Prey SDK headers.
* Crytek - the GameSDK weapon-offset system this piggybacks on.
* Vee - the mod. MIT licensed.
