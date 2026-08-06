#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace preyvr {

// Minimal typed projection of ArkPlayerTargetSelector's 0x70-byte records.
// Only the entity id at +0x68 is currently evidenced; all other bytes remain
// opaque until their semantics are independently reproduced.
inline constexpr std::size_t kInteractionCandidateRecordSize = 0x70;
inline constexpr std::size_t kInteractionCandidateEntityIdOffset = 0x68;

struct InteractionCandidateSnapshot {
    std::vector<std::int32_t> entityIds;
};

std::optional<InteractionCandidateSnapshot> DecodeInteractionCandidates(
    std::span<const std::uint8_t> bytes);
std::optional<std::int32_t> SelectedInteractionEntity(
    const InteractionCandidateSnapshot& snapshot);
bool DidCommittedInteractionTargetChange(
    const InteractionCandidateSnapshot& beforeCandidates,
    std::int32_t beforeUsableEntity,
    const InteractionCandidateSnapshot& afterCandidates,
    std::int32_t afterUsableEntity);

} // namespace preyvr
