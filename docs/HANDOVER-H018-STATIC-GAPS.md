# Handover — five static gaps blocking controller ownership

**Static answers, 2026-09-06:** [H-018 consumer/ownership report](RE-H018-STATIC-GAPS-2026-09-06.md).
Character bindings render independently; the wrench shares the base OnEquip and
has no AttachToHand override (the cited data reference is `.pdata`); GLOO and
shotgun shot construction query the current cached ray; IKLimb and the active-bind
memory walk are laid out. `tools/re/verify_h018_static_gaps.py` passes 50 target-byte
checks and 10 synthetic snapshot checks. No live game access. The original brief
below is retained, including the premises corrected by the report.

**Written 2026-09-06.** Every item here is answerable from the binary. None needs
a running game, and none should be answered by running one.

Context: [`ADDRESS_REGISTRY.md`](ADDRESS_REGISTRY.md) R-088 to R-094,
[`HYPOTHESES.md`](HYPOTHESES.md) H-013 to H-017, and
[`RE-H013-INPUT-CONSUMER-2026-09-06.md`](RE-H013-INPUT-CONSUMER-2026-09-06.md).

## What was just built, so it is not re-derived

Decompiling `RenderCHR` `0x81D0D0` settled the per-frame transform question. It
copies its **third argument** into `CRenderObject+0x00` with twelve float stores,
so editing that argument at a hook's entry is a per-frame transform. The same
listing independently reproduces R-089's near predicate — the function's own test
is `params+0x80` bit 23, or `character+0xAC8` bit 1 reached as `param_1[0x159]`.

Implemented and unit-tested this session, all offline:

* `ApplyRenderMatrixOverride` — per-frame position and rotation on the render
  matrix, replacing the `SetAttAbsoluteDefault` seam that cannot animate (H-017)
* `RotateJointAboutPivot` — rigid subtree rotation for the wrist
* Pose view now clones **both** arrays (`+0x10` relative and `+0x18` absolute),
  removing the arm-chain blocker R-088 identified
* `aim.enable` on the command channel — the aim lane existed and had never been
  reachable by anything but a debugger

## Gap 1 — is the weapon its own `RenderCHR` character? *(decisive)*

**This decides whether the seam just built can move a weapon at all.**

Live observation: an empty spawn shows **one** near character; a save holding a
GLOO cannon shows **two**, stable across passes. The inference is viewmodel arms
plus weapon. It is not proven.

Static route: `RenderCHR` is called only from `FUN_18081BCB0`, which is itself
referenced only from vtables — an `ICharacterInstance::Render`. The question is
whether `CAttachmentBONE` (`0x181D1E440` names the class) dispatches a bound
character object through that same virtual.

* If **yes**: the weapon is its own render object, the override moves it
  independently, and Gap 1 closes items 4 and 5.
* If **no** — the weapon is drawn inside the player character's own draw — then
  editing the player's matrix moves the arms *and* the weapon together, and the
  weapon needs a different seam. **That is the more valuable answer**, because it
  invalidates a lane that currently looks finished.

Also useful: which of the two near objects is which, from the binary rather than
by elimination.

## Gap 2 — the melee `AttachToHand` sibling

R-094, live: switching to the **wrench** never reaches
`CArkWeapon::AttachToHand` `0x16914F0`; switching to the **GLOO cannon** does.
The RVA is correct (Ghidra already names it, and it is virtual — data reference
at `0x182D609B8`), so hooking the body catches every dispatch to *that*
implementation. Melee therefore has its own override.

Find the class and its `AttachToHand`. Without it the wrench — Prey's opening
weapon and the one every player holds first — is outside the weapon lane
entirely.

## Gap 3 — the projectile consumer of the cached aim ray

H-004 is **reproduced** against two native consumers: a `+10` degree edit moved a
wrench wall contact `0.127165` units laterally, and a `-15` degree edit changed
the interaction selector's committed entity from `0xFDE0` to `0x1117`. Both used
synthetic offsets from a debugger.

The project's own records name the outstanding piece: *"the projectile runtime
test remains before controller-owned gameplay aim is complete."*

Statically: which `CArkWeapon` function reads ArkPlayer `+0x17D4` / `+0x17E0`
(R-012) to build a **firearm** shot, and does it read them directly or take a
copy earlier in the frame? `0x1694890` is the firing query and `0x1694A20` the
reticle position (both landmark-verified). If a firearm resolves its shot from a
value cached before `UpdateCachedReticleViewPosAndDir` runs, the aim takeover's
seam is wrong for projectiles specifically and the melee/interaction proofs do
not carry over.

## Gap 4 — the call contract for the native two-bone solver

R-088 located `IK_Solver2Bones` at `0x871CA0`, called from `0x877B50` after
matching ASCII `2BIK`, with siblings `3BIK` `0x872C60` and `CCDX` `0x874810`. It
is compiled and connected, not a stripped registration.

Known: `RCX` model-space goal, `RDX` an `IKLimb`, `R8` a `CPoseData`; reads
relative at `pose+0x10` and absolute at `+0x18` and writes both; no pole vector;
stretches up to `1.25x` per call; refuses goals below `1e-10` and clamps the
law-of-cosines term to `[-0.99, 0.99]`.

**Both arrays are now cloned**, so the blocker on our side is gone. What is
missing is the **`IKLimb` layout** — which fields index the root, mid and end
joints, what `+8` holds besides the `2BIK` tag, and which fields the solver reads
versus writes. Constructing one wrongly means handing a live solver a fabricated
struct, which is the failure mode this project has a standing rule against.

## Gap 5 — the active action-map binding

R-092: the user's profile override is **28 bytes** (`<ActionMaps version="73"/>`),
so the shipped defaults are live. Those defaults are in Arkane-encrypted PAKs —
`GameData.pak` opens `b2 23 7f c9`, `Scripts.pak` `dc bc 6d da`, neither
`PK\x03\x04` — and `tools/Chairloader-src` carries no reader or key.

Consequence today: the attract screen can be dismissed with a synthesised
keypress (R-091, proven by a native mode 2 to 3 transition), but **menu
navigation cannot**, because mode 3 routes through the action map. Loading a save
therefore still needs a human.

Either route is acceptable:

* the **in-memory** `CActionMapManager` layout, so one live read enumerates the
  active binds — `GameActions` constructor `0x1706DA0` holds the action-name
  strings (`menu_confirm` at `+0x668`, `menu_up` at `+0x5C8`, `menu_back`
  `+0x670`, `menu_exit` `+0x680`), and `loadLastSave` is in the same table
* or the **PAK container format**, which would also yield item archetype names —
  `i_giveitem` exists as a console command and is currently useless because
  nothing can name an item to give it

## Standing constraints

* **Never modify the installed game.** Static reads only for this brief.
* **The PDB-derived headers are an oracle, not the target.** Enum *values* have
  carried twice (`xi_thumblx = 0x210`, `eKI_W = 0x10`, both independently
  corroborated in the disassembly); function RVAs never do.
* **Reaching a function is not acceptance.** Five synthesised input events were
  delivered and ignored before H-013 found the actual consumer.
* A negative is valuable and should be stated as one. Gap 1 answered "no" would
  save more time than answered "yes".
