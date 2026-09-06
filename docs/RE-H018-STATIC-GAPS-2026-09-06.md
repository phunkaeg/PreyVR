# H-018 — static consumer and ownership contracts

Static investigation, 2026-09-06, starting at repository HEAD `31b0cde`.
Target is the installed Steam
`PreyDll.dll`, SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`,
Ghidra image base `0x180000000`. Addresses below are RVAs unless stated otherwise.
No game launch, injection, process attachment, or installed-file modification.

## 1. Character attachments have an independent RenderCHR call

**Yes, for a bound skeletal character.** `CArkWeapon::AttachToHand`
`0x16914F0` reads entity slot 0. If its character pointer is non-null it allocates
a 24-byte binding with vtable `0x1CB1328`, stores the character at binding `+8`,
and binds it to the resolved hand attachment at weapon `+0x2B0` via virtual
`+0xD8`. Static meshes and entity bindings take different branches; do not claim
all weapon geometries pass through RenderCHR.

`0x821FD0` is the attachment-manager renderer, called from the parent character's
`0x81BCB0` after its own RenderCHR call. Its bone paths compose the parent matrix
with attachment model-relative QuatT at `+0x130`, then call bound object
`[attachment+0x20]->vtable[+0x18]`. For the character binding this is `0x334DF0`.
That function dispatches `[binding+8]->vtable[+0xC0]`, the character Render
`0x81BCB0`, and hence a separate RenderCHR `0x81D0D0` invocation.

One type branch matters: Render `0x81BCB0` sends default-skeleton type
`0x55AA55AA` (`[character+0x10]+0x238`, animated geometry) to `0x81C730` instead
of RenderCHR. Ordinary skeletal characters take RenderCHR. The binary proves
the independent route and the identity predicate; selecting which geometry
branch a particular asset uses still requires its slot/character receipt. Two
near-character counts alone do not close that identification.

**Identify by ownership, not draw order or a near flag alone:** at that child draw,
render parameters `+0x48` contain the binding pointer (store `0x334E0B`), and
`[binding+8]` is the rendered child character. Match it against the binding reached
from the selected weapon's `+0x2B0` attachment. The parent character owns the
attachment manager at `+0x18`; manager `+0x18` points back to the parent, and manager
`+0x20` is its attachment pointer array. A child matrix edit moves that character
independently of its parent's mesh; it also affects that child's own attachments.
The current `RenderFrame.cpp` hook edits the supplied matrix in place and does
not restore it before returning, so the caller's following attachment render sees
that edit. Redirecting R8 to a separate temporary copy would change this behavior.
The two previously observed runtime pointers cannot be assigned identities from
binary analysis alone; this supplies the exact predicate for doing so.

## 2. Negative: there is no melee AttachToHand override to hook

The handover's alleged vtable reference `0x182D609B8` is a PE exception/unwind
record. `0x16914F0` has a **direct call** at `0x169B6FB`, inside `0x169B450`.
The reference header also declares AttachToHand nonvirtual. Its missing hook hit
therefore cannot establish an override.

The target's own registration at `0x17327F0` associates `ArkWeaponWrench` with
factory vtable `0x1E957E8`; its create entry `+8` is `0x172F5E0`. This allocates
`0x560` bytes, calls the base constructor `0x1690030` with `true`, and installs
primary vtable `0x1E92F00` (LEA/store at `0x172F618`). **Wrench vtable `+8` is
`0x1699800`, the shared CArkWeapon::OnEquip.** Its direct call at `0x16999FF`
reaches `0x169B450`, which resolves `sAttachmentName`, sets weapon `+0x2B0`, and
calls AttachToHand at `0x169B6FB`. The base primary vtable `0x1E6C640` contains
the same OnEquip entry at `+8`.

The actual path is:

```
ArkWeaponWrench primary virtual +8
  -> CArkWeapon::OnEquip 0x1699800
     -> hand-attachment setup 0x169B450
        -> CArkWeapon::AttachToHand 0x16914F0
