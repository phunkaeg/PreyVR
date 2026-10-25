# Native hacking UI and controller bindings — 25 October 2026

Question: can the existing native input and compositor paths drive the hacking
minigame without replacing its puzzle simulation or calling an unproved method?

**Yes, as a source/static integration contract.** The supported Steam assets bind
the minigame's two analog axes to the normal left XInput stick, and its four
answer actions to X/Y/A/B. The native movie can be presented through the already
implemented modal backbuffer panel route. No new native function call is needed.
Game acceptance and headset appearance remain unmeasured.

## Target and method

Steam x64 PreyDll.dll SHA-256:
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
All addresses below are RVAs relative to image base `0x180000000`.

The EGS symbol/decompile corpus supplied names and search candidates only.
Masked-prefix searches located Steam bodies; actual Steam instructions and
strings supply the witnesses. Ghidra discovery found no running instance in
this session, so no program was switched, imported or reanalysed.

The installed archives reject ordinary ZIP and 7-Zip directory reads. That is
not an extraction blocker: the project's existing `extract_native_hud.py` reader
already handles this supported PAK format. It checks the Steam module, OAEP
padding, Twofish known-answer vector, directory bounds, entry sizes/inflation
and CRC. Archive signatures are not authenticated. Existing Twofish dependencies
were reused from the sibling checkout; no packages were installed.

The verifier reads base GameData.pak then overriding Precache/patch.pak. Both
defaultprofile.xml members contain the same hacking bindings, profile version 73.
`libs/config/profiles/default/actionmaps.xml` in the base is an empty ActionMaps
root, so that small file alone would be an invalid negative control. Parsed
member/archive hashes and derived binding facts are in
[steam-hacking-bindings.json](steam-hacking-bindings.json). Full decoded assets
remain only in ignored local build storage and are not redistributed.

## Verified default controls

| Native action | Shipped Xbox input | Quest controller source |
|---|---|---|
| hack_moveprompt_x | xi_thumblx | Left stick X |
| hack_moveprompt_y | xi_thumbly | Left stick Y |
| hack_final1 | xi_x, onPress only | Left X |
| hack_final2 | xi_y, onPress only | Left Y |
| hack_final3 | xi_a, onPress only | Right A |
| hack_final4 | xi_b, onPress only | Right B |

Forward bounded, unrotated analog axes through the existing main-thread immediate input path;
do not rotate by head/body heading, quantize to D-pad taps, or send once per eye.
Use the native input path for ordinary press/release button events so its action
map retains the onPress-only answer policy. Native ProcessInput's final-answer
branch does not independently filter releases before handling the answer; calling
it directly for both edges could answer twice. This work adds no such call.

Default assets are not live user-rebinding evidence. A custom action-map profile
could change these keys; preserve native input handling and diagnose rebinding
separately if the default controls do not reach the minigame.

### Complete action-map coverage

The complete `actionmap=hacking` has **10 actions in both base and overriding
patch**. The six Xbox bindings above are every Xbox binding in that map, rather
than a selection from a longer list. The remaining four actions are keyboard-only:

| Native action | Keyboard input | Activation |
|---|---|---|
| hack_moveup | W | Press and release |
| hack_movedown | S | Press and release |
| hack_moveleft | A | Press and release |
| hack_moveright | D | Press and release |

There is no hacking-map jump, boost, trigger or shoulder action to forward.
The movement phase uses the left stick; the native answer phase uses X/Y/A/B.
Native Start/pause remains a separate generic menu action outside this map.
The adapter orders its Quest face sources A/B/X/Y, which correctly reach native
`hack_final3/4/1/2`. It must not substitute generic A-confirm/B-cancel while the
hacking lane owns those controls.

The verifier checks the exact action count and name set, rejects duplicate or
nested actions, and verifies every keyboard-only action's full attribute set.
Its negative controls remove a keyboard direction and append an unexpected
pad-bound action; both must fail. The derived JSON records all 10 names, the
six Xbox bindings and the four keyboard-only bindings without exporting XML.

## Native producer/consumer witnesses

- `0x1626418`: native Open loads `DanielleHacking2` at string RVA `0x1E63F28`,
  then the same named-element getter, vtable byte offset `+0x60`.
- `0x162646E`: native Open uses player input receiver `player+0x8E0`, mode `7`,
  and calls `0x158F0D0` to enable its own input mode. The mod need not enable
  hacking or change native input maps itself.
- `0x119C71A`: native ArkGame input forwarding loads the hacking object from
  complete game receiver `+0x138`; call `0x119C72F` targets `0x1626CD0`.
- `0x1626CD0` handler ABI: RCX receiver, RDX pointer to CCryName token,
  R8D activation mode, XMM3 value. It refuses inactive state at receiver `+0xF0`.
  The X/Y token cases store the incoming float at receiver `+0x48/+0x4C`.
- `0x1709C6C` onward: native CGameActions constructor assigns the actual interned
  names `hack_moveprompt_y/x` to slots `+0xA30/+0xA38`, and `hack_final1..4`
  to `+0xA60..+0xA78`. The handler consumes those same token slots.
- `0x1709C92/0x1709CA5/0x1709CB8/0x1709CCB`: the same constructor assigns
  `hack_moveup/down/left/right` to `+0xA40/+0xA48/+0xA50/+0xA58`.
  Handler branches at `0x1626D70/0x1626DC2/0x1626E14/0x1626E66` compare
  left/right/up/down respectively and use the press bit of activation mode
  before updating their directional state. These four branches are the keyboard
  alternatives to the two analog stores, not extra required gamepad controls.
