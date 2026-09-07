#pragma once
#include <cstdint>

namespace preyvr {
struct RigIdentity {
    std::uintptr_t weapon = 0;
    std::uintptr_t attachment = 0;
    std::uintptr_t binding = 0;
    std::uintptr_t character = 0;
    std::uint64_t generation = 0;
    std::uint32_t itemId = 0;
};

inline bool SameRigBinding(const RigIdentity& captured, const RigIdentity& current,
                           std::uint32_t selectedItem)
{
    return captured.weapon && captured.attachment && captured.binding && captured.character &&
        captured.generation && captured.itemId && selectedItem == captured.itemId &&
        captured.weapon == current.weapon && captured.attachment == current.attachment &&
        captured.binding == current.binding && captured.character == current.character &&
        captured.itemId == current.itemId;
}
} // namespace preyvr
