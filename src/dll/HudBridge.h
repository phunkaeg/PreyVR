#pragma once

#include <windows.h>

#include <string>

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

// Channel-facing form: copies into a bounded queue; 0 means queued. Native
// dispatch happens only on the main thread. Completion is logged separately.
DWORD CallHudFunction(const char* function, float x, float y, bool twoArguments);
void DrainQueuedHudCalls();

// The last resolved element, or 0. Non-zero means the accessor chain works,
// which is worth knowing separately from whether a call had any visible effect.
// **Prey's own screen-to-canvas conversion, rather than our arithmetic.**
// `IUIElement::ScreenToFlash` sits at vtable `+0x2D0`, and that offset is read
// from the game's own call site rather than counted off a header: the FlowGraph
// node "convert a screen position (Value 0-1) to a actual X,Y position in the
// flash asset" registers at `0x1802F0DD0`, its ProcessEvent at `0x1802F1320`
// resolves the element through `GetInstance` (+0x20), and the callback it
// installs, `0x1802F2E60`, is a three-line forwarder whose only body is
// `CALL qword ptr [R10 + 0x2D0]` (R-119).
//
// The forwarder also fixes the shape. It passes the x and y pointers TWICE --
// `(element, &x, &y, &x, &y, flag)` -- so the conversion is in place, matching
// `ScreenToFlash(const float&, const float&, float&, float&, bool)`. Arguments
// five and six therefore land on the stack, not in registers.
//
// **This matters because it replaces a derived constant with the engine's
// answer.** R-118 measured the HUD canvas as 16:9 fitted to COVER the frame and
// corrected the reticle with that model. The model reproduced three measured
// sprite positions, but it is still our reconstruction: it assumes the canvas
// aspect, and every sample that fixed it sat on one axis. Asking the engine
// removes both limits at once, and it stays right if the constraints change.
//
// `stageScaleMode` is the engine's own flag, described in the binary as "If
// flash asset uses stage.scaleMode this must be true". Which value suits
// DanielleHUD is not established here, so it is a parameter and not a constant.
//
// Returns 0 and writes both outputs on a completed call. Fails closed: no
// module, a prologue mismatch on the getter, no element, a vtable or slot that
// does not point at committed executable memory, or a non-finite input all
// refuse without calling.
DWORD HudScreenToFlash(float screenX, float screenY, bool stageScaleMode,
                       float* outX, float* outY);

// `IUIElement::SUIConstraints`, read through `GetConstraints` at vtable `+0x128`.
//
// **This is the cover-fit at its source.** The struct carries `bScale` and
// `bMax`, and `bMax` is exactly the max-versus-min choice R-118 inferred from
// three measured pixels: cover takes the larger of the two axis ratios, fit
// takes the smaller. Reading it turns that inference into an observation.
//
// Reported, not fabricated -- this reads the live struct the element already
// holds. Nothing here writes constraints back.
struct HudConstraints {
    int positionType = -1;   // 0 fixed, 1 fullscreen, 2 dynamic, 3 fixedDynTexSize
    int left = 0, top = 0, width = 0, height = 0;
    int hAlign = -1, vAlign = -1;  // 0 lower, 1 mid, 2 upper
    bool scale = false;
    bool max = false;
};

// Returns 0 and fills `out` on success; the same fail-closed codes as above.
DWORD HudReadConstraints(HudConstraints* out);

// Queue a main-thread probe of `screenX,screenY`; 0 means queued. The result is
// read back with `HudLastProbe`, which reports the engine's conversion at both
// values of the stage-scale flag beside our own R-118 arithmetic and the live
// constraints -- one record, one frame, one aspect, so the two models can be
// compared without matching separate readings after the fact.
DWORD QueueHudProbe(float screenX, float screenY);
std::string HudLastProbe();

unsigned long long HudElementPointer();
unsigned long long HudCallCount();
unsigned long long HudRefusedCount();

} // namespace preyvr::dll
