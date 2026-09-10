#include "preyvr/WeaponRigAlignment.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace preyvr::weaponrig {
namespace {
constexpr std::uintptr_t kBoneVtable = 0x1D212B8;
constexpr std::uintptr_t kCharacterBindingVtable = 0x1CB1328;
constexpr std::uintptr_t kCharacterVtable = 0x1D22200;
constexpr std::uintptr_t kManagerVtable = 0x1D22110;
constexpr std::uintptr_t kSkeletonVtable = 0x1D2A3E8;
constexpr unsigned kMaxJoints = 768, kMaxAttachments = 256;

bool ValidQ(const Quaternion& q)
{
    const float n = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return std::isfinite(n) && n > 0.81f && n < 1.21f;
}
Quaternion Inverse(const Quaternion& q) { return {-q.x, -q.y, -q.z, q.w}; }

struct Reader {
    Memory memory;
    template<class T> bool Get(std::uintptr_t p, std::size_t offset, T& value) const {
        if (!memory.read || !p || offset > std::numeric_limits<std::uintptr_t>::max() - p ||
            sizeof(T) > std::numeric_limits<std::uintptr_t>::max() - (p + offset)) { return false; }
        return memory.read(memory.context, p + offset, &value, sizeof(T));
    }
    bool Is(std::uintptr_t p, std::uintptr_t expected) const {
        std::uintptr_t v = 0;
        return Get(p, 0, v) && v == expected;
    }
    bool Count(std::uintptr_t array, unsigned limit, unsigned& count) const {
        std::uint32_t raw = 0;
        if (array < 4 || !Get(array - 4, 0, raw)) { return false; }
        count = raw & 0x7fffffffu;
        return count <= limit;
    }
    bool String(std::uintptr_t p, char (&name)[128]) const {
        if (!p) { return false; }
        for (unsigned i = 0; i < sizeof(name); ++i) {
            if (!Get(p, i, name[i])) { return false; }
            if (!name[i]) { return i != 0; }
            // Asset helper names are ASCII. Refuse unsupported names, rather
            // than changing the engine's case-insensitive lookup semantics.
            if (static_cast<unsigned char>(name[i]) > 127) { return false; }
        }
        name[127] = 0;
        return false;
    }
    bool Rotation(std::uintptr_t p, std::size_t offset, Quaternion& q) const {
        return Get(p, offset, q) && ValidQ(q);
    }
    bool PoseRotation(std::uintptr_t array, int index, Quaternion& q) const {
        unsigned count = 0;
        return index >= 0 && Count(array, kMaxJoints, count) &&
            static_cast<unsigned>(index) < count && Rotation(array, index * 0x1Cull, q);
    }
};

bool EqualName(const char* a, const char* b)
{
    for (unsigned i = 0; i < 128; ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
        if (lower(a[i]) != lower(b[i])) { return false; }
        if (!a[i]) { return true; }
    }
    return false;
}

bool Skeleton(const Reader& r, std::uintptr_t base, std::uintptr_t character,
              std::uintptr_t& skeleton, std::uintptr_t& joints, unsigned& count)
{
    return r.Is(character, base + kCharacterVtable) && r.Get(character, 0x10, skeleton) &&
        r.Is(skeleton, base + kSkeletonVtable) && r.Get(skeleton, 8, joints) &&
        r.Count(joints, kMaxJoints, count) && count != 0;
}

bool Descendant(const Reader& r, std::uintptr_t joints, unsigned count, int socket, int wrist)
{
    if (wrist < 0 || static_cast<unsigned>(wrist) >= count) { return false; }
    for (unsigned step = 0; step < count; ++step) {
        if (socket < 0 || static_cast<unsigned>(socket) >= count) { return false; }
        if (socket == wrist) { return true; }
        std::int16_t parent = -1;
        if (!r.Get(joints, socket * 0xA8ull + 0x18, parent)) { return false; }
        socket = parent;
    }
    return false; // cyclic or detached hierarchy
}

bool NoSimulation(const Reader& r, std::uintptr_t attachment)
{
    std::uint8_t simulation = 0, redirect = 0;
    return r.Get(attachment, 0x30, simulation) && r.Get(attachment, 0x33, redirect) &&
        !simulation && !redirect;
}
} // namespace

