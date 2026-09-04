#pragma once

#include <cstdint>

// The x86 debug-register bit layout, kept apart from the Win32 code that applies
// it so it can be pinned by tests.
//
// **Why this is separated at all.** A wrong DR7 encoding does not crash and does
// not report an error -- the breakpoint simply never fires, and the result reads as
// a confident "nothing writes this address". That is the failure shape this project
// has been bitten by repeatedly: a measurement that cannot show the thing it
// claims to test. The arithmetic is small, total, and has no Windows dependency,
// so there is no reason for it to live anywhere it cannot be checked.
namespace preyvr::debugreg {

enum class WatchKind {
    // Traps before the instruction at the address runs. The hardware requires a
    // one-byte length for execute and ignores anything else.
    execute,
    write4,
    write8,
};

struct Encoding {
    // DR7 R/W bits: 00 execute, 01 write, 11 read-or-write.
    std::uint64_t readWrite = 0;
    // DR7 LEN bits: 00 one byte, 01 two, 11 four, 10 eight. Note that four and
    // eight are *not* in numeric order -- 0b11 is four bytes and 0b10 is eight.
    // That inversion is a real part of the ISA and the most likely thing to get
    // silently wrong.
    std::uint64_t length = 0;
    // Bytes minus one. A data breakpoint whose address is not aligned to its own
    // length never fires.
    std::uint64_t alignMask = 0;
};

Encoding EncodingFor(WatchKind kind);

// True when `address` may legally be watched with `kind`.
bool AddressIsAligned(std::uint64_t address, WatchKind kind);

// Returns `dr7` with slot `slot` (0-3) set to watch `kind`, or cleared when
// `enable` is false. Other slots are left exactly as they were, so several
// watches can coexist.
std::uint64_t ApplyDr7(std::uint64_t dr7, unsigned int slot, WatchKind kind, bool enable);

// True when slot `slot` is enabled in `dr7`.
bool Dr7SlotEnabled(std::uint64_t dr7, unsigned int slot);

// The slots reported as having fired, from DR6 -- bit N means slot N. Masked to
// the low four bits, because DR6 carries unrelated status in its upper bits.
std::uint64_t FiredSlotsFromDr6(std::uint64_t dr6);

} // namespace preyvr::debugreg
