#include "preyvr/EngineMap.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::vector<std::uint8_t> BuildSyntheticSupportedImage()
{
    std::size_t size = 0;
    for (const auto& landmark : preyvr::engine::Landmarks()) {
        size = std::max(size, static_cast<std::size_t>(landmark.rva) + landmark.expected.size());
    }
    std::vector<std::uint8_t> image(size, 0xCC);
    for (const auto& landmark : preyvr::engine::Landmarks()) {
        std::copy(
            landmark.expected.begin(),
            landmark.expected.end(),
            image.begin() + static_cast<std::size_t>(landmark.rva));
    }
    return image;
}

} // namespace

int main()
{
    using preyvr::engine::ArkPlayerInteractionLayout;
    using preyvr::engine::ArkPlayerLayout;
    using preyvr::engine::ArkPlayerTargetSelectorLayout;

    Require(preyvr::engine::Landmarks().size() == 28, "all promoted runtime landmarks are typed");
    Require(!preyvr::engine::AllLandmarksMatch({}), "empty validation cannot pass vacuously");
    Require(
        ArkPlayerLayout::interaction + ArkPlayerInteractionLayout::targetSelector == 0xC58,
        "ArkPlayer target-selector offset is stable");
    Require(
        ArkPlayerLayout::interaction + ArkPlayerInteractionLayout::usableEntityId == 0xF34,
        "ArkPlayer usable-entity offset is stable");
    Require(
        ArkPlayerTargetSelectorLayout::candidateRecordSize == 0x70 &&
            ArkPlayerTargetSelectorLayout::candidateEntityId == 0x68,
        "interaction candidate record layout is stable");

    auto image = BuildSyntheticSupportedImage();
    auto results = preyvr::engine::ValidateLandmarks(image);
    Require(preyvr::engine::AllLandmarksMatch(results), "exact supported image passes");

    const auto& target = preyvr::engine::Landmarks().back();
    image[static_cast<std::size_t>(target.rva) + 4] ^= 0x01;
    results = preyvr::engine::ValidateLandmarks(image);
    Require(!preyvr::engine::AllLandmarksMatch(results), "one-byte mutation fails closed");
    Require(results.back().status == preyvr::engine::LandmarkStatus::mismatch,
        "mutation is classified as mismatch");
    Require(results.back().mismatchOffset == 4, "mismatch byte is reported");

    const auto truncated = std::span<const std::uint8_t>(image.data(), 64);
    results = preyvr::engine::ValidateLandmarks(truncated);
    Require(!preyvr::engine::AllLandmarksMatch(results), "truncated image fails closed");
    Require(results.front().status == preyvr::engine::LandmarkStatus::outOfRange,
        "truncated image is classified as out of range");

    std::cout << "PreyVR engine-map tests passed\n";
    return 0;
}
