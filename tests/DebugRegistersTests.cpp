#include "preyvr/DebugRegisters.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using preyvr::debugreg::AddressIsAligned;
using preyvr::debugreg::ApplyDr7;
using preyvr::debugreg::Dr7SlotEnabled;
using preyvr::debugreg::Encoding;
using preyvr::debugreg::EncodingFor;
using preyvr::debugreg::FiredSlotsFromDr6;
using preyvr::debugreg::WatchKind;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// These tests exist because a wrong DR7 encoding is silent. The breakpoint simply
// never fires, and the run reports "nothing writes this address" with no error
// anywhere -- indistinguishable from the real answer being hunted.

void ExecuteWatchesAreOneByte()
{
    const Encoding encoding = EncodingFor(WatchKind::execute);
    Require(encoding.readWrite == 0b00, "execute R/W must be 00");
    Require(encoding.length == 0b00, "execute LEN must be 00 -- the hardware allows nothing else");
    // No alignment constraint: an instruction can start at any address.
    Require(encoding.alignMask == 0, "execute must not constrain alignment");
    Require(AddressIsAligned(0x878745ull, WatchKind::execute),
            "an odd instruction address is a legal execute watch");
}

void LengthCodesForFourAndEightAreInverted()
{
    // 0b11 is FOUR bytes and 0b10 is EIGHT. That inversion is genuinely part of
    // the ISA and is the single most likely thing to be silently transposed,
    // which is the whole reason this file exists.
    Require(EncodingFor(WatchKind::write4).length == 0b11, "write4 LEN must be 0b11");
    Require(EncodingFor(WatchKind::write8).length == 0b10, "write8 LEN must be 0b10");
    Require(EncodingFor(WatchKind::write4).readWrite == 0b01, "write4 R/W must be 01");
    Require(EncodingFor(WatchKind::write8).readWrite == 0b01, "write8 R/W must be 01");
}

void DataAddressesMustBeAlignedToTheirOwnLength()
{
    Require(AddressIsAligned(0x1000ull, WatchKind::write4), "0x1000 is 4-aligned");
    Require(!AddressIsAligned(0x1001ull, WatchKind::write4), "0x1001 is not 4-aligned");
    Require(!AddressIsAligned(0x1002ull, WatchKind::write4), "0x1002 is not 4-aligned");
    Require(AddressIsAligned(0x1004ull, WatchKind::write4), "0x1004 is 4-aligned");

    Require(AddressIsAligned(0x1000ull, WatchKind::write8), "0x1000 is 8-aligned");
    Require(!AddressIsAligned(0x1004ull, WatchKind::write8), "0x1004 is not 8-aligned");

    // The real case this guards: R-078 puts pos.x at target+0x10, so a 4-aligned
    // target yields a legal write4 address for the producer watch.
    Require(AddressIsAligned(0xf02604ull + 0x10, WatchKind::write4),
            "an R-078 target position is a legal write4 address");
}

void EachSlotOccupiesItsOwnBits()
{
    std::uint64_t dr7 = 0;
    dr7 = ApplyDr7(dr7, 0, WatchKind::execute, true);
    dr7 = ApplyDr7(dr7, 1, WatchKind::write4, true);

    Require(Dr7SlotEnabled(dr7, 0), "slot 0 enabled");
    Require(Dr7SlotEnabled(dr7, 1), "slot 1 enabled");
    Require(!Dr7SlotEnabled(dr7, 2), "slot 2 untouched");
    Require(!Dr7SlotEnabled(dr7, 3), "slot 3 untouched");

    // L0 at bit 0, L1 at bit 2.
    Require((dr7 & 0b1ull) != 0, "L0 is bit 0");
    Require((dr7 & 0b100ull) != 0, "L1 is bit 2");
    // Slot 0 field at bits 16-19: execute, one byte -> all zero.
    Require(((dr7 >> 16) & 0b1111ull) == 0b0000, "slot 0 field is execute/1-byte");
    // Slot 1 field at bits 20-23: write (01) with length four (11) -> 0b1101.
    Require(((dr7 >> 20) & 0b1111ull) == 0b1101, "slot 1 field is write/4-byte");

    // Disarming slot 1 must not disturb slot 0. This is what lets the IK capture
    // watch and the producer watch run at the same time.
    dr7 = ApplyDr7(dr7, 1, WatchKind::write4, false);
    Require(Dr7SlotEnabled(dr7, 0), "slot 0 survives slot 1 being disarmed");
    Require(!Dr7SlotEnabled(dr7, 1), "slot 1 disarmed");
    Require(((dr7 >> 16) & 0b1111ull) == 0b0000, "slot 0 field intact");
    Require(((dr7 >> 20) & 0b1111ull) == 0b0000, "slot 1 field cleared");
}

void ReArmingReplacesTheFieldRatherThanMergingIntoIt()
{
    // Without the clear, switching a slot from write4 to write8 would OR 0b10 into
    // 0b11 and leave 0b11 -- watching four bytes while reporting eight.
    std::uint64_t dr7 = ApplyDr7(0, 2, WatchKind::write4, true);
    dr7 = ApplyDr7(dr7, 2, WatchKind::write8, true);
    Require(((dr7 >> 24) & 0b1111ull) == 0b1001, "slot 2 field is exactly write/8-byte");
}

void OutOfRangeSlotsChangeNothing()
{
    const std::uint64_t dr7 = ApplyDr7(0xDEADBEEFull, 4, WatchKind::write4, true);
    Require(dr7 == 0xDEADBEEFull, "slot 4 does not exist and must not alter DR7");
    Require(!Dr7SlotEnabled(0xFFFFFFFFull, 4), "slot 4 is never reported enabled");
}

void Dr6ReportsFiredSlotsInItsLowFourBitsOnly()
{
    // The upper bits carry unrelated status (single-step, task switch). Reading
    // them as slots would attribute a trap to a watch that never fired.
    Require(FiredSlotsFromDr6(0xFFFF0FF1ull) == 0b0001, "only the low nibble names slots");
    Require(FiredSlotsFromDr6(0x00004000ull) == 0, "a single-step flag names no slot");
    Require(FiredSlotsFromDr6(0b1010ull) == 0b1010, "slots 1 and 3 both fired");
}

} // namespace

int main()
{
    ExecuteWatchesAreOneByte();
    LengthCodesForFourAndEightAreInverted();
    DataAddressesMustBeAlignedToTheirOwnLength();
    EachSlotOccupiesItsOwnBits();
    ReArmingReplacesTheFieldRatherThanMergingIntoIt();
    OutOfRangeSlotsChangeNothing();
    Dr6ReportsFiredSlotsInItsLowFourBitsOnly();
    std::cout << "debug register tests passed\n";
    return 0;
}
