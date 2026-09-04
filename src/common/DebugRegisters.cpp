#include "preyvr/DebugRegisters.h"

namespace preyvr::debugreg {
namespace {

constexpr unsigned int kSlots = 4;

// Per slot n: Ln at bit 2n, R/W at bit 16+4n, LEN at bit 18+4n.
constexpr std::uint64_t LocalEnableBit(unsigned int slot) { return 1ull << (2u * slot); }
constexpr unsigned int FieldShift(unsigned int slot) { return 16u + 4u * slot; }

} // namespace

Encoding EncodingFor(WatchKind kind)
{
    switch (kind) {
    case WatchKind::execute:
        return {0b00, 0b00, 0};
    case WatchKind::write4:
        return {0b01, 0b11, 3};
    case WatchKind::write8:
        return {0b01, 0b10, 7};
    }
    return {0b00, 0b00, 0};
}

bool AddressIsAligned(std::uint64_t address, WatchKind kind)
{
    return (address & EncodingFor(kind).alignMask) == 0;
}

std::uint64_t ApplyDr7(std::uint64_t dr7, unsigned int slot, WatchKind kind, bool enable)
{
    if (slot >= kSlots) {
        return dr7;
    }
    const std::uint64_t enableBit = LocalEnableBit(slot);
    const unsigned int shift = FieldShift(slot);
    const std::uint64_t fieldMask = 0b1111ull << shift;
    dr7 &= ~(enableBit | fieldMask);
    if (enable) {
        const Encoding encoding = EncodingFor(kind);
        dr7 |= enableBit;
        dr7 |= (encoding.readWrite | (encoding.length << 2)) << shift;
    }
    return dr7;
}

bool Dr7SlotEnabled(std::uint64_t dr7, unsigned int slot)
{
    if (slot >= kSlots) {
        return false;
    }
    return (dr7 & LocalEnableBit(slot)) != 0;
}

std::uint64_t FiredSlotsFromDr6(std::uint64_t dr6) { return dr6 & 0b1111ull; }

} // namespace preyvr::debugreg
