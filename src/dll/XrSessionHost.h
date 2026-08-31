#pragma once

#include <Windows.h>

// Hurdle 2: an OpenXR session hosted inside Prey, bound to **Prey's own D3D11
// device**, submitting Prey's own backbuffer.
//
// The session probe already proved this code path works, but it created its own
// device on whatever adapter the runtime asked for. That is the easy case. Here
// the device belongs to Prey, was created long before we loaded, and sits on
// whatever adapter Prey chose -- so the adapter question stops being theoretical.
//
// **This is what makes running under xr-sim worth doing.** xr-sim requires
// adapter index 3; Prey creates its device on index 0. That mismatch is exactly
// what `r_overrideDXGIAdapter` (R-052) exists to fix, and the real headset can
// never test it, because there the required adapter *is* index 0 and a broken
// override looks perfectly correct. The simulator hands us the adversarial case
// inside the real game.
//
// We cannot fix a mismatch at runtime: the cvar is read once during device
// creation, long before this DLL loads. So the honest thing is to **detect it
// precisely and say what to set before the next launch**, rather than attempt a
// session that will fail with a message pointing somewhere else.
//
// Bring-up runs the whole XR frame on Prey's render thread at `RT_EndFrame`,
// because that is the only place the backbuffer is valid. That deviates from
// XR-005's game/render split, so the frame contract's rule is opted out of
// explicitly via `AllowSingleThreaded` rather than left to fire every frame --
// and it has to go back before per-eye submission ships.
//
// Never starts on load. Like every other capability here, it is reached only
// through an explicit export.
namespace preyvr::dll {

enum class XrSessionStatus : DWORD {
    idle = 0,             // never started
    running = 1,          // session live and submitting
    adapterMismatch = 2,  // Prey's device is on the wrong GPU for this runtime
    unavailable = 3,      // no runtime, no system, or the loader is absent
    failed = 4,
    stopped = 5,
};

// Starts the session. Returns an XrSessionStatus.
//
// On adapterMismatch the log carries the index to set, and nothing is created --
// a session bound to the wrong device either fails at creation or, worse,
// succeeds and silently never displays.
DWORD StartXrSession();

// Requests a stop. The session is torn down from the render thread at the next
// frame boundary rather than here, because the D3D resources it holds belong to
// that thread.
DWORD StopXrSession();

DWORD XrSessionStatusValue();
unsigned long long XrSubmittedFrameCount();

// Called from the frame observer, on the render thread. Returns immediately when
// no session is running.
void ServiceXrFrame(void* renderer);

} // namespace preyvr::dll
