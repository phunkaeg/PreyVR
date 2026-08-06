#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace preyvr::engine {

inline constexpr std::string_view kSupportedPreyDllSha256 =
    "7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7";

struct ArkPlayerLayout {
    static constexpr std::uintptr_t interaction = 0xAC8;
    static constexpr std::uintptr_t camera = 0x12A0;
    static constexpr std::uintptr_t cachedReticleOrigin = 0x17D4;
    static constexpr std::uintptr_t cachedReticleDirection = 0x17E0;
    static constexpr std::uintptr_t reticleScreenPosition = 0x17EC;
};

struct ArkPlayerInteractionLayout {
    static constexpr std::uintptr_t interactionInfo = 0x128;
    static constexpr std::uintptr_t targetSelector = 0x190;
    static constexpr std::uintptr_t usableEntityId = 0x46C;
};

struct ArkPlayerTargetSelectorLayout {
    static constexpr std::uintptr_t interactDistance = 0x0C;
    static constexpr std::uintptr_t forceSelectEntityId = 0x1C;
    static constexpr std::uintptr_t candidatesBegin = 0x20;
    static constexpr std::uintptr_t candidatesEnd = 0x28;
    static constexpr std::size_t candidateRecordSize = 0x70;
    static constexpr std::uintptr_t candidateEntityId = 0x68;
};

struct RendererLayout {
    static constexpr std::uintptr_t singletonPointerRva = 0x2B3E8E0;
    static constexpr std::uintptr_t swapchain = 0xAE88;
    static constexpr std::uintptr_t device = 0xAF28;
};

struct Landmark {
    std::string_view id;
    std::string_view name;
    std::uintptr_t rva;
    std::span<const std::uint8_t> expected;
};

enum class LandmarkStatus {
    match,
    outOfRange,
    mismatch,
};

struct LandmarkValidation {
    const Landmark* landmark = nullptr;
    LandmarkStatus status = LandmarkStatus::outOfRange;
    std::size_t mismatchOffset = 0;
};

std::span<const Landmark> Landmarks();
LandmarkValidation ValidateLandmark(
    std::span<const std::uint8_t> mappedImage,
    const Landmark& landmark);
std::vector<LandmarkValidation> ValidateLandmarks(std::span<const std::uint8_t> mappedImage);
bool AllLandmarksMatch(std::span<const LandmarkValidation> results);
std::string_view ToString(LandmarkStatus status);

} // namespace preyvr::engine
