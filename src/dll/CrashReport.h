#pragma once

// A fatal exception leaves evidence beside PreyVR.log: crash-<pid>.txt (the code,
// the faulting address and the faulting thread's stack, each frame as
// module+offset, which PreyVR.map turns into a function) and crash-<pid>.dmp.
//
// **Why.** A headset session ended with Prey gone and only the exit code in
// Meta's log (0xC00000FD, a stack overflow). Without the stack there is nothing
// to fix, and the PC has no debugger.
//
// The dying thread does no work itself: a stack overflow leaves it a few
// kilobytes, so it hands its context to a thread started at install and waits.
// Only stack overflows are taken first-chance (they are never recovered); every
// other code goes through the unhandled-exception filter, so exceptions the game
// or the mod handle (the debug-register traps, the engine's own __try) never
// produce a report. Once per process.
namespace preyvr::dll {

void InstallCrashReport();

}
