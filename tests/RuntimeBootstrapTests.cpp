#include "preyvr/EngineMap.h"
#include "preyvr/FrameObserver.h"
#include "preyvr/OpenXRBootstrap.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
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
    auto image = BuildSyntheticSupportedImage();
    constexpr std::uintptr_t moduleBase = 0x180000000ull;
    const auto plan = preyvr::PlanFrameObserver(image, moduleBase);
    Require(plan.has_value(), "supported mapped image produces a frame-observer plan");
    Require(plan->targetRva == 0xF7E210 &&
            plan->targetAddress == moduleBase + 0xF7E210,
        "plan resolves the promoted EndRendererScene target");

    image[0xF7E210] ^= 0x01;
    Require(!preyvr::PlanFrameObserver(image, moduleBase).has_value(),
        "one-byte EndRendererScene mutation fails closed");
    Require(!preyvr::PlanFrameObserver(image, 0).has_value(),
        "null module base fails closed");
    Require(!preyvr::PlanFrameObserver(
            image, std::numeric_limits<std::uintptr_t>::max() - 0x100).has_value(),
        "overflowing target address fails closed");

    using Status = preyvr::OpenXRBootstrapStatus;
    Require(preyvr::EvaluateOpenXRBootstrap({}) == Status::disabled,
        "OpenXR bootstrap defaults disabled");
    Require(preyvr::EvaluateOpenXRBootstrap({true, false, false, false, false, false, false, false}) ==
            Status::runtimeNotRegistered,
        "missing active runtime is distinguished");
    Require(preyvr::EvaluateOpenXRBootstrap({true, true, false, false, false, false, false, false}) ==
            Status::runtimeManifestMissing,
        "missing runtime manifest is distinguished");
    Require(preyvr::EvaluateOpenXRBootstrap({true, true, true, false, false, false, false, false}) ==
            Status::runtimeManifestInvalid,
        "invalid runtime manifest is distinguished");
    Require(preyvr::EvaluateOpenXRBootstrap({true, true, true, true, false, false, false, false}) ==
            Status::loaderMissing,
        "missing app-local loader is distinguished");
    Require(preyvr::EvaluateOpenXRBootstrap({true, true, true, true, true, false, false, false}) ==
            Status::loaderWrongArchitecture,
        "wrong loader architecture is distinguished");
    Require(preyvr::EvaluateOpenXRBootstrap({true, true, true, true, true, true, false, false}) ==
            Status::loaderNotDll,
        "non-DLL loader image is distinguished");
    Require(preyvr::EvaluateOpenXRBootstrap({true, true, true, true, true, true, true, false}) ==
            Status::loaderEntryPointMissing,
        "loader without xrGetInstanceProcAddr is distinguished");
    Require(preyvr::EvaluateOpenXRBootstrap({true, true, true, true, true, true, true, true}) == Status::ready,
        "complete loader/runtime preflight is ready without runtime initialization");
    Require(preyvr::ToString(Status::ready) == "ready",
        "OpenXR bootstrap status has stable telemetry text");

    std::cout << "PreyVR runtime-bootstrap tests passed\n";
    return 0;
}
