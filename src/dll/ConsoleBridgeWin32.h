#pragma once

#include <Windows.h>

// Types an allowlisted command into Prey's console.
//
// This exists to make a test scene *deterministic*. Comparing two captured
// frames is meaningless while the simulation is running: NPCs move, particles
// spawn, and temporal AA jitters a static image frame to frame. `t_Scale 0`
// plus `r_AntialiasingMode 0` is what turns "the image changed" from noise into
// evidence. Driving the console is also far more robust than clicking menus at
// screen coordinates.
//
// Two deliberate constraints:
//
// **Allowlist, fail closed.** Every command goes through preyvr::console::Classify
// first, which denies anything not on a short, justified list. `ExecuteString`
// will happily run whatever it is given, including `exec` against a file on
// disk, so an automation path without a gate is one typo from writing to a
// profile.
//
// **The engine's own deferred queue.** Commands are submitted with
// bDeferExecution = true, which pushes onto the console's deferred list for the
// engine to drain on its own update rather than executing on our thread. The
// residual risk is that the list push itself has no visible lock in the
// disassembly, so it is not provably safe against a concurrent drain -- but it
// is a far smaller surface than running arbitrary console work on a foreign
// thread, and submissions are serviced from the frame observer so they at least
// land on an engine thread at a frame boundary.
namespace preyvr::dll {

enum class ConsoleBridgeResult : DWORD {
    ok = 0,
    denied = 1,            // rejected by the allowlist policy
    unavailable = 2,       // gEnv, pConsole, or PreyDll could not be resolved
    signatureMismatch = 3, // ExecuteString's prologue did not match
    busy = 4,              // a command is already queued and not yet submitted
    tooLong = 5,
};

// Queues one command. It is submitted to the engine from the next observed
// frame, so the observer must be enabled for anything to happen.
DWORD QueueConsoleCommand(const char* command);
// Completion is reported only after engine CVar readback equals the requested value.
DWORD QueueVerifiedRendererSetting(const char* command);
unsigned long long VerifiedRendererSettingCount();
DWORD VerifiedRendererSettingResult();

// Called from the frame observer; returns immediately when nothing is queued.
void ServiceConsoleQueue();

DWORD LastConsoleBridgeResult();
unsigned long long SubmittedConsoleCommandCount();

} // namespace preyvr::dll
