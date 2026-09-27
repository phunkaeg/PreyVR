# Native HUD status on the wrist

## Decision

Use Prey's own health, psi and suit presentation for the wrist display. The
GDI card in `WristPanel.cpp` is a prototype, not the intended final artwork.
Keep the existing wrist pose, gaze/visibility gating, size preference and
swapchain ownership; replace the source of the pixels once the native widget
boundary is established. Holsters are independent of this change.

Prefer rendering the original status subtree to a transparent target. This
preserves its icons, animations and additional state rather than trying to
recreate the movie from the five numbers currently sampled for the prototype.
Do not substitute a fixed rectangle cut from the complete HUD: reticles and
prompts can move, and native layout/animation can move the status graphics.

## What is established

Static target: Steam x64 `PreyDll.dll`, image base `0x180000000`, SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.

The existing body-equipment evidence verifies native health and armor update
producers. This follow-up also inspected `/Prey/PreyDll.dll` through Ghidra:

| Producer RVA | Movie dispatch | Observation |
| --- | --- | --- |
| `0x158CAB0` | `healthUpdate` | Existing evidence includes current/max health plus additional presentation arguments. |
| `0x13611D0` | `armorUpdate` | Existing evidence computes suit integrity from damage and maximum. |
| `0x15AD7C0` | `psiUpdate` | Gets the HUD through `0x1665780`, then invokes its virtual `+0x210`. Four arguments are assembled: rounded fields at component `+0x400`, `+0x404`, a caller integer, and rounded `+0x408`. The meaning of `+0x404` is not established here. |

These are UI function names, **not display-object paths**. They show why a card
with only current and maximum numbers cannot be assumed to reproduce the full
native presentation. No new native call has been added based on this inspection.

`HudLayer.cpp` already captures the complete DanielleHUD draw into a transparent
texture. Historical source-texture evidence is documented in
[the interface investigation](RE-VR-INTERFACE-2026-09-10.md). It does not establish
an independent status-widget render target or status-only display subtree.

## Extraction completed: native widget boundary

The offline extractor now recovers both the base and patched HUD from the local
Steam installation. All four extracted files pass archive size/CRC checks; every
key block passes OAEP/SHA-256 padding checks. The patched movie is 732,099 bytes,
SHA-256 `35974883f152e345834ca690c8f6cb280152cee0ae724280e3cc98f75fb9c9dc`.
The patched binary UI definition is 64,010 bytes,
SHA-256 `57b89b80082a6dc93ae4b947739acf0cfb2c7c6e07edac137daac584925eb1df`.
These are extracted assets, not DLL addresses or rendered-image captures.

JPEXS 26.3.0 exported the movie's ActionScript and tag tree without running the
game. The class references and named display-list placements agree:

| Widget | Exact display-object path | Patched movie sprite ID / draw depth |
| --- | --- | --- |
| Health | `_root.safe.quadrant_SW.health_mc` | 1263 / 54 |
| Psi | `_root.safe.quadrant_SW.psi_mc` | 1283 / 107 |
| Suit integrity | `_root.safe.quadrant_SW.armor_mc` | 1289 / 150 |

Sprite IDs are asset-version-specific corroboration, **not native object pointers
or stable integration identifiers**. Use verified paths and movie generation.
The complete metadata is in
[widget-map.json](evidence/native-hud-2026-09-27/widget-map.json).

The UI function names resolve to ActionScript controller objects (`health`,
`psiM`, `armor`), which in turn retain references to these display objects. They
are not interchangeable. `Health` owns regeneration/damage masks, a limiter and
last-chance animation; `PsiMeter` owns cost-preview/limiter graphics; `Armor` owns
damage animation. Native meters contain more information than the numeric card.

The shared parent also contains `bg_mc`, `bg2_mc`, `line_mc`, `status_effects`,
`power_mc`, `climb_mc`, `flashlight_mc`, `stealth` and `pickup`. A first native
wrist capture should retain the three meters, backgrounds/line and status
effects, in authored draw order, while excluding the other siblings. Moving the
whole quadrant would incorrectly relocate climb/pickup prompts.

Two transform owners matter:

- `Health` initializes the quadrant's X/Y rotations to -15 degrees. A wrist
  panel needs an explicit decision about flattening that authored perspective;
  it must not accidentally acquire a second tilt from the wrist pose.
- `EquippedPower` shifts all three meters by 100 authored units when opened,
  retaining a 12-unit stagger, and changes the backgrounds. `SafeFrame` also
  positions/scales the quadrant. These are reasons to sample fresh state and
  restore it after capture, not hard-code a screen crop or repeatedly rescale.

There is also a real asset function `setWidgetsScale -> sf.resizeWidgets`, but
it scales all HUD quadrants, wheel and reticle. It is **not** an independent
status/reticle size control.

## Reproduce the extraction

From this checkout, with Python 3.12 (use its absolute path on this machine):

```powershell
python -m pip install --target build/re-tools pycryptodome twofish==0.3.0
python -E -B tools/re/extract_native_hud.py --deps build/re-tools --game 'D:/SteamLibrary/steamapps/common/Prey' --out build/native-hud/verified
python -E -B tests/test_extract_native_hud.py -v
```

The tool validates the exact Steam DLL hash, reads its embedded public key
without loading the DLL, decodes only the two requested assets from each PAK,
checks bounds/decompression/CRC and decodes CryXmlB. It refuses output inside
the game installation. The original files are opened read-only. Nine offline
tests cover stored/deflated entries, Twofish known-answer and CTR byte order,
OAEP corruption, bad CRC, truncation, paths, XML bounds/cycles and wrong DLLs.
CRC is an integrity check; this tool does not authenticate the archive signature.

