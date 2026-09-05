# H-005C — native near selection, matched camera origins, and movement input

Static investigation, 2026-09-05. Answers
[the H-005C handover](HANDOVER-H005C-SELECTION-ORIGIN-AND-INPUT.md) and extends
[H-005B](RE-H005B-MODEL-FRAME-ARM-CHAIN-2026-09-05.md).

## Decisions

1. **Near selection is available at RenderCHR entry.** Test bit `0x800000` of
   `SRendParams+0x80` OR bit `2` of `character+0xAC8`. This is the function's own
   predicate for the eventual render object's `FOB_NEAREST`. No mid-function
   marker or entity-slot pointer chase is required.
2. **The native camera getter returns the very camera the mod edits.** However,
   the handover's inference that every near matrix must therefore alternate is
   too broad: the character route with `slot+0x68 != nullptr` uses camera basis,
   not camera position. The current near-VP stereo patch also adds a second
   translation that must be included in the conversion. Publish a matched
   matrix/origin/eye tuple before enabling the full inverse consumer.
3. **Analog locomotion has a concrete native event and player handler.** The
   56-byte Prey event has keyName at `+0x10`, value at `+0x20`. Native gamepad
   events use device `3`, state `8`, `xi_thumblx`/`0x210` and
   `xi_thumbly`/`0x211`. The corresponding player actions are `xi_movex` and
   `xi_movey`; their native handlers store movement at player-input `+0x5C/+0x60`.
   Binding those input names to those actions is configuration-dependent and
   must be checked against the active map; a string registration alone does not
   prove that XML binding.

**Scope:** no process attachment, injection, live memory access, event posting,
game-file modification, DLL build or deployment. The owning live agent retains
those tasks. Only research documentation and an offline verifier were added.

## Evidence and reproducibility

Target is the installed Steam **file**, not a process image:
`D:\SteamLibrary\steamapps\common\Prey\Binaries\Danielle\x64\Release\PreyDll.dll`.
SHA-256: `7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
Addresses below are RVAs; Ghidra preferred image base is `0x180000000`.
Ghidra preflight found `/Prey/PreyDll.dll` open. Assembly and decompilation were
checked against disk bytes. Raw receipts are in ignored `captures/re/h005c/`,
with earlier entity/render receipts in `captures/re/h005b/` and `h005/`.

The project graph was queried first (`input action camera stereo`); its September
3 snapshot predates RenderFrame and this handover. The Prey PDB-derived headers
under `tools/Chairloader-src/Common/Prey/` supplied vocabulary and layout leads;
their function RVAs are **not** Steam addresses. The CE5 input struct is not ABI
compatible. Fleet BN-INP-001 still requires a press/release/neutralization owner
and a native gameplay result; these static findings do not close that live gate.

Offline checks: `py -3 tools/re/verify_h005c_selection_origin_input.py`.
They pin byte landmarks and exercise the near predicate, event ABI and origin
algebra. Synthetic fixtures are not evidence of rendered or gameplay behavior.
Result: **19 static landmarks and 9 ABI/algebra fixtures pass**. The
`--fixtures-only` option runs without needing the game DLL.

## 1. Near selection at the existing entry hook

ABI at `0x81D0D0`: RCX = character, RDX = SRendParams, R8 = Matrix34,
R9 = pass information. The following is read-only pseudocode for the existing
hook's arguments, not a new injection point:

```cpp
const bool nearest =
    (ReadU32(params, 0x80) & 0x00800000u) != 0 ||
    (ReadU8(character, 0xAC8) & 0x02u) != 0;
