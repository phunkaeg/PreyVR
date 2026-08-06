#include "preyvr/InteractionQuery.h"

#include <cstring>

namespace preyvr {

std::optional<InteractionCandidateSnapshot> DecodeInteractionCandidates(
    std::span<const std::uint8_t> bytes)
{
    if (bytes.size() % kInteractionCandidateRecordSize != 0) {
        return std::nullopt;
    }

    InteractionCandidateSnapshot snapshot;
    snapshot.entityIds.reserve(bytes.size() / kInteractionCandidateRecordSize);
    for (std::size_t offset = 0; offset < bytes.size();
         offset += kInteractionCandidateRecordSize) {
        std::int32_t entityId = 0;
        std::memcpy(
            &entityId,
            bytes.data() + offset + kInteractionCandidateEntityIdOffset,
            sizeof(entityId));
        snapshot.entityIds.push_back(entityId);
    }
    return snapshot;
}

std::optional<std::int32_t> SelectedInteractionEntity(
    const InteractionCandidateSnapshot& snapshot)
{
    if (snapshot.entityIds.empty()) {
        return std::nullopt;
    }
    return snapshot.entityIds.back();
}

bool DidCommittedInteractionTargetChange(
    const InteractionCandidateSnapshot& beforeCandidates,
    std::int32_t beforeUsableEntity,
    const InteractionCandidateSnapshot& afterCandidates,
    std::int32_t afterUsableEntity)
{
    const auto beforeSelected = SelectedInteractionEntity(beforeCandidates);
    const auto afterSelected = SelectedInteractionEntity(afterCandidates);
    return beforeSelected.has_value() && afterSelected.has_value() &&
        *beforeSelected == beforeUsableEntity &&
        *afterSelected == afterUsableEntity &&
        beforeUsableEntity != afterUsableEntity;
}

} // namespace preyvr
