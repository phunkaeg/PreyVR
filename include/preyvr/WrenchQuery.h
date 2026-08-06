#pragma once

#include "preyvr/VrMath.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace preyvr {

struct WrenchComponentState {
    std::uint64_t packageId;
    std::uint64_t criticalPackageId;
    std::uint64_t chargedPackageId;
    std::uint64_t chargedCriticalPackageId;
    std::int32_t hitType;
    float hitOffset;
    float maxForceMassScale;
    float rayRange;
    float speedRangeFactor;
    float speedRangeMax;
    float fatigueThisHit;
    bool chain;
    bool interrupt;
    bool inChainWindow;
    bool dodge;
    std::uintptr_t sneakAttackMetaTagsBegin;
    std::uintptr_t sneakAttackMetaTagsEnd;
    std::uintptr_t sneakAttackMetaTagsCapacity;
};

static_assert(offsetof(WrenchComponentState, hitType) == 0x20);
static_assert(offsetof(WrenchComponentState, hitOffset) == 0x24);
static_assert(offsetof(WrenchComponentState, rayRange) == 0x2C);
static_assert(offsetof(WrenchComponentState, sneakAttackMetaTagsBegin) == 0x40);
static_assert(sizeof(WrenchComponentState) == 0x58);

struct RayHit {
    float distance;
    std::uint32_t padding04;
    std::uintptr_t collider;
    std::int32_t partIndex;
    std::int32_t partId;
    std::int16_t surfaceIndex;
    std::int16_t originalMaterialId;
    std::int32_t foreignIndex;
    std::int32_t nodeIndex;
    Vec3 point;
    Vec3 normal;
    std::int32_t terrain;
    std::int32_t primitiveIndex;
    std::uint32_t padding44;
    std::uintptr_t next;
};

static_assert(offsetof(RayHit, collider) == 0x08);
static_assert(offsetof(RayHit, point) == 0x24);
static_assert(offsetof(RayHit, normal) == 0x30);
static_assert(offsetof(RayHit, next) == 0x48);
static_assert(sizeof(RayHit) == 0x50);

std::optional<WrenchComponentState> DecodeWrenchComponentState(
    std::span<const std::uint8_t> bytes);
std::optional<std::vector<RayHit>> DecodeRayHits(std::span<const std::uint8_t> bytes);
const RayHit* NearestRayHit(std::span<const RayHit> hits);

} // namespace preyvr