```

The decisive instructions are:

| RVA | Operation |
|---|---|
| `0x81D127` | Load dword `[params+0x80]` |
| `0x81D13A` | Test bit `0x17`; jump to set-near if true |
| `0x81D141` | Test byte `[character+0xAC8]`, mask `2` |
| `0x81D14A` | If both false, **clear** object flag bit `0x17` |
| `0x81D155` | Otherwise **set** object flag bit `0x17` |
| `0x81D15E` | Also write `params+0xAD = 1` on the near branch |
| `0x81D189` | OR the complete params flags into `object+0x40` |

An old flag in a pooled render object cannot invalidate this predicate: this
code explicitly clears the bit on the false path. `params+0xAD` is an output of
this decision, so it is not the entry-time input to use.

`CEntityObject::Render` (`0x973400`) sets `params+0x48 = slot` and ORs the slot's
dword `+0xA8` into params flags `+0x80`. Separately, slot byte `+0xAC`, bit `2`,
selects the camera-relative matrix construction, including the test at
`0x974551`. These are **different fields and different decisions**. A slot test
alone omits the character's own near flag; the instance token is only a slot
pointer on routes that supplied one. Do not reinterpret arbitrary instance
tokens as slots or assume that the matrix-origin branch and final near flag
are interchangeable.

Near is a render classification, not proof of player identity. Retain the
existing player/rig selection and distinguish `(character, instance token,
pass/view, render epoch)` where one character has more than one draw. A later
non-near draw must not replace that character's selected near sample. The
existing 18-byte RenderFrame entry signature matches this Steam binary.

## 2. Camera identity, ordering and the complete conversion

### It is the same camera

The entity branch calls virtual `ISystem+0x388` through the system pointer at
module RVA `0x224DA60`. Its getter is `0xDF2BB0`:

```asm
lea rax, [rcx+788h]
ret
```

`CameraEditHook.cpp` also writes `system+0x788`. Its normal alternating-eye
path in `RenderWithCameraEdit` is:

1. Copy that camera into the stack-local `restore.bytes`, then `edited`.
2. Apply any armed head rotation to `edited`.
3. `BuildSyntheticEye` adds `right * signedHalfIpd` to its translation.
4. Copy `edited` into the live camera.
5. Call the original `CSystem::Render` (`0xE0BA30`).
6. Restore the saved camera, except for the conditional retention discussed below.

The native CSystem render function itself passes `this+0x788` into camera/pass
construction before its world-render virtual call. Thus **an entity-render
camera read made within this normal original-render call is after the mod's
eye write**. The getter supplies no separate cyclops object. This does not prove
that every worker/secondary render uses that synchronous window: record the
epoch and ordering for the selected hand draw instead of sampling the global
from a later skinning job. Diagnostic double-render/replay paths need their own
matching; the normal-path ordering is not a blanket guarantee for every mode.

### There are two character near-matrix paths

Let `W` be the incoming model-to-world matrix, `M` the matrix received by
RenderCHR, and `Cread` the native camera read for that matrix. Use column-vector
notation; Matrix34 in memory is still row-major with translation indices 3/7/11.

| Character entity slot | Translation supplied to RenderCHR | Consequence under a translation-only eye edit |
|---|---|---|
| `slot+0x68 == nullptr` | `W.translation - Cread`, getter call `0x974735` | `M = T(-Cread) W`; if W is fixed and Cread alternates, M alternates |
| `slot+0x68 != nullptr` | Camera basis inverse-transpose times the pointed-to position; getter call `0x97456F` | Camera position is not read; the eye translation alone cannot make M alternate |

For an orthonormal camera basis `R`, the second branch computes `R * position`.
Both branches retain the model basis; they do not apply the camera's inverse
rotation to the model axes. H-005B gives the full matrix derivation. The raw
slot branch used by the presently selected hands remains a live observation;
near flags and skeleton identity alone do not answer it.

### Matching origins cancels eye translation in the unpatched native case

For a desired world point `P`, the original translation-free near view requires:

```text
pModel = inverse(M) * (P - Cread)
```

In the ordinary branch, substituting `M = T(-Cread) W` gives
`pModel = inverse(W) P`. Eye translation cancels exactly. Therefore the handover's
claim that *any* absolute placement necessarily inherits an eye bug is false.
It is a **mismatched** origin/matrix that causes the problem, not inversion itself.

### The active near stereo patch changes that equation

`NearViewStereo.cpp::PackViewInfoWithEyeOffset` modifies the translation row of
viewInfo's near VP at `+0xA0`, immediately around the packer at `0xFB57A0`:

```text
VP'[3][j] = VP[3][j] - d.x*VP[0][j] - d.y*VP[1][j] - d.z*VP[2][j]
```

In column notation this inserts `T(-d)` ahead of the translation-free view and
projection. `d` comes from `LastRenderedEye`, the global camera's right axis and
this feature's own half-IPD setting (or zero-delta control). The rendered target
must satisfy:

```text
M * pModel - d = P - Ceye
pModel = inverse(M) * (P - Ceye + d)
       = inverse(M) * (P - Ceff), where Ceff = Ceye - d
