# Handover — which listener gates the title screen, and what its handler requires

**Written 2026-09-06**, after R-090. One question, with a consumer already built
and waiting. Prior context: [R-089 and R-090 in `ADDRESS_REGISTRY.md`](ADDRESS_REGISTRY.md),
[`RE-H005C-SELECTION-ORIGIN-INPUT-2026-09-05.md`](RE-H005C-SELECTION-ORIGIN-INPUT-2026-09-05.md),
and the 2026-09-06 live section of
[`HANDOVER-H005C-SELECTION-ORIGIN-AND-INPUT.md`](HANDOVER-H005C-SELECTION-ORIGIN-AND-INPUT.md).

## The question

Prey's title screen sits on `ActiveUserManagerBase SetListening(true)` waiting
for a key. **Synthesised input reaches the normal listener list and the screen
does not respond.**

So: **which registered input listener backs that screen, and what does its
handler require that a synthesised event does not supply?**

## What is already proven — do not re-derive any of it

The whole path from our call to the listener walk is decoded and permissive.
This is the part that was expected to be wrong, and it is not.

`PostInputEvent` **`0x9D6D30`**:

```c
if (((force != 0) || (*(char*)(pInput + 0x78) != 0)) &&
    ((keyId != -1) || (state == 0x10)))
{
    SendEventToListeners(pInput, event);        // 0x9D7430, always reached
    if (returned && *(char*)(pInput + 0x79) == 0 && event->pSymbol != NULL) {
        // hold-symbol bookkeeping only; skipped whole for a null symbol
    }
}
```

`SendEventToListeners` **`0x9D7430`**, four stages, first non-zero return wins:

| stage | member | dispatch |
|---|---|---|
| console listeners | `pInput+0x38` (intrusive list) | listener vtable `+0x10` when `state == 0x10`, else `+0x08` |
| exclusive listener | `pInput+0x48` (single pointer) | same |
| blocking query | virtual `*pInput + 0x188` → `0x9D7790` | `(keyId, deviceType, deviceIndex)` |
| **normal listeners** | `pInput+0x28` (intrusive list) | same, **only when the query returned 0** |

The blocking query at **`0x9D7790`** is **not a defined function in the current
database** — create it. Decoded from bytes: it walks a list at `this+0xD0` whose
entries are `{keyId +0x14, deviceIndex +0x18, all-devices flag +0x19}`, and
returns 0 immediately when that list is empty. It blocks only what something
explicitly registered.

Known `IInput` members: `+0x28` normal listeners, `+0x38` console listeners,
`+0x48` exclusive listener, `+0x78` posting-enabled, `+0x79` hold-symbol
suppression, `+0xD0` blocked inputs. Vtable at RVA **`0x1D5C4F0`**;
`PostInputEvent` is slot 12, the blocking query slot 49 (`+0x188`).

The first twenty vtable slots, read live, as RVAs:

```
0=0x9d1860  1=0x9d5d20  2=0x9d7160  3=0x9d5c40  4=0x9826f0  5=0x9d5e60
6=0x9d71b0  7=0x9843b0  8=0x16fe0d0 9=0x9d5e00 10=0x9d6310 11=0x9d6a60
12=0x9d6d30 13=0x9d6fd0 14=0x1706520 15=0x9d6350 16=0x9d6480 17=0x9d1940
18=0x9d6ce0 19=0x9d1b70
```

Slot 10 / 11 are the `EnableEventPosting` / `IsEventPostingEnabled` pair whose
adjacency fixes the table (R-080), confirmed live: `alignment_pair_slot=10
confirmed=1 post_input_event_slot=12 rva=0x9d6d30`.

The `SInputEvent` ABI is R-089 and is byte-proven: 0x38 bytes, `deviceType 0x00`,
`state 0x04`, `inputChar 0x08`, **`keyName 0x10` (a pointer)**, `keyId 0x18`,
`modifiers 0x1C`, `value 0x20`, `pSymbol 0x28`, `deviceIndex 0x30`. CryEngine 5's
public layout puts the key name at `0x08` and is wrong here.

## Four recorded negatives, from a live run

Every one delivered — our counter `inputPosted` incremented — and every one left
the screen byte-identical, compared as per-eye captures rather than by eye:

