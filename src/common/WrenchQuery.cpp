#include "preyvr/WrenchQuery.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace preyvr {
namespace {

bool Finite(float value)
{
    return std::isfinite(value);
}

bool Finite(Vec3 value)
{
    return Finite(value.x) && Finite(value.y) && Finite(value.z);
}

} // namespace

std::optional<WrenchComponentState> DecodeWrenchComponentState(
    std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != sizeof(WrenchComponentState)) {
        return std::nullopt;
    }
    WrenchComponentState state{};
    std::memcpy(&state, bytes.data(), sizeof(state));
    if (!Finite(state.hitOffset) || !Finite(state.maxForceMassScale) ||
        !Finite(state.rayRange) || !Finite(state.speedRangeFactor) ||
        !Finite(state.speedRangeMax) || !Finite(state.fatigueThisHit) ||
        state.hitOffset < 0.0f || state.rayRange <= 0.0f || state.rayRange > 100.0f) {
        return std::nullopt;
    }
    return state;
}

std::optional<std::vector<RayHit>> DecodeRayHits(std::span<const std::uint8_t> bytes)
{
    constexpr std::size_t maximumFixtureHits = 64;
    if (bytes.size() % sizeof(RayHit) != 0 ||
        bytes.size() / sizeof(RayHit) > maximumFixtureHits) {
        return std::nullopt;
    }

    std::vector<RayHit> hits(bytes.size() / sizeof(RayHit));
    if (!bytes.empty()) {
        std::memcpy(hits.data(), bytes.data(), bytes.size());
    }
    for (const RayHit& hit : hits) {
        if (!Finite(hit.distance) || hit.distance < 0.0f ||
            !Finite(hit.point) || !Finite(hit.normal)) {
            return std::nullopt;
        }
    }
    return hits;
}

const RayHit* NearestRayHit(std::span<const RayHit> hits)
{
    if (hits.empty()) {
        return nullptr;
    }
    return &*std::min_element(hits.begin(), hits.end(), [](const RayHit& left, const RayHit& right) {
        return left.distance < right.distance;
    });
}

} // namespace preyvr