```

OnEquip can return before setup: owner ID must be `0x7777`, and equipment query
`0x1275500(player+0x14B8, itemId)` must return false. That query compares the item
or its equip ID against equipment `+0x58`. An already-selected item is one concrete
static reason that an OnEquip entry need not reach AttachToHand. This does **not**
establish why R-094 missed it. Check the selected object's identity, these guards,
and hook coverage; inventing a second AttachToHand address would hide the issue.

The `.pdata` record at RVA `0x2D609B8` is three DWORDs:
`{ begin=0x16914F0, end=0x16917EB, unwind=0x21D42F0 }`.
It is not an eight-byte function pointer. The offline check verifies both its
exception-directory membership and the absence of an absolute pointer to
AttachToHand in the image.

## 3. Firearm consumers query the current cached ray at shot construction

`0x1694890` checks local-player owner `0x7777`, gets ArkPlayer, and calls
`[player+0x40]->vtable[0]` with a six-float **stack destination**. The resolved
getter `0x157CBB0` loads interface `+0x1794..+0x17A8`, i.e. player
`+0x17D4..+0x17E8`, on every call. There is no separate weapon aim cache in this
query. It raycasts `origin + direction * [weapon+0x344]` and returns
`ReticleInfo { IEntity* at +0, Vec3 target at +8 }`.

Two concrete firearm routes are established from factories and vtables:

| Consumer | Target route | Use of query result |
| --- | --- | --- |
| CArkWeaponGooGun::OnPreRender `0x169F420` | `ArkWeaponGooGun` registration -> `0x172B4E0` -> factory `0x1E956C8`, create `0x172F900` -> constructor `0x169E1D0`; secondary subobject `weapon+0x190`, vtable `0x1E6E840`, slot `+0x30` | When pending-spawn byte `weapon+0x4CD` is set, calls firing-position query `0x1694BC0`, then ray query at `0x169F48D`. Normalizes `(raycast target - firing position)` and passes it into `0x1677040` at `0x169F5A2`; clears pending-spawn byte afterward. |
| CArkWeaponShotgun::StartAttack `0x16AAFF0` | `ArkWeaponShotgun`, `ArkWeaponPistol`, and `ArkWeaponToyGun` registrations all use factory helper `0x172B6F0` -> factory `0x1E95758`, create `0x172FA80` -> constructor `0x16A6F00`; primary vtable `0x1E6F780`, slot `+0xE8` | Calls `FireWeapon` at `0x16AB0B1`, then ray query at `0x16AB0BE`. Passes its target to point-blank handling `0x16A83F0` (call `0x16AB3B6`) and pellet spawning `0x16AAB70` (call `0x16AB451`). |

Two additional direct call sites are `0x13BB78C` in `0x13BB530` and `0x16A2CC3`
in `0x16A29E0`. The latter has a conditional reticle branch and otherwise uses a
weapon-transform direction; **do not generalize the two established firearm
routes into proof that every weapon branch always uses the ray**.

**Ordering conclusion:** no earlier firearm-only ray copy invalidates the current
takeover seam on the two routes above. They consume the latest contents of the
shared cache when invoked. The cache producer `0x1585320` still has one direct
caller, HUD OnPreRender `0x1667440`, at `0x1667718`. Current
`src/dll/AimTakeover.cpp` calls the original producer first, then replaces the
cached direction. Later getters consequently read that replacement. A shot before
the next producer call consumes the previously published cache; these bytes do
not promise same-frame controller-sample freshness or establish callback order
across all systems. Projectile impact acceptance remains a live test.

The raycast origin and projectile muzzle are distinct: the current mod preserves
the native ray origin, while weapon logic obtains its firing position separately.
A rendered-weapon matrix override alone does not establish projectile origin
ownership.

## 4. Exact IKLimb read contract

The native record is **0x30 bytes**, not three joint indices. The loader
`0x8B44D0`, reached from the `LimbIK_Definition` XML branch in `0x8B7A00`, constructs
records and appends them to default-skeleton `+0x68` with stride `0x30`.
The CE source at `Code/CryEngine/CryAnimation/Model.h` supplies field vocabulary;
the following accesses are independently present in the target.

| Offset | Target field | Direct 2BIK leaf `0x871CA0` |
| --- | --- | --- |
| `+0x00` | 64-bit limb-definition handle | Not read; used by higher-level lookup |
| `+0x08` | uint32 solver tag, `0x4B494232` = memory bytes `32 42 49 4B` (`2BIK`) | Not read; dispatchers test it |
| `+0x0C` | uint32 iteration count | Not read; CCDX reads it at `0x874962`, `0x8752A0` |
| `+0x10` | float convergence threshold | Not read; CCDX compares it at `0x875275` |
| `+0x14` | float step size | Not read; CCDX multiplies by it at `0x874CD2` |
| `+0x18` | Pointer to joint-chain records | Read; four indices below |
| `+0x20` | Pointer to int16 limb-descendant list | Not read; higher dispatcher reconstructs these afterward |
| `+0x28` | Pointer to int16 root-to-end path | Read during stretching, including count at data `-4` |

Each joint-chain record is `0x10` bytes: signed int32 joint index at `+0`, padding
at `+4`, joint-name pointer at `+8`. The loader gets the names from the skeleton;
the leaf reads **indices only**. Its prologue loads:

| Chain record | Offset from chain data | Meaning |
| --- | --- | --- |
| 0 | `+0x00` | Parent of upper/root joint |
| 1 | `+0x10` | Upper/root joint |
| 2 | `+0x20` | Mid/elbow joint |
| 3 | `+0x30` | End/wrist joint |

The loader's 2BIK branch checks that the middle joint is the end's parent and the
upper joint is the middle's parent. The root-to-end path follows actual parents
to skeleton root 0 and reverses that order. Its CryArray size is
`uint32(data[-4]) & 0x7FFFFFFF`; the leaf starts reconstruction at path element 1
using element 0 as the already-valid parent. **The size is not stored next to the
pointer inside IKLimb.** No chain-size guard or index validation exists in the
leaf before reading records 0..3.

ABI remains `void leaf(const Vec3* modelGoal, const IKLimb* limb, CPoseData* pose)`
in RCX/RDX/R8. Goal and limb, chain records, names, path and array headers are
read-only. The leaf loads pose relative pointer `+0x10` and absolute pointer
`+0x18`; it writes their QuatT elements (stride `0x1C`), **not** the two pointer
fields. Stretch scales mid/end relative translations and rebuilds the absolute
root-to-end path. Rotation solving changes chain absolute rotations and derives
corresponding relative rotations. It does not finish arbitrary descendants or
accept a wrist orientation/pole vector.

Correction to an easy misreading: the `1e-10` early-out is **squared distance from
the current end position to the goal**, not squared length of the goal relative
to model origin. A goal at `(0,0,0)` is not intrinsically rejected.

Use the selected skeleton's native limb where possible. A private direct-leaf
view must provide four valid, name-resolved indices, their real parent relations,
a readable count prefix and complete root path, and coherent private copies of
both pose arrays. Do not pass a fabricated handle to the higher dispatcher
`0x874210`; it resolves a real handle through skeleton virtual `+0x68`. Its tag
test at `0x87451F` and call at `0x874531` also confirm the direct ABI. Full solver
behavior and reconstruction cautions remain in the H-005B report.

## 5. Read-only binding enumeration contract; no PAK decryption required

**Manager discovery:** qword at module RVA `0x248BCE0` is assigned the complete
`CActionMapManager*` by constructor `0x3CD940`, store `0x3CDAEE`. Validate primary
vtable `base+0x1CBFC48` and secondary `manager+8` vtable `base+0x1CBFE08` before
walking. This constructor-only global is not a lifetime guarantee; a failed read
or validation must fail the receipt, not become an empty binding list. The input
listener at the secondary base has OnInputEvent `0x3D0960`.

| Manager offset | Field |
| --- | --- |
| `+0x30` | IInput pointer |
| `+0x38/+0x40` | Map-name tree head / count |
| `+0x48/+0x50` | Action-filter tree head / count |
| `+0x58/+0x60` | **Input-CRC-to-binding multimap head / count** |
| `+0xDC` | Manager enabled byte (OnInputEvent sees secondary `+0xD4`) |

The CRC multimap is the shortest route. `0x3CEA70` computes a case-folded CRC32
from **SInputEvent keyName at `+0x10`**, then walks this exact tree. `0x3CE210`
inserts the corresponding binding records. Names and pointer roles are thus
confirmed independently by producer and consumer.

All these target MSVC tree nodes have `left +0`, `parent +8`, `right +0x10`,
color byte `+0x18`, and **is-nil byte `+0x19`**. The head is the sentinel, with
root at head `+8`, minimum at head `+0`, maximum at head `+0x10`. Empty root is the
sentinel. A **CRC binding node** additionally has:

| Node offset | Payload |
| --- | --- |
| `+0x20` | uint32 lowercase input CRC; padding at `+0x24` |
| `+0x28` | `SActionInput*` |
| `+0x30` | `CActionMapAction*` |
| `+0x38` | `CActionMap*` |

Duplicate CRCs are allowed: the same key can bind several actions/maps. Enumerate
every node; do not use a dictionary keyed only by CRC.

| Pointed-to object | Useful fields, all offsets from its complete pointer |
| --- | --- |
| CActionMap (`0x68` bytes) | enabled byte `+8`; manager backpointer `+0x10`; action tree head/count `+0x18/+0x20`; listener entity ID `+0x28`; map-name char pointer `+0x58` |
| CActionMapAction (`0x70` bytes) | CCryName action string pointer `+0x40`; input-pointer vector begin/end/capacity `+0x48/+0x50/+0x58`; parent map `+0x60` |
| SActionInput (`0xE0` bytes) | action-device enum uint32 `+0`; **current key string pointer `+0x18`**; default key string pointer `+0x50`; CRC uint32 `+0x98`; activation mask `+0xC4`; modifiers `+0xC8`; current state `+0xD0`; analog operation `+0xD4`, comparison float `+0xAC` |

The two key strings are CryFixedString instances: follow their **pointer fields**,
not the nearby inline storage (current inline buffer `+0x20`, default `+0x58`).
Constructor `0x3C4AE0` and AddAndGetActionInput `0x3C5890` establish those offsets;
the latter lowercases both strings and computes/checks CRC `+0x98`.
Action getter `0x10EA6E0` is `lea rax,[rcx+0x40]; ret`; map-name getter `0x3C6550`
returns `[rcx+0x58]`.

**Enabled map is not the whole active predicate.** Manager virtual `+0x1A8`,
`0x3CDFE0`, checks every filter in manager `+0x48`. Filter-tree nodes have name
pointer `+0x20`, `CActionFilter* +0x28`. A filter has enabled byte `+8`, CCryName
set head/count `+0x20/+0x28`, type int32 `+0x30`, name char pointer `+0x38`.
Filter set nodes hold the interned action-name pointer at `+0x20` (compare pointer
identity, just as the game does). `0x3C3600` blocks when:

```
filter.enabled && ((filter.type == 0) == action_is_absent_from_set)
```

Thus type 0 allows listed actions and blocks absent ones; nonzero type blocks
listed actions. The manager blocks if **any** enabled filter blocks. A binding
is enabled/unfiltered only when manager and map are enabled and no filter blocks
its action. Preserve disabled/filtered entries and the blocking filter names in
the receipt; they explain a negative result.

The event gates are additional: `0x3C5A80/0x3C6660` check press/release/hold masks,
modifiers, delays and analog comparisons. `0x3D0960` and downstream dispatch also
have console/input-blocking gates. An enumerated enabled binding is a candidate
for an event, not an acceptance receipt. `SActionInput::inputDevice` and
`SInputEvent::deviceType` are different enum domains; do not copy one into the
other. Post the enumerated native key name with the separately resolved native
key ID and correct press/release states. A uint32 CRC is not a key ID.

For the live owner: take one coherent, bounded read transaction at the menu,
capture the manager, tree nodes, pointed-to objects and strings into a replayable
receipt, and emit rows for `menu_*` and `loadLastSave`. Validate sentinels, counts,
pointer ranges, cycles, vtables, backpointers and key CRCs; reject incomplete or
changing snapshots. This does not require calling any native enumeration function
or extracting/decrypting the shipped PAKs. The actual shipped key names remain
unknown until that read is made.

## Verification and evidence limits

The static discriminator is the installed module's exact bytes, checked against
Ghidra readings and control flow. Reference headers and local CE source supply
names, never address translation. The Sep-3 project graph was queried first; it
predates H-018 and does not replace these bytes. Fleet BN-INP-001 / BN-UI-001
remain open for runtime acceptance, so this does not promote fleet live evidence.

The companion offline verifier pins render/attachment dispatch, the wrench
factory/equip path, shot-query calls, solver fields, manager/binding layouts and
the exception record. It never loads the game DLL or opens a process. An offline
binding snapshot fixture checks enumeration independently of native execution.

Run with the installed working Python (the unconfigured generic `python` shim
is unnecessary):

```powershell
& 'C:/Python314/python.exe' -B tools/re/verify_h018_static_gaps.py
& 'C:/Python314/python.exe' -B tools/re/decode_h018_bind_snapshot.py --self-test
# Later, with a captured receipt supplied by the live owner:
& 'C:/Python314/python.exe' -B tools/re/decode_h018_bind_snapshot.py path/to/receipt.json
```

**Result: 50 static checks and 10 synthetic checks pass.** The entire 256-entry
native CRC table at `0x1C8AE40` is checked against reflected IEEE CRC32, connecting
the snapshot decoder's CRC calculation to the game's lookup loop. The synthetic snapshot
deliberately assigns two actions the same made-up key; it tests duplicate CRC
preservation, disabled manager/map, both filter types, and rejection of cycles,
bad counts/CRCs, missing string memory, and wrong parent-map pointers. It is not
shipped binding data. Receipt JSON uses `module_base`, `module_sha256`, `manager`,
and non-overlapping `regions: [{address, hex}]`; no process ID or native call is
accepted by the decoder. Capture coherence is the producer's responsibility.

Named eleven verified functions in Ghidra, updated the character-render answer
and added the equip, firearm, IK and manager contracts as listing comments; saved
`/Prey/PreyDll.dll`. Local raw evidence is in the gitignored
`captures/traces/2026-09-06-h018-static-receipts.json` and the exact annotation
payload in `captures/traces/2026-09-06-h018-ghidra-annotations.json`. The tracked
verifier and this report carry the reproducible evidence independently of those
local receipts.
