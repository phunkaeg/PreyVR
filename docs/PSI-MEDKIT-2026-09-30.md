# Psychic targeting and one medkit slot

Implemented on `codex/psi-medkit`, based on integration `c9b29be`. No game runs
were made for this work. These are opt-in candidates pending in-game/headset
acceptance, rather than a release or a claim that every power has been tested.

## Player controls

The existing TranStar-styled VR options panel has an **Abilities** tab. Choices
save to the existing `%LOCALAPPDATA%/PreyVR/vr-options.ini`; older partial files
keep Original targeting and medkit slot off. Empty rows cannot be selected.

- **Psychic Targeting: Original / Head / Left Controller.** In either new mode,
  hold left trigger to target the equipped power, then release to cast. Equip
  powers through Prey's normal wheel. Original preserves existing native input.
- **Left-hip Medkit Slot: Off / On.** Fresh left-grip squeeze near the estimated
  left hip claims the slot. Keep grip held, then freshly press left trigger to
  request one native medkit transaction. The hand can leave the belt first.
  Release and re-grab for another attempt, including after a native refusal.

Menus, XR epoch/reference changes, player changes, settings changes, tracking
loss and frame gaps above 200 ms cancel pending gestures and require neutral
input. A menu epoch catches a pause opened and closed while player updates were
suspended. Two-hand grip, both-grip recenter and face-button actions take priority.
The medkit claim excludes support-grip acquisition and psychic-trigger ownership.
There is no new medkit mesh or belt prop. The estimated slot is 25 cm left and
65 cm below the tracked head, with a 14 cm acquisition radius and the existing
yaw-only torso estimate. Seated fit and reachability need player testing.

Diagnostic commands are `psi.target 0|1|2` and `medkit.slot 0|1`. Bare commands
report settings, scope/read/activation counters, native medkit outcomes and fault
state. Commands are session changes; the options menu supplies persistence.

## Native contract and implementation

Question: can the selected power's native preview and activation use the same
head/left ray without changing weapon aim, and can a body-slot gesture use a
medkit without bypassing Prey's inventory/use rules?

Target: Steam `PreyDll.dll`, x64/Windows ABI, image base `0x180000000`, SHA-256
`7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7`.
The EGS symbol corpus supplied names and correspondence leads. Steam Ghidra
decompiles, instruction witnesses and full-function bytes supplied target proof;
no shared offset between builds or donor source was used as a shipping contract.

| Steam RVA | Verified contract |
| --- | --- |
| `0x157C990` | Current native player singleton |
| `0x157CB80` | Whole player -> pointer at player+0x678 -> embedded psi power+8 |
| `0x1587C30` | Focus accessor on player+0x678, loads secondary member+0x118 |
| `0x157CBB0` | IArkPlayer receiver player+0x40, copies 24-byte origin/direction and returns output pointer |
| `0x15BC840` | Native psi Update; selected ID+0x230, array+0x40, UpdateTargeting virtual+0x50 |
| `0x15B8E00` | Native Select; progression, affordability and cooldown admission |
| `0x15B5D90` | ActivateSelectedPower; selected native Start virtual+0xC8 and native cost handling |
| `0x15AFE50` | Area target selection reads the reticle getter, queries geometry and stores native target+0xE0 |
| `0x15BDCA0` | Individual target selection reads the same getter for range/cone/proximity/LOS |
| `0x15BDA40` | Individual candidate check; third argument normally bypasses angle for the HUD-preferred entity |
| `0x124D8F0` | Native focus input proves Start(false), Select(equipped+0x234), Activate and Stop ordering |
| `0x124DE50` / `0x124DF60` | Native focus Start/Stop own admission, selection, time scaling and input policy |
| `0x1381C50` | Resolves and caches 64-bit `ArkPickups.Medical.Medkit` archetype ID |
| `0x1381AF0` | Native ConsumeItem transaction: player inventory+0x1810, secondary+0x50; CanConsume then UseFromInventory |

Psychic scopes are thread-local and exist only around native psi Update, Select
and Activate. The getter first calls its original once, then replaces this
player's output only when the scoped pose/reference/settings/menu owner is valid.
The camera and cached weapon ray are never rewritten, and targeting updates are
not replayed. Native particle previews and native selected targets remain owned
by Prey. The HUD-preferred candidate must pass the native angular test in a VR
scope; its normal distance, eligibility and line-of-sight rules remain.

