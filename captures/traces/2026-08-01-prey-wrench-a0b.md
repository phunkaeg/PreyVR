# Prey wrench detached-aim A0b

- Date: 2026-08-01
- Target: supported Steam `PreyDll.dll` SHA-256 `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`
- Live PID: `783748`
- Module base: `0x7FFE1DA20000`

## Question

Does changing only the direction used by one native `ArkWrenchComponent::GetHits` query move the returned collision contact while camera and persistent ArkPlayer state remain untouched?

## Stops and safety

- Post-reticle-copy: `PreyDll.dll+0x13BD6F5`
- Result ready: `PreyDll.dll+0x13BFAA9`
- Only `[RSP+0x64..0x6F]` in the current `GetHits` stack frame was written.
- No camera, code, heap, UI-reticle, or persistent ArkPlayer memory was written.
- All hardware breakpoints were cleared and Prey resumed normally after the synthetic result.

## Baseline swing

Stack-local ray bytes:

```text
CA 65 43 44 85 2E C4 44 7B AC 88 41
7F E5 DE BD B8 E1 7B BF 5E 11 11 BE
```

- Origin: `(781.590454, 1569.453735, 17.084219)`
- Direction: `(-0.108836, -0.983913, -0.141668)`
- Returned vector: one `0x50`-byte record
- Distance: `0.696134`
- Collider: `0x2231174EDF0`
- Point: `(781.514709, 1568.768799, 16.985590)`
- Normal: approximately `(0, 1, 0)`

Raw result record:

```text
DC 35 32 3F FE 7F 00 00 F0 ED 74 11 23 02 00 00
16 00 00 00 00 58 00 00 86 00 00 00 FF FF FF FF
00 00 00 00 F1 60 43 44 9A 18 C4 44 7D E2 87 41
00 00 C0 34 FF FF 7F 3F 00 00 00 00 00 00 00 00
FF FF FF FF F0 00 00 00 92 2A AE 1D FE 7F 00 00
```

## Synthetic swing

Unmodified stack-local ray bytes:

```text
E1 65 43 44 A2 2E C4 44 16 B0 88 41
C9 F1 D1 BD 56 BF 7C BF B1 A6 F8 BD
```

- Origin: `(781.591858, 1569.457275, 17.085979)`
- Original direction: `(-0.102512, -0.987295, -0.121412)`
- Baseline-origin delta: `0.004195` world units
- Baseline-direction delta before modification: `1.231205°`

The headless probe applied a positive 10-degree Z-up yaw to this swing's actual direction:

- Written direction: `(0.070487, -0.990096, -0.121412)`
- Horizontal yaw: `10.000001°`
- Full 3D angular delta: `9.925828°`
- Exact 12 written and read-back bytes: `AA 5B 90 3D F3 76 7D BF B1 A6 F8 BD`

The returned vector again contained one record:

- Distance: `0.695485`
- Collider: `0x2231174EDF0` — unchanged
- Point: `(781.640869, 1568.768677, 17.001547)`
- Normal: approximately `(0, 1, 0)` — unchanged

Raw result record:

```text
4A 0B 32 3F FE 7F 00 00 F0 ED 74 11 23 02 00 00
16 00 00 00 00 58 00 00 86 00 00 00 FF FF FF FF
00 00 00 00 04 69 43 44 99 18 C4 44 2B 03 88 41
00 00 C0 34 FF FF 7F 3F 00 00 00 00 00 00 00 00
FF FF FF FF 00 F8 8E 3F B3 D4 A0 1E FE 7F 00 00
```

## Result

Strong pass:

- Same collider and effectively identical wall depth (`-0.000650` distance delta).
- Contact moved `( +0.126160, -0.000122, +0.015957 )`, or `0.127165` world units total.
- The synthetic point equals `origin + writtenDirection * returnedDistance` within float tolerance.
- The movement is lateral on the same flat wall and agrees with the positive yaw direction.

This is direct runtime proof that controller-owned direction can steer Prey's native melee physics query independently of the camera. It validates the shared detached gameplay-ray seam for melee; it does not by itself solve visual weapon placement or input/action synthesis.