Status ReadBasis(const Memory& memory, std::uintptr_t moduleBase,
                 const RigIdentity& owner, std::uintptr_t absolutePose,
                 int wristJoint, Basis& out)
{
    out = {};
    const Reader r{memory};
    if (!moduleBase || moduleBase > std::numeric_limits<std::uintptr_t>::max() - 0x2000000 ||
        !owner.generation || !owner.itemId || !owner.weapon || !owner.attachment || !owner.binding ||
        !owner.character) { return Status::invalidOwner; }
    std::uintptr_t mount = 0, binding = 0, manager = 0, character = 0;
    if (!r.Get(owner.weapon, 0x2B0, mount) || mount != owner.attachment ||
        !r.Is(mount, moduleBase + kBoneVtable) || !r.Get(mount, 0x20, binding) ||
        binding != owner.binding || !r.Get(mount, 0x28, manager) ||
        !r.Is(manager, moduleBase + kManagerVtable) || !r.Get(manager, 0x18, character) ||
        character != owner.character) { return Status::invalidOwner; }
    if (!r.Is(binding, moduleBase + kCharacterBindingVtable)) { return Status::unsupportedBinding; }
    if (!NoSimulation(r, mount)) { return Status::simulatedMount; }
    std::uintptr_t skeleton = 0, joints = 0, bindPose = 0;
    unsigned count = 0;
    if (!Skeleton(r, moduleBase, character, skeleton, joints, count) ||
        !r.Get(skeleton, 0x30, bindPose)) { return Status::invalidSkeleton; }
    Basis candidate{};
    if (!r.Get(mount, 0x15C, candidate.socketJoint)) { return Status::invalidMount; }
    if (!Descendant(r, joints, count, candidate.socketJoint, wristJoint)) { return Status::unrelatedSocket; }
    Quaternion wrist{}, socket{}, bind{}, absoluteDefault{}, extra{};
    if (!r.PoseRotation(absolutePose, wristJoint, wrist) ||
        !r.PoseRotation(absolutePose, candidate.socketJoint, socket) ||
        !r.PoseRotation(bindPose, candidate.socketJoint, bind) ||
        !r.Rotation(mount, 0x114, absoluteDefault) || !r.Rotation(mount, 0x14C, extra)) {
        return Status::invalidMount;
    }
    // The consumer reads J * relativeDefault * K. ProjectAttachment rebuilds
    // relativeDefault = inverse(B) * A only while projected bit 0x4000 is clear.
    // With the bit set, use the field the consumer will actually read, including
    // any native adjustment made since projection.
    std::uint32_t flags = 0;
    if (!r.Get(mount, 8, flags)) { return Status::invalidMount; }
    Quaternion relativeDefault = Multiply(Inverse(bind), absoluteDefault);
    if ((flags & 0x4000u) && !r.Rotation(mount, 0xF8, relativeDefault)) { return Status::invalidMount; }
    candidate.weaponInWrist = Multiply(Multiply(Multiply(
        Inverse(wrist), socket), relativeDefault), extra);

    std::uintptr_t weaponCharacter = 0, weaponSkeleton = 0, weaponJoints = 0;
    unsigned weaponCount = 0;
    if (!r.Get(binding, 8, weaponCharacter) || weaponCharacter == character ||
        !Skeleton(r, moduleBase, weaponCharacter, weaponSkeleton, weaponJoints, weaponCount)) {
        return Status::invalidSkeleton;
    }
    const std::uintptr_t weaponManager = weaponCharacter + 0x18;
    std::uintptr_t managerOwner = 0, helpers = 0, helperName = 0;
    unsigned helperCount = 0;
    if (!r.Is(weaponManager, moduleBase + kManagerVtable) ||
        !r.Get(weaponManager, 0x18, managerOwner) || managerOwner != weaponCharacter ||
        !r.Get(weaponManager, 0x20, helpers) || !r.Count(helpers, kMaxAttachments, helperCount) ||
        !r.Get(owner.weapon, 0x2F0, helperName) || !r.String(helperName, candidate.helper)) {
        return Status::missingHelper;
    }
    // Mirror native lookup precedence: attachment first, then skeleton joint.
    // Never fall through from an unsupported matching attachment to a joint.
    for (unsigned i = 0; i < helperCount; ++i) {
        std::uintptr_t helper = 0, namePointer = 0;
        char name[128]{};
        if (!r.Get(helpers, i * 8ull, helper) || !r.Is(helper, moduleBase + kBoneVtable) ||
            !r.Get(helper, 0x10, namePointer) || !r.String(namePointer, name)) {
            return Status::unsupportedHelper;
        }
        if (!EqualName(candidate.helper, name)) { continue; }
        std::uintptr_t helperManager = 0;
        int helperJoint = -1;
        if (!r.Get(helper, 0x28, helperManager) || helperManager != weaponManager ||
            !NoSimulation(r, helper) || !r.Get(helper, 0x15C, helperJoint) ||
            helperJoint < 0 || static_cast<unsigned>(helperJoint) >= weaponCount) {
            return Status::unsupportedHelper;
        }
        Quaternion authored{}, helperExtra{};
        if (!r.Rotation(helper, 0x114, authored) || !r.Rotation(helper, 0x14C, helperExtra)) {
            return Status::invalidBasis;
        }
        candidate.barrelInWeapon = Multiply(authored, helperExtra);
        candidate.source = Source::attachmentDefault;
        out = candidate;
        return Status::ready;
    }
    for (unsigned i = 0; i < weaponCount; ++i) {
        std::uintptr_t namePointer = 0;
        char name[128]{};
        if (!r.Get(weaponJoints, i * 0xA8ull, namePointer) || !r.String(namePointer, name)) {
            return Status::missingHelper;
        }
        if (!EqualName(candidate.helper, name)) { continue; }
        std::uintptr_t weaponBind = 0;
        if (!r.Get(weaponSkeleton, 0x30, weaponBind) ||
            !r.PoseRotation(weaponBind, static_cast<int>(i), candidate.barrelInWeapon)) {
            return Status::invalidBasis;
        }
        candidate.source = Source::jointBind;
        out = candidate;
        return Status::ready;
    }
    return Status::missingHelper;
}

