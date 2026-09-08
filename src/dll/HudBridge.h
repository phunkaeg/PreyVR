#pragma once

#include <windows.h>

// Calling Prey's own HUD, through Prey's own dispatchers.
//
// **This is the HUD lane's foundation, and it is deliberately narrow.** The full
// goal -- a HUD-only transparent texture composited as its own OpenXR layer --
// needs the named element's rendering redirected into a private target *and*
// the baked-in HUD removed from the scene images. F-014/F-015 record UI
// corruption and deadlock from second-render experiments, so that is not
// something to attempt on the strength of a few resolved addresses.
//
// What *is* safe, and useful now, is driving the HUD through the engine's own
// dispatch. The reticle lane's whole weakness was that writing
// `ArkPlayer+0x17EC` proves a memory write and not visual movement. R-109 closed
// that: the engine's own reset writes the field and then dispatches, in one
// function, and this makes the same pair available.
//
// The route, all read from this build (R-108, corrected and extended by R-109):
//
//   element = (*(uiSystem + 0x60))(uiSystem, "DanielleHUD")        0x1665780
//   0x11797C0(element, name, x, y)                                 TWO floats
//   0x118C970(element, name, value)                                ONE float
//     -> (*(*element + 0x210))(element, name, args, 0, 0)          CallFunction
//
// Both build the argument array themselves, which is why they are the entry
// points rather than `+0x210` directly: constructing `SUIArguments` by hand
// would be fabricating a struct, which this project has a standing rule against.
//
// **The floats are passed in XMM2/XMM3.** The decompiler shows them as
// `undefined4`, and reading that as integer arguments in R8D/R9D would put them
// in the wrong registers entirely. The prologues are the evidence: the two-float
// entry saves XMM3 *and* XMM2, the one-float entry saves only XMM2.
//
// **A completed dispatch is not visual acceptance, and this is measured rather
// than assumed.** `hud.call SetCrosshairPosition` returned 0 against a name that
// does not exist anywhere in the binary (F-011). Scaleform resolves the function
// name inside the movie and silently does nothing when it is absent, so a zero
// return proves the ABI and the element -- never the name. Only names read from
// a native call site are known to exist.
namespace preyvr::dll {

// Dispatches `function` on the DanielleHUD element with two floats (0x11797C0).
// Fails closed at every step: no UI singleton, no element, a prologue that does
// not match, or a non-finite value all refuse rather than call. Returns 0 on a
// completed dispatch.
DWORD CallHudTwoFloat(const char* function, float x, float y);

// The same, with one float (0x118C970). This is the entry the native reticle
// reset uses for `reticleXOffset` / `reticleYOffset`.
DWORD CallHudOneFloat(const char* function, float value);

// Channel-facing form: selects the two-float entry when `twoArguments`.
DWORD CallHudFunction(const char* function, float x, float y, bool twoArguments);

// The last resolved element, or 0. Non-zero means the accessor chain works,
// which is worth knowing separately from whether a call had any visible effect.
unsigned long long HudElementPointer();
unsigned long long HudCallCount();
unsigned long long HudRefusedCount();

} // namespace preyvr::dll
