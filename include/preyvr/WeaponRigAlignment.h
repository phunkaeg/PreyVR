#pragma once

#include "preyvr/RigOwnership.h"
#include "preyvr/VrMath.h"
#include <cstddef>

namespace preyvr::weaponrig {

enum class Status {
    ready, applied, disabled, noSample, invalidOwner, unsupportedBinding, invalidSkeleton,
    invalidMount, simulatedMount, unrelatedSocket, missingHelper,
    unsupportedHelper, invalidBasis, staleAim, writeFailed
};
const char* StatusName(Status status);
enum class Source { none, attachmentDefault, jointBind, wrenchModel };
const char* SourceName(Source source);

// Read-only adapter. The DLL supplies a guarded reader; tests supply bounded
// engine-layout fixtures. No virtual/native functions are invoked by this lane.
struct Memory {
    void* context = nullptr;
    bool (*read)(void*, std::uintptr_t, void*, std::size_t) = nullptr;
};

struct Basis {
    Quaternion weaponInWrist{};
    Quaternion barrelInWeapon{};
    int socketJoint = -1;
    Source source = Source::none;
    char helper[128]{};
};

// Steam target 7d6e322f...05311a7 only. Reads the current owning arm pose and
// authored weapon data, not another character's in-flight animation output.
// The owner must already have been matched to the selected local equipment.
// A supported socket must be the driven wrist or its descendant. The native
// ADIK pass preserves that relative rotation when it propagates descendants.
Status ReadBasis(const Memory& memory, std::uintptr_t moduleBase,
                 const RigIdentity& owner, std::uintptr_t absolutePose,
                 unsigned absoluteCount, int wristJoint, Basis& out);

// Forward contract: character * wrist * weaponInWrist * barrelInWeapon = aim.
// The native ammo-helper consumer uses its +Y column as forward. Both inputs
// here are already in engine axes; do not apply an additional XR axis change.
bool SolveWrist(const Quaternion& characterWorld, const Quaternion& aimWorld,
                const Basis& basis, Quaternion& wristModel);

} // namespace preyvr::weaponrig
