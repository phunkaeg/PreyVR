#pragma once

#include <windows.h>

// Resolves the native input path, so locomotion can drive Prey's own systems
// rather than a parallel simulation.
//
// **What this closes.** R-070 established the route: Prey did not replace
// CryEngine's input layer, so `IInput::PostInputEvent` feeds the same pipeline
// the keyboard and gamepad do, and a synthesised action reaches every consumer a
// real button does. It listed two things as open and said both "want a live
// process rather than a guess":
//
//   1. `gEnv`'s `pInput` slot -- **already answered in our own receipts.**
//      `SYSTEM_VTABLE.md` records slot 84 `GetIInput` relaying `gEnv+0x58`, one
//      of the 24 accessors each confirmed to land on its correctly-named member.
//      H-006 listed it as open only because that work happened separately.
//   2. The `PostInputEvent` vtable index -- genuinely open, and what this probe
//      is for.
//
// **Why a probe rather than a constant.** Counting the CryEngine 5 public header
// gives index 12. That is `INFERENCE` and no more: Prey is an Arkane fork of a
// much older CryEngine, and the touch-event pair sitting at slots 5 and 6 in the
// modern header is exactly what an older fork would lack -- which would move
// `PostInputEvent` to slot 10. Shipping a guessed index would mean calling an
// arbitrary virtual on a live engine object.
//
// So the probe **measures the alignment instead of assuming it**, using the same
// technique R-043 used on the `CSystem` vtable and R-054 on `C3DEngine`:
// `EnableEventPosting(bool)` and `IsEventPostingEnabled() const` are adjacent and
// act on the same member, so a correctly aligned table shows a `MOV [RCX+d],DL`
// setter immediately followed by a `MOV AL,[RCX+d]` getter **with the same
// displacement**. Finding that pair fixes the table, and `PostInputEvent` is the
// next slot but one by construction.
//
// Read-only and idempotent. It calls nothing, writes nothing, and hooks nothing.
namespace preyvr::dll {

// Resolves and caches. Returns 0 on success, 2 if `pInput` is still null (the
// input system is not up yet -- retry once in a level, which is a real answer
// rather than a failure), non-zero otherwise.
DWORD ResolveInputPath();

unsigned long long InputPathGEnv();
unsigned long long InputPathInputPointer();
unsigned long long InputPathVtable();

// The slot the setter/getter pair was found at, and whether it was found at all.
// **Check `InputPathAlignmentConfirmed` before using the RVA below.** When it is
// 0 the published index is the header-derived guess, not a measurement, and
// calling through it would be calling an unknown virtual.
DWORD InputPathAlignmentSlot();
DWORD InputPathAlignmentConfirmed();

// `IInput::PostInputEvent` as a PreyDll RVA, ready to check against Ghidra before
// anything is ever called through it.
unsigned long long InputPathPostInputEventRva();

// The first 20 vtable slots as RVAs, so the table can be read by eye if the
// alignment test fails and the shape has to be judged by hand.
unsigned long long InputPathSlotRva(unsigned int slot);

} // namespace preyvr::dll
