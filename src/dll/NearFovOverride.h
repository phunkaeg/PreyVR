#pragma once

#include <windows.h>

// The viewmodel FOV, asserted where the renderer actually reads it.
//
// **Why the console cvar never sticks.** `r_DrawNearFoV` is not the value the
// near pass consumes. `RT_BeginFrame` latches the cvar once per frame into
// `CD3D9Renderer+0x95B4`, and that copy is what the pass reads (R-069). Prey's
// own zoom manager rewrites the cvar continuously -- hard-coding 55 on reset and
// rescaling it with the horizontal FOV while zooming -- so a one-shot console
// write is overwritten by the next thing the game feels like doing, and a level
// load loses it every time. This project has been reapplying it by hand after
// every load for several sessions, which is a workaround for not knowing that.
//
// Writing the latched copy directly steps out of that race entirely: whatever
// the cvar says, and whoever last wrote it, the pass reads what we put here.
//
// **Asserted at each render entry, not once.** The latch happens every frame in
// `RT_BeginFrame`, so a single write would survive exactly one frame. This runs
// immediately before the engine's own render call, which is after the latch and
// before the near pass consumes it.
namespace preyvr::dll {

// Decidegrees so the channel stays integral -- 1233 is 123.3 degrees. Zero
// disables the assertion and leaves the engine's own value alone, which is the
// default: this changes what the wearer sees and should not switch itself on.
// Refused outside (0, 1790]; a near FOV of zero or 180 degrees is not a value
// anybody meant to ask for.
DWORD SetNearFovDeciDegrees(unsigned int deciDegrees);
DWORD NearFovDeciDegrees();

// Called from each render entry, just before the engine renders. Cheap and
// silent when disabled.
void AssertNearFovForRender();

unsigned long long NearFovAppliedCount();
// Frames where the assertion was wanted but the renderer could not be reached.
// Distinguishes "off" from "on and not working", which one counter cannot.
unsigned long long NearFovRefusedCount();

// **The value found in the renderer immediately BEFORE we overwrote it**, in
// decidegrees. This is the receipt for the whole premise: if it keeps reading
// back as 550, or as something that tracks the horizontal FOV, then another
// writer really is clobbering the cvar every frame and this lane is the reason
// the viewmodel stays correct. If it reads back as our own value, the cvar was
// never being fought over and this lane is redundant.
DWORD NearFovObservedDeciDegrees();

} // namespace preyvr::dll