```

Here `Ceye` is the world-view origin corresponding to the target draw, not any
later camera sample. If `Ceye = Ccyclops + e` and **d equals e**, this simplifies
to `inverse(M) * (P - Ccyclops)`. If near stereo is disabled, rejected or in
zero-delta mode, `d=0`; use the unsimplified equation. The world stereo IPD and
near half-IPD are separate settings, so equality must be recorded, not assumed.

Consequences:

* On the camera-space-position branch, a stable M with matched d=e permits a
  stable absolute model point using the cyclops origin. Subtracting Ceye instead
  adds an extra eye-dependent model displacement: the final near VP subtracts
  the eye displacement again.
* On the ordinary branch, M may already contain `-e`. Using the active-patch
  equation can then yield an eye-dependent pModel even though the final rendered
  point is correct. A stable anatomical pose needs a consistent origin policy
  across this matrix and the near VP, not another compensating offset in a bone.
  Do not alter the globally accepted near stereo path based only on this static
  possibility; first identify the selected slot's branch.
* A raw RenderFrame matrix alone is insufficient. Mixing it with the latest
  camera, the previous eye, or an assumed half-IPD can recreate FAIL-HAND-037.

These equations extend H-005B's **native, unpatched** transform discussion; they
do not propose changing the near camera as a hand-control mechanism.

### The cyclops sample exists locally, but needs an owned publication

Capture the base pose from `edited` **after head tracking and immediately before
BuildSyntheticEye**, then capture the actual written eye pose after it. For the
normal path, this is the correct place to publish base origin, eye origin and
eye displacement with a render epoch. It is currently a stack-local opportunity,
not a RenderFrame-accessible cyclops field.

Two source defects/limitations matter before calling that publication reliable:

* At `CameraEditHook.cpp`'s `keepRotation` branch, when
  `headRotationApplied && gKeepHeadRotation` is true, the code skips the **entire**
  camera restore. Despite the comment, it retains eye translation and projection
  too. Default `gKeepHeadRotation` is false, so this is conditional; it is not a
  proven cause of the reported headset failure. Restore non-rotation state while
  preserving only the intended rotation, or otherwise establish a fresh base
  camera before the next edit. A value read at the start of the next render is
  not automatically cyclops merely because it predates that render's eye edit.
* RenderFrame's `valid=false; memcpy; valid=true` is not a safe concurrent
  publication. A reader can observe true before a writer clears it and then race
  the plain float copy. Publish the expanded tuple with synchronization or
  immutable owned-buffer lifetime that prevents concurrent access to its plain
  data. Merely adding more fields under the existing valid flag is insufficient.

Recommended tuple: selected character/instance, near predicate, slot-origin
branch when known, raw M, base/eye origin, actual near delta and whether applied,
view/pass identity, eye and epoch. Copy all retained data before the originating
stack or render object expires. Do not make a later worker dereference saved
SRendParams or slot pointers.

## 3. Input ABI and action delivery

### Prey's x64 SInputEvent

| Offset | Size | Field |
|---|---|---|
| `0x00` | 4 | deviceType: keyboard=0, mouse=1, joystick=2, gamepad=3 |
| `0x04` | 4 | state: pressed=1, released=2, down=4, changed=8, UI=16 |
| `0x08` | 2 | Windows wchar_t inputChar |
| `0x0A` | 6 | alignment padding |
| `0x10` | 8 | TKeyName: pointer to a NUL-terminated key-name string |
| `0x18` | 4 | keyId |
| `0x1C` | 4 | modifiers |
| `0x20` | 4 | float value |
| `0x24` | 4 | alignment padding |
| `0x28` | 8 | SInputSymbol pointer |
| `0x30` | 1 | deviceIndex |
| `0x31` | 7 | tail padding; total size **0x38**, alignment 8 |

Evidence is independent producer/consumer agreement, not just the header:
PostInputEvent `0x9D6D30`, SendEventToListeners `0x9D7430`, XInput Update
`0x9DAA20`, action matching `0x3CEA70`, modifier/state filtering `0x3C6660`, and
refire event copying `0x3D2360`. The refire path copies seven qwords (0x38 bytes).
The wchar_t name/width comes from the target PDB-derived header and Windows ABI;
the movement producer does not need that field. Zero the whole event.

CE5's keyName-at-8 layout is wrong here. Do not add a CE5 deviceUniqueID or use an
imagined motion-controller device enum; the native XInput producer sets `3`.

### Exact native analog event

`CXInputDevice::Init` at `0x9D9EF0` registers these symbols through `0x9D92D0`:

| Meaning | Hardware symbol ID | EKeyId | Input keyName | Native player action | Player handler RVA |
|---|---|---|---|---|---|
| Left stick X | `0x80000` | `0x210` | `xi_thumblx` | `xi_movex` | `0x158FD20` |
| Left stick Y | `0x100000` | `0x211` | `xi_thumbly` | `xi_movey` | `0x158FD80` |

The event uses **EKeyId**, not the hardware symbol ID. In `0x9DAA20`, both stick
producers set state `8`, device `3`, zero modifiers and normalized signed values
in `[-1,1]`. Center is exactly zero. Positive X/Y follow XInput's right/up stick
signs; the player handlers store the values without a sign inversion.

Minimal proposed wire object (requires an active binding and gameplay ownership):

```text
deviceType = 3; state = 8; inputChar = 0;
keyName = stable "xi_thumblx"; keyId = 0x210;
modifiers = 0; value = clamp(stickX, -1, 1);
pSymbol = nullptr; deviceIndex = the intended controller index;
// Y is a second event with "xi_thumbly", 0x211 and stickY.
```

Reject non-finite stick values before construction. This posting point is
downstream of XInput's hardware deadzone processing; apply the intended VR
deadzone once. Native XInput posts Changed when an axis changes (including its
return to zero); it does not require a digital press/release wrapper for axes.

R-080's call remains `IInput* = *(gEnv+0x58)`, vtable slot 12 / byte `+0x60`:
RCX input, RDX event pointer, R8B force. Native return type is **void**; reaching
PostInputEvent or returning from it is not acceptance evidence. Use force=false.

The event pointer is consumed synchronously, but action refire state can retain
a **copy containing its pointers**. Key names therefore need stable storage.
PostInputEvent explicitly tolerates null pSymbol; that skips native symbol-held
bookkeeping. This makes null appropriate for a proposed changed-axis event,
provided the live consumer chain is checked. Never supply a temporary fake
SInputSymbol. Keyboard hold/repeat behavior is a separate ownership problem.

### Proved player action endpoints; configuration remains separate

GameActions constructor `0x1706DA0` puts `xi_movex` and `xi_movey` at its
`+0x2F0/+0x2F8`. ArkPlayerInput constructor `0x158D1C0` registers those exact
members against `0x158FD20/0x158FD80`. Both handlers:

* Read the float fifth argument at entry stack `+0x28`.
* Store X at `this+0x5C` or Y at `this+0x60`.
* Substitute zero while cinematic mode `this+0x94` is nonzero.
* Clear the four digital movement-button flags at `+0x6C` and evaluate sprint
  cancellation thresholds. Mixed keyboard and synthetic gamepad movement is
  therefore not a transparent additive combination.

The same constructor binds the digital actions:

| Action | Keyboard key / EKeyId (target header) | Native handler RVA |
|---|---|---|
| moveforward | w / `0x10` | `0x1590550` |
| moveback | s / `0x1E` | `0x15904F0` |
| moveleft | a / `0x1D` | `0x15905B0` |
| moveright | d / `0x1F` | `0x1590610` |

These are action names, not input-event names. Posting an event whose keyName is
`xi_movex` does not directly invoke that action: the action manager hashes
**input names** and looks up configured binds.

The installed GameData/Scripts PAKs did not parse as ordinary ZIP archives;
their default profile was not extracted during this investigation. Therefore
the default/rebound XML pairing `xi_thumblx -> xi_movex` and
`xi_thumbly -> xi_movey`, and the WASD pairings, remain **binding candidates**,
not byte-proven active binds. Inspect the current player map's GetActionInput
results or an already available exported profile before posting. Do not infer
that a missing movement result disproves the ABI.

### Device filtering and ownership

`PostInputEvent` first checks posting-enabled/force and rejects unknown key ID
`-1` except UI events. `0x9D7430` then visits console listeners, the exclusive
listener, the input-blocking query and normal listeners. The blocking query
receives **keyId, deviceType, deviceIndex**. force bypasses the posting-enabled
gate only; it does not bypass those consumers.

ActionMapManager's IInputEventListener entry is **`0x3D0960`** (the listener is a
secondary base at manager+8). It checks manager enable state and console state,
then `0x3CEA70` computes a case-folded CRC of event.keyName. Candidate binds pass
the action filter and map enable/state/modifier logic at `0x3C5A80/0x3C6660`.
**There is no generic event.deviceType == bind.deviceType comparison in this
inspected matching path.** Device metadata used when loading/rebinding maps is
not such a runtime check. Nevertheless, claim the real producer's gamepad type
and intended index: the upstream blocking layer and other listeners use them.

`0x3CFAA0` dispatches accepted actions to registered blocking/extra/map
listeners, then looks up **one entity ID from that action map**, gets that
entity's game object and calls its action listener. It does not walk all actors.
ArkPlayerInput `0x158F910` dispatches through this player's handler table and
forwards to components of its stored player reference at `+0x70`.
`0x158EEC0` selects player/menu/hacking/etc. maps and sets their listener ID to
`0x7777` on enable. This is positive evidence for the player route, not permission
to hard-code a fabricated entity into a global event (the event has no entity ID).

Thus the shot-evaluator "every NPC" failure does not carry over mechanically.
The real scope risks here are **other active listeners and UI modes**, plus
competition with a physical gamepad/keyboard. Menu/hacking/flycam routes are
explicitly present. Do not post gameplay movement from every per-eye render
call or while those modes own the stick.

## Bounded next proofs for the owning live lane

These are proposed protocols, not experiments performed here. They use existing
DLL observation where possible and do not require another injector.

1. **Selection.** Hypothesis: the entry predicate equals the eventual near bit.
   Control: ordinary world characters and near=false draws. Variable: both
   predicate inputs, recorded independently. Decision: the selected near hand
   is recognized at entry and no non-near sample replaces it. Correlate any
   already available object flag observation; do not install a second marker
   solely to repeat the static truth table.
2. **Origin.** Hold input/controller target and scene fixed, compare left/right
   locked-eye records with known epoch/view identity. Record raw M, slot+0x68
   branch, pre-eye base, written eye origin and actual near d. Compute both
   sides of `M*pModel-d = P-Ceye`. The control is d=0 in an explicitly agreed
   diagnostic, not silently changing the accepted stereo configuration. Pass
   means matched equations, stable model targets on the stable-M path, and
   correctly fused hand targets at several depths. Repeat after recenter. A
   counter alone cannot retire FAIL-HAND-037.
3. **Movement.** First verify active key-name binds. On the game input/update
   thread, one owned stream per simulation update, compare neutral with a bounded
   X-only then Y-only Changed event, ending each with Changed(0). Observe native
   player movement/capsule, not only posted values; verify direction and speed.
   Gate to gameplay/focus/controller validity. Have an explicit neutralization
   owner on disarm, loss of focus/tracking, mode transitions and unload. A zero
   sent after a map is disabled may be filtered, so confirm native movement is
   cleared at transitions or deliver the neutral while the route still accepts
   it. Do not fake a device disconnect or force through console/menu filters.

No live gate is marked passed by this report. The implementation lane can now
replace the late near marker, publish the missing origin contract and build a
movement event adapter without guessing offsets or confusing keys with actions.