bool SolveWrist(const Quaternion& characterWorld, const Quaternion& aimWorld,
                const Basis& basis, Quaternion& wristModel)
{
    if (basis.source == Source::none || !ValidQ(characterWorld) || !ValidQ(aimWorld) ||
        !ValidQ(basis.weaponInWrist) || !ValidQ(basis.barrelInWeapon)) { return false; }
    wristModel = Multiply(Multiply(Multiply(Inverse(characterWorld), aimWorld),
                                   Inverse(basis.barrelInWeapon)), Inverse(basis.weaponInWrist));
    return true;
}

const char* SourceName(Source source)
{
    switch (source) {
    case Source::attachmentDefault: return "attachment_default";
    case Source::jointBind: return "joint_bind";
    default: return "none";
    }
}
const char* StatusName(Status status)
{
    switch (status) {
    case Status::ready: return "basis_ready";
    case Status::applied: return "applied";
    case Status::disabled: return "disabled";
    case Status::noSample: return "no_sample";
    case Status::invalidOwner: return "invalid_owner";
    case Status::unsupportedBinding: return "unsupported_binding";
    case Status::invalidSkeleton: return "invalid_skeleton";
    case Status::invalidMount: return "invalid_mount";
    case Status::simulatedMount: return "simulated_mount";
    case Status::unrelatedSocket: return "unrelated_socket";
    case Status::missingHelper: return "missing_helper";
    case Status::unsupportedHelper: return "unsupported_helper";
    case Status::invalidBasis: return "invalid_basis";
    case Status::staleAim: return "stale_aim";
    case Status::writeFailed: return "write_failed";
    }
    return "unknown";
}
} // namespace preyvr::weaponrig
