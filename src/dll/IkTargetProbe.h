#pragma once

#include <windows.h>

// H-005, the producer hunt: name the code that writes the hand IK targets.
//
// **Where this picks up.** R-078 established the layout and then disproved its own
// headline claim: the targets are *not* static rest poses. A live write moved them
// and the game restored both within 2.5 seconds. So a takeover cannot poke the
// buffer -- it has to hold the seam where the value is produced, and that seam has
// no name yet.
//
// **The two steps, both on hardware breakpoints.**
//
//  1. `ArmIkCapture` enables the engine own debug switch and puts an *execute*
//     watch on R-077 `LEA` at `0x878744`. That instruction is 0xBF4 bytes into its
//     function, so it is not MinHook-able, and it sits in a region Ghidra has not
//     analysed, so patching it would mean relocating instructions nobody has read.
//     A debug register changes no bytes. When it trips, `r12` is a live IK target,
//     `rsi` names the limb, `r13` the owning object and `rdi` the skeleton.
//
//  2. `ArmIkProducerWatch` puts a *write* watch on one captured target position.
//     The faulting instruction is the producer. That is the whole question.
//
// **Why the engine switch is safe to touch.** It is one byte at
// `PreyDll+0x2257810` that gates a debug log path. The Frida capture on 2026-09-04
// set it and restored it. This does the same, keeps the original value, and
// restores it on disarm -- the bounded written protocol the project requires, not
// an exception to it.
//
// **What this does not do.** It writes nothing to the targets and takes nothing
// over. It answers one question -- who writes these -- and that answer is what
// M4 needs before a hand takeover can be written at all.
namespace preyvr::dll {

// Enables the log gate and arms the execute watch. `maxCaptures` bounds how many
// hits are recorded; the watch stops trapping once it is met.
DWORD ArmIkCapture(unsigned int maxCaptures);

// Restores the log gate to its original value and disarms the execute watch.
DWORD DisarmIkCapture();

// One distinct IK target seen during capture, folded across hits.
struct IkTargetRow {
    unsigned long long targetAddress = 0;   // r12
    unsigned long long limbId = 0;          // rsi -- 0x4EC / 0x7E0 and any others
    unsigned long long owner = 0;           // r13
    unsigned long long skeleton = 0;        // rdi -- differs per character
    unsigned int hits = 0;
    // Live read of the QuatT at the time of the census call, not at trap time.
    // Position in millimetres so it crosses the boundary as integers.
    int posX = 0, posY = 0, posZ = 0;
    unsigned int quatMagnitude = 0;         // thousandths; 1000 = a real rotation
    unsigned int readable = 0;              // 0 = the address has gone stale
};

// Distinct (target address) rows found. Folding by address rather than reporting
// every hit is what turns thousands of traps into the handful of live objects that
// actually exist.
unsigned int IkTargetRowCount();
bool ReadIkTargetRow(unsigned int index, IkTargetRow& out);

// Puts a write watch on `targetAddress + 0x10` (pos.x) using debug slot 1, so it
// can run alongside the capture watch in slot 0.
DWORD ArmIkProducerWatch(unsigned long long targetAddress, unsigned int maxCaptures);
DWORD DisarmIkProducerWatch();

// Distinct producer instructions found, as PreyDll RVAs -- directly usable against
// Ghidra and the registry.
unsigned int IkProducerCount();
unsigned long long IkProducerRva(unsigned int index);
unsigned int IkProducerHits(unsigned int index);

// A write breakpoint reports *after* the store retires, so `rip` is the
// instruction following the writer. Published so nobody subtracts twice or not at
// all -- the RVA above is the raw trap address, and this says what it means.
DWORD IkProducerRipIsAfterWrite();

// Gate state, so a capture that finds nothing can be told apart from a gate that
// never opened.
DWORD IkLogGateOriginalValue();
DWORD IkLogGateCurrentValue();
DWORD IkCaptureArmed();

} // namespace preyvr::dll
