#pragma once

#include "preyvr/DebugRegisters.h"

#include <windows.h>

// Hardware breakpoints: read a register at an address, or find what writes one.
//
// **Why this exists.** H-005 stalled on a question no amount of static reading was
// converging on: the IK targets are the right ones -- an asynchronous write moved
// the weapon for a frame -- but the only site we hold (R-077's `LEA` at
// `0x878744`) is *downstream of consumption*, so writing there does nothing. The
// producer has to be named, and it is reached through a pointer computed at
// runtime, which is precisely the case a binary search cannot close.
//
// The CPU already has the instrument. Four debug registers trap on execute or on
// write to a given address, and the trapping context carries the answer directly:
// at an execute trap the general registers name the live object; at a write trap
// the faulting `RIP` *is* the producer.
//
// **Why hardware rather than a patch.** Every other seam this project holds is a
// MinHook trampoline at a function entry. Neither thing we need here is a function
// entry: `0x878744` is 0xBF4 bytes into its function, and the producer's address is
// unknown by definition. Patching mid-function means relocating whatever
// instructions the jump lands on, in a region Ghidra has not even analysed. A debug
// register modifies no bytes at all, so there is nothing to get wrong and nothing
// to restore.
//
// **Two costs, both real.**
//
//  1. **It fights a debugger.** x64dbg wants the same four registers. Running both
//     silently yields wrong results rather than an error, so do not.
//  2. **Threads created after arming are not covered.** `ArmedThreadCount` and
//     `MissedThreadCount` are published for exactly this reason: an empty result is
//     only evidence of absence if every thread was actually armed. Call
//     `RearmThreads` after the game has spawned its workers.
//
// **Bounded by construction.** Each slot stops itself after `maxCaptures` and the
// capture ring is fixed-size and preallocated, because the handler runs inside an
// exception on a game thread where allocating would be a bad idea and blocking
// would be a worse one.
namespace preyvr::dll {

// One definition, shared with the pure encoder in `preyvr::debugreg` so the bit
// layout the tests pin is the same one the hardware is programmed with.
//
//  - `execute` traps *before* the instruction runs, so the captured registers are
//    its inputs. Length is forced to one byte, which is what the hardware requires.
//  - `write4` / `write8` trap *after* the store retires, so the captured `rip` is
//    the instruction following the writer, and the address must be aligned to its
//    own length or the breakpoint never fires at all.
using WatchKind = debugreg::WatchKind;

// One trapped event. Wide on purpose: the whole point of this build is that we do
// not yet know which register carries the thing we need, and a second run costs a
// launch, a load and a human staring at a screen.
struct WatchCapture {
    unsigned long long rip = 0;
    unsigned long long rax = 0, rcx = 0, rdx = 0, rbx = 0;
    unsigned long long rsp = 0, rbp = 0, rsi = 0, rdi = 0;
    unsigned long long r8 = 0, r9 = 0, r10 = 0, r11 = 0;
    unsigned long long r12 = 0, r13 = 0, r14 = 0, r15 = 0;
    // `[rsp]` as read at the trap. At an execute trap on a function entry this is
    // the return address; elsewhere it is only a hint, so it is reported as raw
    // data and not called a caller.
    unsigned long long stackTop = 0;
    unsigned long long sequence = 0;
    unsigned int threadId = 0;
    unsigned int slot = 0;
};

// Arms `slot` (0-3). Refuses rather than half-working:
//  - a `write4`/`write8` address that is not aligned to its length **never fires**
//    on x86, so an unaligned request is rejected instead of arming a watch that
//    would report a confident zero;
//  - a slot already armed must be disarmed first, so two callers cannot silently
//    take each other's register.
// Returns 0 on success, non-zero on refusal, and logs which.
DWORD ArmWatch(unsigned int slot, unsigned long long address, WatchKind kind,
               unsigned int maxCaptures);

DWORD DisarmWatch(unsigned int slot);
DWORD DisarmAllWatches();

// Installs the debug registers on every thread that exists right now. Called by
// `ArmWatch`; call it again after the game has spawned more threads.
DWORD RearmThreads();

// Total traps on that slot, including ones past `maxCaptures` that were counted
// but not stored. A count far above the stored total means the site is hot.
unsigned long long WatchHitCount(unsigned int slot);

// Entries currently in the ring, across all slots.
unsigned int WatchCaptureCount();
bool ReadWatchCapture(unsigned int index, WatchCapture& out);
void ClearWatchCaptures();

// Bit N set = slot N armed.
DWORD WatchArmedMask();
DWORD WatchArmedThreadCount();
DWORD WatchMissedThreadCount();

// 1 if the vectored handler is installed.
DWORD WatchHandlerInstalled();

// Traps the handler saw that belonged to no slot of ours -- passed through to
// whoever else is handling them. Non-zero alongside zero hits usually means
// something else owns the debug registers.
unsigned long long WatchForeignTrapCount();

// Applies a bounded offset to a Vec3 immediately after a *specific instruction*
// writes it.
//
// **This is the takeover mechanism, and it reuses the instrument that found the
// problem.** R-082 established that `0x87BC36` writes the IK target position last
// in every cycle, so anything written earlier is overwritten before use -- six
// live bump attempts confirmed that racing it does not work. The site is
// mid-function, so MinHook cannot take it without patching exact instruction
// bytes; but a data write watch already traps there, and traps *after the store
// retires*, which is exactly where an override has to land.
//
// `matchRip` is an absolute address, not an RVA: the caller resolves it against
// the live module, so a stale RVA cannot silently apply at the wrong site.
//
// Read-modify-write, so the hand keeps following the animation and is only
// displaced from it. Bounded three ways -- 10 m magnitude, 120 s deadline, and a
// 4-aligned target -- because an override left armed is a permanent edit to a
// running game that nobody would think to look for.
//
// Returns 0 on success; 2 unaligned or null, 3 offset too large, 4 bad duration,
// 5 the slot is not armed so nothing would ever trap.
DWORD ArmApplyOffset(unsigned int slot, unsigned long long vec3Address,
                     unsigned long long matchRip, float dx, float dy, float dz,
                     unsigned int seconds);
DWORD DisarmApplyOffset(unsigned int slot);

unsigned long long ApplyOffsetAppliedCount();
// Traps that were NOT the matching site, or produced a non-finite result. A high
// skip count with zero applied means the match address is wrong -- which is a
// different problem from the override not being armed.
unsigned long long ApplyOffsetSkippedCount();
unsigned long long ApplyOffsetExpiredCount();

} // namespace preyvr::dll
