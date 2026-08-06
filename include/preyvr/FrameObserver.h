#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace preyvr {

struct FrameObserverPlan {
    std::uintptr_t moduleBase = 0;
    std::uintptr_t targetRva = 0;
    std::uintptr_t targetAddress = 0;
};

// Produces a target only when the exact promoted EndRendererScene landmark
// matches the mapped image and address arithmetic cannot overflow.
std::optional<FrameObserverPlan> PlanFrameObserver(
    std::span<const std::uint8_t> mappedImage,
    std::uintptr_t moduleBase);

} // namespace preyvr