| attempt | key | state | device | result |
|---|---|---|---|---|
| gamepad accept | `xi_a` `0x20A` | pressed/released | 3 | no change |
| keyboard space | `space` `0x38` | pressed/released | 0 | no change |
| keyboard space, forced | `space` `0x38` | pressed/released | 0 | no change |
| keyboard space, UI state | `space` `0x38` | 16 | 0 | no change |
| keyboard enter | `enter` `0x1B` | pressed/released | 0 | no change |

**So it is not the device, not the posting gate, not the UI state, and not
`force`.** Please do not spend a pass re-establishing any of those.

## A route that should be mechanical

The listener list is `pInput+0x28`. Find the function that **inserts** into it —
`AddEventListener`, almost certainly among the twenty slots above — and then take
its callers. That enumerates every registered listener in the build, which turns
"which one is it" from a search into a list to read.

From there the interesting handlers are vtable `+0x08` (`OnInputEvent`) and
`+0x10` (`OnInputEventUI`) on whichever listener belongs to the front end.

`ActiveUserManagerBase` and `SetListening` appear as strings in the log output,
so a string search is a second independent way in. `CGame::RemoveExclusiveController`
logs `deviceId: 0, deviceIndex: -1` at startup, which is why an active-controller
or device-index predicate is the leading suspect.

## Suspects worth confirming or killing explicitly

* **A non-null `pSymbol` requirement.** We pass null. `PostInputEvent` tolerates
  it, but a listener that dereferences or tests `pSymbol` would drop the event
  without any of our counters noticing.
* **An active-user / device-index gate.** `deviceIndex` is `0`, and the game
  removed its exclusive controller with `deviceIndex: -1`.
* **A different input route entirely** — the front end reading the Windows
  message loop or raw input rather than `IInput`. This would be the most
  valuable negative of the three, because it means no synthesised `SInputEvent`
  can ever drive the title screen and the menu lane needs a different seam.

## Why this matters beyond a menu

The product's end state is "no mouse". Menu navigation through the engine's own
input layer *is* the controller binding; it is not a testing convenience. The
same answer also unblocks **locomotion**, which is built and tested as a pure
layer but deliberately unwired: `xi_thumblx`/`0x210` and `xi_thumbly`/`0x211`
reach player handlers `0x158FD20`/`0x158FD80`, storing at `playerInput+0x5C/+0x60`
(R-089). If a synthesised event cannot reach a consumer, that lane is blocked for
the same reason.

## Live confirmation is now cheap

There is a working unattended loop: `tools/Invoke-PreyVRLaunch.ps1` starts the
game with an explicit XR child environment, Frida `LoadLibraryW` injects the mod
(**never x64dbg `loadlib`** — F-008), and `xrsim-shot.ps1` reads back what the
compositor would show, per eye, with a JSON sidecar. So a candidate predicate can
be tested against a real screen in one cycle rather than argued about.

Once you have a candidate, the mod can post any key id and state through its file
command channel: `input.key <keyId> <state> <valueMilli>`, `input.force <0|1>`.

## Standing constraints

* **Never modify the installed game.** All live work read-only unless a bounded,
  reversible written protocol exists.
* **The PDB-derived headers under `tools/Chairloader-src/` are an oracle, not the
  target.** Their function RVAs are for a different build. Enum *values* have
  carried across twice now — `xi_thumblx = 0x210` and `eKI_W = 0x10` are both
  independently corroborated by the disassembly — but nothing else should be
  taken on their word.
* **Reaching or returning from a native call is not acceptance evidence.** That
  is the whole reason this handover exists: five events were delivered and the
  screen did not move.
* A cvar existing proves nothing (H-012). Four animation cvars reach the engine
  and do nothing.

## Rakes, from this project's own record

Every one of these was documented before it was stepped on again.

1. **`MH_Initialize` lived in one feature's enable path**, so every other hook
   installer silently depended on that feature having been turned on. Cost three
   live runs; now `EnsureMinHook()` in `src/dll/MinHookInit.h`.
2. **A decode that replicates cleanly is not a decode that is right.** An `rsi`
   value was read as a limb id when it was a byte offset (`IMUL RSI,R10,0x1C`),
   and it agreed with the rig names twice before anyone checked.
3. **Existence is not a receipt.** A capture was judged by whether the file was
   there, which let a previous run's image be reported as a new one.
4. **Read the picture before drawing the conclusion.** `Game.log` showing a
   campaign level loading was read as input having worked; Prey preloads that
   level while sitting at the title, and the capture still said "Press Any Key".