The new rays use one immutable gameplay pose publication. Head starts at the
gameplay camera centre. Left starts at the controller's head-relative world
position, rotating both origin and direction by the same play-space yaw. This
does not use the weapon's muzzle calibration or the movable HUD reticle origin.

Activation requires a preview no older than 100 ms, matching player, component,
selected power pointer/ID, XR epoch, reference, settings and menu generation. Its
activation-time ray stays the preview ray even if a newer pose has arrived. A
selection change invalidates the preview; a second activation cannot reuse it.
The native power retains its cached target. Powers that already have a native
mimic-grab sequence target retain that native sequence policy; this is not a new
Mimic embodiment implementation. Generic/latent powers and every power variant
still need live acceptance; the two concrete target-consumer families above are
the static coverage, not an exhaustive execution claim.

Left-trigger focus ownership retains only identity tokens, never an old pointer
for later dereference. Cleanup re-resolves the current singleton, component and
focus and stops only our hidden focus. Selection failures release newly opened
focus; a native cast that clears its own selection still releases our focus/time
policy. If native UI takes over, cleanup does not close that UI.

ConsumeItem returns **0 absent**, **1 native refusal**, or **2 native use invoked**.
The mod reports these separately and never edits health, inventory counts or
cooldowns. No item pointer is retained. Cinematic/item restrictions, menus and
active focus prevent slot dispatch. Acquisition and accepted native use request
existing left-controller feedback, subject to the haptics option.

All 18 complete native function bodies are hashed before hooks are installed.
Five MinHook detours are enabled together after originals are available. Partial
installation rolls back. Function sizes include split/chained unwind fragments;
the first unwind entry alone is not used as the body. A hard adapter failure
disarms the feature instead of retrying a faulting transaction every frame.

## Evidence and reproduction

- [Raw Steam decompiles](evidence/psi-medkit-2026-09-30/steam-decompiles.txt)
- [Native candidate angular-bypass decompile](evidence/psi-medkit-2026-09-30/steam-candidate.txt)
- [Read-only byte/ABI report](evidence/psi-medkit-2026-09-30/steam-contracts.json)
- [Offline build/test summary](evidence/psi-medkit-2026-09-30/validation.txt)
- `tools/re/verify_psi_medkit.py`: exact DLL hash, x64 identity, 18 body hashes,
  relocation checks, 54 single-byte mutation refusals, concrete reticle vtable
  and producer/consumer call witnesses, medkit archetype string.
- `psi_medkit_contracts`: independent head/hand rays, coordinate mapping, scope
  buffer bounds, preview provenance and gesture/neutral-input state machines.
- `psi_medkit_runtime`: production hooks and adapter with mocks at native ABI
  boundaries; original-call counts, scope isolation, HUD-candidate policy,
  preview/cast continuity, wrong-thread/reference refusal, canceled/short menu
  spans, long gaps, focus cleanup and native medkit result domains.
- VR options tests: persistence, independently disabled defaults, settings
  dispatch, empty-row navigation and production GDI panel raster/resources.

Run `python -E -B tools/re/verify_psi_medkit.py` with a working Python 3.12.
Build Release with the normal CMake workflow, then
`ctest --test-dir <build> -C Release --output-on-failure`. No game or XR runtime
is needed by these tests. Harness dispatch success is not native acceptance.

## Next headset acceptance

1. With both features off, compare shooting, the native power wheel, inventory,
   support grip and psychoscope to the integration baseline.
2. Test an area power and an entity-targeted power. Aim a gun at A while looking
   at B in Head mode, then hold left trigger. Its preview and cast should select
   B. In Left Controller mode aim the left hand at C while looking/shooting
   elsewhere; preview and cast should agree at C. Check behind-wall refusal,
   power costs, cooldowns, PsiHypo policy and insufficient-psi feedback.
3. Test menu, inventory, recenter, tracking loss and weapon/power changes during
   a held target. There must be no release-to-cast surprise or persistent time
   slowdown afterward. Include self/instant, sustained and Mimic powers.
4. Test one medkit at damaged health, no medkits, full health and cinematic/dead
   states. Check native inventory decrements once only, appropriate native heal,
   refusal feedback, and no additional use from repeated triggers on one grip.
5. Check seated/standing left-hip reach, accidental grab rate, support-grip and
   psychoscope conflicts, and whether the native targeting visuals are sufficient.

Hook installation in Prey, native execution, visual feedback, performance and
headset feel remain unmeasured. No release package was installed or published.
