# Prey native interaction detached-aim A0

- Date: 2026-08-01
- Scene: stationary at the selection edge of a keypad; no Use input was sent
- Target: Steam `PreyDll.dll`, SHA-256 `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`
- Debuggee PID: `783748`
- Module base: `0x7FFE1DA20000`

## Guarded live sites

| Observation | RVA | Live address |
| --- | ---: | ---: |
| Stack-local ray copied | `0x159A70A` | `0x7FFE1EFBA70A` |
| Scaled native-query vector ready | `0x159A755` | `0x7FFE1EFBA755` |
| Candidate vector ready | `0x159A63E` | `0x7FFE1EFBA63E` |
| Interaction update returned | `0x15850EB` | `0x7FFE1EFA50EB` |

ArkPlayer was stable at `0x223EF1FCB20`; its selector was `0x223EF1FD778`. The interaction distance at selector `+0x0C` was `2.5`. Candidate records were `0x70` bytes with the entity id at `+0x68`; usable entity was read at ArkPlayer `+0xF34`.

## Centered-keypad calibration

The initial centered view selected keypad entity `0xFDE0` (`64992`). Both a +10-degree and an opposite -15-degree one-frame yaw retained the same selected and usable entity. This was a useful null showing that the keypad's interaction tolerance was wider than the centered probes, so the camera was moved to the native prompt's selection edge before the decisive pair.

## Edge baseline

The unchanged stack ray was:

```text
E4 39 43 44 E3 2D C4 44 1E 85 88 41 61 19 63 BE 54 54 69 BF 13 6D B1 BE
```

- origin: `(780.90454, 1569.434, 17.064999)`
- direction: `(-0.2217765, -0.911443, -0.3465353)`
- candidate range: `[0x2248E29F670, 0x2248E29F750)`
- candidate count: `2`
- candidate entity ids: `[0xFDE0, 0xFDE0]`
- selected entity: `0xFDE0`
- usable entity after update: `0xFDE0`

## Positive-yaw discriminant

Normal idle drift produced this actual source ray before mutation:

```text
EC 39 43 44 FE 2D C4 44 EC 88 88 41 D1 84 66 BE B4 C4 6A BF B2 81 A8 BE
```

The helper generated a +15-degree direction `(0.019908220, -0.944080055, -0.329114497)` and bytes:

```text
90 16 A3 3C 3B AF 71 BF B2 81 A8 BE
```

The exact scaled 2.5-unit vector immediately before Prey's native query was `(0.04977055, -2.3602002, -0.8227862)`:

```text
34 DC 4B 3D 85 0D 17 C0 1E A2 52 BF
```

The candidate count changed from two to one, but selected and usable entity remained `0xFDE0`. This proves the candidate set responds to the transient direction but is not by itself the changed-target pass.

## Negative-yaw changed target

The next frame's unmodified source ray was:

```text
EA 39 43 44 FE 2D C4 44 EB 88 88 41 CA D1 66 BE 79 D1 6A BF 1F 20 A8 BE
```

- origin: `(780.9049, 1569.4373, 17.066854)`
- direction: `(-0.22540966, -0.9172588, -0.32837006)`

The helper generated a -15-degree direction `(-0.455133051, -0.827663660, -0.328370064)` and exact bytes:

```text
33 07 E9 BE C4 E1 53 BF 1F 20 A8 BE
```

The exact scaled 2.5-unit vector immediately before the query was `(-1.1378326, -2.069159, -0.8209252)`:

```text
80 A4 91 BF 1A 6D 04 C0 27 28 52 BF
```

The resulting candidate range was `[0x2248E29F670, 0x2248E29F6E0)`, one record. Its entity id was `0x1117` (`4375`). At the interaction-update return, ArkPlayer `+0xF34` was also `0x1117`.

## Safety and interpretation

Only the twelve direction bytes at `[RSP+0x5C..0x67]` in each current `UpdateCandidates` stack frame were written. Each write was read back. The persistent ArkPlayer ray was checked after each mutation and retained its original bytes; camera, code, heap, and UI-reticle memory were not written. No interaction action was invoked. The stack write expired on return, all breakpoints were cleared, and Prey resumed normally.

This is a strong pass: changing only a transient copy of the gameplay ray changed both Prey's native selected candidate and the committed usable entity from keypad `0xFDE0` to `0x1117`, independently of the camera and stored ray. It establishes the native motion-controller use/highlight seam while preserving `ArkPlayerInteraction::Interact` as the later action-execution path.