Use the official [JPEXS CLI](https://github.com/jindrapetrik/jpexs-decompiler/wiki/Commandline-arguments)
to inspect the patched movie (the commands below use the local downloaded 26.3.0
tool, which is not distributed with PreyVR):

```powershell
java '-Djava.awt.headless=true' -jar build/native-hud/ffdec/ffdec.jar -export script build/native-hud/hud-as build/native-hud/verified/patch/libs/ui/gfx/danielle_hud.gfx
java '-Djava.awt.headless=true' -jar build/native-hud/ffdec/ffdec.jar -swf2xml build/native-hud/verified/patch/libs/ui/gfx/danielle_hud.gfx build/native-hud/hud-movie.xml
python -E -B tools/re/map_native_hud.py --movie build/native-hud/verified/patch/libs/ui/gfx/danielle_hud.gfx --definition build/native-hud/verified/patch/libs/ui/uielements/daniellehud.xml --movie-xml build/native-hud/hud-movie.xml --scripts build/native-hud/hud-as --out build/native-hud/widget-map.json
```

Extracted assets, exported scripts and third-party tools stay under ignored
`build/`; only our tools, tests, hashes and structural findings enter Git.

## Prior extraction limit (resolved)

The previous investigation stopped at ordinary ZIP-reader failure. That failure
was correctly scoped; it is superseded by the offline reader above.

The installed main-game PAK directories were checked offline. Python's ZIP
reader rejects their central directories, including GameData, Scripts and the
Precache patch. A ZIP-signature check alone returns true for some of these files;
that is not successful extraction. Scoped searches found no extracted HUD movie
in the local project or the supplied sibling repository. This is a coverage
limit, not proof that the assets cannot be recovered.

Chairloader's [ExtractionStage.cpp](https://github.com/thelivingdiamond/Chairloader/blob/main/Src/Preditor/Launcher/ExtractionStage.cpp)
uses the initialized engine's CryPak archive service, including optimized reads
for encrypted PAKs. It is not a standalone ZIP decoder. It was inspected, not
executed: this task retains the user's static/code-only restriction.

## Render integration still required

The extraction resolves the asset paths, not the render transaction. Static
review of the Steam target identifies the following additional candidates:

| Receiver / method | Steam RVA / slot | Established scope |
| --- | --- | --- |
| Native Flash render proxy | `0xE8C5E0` | Acquires proxy critical section at `+0xE8`, then the global Flash lock before root Display; releases once afterwards. |
| Root at proxy `+0xA8`, vtable `0x1EB4B60` | Display `0x18AB710`, slot `+0x130` | Existing inventory replay seam constructs its display context per invocation. |
| Same root | SetVariable `0x18ABCC0`, slot `+0x80` | Path/value setter; non-normal modes can install sticky variables. Temporary capture must not use those modes. |
| Same root | GetVariable `0x18ABE10`, slot `+0x88` | Path lookup and conversion through `0x183F680`; setter conversion is `0x183F4F0`. |

The last two slots are read from this concrete Steam vtable; the EGS symbol
names supplied leads and were cross-checked against Steam bodies. The value
converters read/write type at `+8`, payload at `+0x10` and manage object/string
references when flag `0x40` is set. This is **not yet a complete value-lifetime or
render-thread mutation contract**. Do not ship a guessed struct or leak managed
values while enumerating display objects. The HUD is declared `render_lockless=1`;
verify how its render callback observes any temporary display-state changes.

Required implementation steps:

1. Resolve the status objects by verified movie paths, scoped to the current
   movie generation. Verify the target build's display-object API and receiver.
2. Render status in its authored order, including masks and filters, into a
   private transparent target at the native owning render seam. Advance the
   movie only once. Do not repeat arbitrary queued Flash callbacks or releases.
3. Preserve fresh authored transforms rather than compounding wrist transforms
   or fighting animation. Restore every temporary change before returning.
4. Publish status pixels with their capture generation/frame, dimensions and
   format. Reuse wrist pose/gaze logic and preserve premultiplied alpha and the
   source's colour-space interpretation; the existing opaque GDI upload is not
   the native-pixel transfer contract.
5. Keep the normal status visible on capture/consumer failure. Removing its
   forward-facing copy requires a successful consumer handshake and an explicit
   policy for when the wrist is lowered; it must not disappear merely because
   the preference is enabled.
6. Check status against the original at full/low health, damage/healing, psi
   spending/recovery and suit damage, including native warnings and unavailable
   psi. Check menus, save/load, reference-space changes and unsupported builds.

Initial implementation should preserve the forward-facing native status while
validating the wrist copy. A later option can move it exclusively to the wrist
after visibility and failure recovery have been verified.

## Current outcome

**Superseded September 28:** native sprite filtering, transparent capture and
wrist submission are now implemented in the [native wrist candidate](NATIVE-WRIST-IMPLEMENTATION-2026-09-28.md).
The outcome below records the extraction-only milestone; the candidate still
awaits in-game/headset verification.

Native asset extraction and widget mapping are implemented and verified offline.
Native-widget render isolation and wrist submission are not wired yet. The
optional, default-off prototype card is unchanged. The native HUD remains intact.
No game was launched, attached or modified; no new DLL or headset acceptance is
claimed by this extraction work.
