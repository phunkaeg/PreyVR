#pragma once

#include "preyvr/VrMath.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <span>

namespace preyvr {

// Exact typed view of ArkPlayer+0x17D4 through +0x17F3 on the supported build.
struct CachedReticleState {
    Vec3 origin;
    Vec3 direction;
    Vec2 screenPosition;
};

static_assert(offsetof(CachedReticleState, origin) == 0x00);
static_assert(offsetof(CachedReticleState, direction) == 0x0C);
static_assert(offsetof(CachedReticleState, screenPosition) == 0x18);
static_assert(sizeof(CachedReticleState) == 0x20);

std::optional<CachedReticleState> DecodeCachedReticleState(
    std::span<const std::uint8_t> bytes);
bool IsUsable(const CachedReticleState& state);
float DirectionAngularDeltaDegrees(
    const CachedReticleState& before,
    const CachedReticleState& after);
float DirectionYawDeltaDegrees(Vec3 before, Vec3 after);
bool DidScreenReticleMoveRay(
    const CachedReticleState& before,
    const CachedReticleState& after,
    float minimumScreenDelta,
    float minimumDirectionDeltaDegrees);

// Research-only policy for the one-query A0b wrench experiment. CryEngine is
// Z-up, so yaw rotates the XY direction while preserving origin and reticle UI.
inline constexpr float kMaximumSyntheticYawDegrees = 15.0f;
std::optional<CachedReticleState> MakeBoundedYawProbe(
    const CachedReticleState& source,
    float yawDegrees);
std::optional<std::array<std::uint8_t, sizeof(Vec3)>> EncodeDirectionBytes(Vec3 direction);
float ExpectedRaySeparation(float distance, float angularDeltaDegrees);

} // namespace preyvr
