# Next session — start here

**Written 2026-09-07, corrected 2026-09-08 after review.** Version
`0.3.1-static-ik-integration`. **All three build trees now hold the identical
DLL** (SHA-256 begins `0FDC2DB3A915E5E9`, 711,680 bytes) and each passes 27/27,
so `build/headless` is no longer the trap it was -- it held a DLL from
2026-08-31 with neither the IK lane nor the yaw fix. The launcher defaults to
`build/configure/Release/PreyVR.dll`; any of the three is now correct.

## The one test queued

The controller drives the hand through Prey's own arm IK, the weapon follows and
the elbow bends — all confirmed yesterday (R-104). Two defects were found in the
same session, fixed, and **not yet retested**:

1. **The hand yawed with the headset.** Fixed by rotating head-relative offsets
   by the *body* yaw. Toggle: `aim.bodyyaw 1` (default) vs `0` (old behaviour).
   **Test by looking up as well as around.** Review on 2026-09-08 found the first
   fix silently restored the old behaviour whenever the head yaw became undefined
   -- looking near-vertical -- which is the one posture a wearer would plausibly
   have blamed on something else. It now holds the last defined head yaw (the
   body has not turned just because the head tilted) and refuses the sample
   outright if none was ever defined. `yawHeld` counts held frames,
   `yawUnavailable` counts refusals.
2. **The hand flickered between the goal and the animated pose.** Fixed by
   sharing the snapshot lock and letting non-owner characters skip the IK lock.

Both need a fresh launch: the module pins itself for the process lifetime, so a
running game cannot pick up a new DLL.

## Bring-up, in order

Every step below has cost a session at least once when skipped or reordered.

```powershell
# 1. Launch. Run from anywhere; -Headset leaves the machine's OpenXR runtime alone.
D:\Dev` Debug\PreyVR\tools\Invoke-PreyVRLaunch.ps1 -Headset
```

Then inject the DLL into the PID it prints. Frida 17 removed
`Module.findExportByName`; the call is `Module.load("<repo>\build\configure\Release\PreyVR.dll")`.
**Never x64dbg `loadlib`** — F-008, it leaves the hijacked thread suspended.

```powershell
# 2. Bring-up. -LogDir only when the game was NOT started by the launcher
#    (a Steam launch puts the channel in <Documents>\PreyVR).
D:\Dev` Debug\PreyVR\tools\Invoke-PreyVRStartup.ps1
```

**Prey must be the foreground window.** Defocused, CryEngine renders no frames,
so the console queue never drains (`console result=4` is *busy*) and no head pose
is published (`view.recenter result=1` is *no pose*). Three confusing failures,
one cause. This has now happened in three separate sessions.

```powershell
# 3. Load a save and equip a weapon. Then:
D:\Dev` Debug\PreyVR\tools\Invoke-PreyVRIkTest.ps1                      # observe
D:\Dev` Debug\PreyVR\tools\Invoke-PreyVRIkTest.ps1 -TestMillimetres 300 # fixed goal
D:\Dev` Debug\PreyVR\tools\Invoke-PreyVRIkTest.ps1 -Drive               # right hand
D:\Dev` Debug\PreyVR\tools\Invoke-PreyVRIkTest.ps1 -Drive -Hands 3      # both hands
```

**Use `ik.calibrate`, not `hand.calibrate`.** The latter belongs to the old hand
lane, which must stay at `hand.mode 0` -- the two refuse each other because
running both applies the controller twice. The scripts issue the right one; this
is here because the startup script used to end by recommending the wrong one.

**The yaw check needs two deliberate movements**, and the second is not obvious:
hold a controller still and turn your head (the hand must not move), then look
straight up and repeat (`yawHeld` climbs, and the hand still must not move).

**Re-equip the weapon after `ik.mode 1` arms.** Ownership is captured on the
attach call, so the first observe run reports `ikOwner=0x0` until a weapon is
equipped with the hook already installed. The script stops rather than continue.

## What to look at

| field | good | what a bad value means |
|---|---|---|
| `ikMatched` vs `ikWrittenR` | equal, both climbing | a gap is refused frames, not idle ones |
| `ikBusy` | near flat | the lock fix did not take; the hand will flicker |
| `ikNoPose` | flat | controllers untracked or the sample is stale |
| `ikClamped` | only at full reach | constant clamping means the model-space scale is wrong |
| `camYawMdeg` / `headYawMdeg` / `playYawMdeg` | `play = cam - head`, steady when only the head moves | `play` moving on head-only motion means the yaw fix failed |
| `yawHeld` | climbs only while looking near-vertical | climbing otherwise means head yaw is being lost when it should not be |
| `yawUnavailable` | zero after the first tracked frame | non-zero means the lane refused because no head yaw was ever defined |
| `viewPoseFallback` | near zero | climbing fast means publisher/reader contention on the head pose |
| `ikEquipGen` | bumps on each equip | ownership rebinding after a weapon swap |

**Judge by counters that change, not by ones that are merely non-zero.** Several
of them are cumulative and carry values from before the controllers woke up.

## Traps, each paid for once

- **A stale pin.** The viewmodel is a *different* character after every weapon
  change (F-009). The lane now selects by the live owner chain, but a lane that
  reports perfect counters while a wearer sees nothing is a stale pin until
  proved otherwise. `handMatched=0` with `handSkipped` climbing names it.
- **`hand.mode 2` and `ik.mode 2` are mutually exclusive** and refuse each other.
  The old hand lane composes onto the skinned pose; running both applies the
  controller twice.
- **Recentre, refocus or re-equip invalidates the wrist calibration.** Re-issue
  `ik.calibrate`.
- **`aim.origin 1` is a diagnostic, not a muzzle mode.** A shot still converges
  from the weapon's authored muzzle helper toward the reticle ray.

## After the retest

If the yaw and flicker are both clean, the hand lane is finished and the next
lanes are, in rough order of value:

1. **Barrel alignment** — the last piece of controller-owned aim. Fire a
   stationary GLOO shot and read `muzzleFallback`, `muzzleAimGapMm`,
   `muzzleGripGapMm`; those are the first real measurements of how far the
   authored muzzle sits from the controller.
2. **Locomotion** — written and unit-tested as a pure layer, still unwired.
3. **H-019**, the GLOO ammo display separating between the eyes. Run
   `xr.stereo 0 50` first: if the offset survives a zero IPD it is not our camera
   at all, which eliminates a whole lane.
4. **H-020**, animation suppression. The cvar route is dead (H-012); the first
   named candidate is the weapon attachment's spring at `[weapon+0x2B0]+8+0x28`.

## Reverting anything

`ik.mode 0`, `hand.mode 0`, `aim.enable 0`, `aim.origin 0`. All writes are
per-frame edits the engine rebuilds, so nothing persists past a disable.
