#include "preyvr/AimState.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace preyvr {
namespace {

bool Finite(float value)
{
    return std::isfinite(value);
}

float Length(Vec3 value)
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

} // namespace

std::optional<CachedReticleState> DecodeCachedReticleState(
    std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != sizeof(CachedReticleState)) {
        return std::nullopt;
    }

    CachedReticleState state{};
    std::memcpy(&state, bytes.data(), sizeof(state));
    if (!IsUsable(state)) {
        return std::nullopt;
    }
    return state;
}

bool IsUsable(const CachedReticleState& state)
{
    const bool finite =
        Finite(state.origin.x) && Finite(state.origin.y) && Finite(state.origin.z) &&
        Finite(state.direction.x) && Finite(state.direction.y) && Finite(state.direction.z) &&
        Finite(state.screenPosition.x) && Finite(state.screenPosition.y);
    if (!finite) {
        return false;
    }

    const float directionLength = Length(state.direction);
    return directionLength >= 0.95f && directionLength <= 1.05f &&
        state.screenPosition.x >= -1.0f && state.screenPosition.x <= 2.0f &&
        state.screenPosition.y >= -1.0f && state.screenPosition.y <= 2.0f;
}

float DirectionAngularDeltaDegrees(
    const CachedReticleState& before,
    const CachedReticleState& after)
{
    const float beforeLength = Length(before.direction);
    const float afterLength = Length(after.direction);
    if (beforeLength <= 1.0e-6f || afterLength <= 1.0e-6f) {
        return 0.0f;
    }
    const float dot =
        before.direction.x * after.direction.x +
        before.direction.y * after.direction.y +
        before.direction.z * after.direction.z;
    const float cosine = std::clamp(dot / (beforeLength * afterLength), -1.0f, 1.0f);
    constexpr float radiansToDegrees = 57.29577951308232f;
    return std::acos(cosine) * radiansToDegrees;
}

float DirectionYawDeltaDegrees(Vec3 before, Vec3 after)
{
    if (!Finite(before.x) || !Finite(before.y) ||
        !Finite(after.x) || !Finite(after.y)) {
        return 0.0f;
    }
    const float beforeHorizontal = before.x * before.x + before.y * before.y;
    const float afterHorizontal = after.x * after.x + after.y * after.y;
    if (beforeHorizontal <= 1.0e-12f || afterHorizontal <= 1.0e-12f) {
        return 0.0f;
    }
    constexpr float radiansToDegrees = 57.29577951308232f;
    const float crossZ = before.x * after.y - before.y * after.x;
    const float dot = before.x * after.x + before.y * after.y;
    return std::atan2(crossZ, dot) * radiansToDegrees;
}

bool DidScreenReticleMoveRay(
    const CachedReticleState& before,
    const CachedReticleState& after,
    float minimumScreenDelta,
    float minimumDirectionDeltaDegrees)
{
    if (!IsUsable(before) || !IsUsable(after) ||
        !Finite(minimumScreenDelta) || !Finite(minimumDirectionDeltaDegrees) ||
        minimumScreenDelta < 0.0f || minimumDirectionDeltaDegrees < 0.0f) {
        return false;
    }

    const float screenX = after.screenPosition.x - before.screenPosition.x;
    const float screenY = after.screenPosition.y - before.screenPosition.y;
    const float screenDelta = std::sqrt(screenX * screenX + screenY * screenY);
    return screenDelta >= minimumScreenDelta &&
        DirectionAngularDeltaDegrees(before, after) >= minimumDirectionDeltaDegrees;
}

std::optional<CachedReticleState> MakeBoundedYawProbe(
    const CachedReticleState& source,
    float yawDegrees)
{
    if (!IsUsable(source) || !Finite(yawDegrees) ||
        std::fabs(yawDegrees) < 0.1f ||
        std::fabs(yawDegrees) > kMaximumSyntheticYawDegrees) {
        return std::nullopt;
    }

    constexpr float degreesToRadians = 0.017453292519943295f;
    const float radians = yawDegrees * degreesToRadians;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);

    CachedReticleState probe = source;
    probe.direction = {
        cosine * source.direction.x - sine * source.direction.y,
        sine * source.direction.x + cosine * source.direction.y,
        source.direction.z,
    };
    if (!IsUsable(probe)) {
        return std::nullopt;
    }
    return probe;
}

std::optional<std::array<std::uint8_t, sizeof(Vec3)>> EncodeDirectionBytes(Vec3 direction)
{
    const float length = Length(direction);
    if (!Finite(direction.x) || !Finite(direction.y) || !Finite(direction.z) ||
        length < 0.95f || length > 1.05f) {
        return std::nullopt;
    }

    std::array<std::uint8_t, sizeof(Vec3)> bytes{};
    std::memcpy(bytes.data(), &direction, sizeof(direction));
    return bytes;
}

float ExpectedRaySeparation(float distance, float angularDeltaDegrees)
{
    if (!Finite(distance) || !Finite(angularDeltaDegrees) || distance < 0.0f) {
        return 0.0f;
    }
    constexpr float degreesToRadians = 0.017453292519943295f;
    return 2.0f * distance *
        std::sin(std::fabs(angularDeltaDegrees) * degreesToRadians * 0.5f);
}

} // namespace preyvr
