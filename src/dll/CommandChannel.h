#pragma once

#include <windows.h>

// Drives the mod from a text file, so routine testing needs no injector.
//
// **Why this exists.** Every live protocol so far has been driven through Frida,
// and `frida-agent.dll` crashed the host **four times in one session** with four
// different symptoms -- a wedged session, a null dereference, a wild-pointer read
// and another null. Both ways of calling into the DLL cost a run: a
// `CreateThread`-per-call helper destabilised the agent, and calling exports
// directly hit the F-009 abort that leaves a control mutex held. Trading one for
// the other is not progress, so this removes the injector from the loop entirely.
//
// It also makes a protocol **data rather than code**. A new test is a few lines in
// a file; it does not need a rebuild, and it does not need a relaunch to pick up,
// which is otherwise forced by the module pinning itself.
//
// **Fail-closed, like the console allowlist.** The channel does not invoke exports
// by name -- that would be a general call-anything primitive with a text file as
// its argument. It accepts a fixed set of verbs, each wired to one operation, and
// ignores everything else. Console commands still pass through the existing
// console allowlist, so this adds no reach there.
//
// **Nothing happens without a file.** The poll thread starts after the landmark
// gate has passed, so an unsupported build never gets a channel, and it does
// nothing at all until someone writes a command file.
namespace preyvr::dll {

// Starts the poll thread. Called from the bootstrap once the build is verified.
// Safe to call twice.
DWORD StartCommandChannel();

// Commands are read from `<log directory>/commands.txt` and results written to
// `<log directory>/results.txt`. The command file is **truncated once read**, so a
// command runs once rather than every poll.
//
// One verb per line, `#` comments and blank lines ignored:
//
//     observer 1                  frame observer (also initialises MinHook)
//     xr.runtime <path>           OpenXR runtime manifest; before xr.start
//     xr.srgb 1                   prefer the sRGB swapchain; before xr.start
//     xr.start                    begin the session
//     xr.native 1                 keep Prey's own projection per eye
//     xr.stereo <ipdmm> <halffov> arm synthetic stereo
//     xr.submit 1                 submit stereo frames
//     view.observe 1              install the view seam
//     view.recenter               set the play-space reference
//     view.apply 1                head rotation into the game camera
//     view.position 1             positional 6DoF
//     near.enable 1               per-eye offset for the near/viewmodel pass
//     near.halfipd <mm>           half-IPD for that offset
//     near.zero 1                 zero-delta control: arms every path, moves nothing
//     hand.mode <0|1|2>           off / passthrough control / apply
//     hand.joint <index>          which joint's subtree moves
//     hand.offset <x> <y> <z>     model-space millimetres
//     hand.right <index>          joint whose subtree the RIGHT controller drives
//     hand.left <index>           joint whose subtree the LEFT controller drives
//     hand.calibrate              record where the controllers are now as zero
//     hand.drive 1                controller-driven instead of a fixed offset
//     hand.scale <percent>        displacement scale; 100 is one-to-one
//     hand.character <hex>        restrict to one character instance
//     weapon.observe 1            capture the equipped weapon's attachment
//     weapon.offset <x> <y> <z>   model-space millimetres for the mount
//     weapon.apply 1              write the offset; 0 restores the captured mount
//     weapon.calibrate            record the controller aim rotation as zero
//     weapon.rotate 1             weapon rotation follows the aim controller
//     console <command>           through the existing fail-closed allowlist
//     report                      write every counter to the result file
//
// A verb that is not on this list is reported as rejected rather than ignored
// silently, because a typo that does nothing is indistinguishable from a
// mechanism that does not work -- a confusion this project has paid for.
unsigned long long CommandChannelProcessedCount();
unsigned long long CommandChannelRejectedCount();
DWORD CommandChannelRunning();

} // namespace preyvr::dll