- `0x1626EB8`: final-answer handling requires receiver `+0x5C` enabled and
  receiver state `+0xF0 == 5`; the branch does not independently filter activation
  mode. This is why hardware action-map `onPress` filtering must be preserved.
- Existing H-005C/R-097 evidence proves ActionMap dispatch and local-player
  listener routing; a named CCryName field is not itself a hardware key ID.

The checked-in verifier confirms concrete instruction/string/token landmarks and
rejects a corrupted witness byte. These are targeted witnesses, not full-body
function hashes: PE unwind extents may split a function into several fragments.
The EGS ProcessInput export is a misleading five-byte JUMPOUT; actual PE bytes
decode normally, so its short decompile was not evidence of a missing handler.

## Presentation and ownership

The installed `DanielleHacking2` definition names
`GFX/danielle_hacking_2.gfx`, lockless layer 2, centered dynamic layout,
scale=1/max=0. Its native update owns cursor position, acceleration, collision,
timers, stun and target/answer state. Generic mouse clicks are not a substitute
for that simulation. Avoid pointer-driven keyboard/mouse-mode switches while
the hacking lane owns input, to keep controller prompt glyphs stable.

`XrSessionHost.cpp` already selects the native backbuffer as panelSource for
ordinary modal screens; only the explicit inventory path substitutes a private
texture. Adding hacking to validated modal detection therefore uses that existing
route without replaying native UI updates, copying a guessed movie vtable, or
adding an independent swapchain. Live framing still needs acceptance.

HudBridge now includes that native movie name while retaining its existing
concrete CFlashUIElement vtable/getter-byte checks. `HudHackingIsOpen()` requires
a fresh sample and refuses pause, loading, shell and dialog overlays. PDA and
external-inventory backing content may remain beneath it. Visibility-content
masks now advance HudMenuEpoch even when one modal screen replaces another;
otherwise an old queued PDA press could arrive at hacking.

## Current source integration contract

`HackingLane.cpp` posts normal left-stick and face-button events through the
existing drain-thread `PostRawInputImmediate` boundary. It does not call the
recovered handler ABI or alter native puzzle state. Its pure `HackingInput`
state machine applies a radial stick deadzone while retaining the native axis
frame, primes both axes to zero, and requires a neutral source before arming.
Delivered controls are acknowledged only after a successful post; refused
cleanup is retried. Fresh tracking, XR epoch, recenter generation, modal-content
epoch and settings generation scope each input context.

The adapter acquires controls only after `GameplayInputReleasedForHacking()`
confirms that prior delivered gameplay movement/action keys have been released.
Gameplay stays blocked while hacking has delivered release debt. Those predicates
do not call each other, and both owners retain their cleanup path while blocked.
Generic scoped menu taps and pointer events are suppressed during hacking;
already-delivered menu releases and unscoped native Start remain legal.

`CameraEditHook` drains HUD calls and refreshes modal state before queued input.
The HUD drain refreshes even on early return; delivered queued input refreshes
again. Hacking refreshes before active input and before each new face press.
Opening the VR options menu requests native pause first, rather than letting
the hacking timer run underneath the custom menu. These are source contracts and
fixture boundaries; they are not claims of live native acceptance.

## Reproduction and tests

```powershell
& 'C:/Users/meise/AppData/Local/Programs/Python/Python312/python.exe' -E -B `
  tools/re/verify_hacking_input.py `
  --deps 'D:/Dev Debug/PreyVR-integration/build/re-tools' `
  --out docs/evidence/ui-hacking-2026-10-25/steam-hacking-bindings.json
```

The verifier emits only derived metadata, never XML/movie assets or key material.
It reads the installed game without loading DLLs or opening a process.

`tests/HudModalTests.cpp` includes the actual HudBridge implementation and tests
its production modal publisher: PDA/hacking/pause changes, same-content epochs,
PDA backing, dialog coverage, unverified visibility, stale input and a failed poll.
Only external native boundaries are stubbed. The native polling vtable gates are
not exercised against fake objects. Standalone MSVC x64 Release test passed:

`hud_modal ok: content epochs, hacking ownership, coverings, freshness and failed-poll control`

The current CMake configuration includes `hacking_input`, `hud_modal` and
`hacking_runtime`. The latter compiles the actual adapter with stubbed external
boundaries, including modal refresh and prior-owner release completion; the
pure state-machine test covers delivery acknowledgements and neutral rearming.
These tests do not emulate a native puzzle accepting the posted events. The full
combined Release build passed **80/80 offline tests**, including the four new
targets `ui_poke`, `hacking_input`, `hud_modal` and `hacking_runtime`. Full output
is frozen in [ctest.txt](ctest.txt); [build.txt](build.txt) records the final
incremental build. The production options raster is [input-page.png](input-page.png).
No game, injection, xr-sim session or headset test was performed.

The first full-suite attempt exposed a fixture clock issue: rapid publications
could have different sequence numbers but identical real QPC timestamps. The
router correctly refused that discontinuity. The fixture now waits for the next
real monotonic timestamp rather than inventing a future stamp; the adapter test
then passed 200 independent process runs and the final full suite. It also checks
that the first of simultaneous face presses can close the modal and prevent
the second reaching the replacement owner. Native game acceptance and headset
interaction remain separate checks.
