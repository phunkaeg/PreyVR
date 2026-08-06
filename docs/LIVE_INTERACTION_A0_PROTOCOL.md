# Live interaction A0 protocol

Status: completed 2026-08-01. The edge-position -15-degree pass changed both selected and usable entity from `0xFDE0` to `0x1117`; see [`../captures/traces/2026-08-01-prey-interaction-a0.md`](../captures/traces/2026-08-01-prey-interaction-a0.md).

This is a no-button, one-frame proof that Prey's native **use/highlight target** follows the detached gameplay ray. It does not invoke `Interact`, change the camera, or write persistent ArkPlayer state.

Run it only after the wrench A0b protocol, unless the scene does not provide a safe static melee target. Use two nearby, non-hazardous interactable objects that can be distinguished by their prompts or entity IDs. Keep the player and camera stationary.

## Guarded Steam-build locations

| Purpose | RVA | Live address |
| --- | ---: | --- |
| Capture ArkPlayer pointer before ray getter | `0x159A6F4` | `PreyDll.dll+0x159A6F4` |
| Stack-local ray copied; optional 12-byte edit | `0x159A70A` | `PreyDll.dll+0x159A70A` |
| Candidate vector ready | `0x159A63E` | `PreyDll.dll+0x159A63E` |
| Interaction update returned to ArkPlayer | `0x15850EB` | `PreyDll.dll+0x15850EB` |

The post-getter stack layout is:

| Bytes | Meaning |
| --- | --- |
| `[RSP+0x50..0x5B]` | `Vec3` origin; observe only |
| `[RSP+0x5C..0x67]` | `Vec3` direction; the only permitted write |

At `0x159A63E`, `RBX` is `ArkPlayerTargetSelector*`. Its candidate vector is `[RBX+0x20, RBX+0x28)`, each record is `0x70` bytes, and the selected entity ID is the dword at `[vectorEnd-0x08]`. An empty vector means no selected target. At `0x15850EB`, `RDI` is ArkPlayer and `[RDI+0xF34]` is the native usable entity ID after the interaction update.

## Generate the bounded direction bytes

After reading the three source floats at `[RSP+0x5C]`, use the headless helper rather than hand-packing floats:

```powershell
./build/headless/Release/preyvr_ray_probe.exe <x> <y> <z> 10
```

It calls the same tested Z-up yaw policy used by the wrench A0b experiment and prints exactly twelve little-endian bytes. The helper rejects non-normalized directions and yaw magnitudes above 15 degrees.

## Pass A: unchanged native target

1. Set one-shot breakpoints at the four guarded locations above.
2. At `0x159A6F4`, record `RAX` as the ArkPlayer base, then continue.
3. At `0x159A70A`, capture all 24 ray bytes at `[RSP+0x50]` and decode the direction. Do not write anything. Continue.
4. At `0x159A63E`, record candidate begin/end and the selected entity ID, if any. Continue.
5. At `0x15850EB`, record `[RDI+0xF34]`, remove the one-shot breakpoint, and resume to idle.

## Pass B: one-frame synthetic target

1. Without moving the player or camera, re-arm the same one-shot breakpoints.
2. Confirm the ArkPlayer pointer at `0x159A6F4` matches pass A.
3. At `0x159A70A`, confirm the unmodified 24 ray bytes match pass A within normal floating-point jitter.
4. Write only the helper's twelve output bytes to `[RSP+0x5C..0x67]`. Read them back once, then continue.
5. Record the candidate vector and selected entity ID at `0x159A63E`, then the usable entity ID at `0x15850EB`.
6. Clear every breakpoint and resume. No restoration write is required: the modified ray existed only in the expired stack frame.

## Interpretation

- **Strong pass:** pass B selects a different plausible entity while the camera and stored ArkPlayer ray remain unchanged.
- **Useful null:** pass B changes the candidate vector or aim-distance scoring but lands on the same entity. Repeat with a scene that has two close targets or the opposite yaw sign.
- **Fail/stop:** persistent ray/camera bytes change, the target becomes an implausible entity, an action executes without input, Prey faults, or any guard signature fails.

Save both local rays, candidate-vector summaries, selected and usable entity IDs, exact written bytes, yaw sign, module hash, and a short scene description under `captures/traces/`. Promote the pair into a deterministic fixture before implementing a persistent controller-ray hook.
