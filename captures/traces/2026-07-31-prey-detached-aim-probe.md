# Detached-aim reversible probe

- Date: 2026-07-31
- Target: `PreyDll.dll` SHA-256 `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`
- Debugger: x64dbg MCP, hardware execute breakpoint
- Game state: loaded save, normal gameplay

## Static/live anchors

- `ArkPlayer::UpdateCachedReticleViewPosAndDir`: RVA `0x1585320`
- Live module base: `0x7FFE31730000`
- Live function address: `0x7FFE32CB5320`
- Live ArkPlayer pointer: `0x271F571E020`
- Cached ray origin: ArkPlayer `+0x17D4`
- Cached ray direction: ArkPlayer `+0x17E0`
- Reticle screen position: ArkPlayer `+0x17EC`

## Probe

At function entry, the 32-byte origin/direction/reticle block was:

```text
9B 70 43 44 5D 70 C4 44 9A A1 88 41
EA 58 72 BF 03 6B 7B BE 46 A8 55 BE
00 00 00 3F 33 33 13 3F
```

This represents reticle `(0.500, 0.575)` and cached direction approximately `(-0.946669, -0.245525, -0.208650)`.

Only reticle X was changed to `0.625` (`00 00 20 3F`) while execution and therefore the camera matrix were frozen. Running the updater to its return produced:

```text
E9 6F 43 44 B5 71 C4 44 97 A1 88 41
32 09 78 BF A4 14 20 3E B5 78 44 BE
00 00 20 3F 33 33 13 3F
```

The resulting direction was approximately `(-0.968890, +0.156329, -0.191867)`. The substantial sign/magnitude change in the second component proves the world ray is derived from the separate reticle coordinate rather than being an immutable camera-forward vector.

## Restoration and safety

The original 32 bytes were written back and read back exactly before clearing the hardware breakpoint. The game was then resumed. No hook remained installed and no on-disk game file was modified.

## Scope of proof

This passes the engine-ray half of detached aiming. PDB-mapped `CArkWeapon::GetReticleInfoForFiring`, `GetReticlePosition`, and `FindIronsightsTarget` consume this ray, but this capture does not yet contain an actual before/after projectile or hitscan impact. That runtime endpoint is test A0b.
