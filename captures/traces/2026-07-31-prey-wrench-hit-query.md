# Prey wrench hit-query trace — 2026-07-31

## Scope and safety

This trace follows one primary wrench swing from input into Prey's native melee physics query. x64dbg used temporary hardware execution breakpoints only. No game bytes were patched. Every breakpoint was cleared before execution resumed, and the final debugger state was running with no hardware breakpoints.

Target build: installed Steam `PreyDll.dll`, SHA-256 `7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`.

## Native action path

| Function | Steam RVA | Live evidence |
| --- | ---: | --- |
| `ArkWeaponWrench::OnActionAttackPrimary` | `0x16B1FD0` | Hit on the press transition. `RCX=0x271F157DB50` was the wrench, `EDX=0x7777` the local-player entity id, `R9D=1`, and the stack input float was `1.0`. |
| `ArkWeaponWrench::OnHit` | `0x16B2BC0` | Hit once at the animation impact with the same wrench object. Direction and damage scale are XMM arguments and were not exposed by the debugger MCP. |
| `ArkWrenchComponent::OnHit` | `0x13BF730` | Mapped from the PDB bridge and its caller; it owns the deeper hit flow. |
| `ArkWrenchComponent::GetHits` | `0x13BD620` | Hit during the wall strike and ran to return, exposing its complete `std::vector<ray_hit>` result. |

At `GetHits`, the observed ABI was:

- `RCX=0x271F157E018`: `ArkWrenchComponent*`
- `RDX=0xAADCAFD638`: hidden `std::vector<ray_hit>` return object
- `R8=0x271F157DB50`: `ArkWeaponWrench*`
- component offset from weapon: `+0x4C8`
- angle: `XMM3`, copied by the caller from `XMM15`

The caller at `PreyDll.dll+0x13BFA85` sets `XMM3`, loads the return buffer into `RDX`, the weapon into `R8`, the component into `RCX`, then calls `GetHits`.

## Detached-ray dependency

Paired decompilation shows `GetHits` calling the `IArkPlayer` subobject at `ArkPlayer+0x40` to obtain six floats. This is the already mapped `IArkPlayer::GetReticleViewPositionAndDir` path, so the melee broadphase and `RayWorldIntersection(Game)` queries originate from the same cached ray used by firearm reticle/firing code.

The live cached reticle block at `ArkPlayer+0x17D4` decoded to:

- origin `(767.571289, 1565.050171, 17.055344)`
- direction `(0.582303, 0.694913, -0.421923)`
- screen coordinate `(0.500, 0.575)`

## Component fixture

The first `0x58` bytes of the live wrench component retain these query parameters:

- hit offset `1.0`
- maximum force/mass scale `50.0`
- ray range `2.75`
- speed-range factor `0.25`
- speed-range maximum `2.0`
- fatigue for this hit `17.5`

## Returned contacts

The returned vector spanned `0x140` bytes. With `sizeof(ray_hit)=0x50`, it contained four records:

| Hit | Distance | Collider | Surface | Part / part id | Point | Normal |
| ---: | ---: | --- | ---: | --- | --- | --- |
| 0 | `0.531901` | `0x271F2D85A70` | 153 | `0 / 0` | `(768.089722, 1565.331543, 16.855158)` | `(-0.642265, -0.766471, -0.004221)` |
| 1 | `1.253724` | `0x2720B7E7EE0` | 152 | `0 / 0` | `(768.918823, 1565.490601, 16.550604)` | `(-0.764409, -0.644691, -0.007293)` |
| 2 | `0.976837` | `0x2720B7DD180` | 152 | `1 / 1024` | `(768.508179, 1565.452271, 16.576765)` | `(-0.002074, -0.002475, 0.999995)` |
| 3 | `1.283386` | `0x271F484F060` | 136 | `0 / 0` | `(768.909058, 1565.478882, 16.447426)` | `(-0.001109, 0.002940, 0.999995)` |

The raw `0x58`-byte component and `0x140`-byte result are compiled into `wrench_query_fixture`. That headless test verifies the layouts, parameter values, four records, nearest-contact policy, and fail-closed handling of truncated data.

## Interpretation and next proof

This closes the discovery question for the melee query source: Prey already feeds the detachable cached reticle ray into native wrench collision detection. It does not yet prove that a synthetic/controller offset changes the final impact. Test A0b remains a bounded fixed-camera before/after strike, ideally against a large static target, with the cached ray changed and restored around the query.
