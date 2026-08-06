#include "preyvr/FrameObserver.h"

#include "preyvr/EngineMap.h"

#include <limits>

namespace preyvr {

std::optional<FrameObserverPlan> PlanFrameObserver(
    std::span<const std::uint8_t> mappedImage,
    std::uintptr_t moduleBase)
{
    if (moduleBase == 0) {
        return std::nullopt;
    }

    const engine::Landmark* target = nullptr;
    for (const auto& landmark : engine::Landmarks()) {
        if (landmark.id == "renderer.end") {
            target = &landmark;
            break;
        }
    }
    if (target == nullptr ||
        engine::ValidateLandmark(mappedImage, *target).status != engine::LandmarkStatus::match ||
        target->rva > std::numeric_limits<std::uintptr_t>::max() - moduleBase) {
        return std::nullopt;
    }

    return FrameObserverPlan{
        moduleBase,
        target->rva,
        moduleBase + target->rva,
    };
}

} // namespace preyvr
