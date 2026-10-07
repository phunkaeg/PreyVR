#include "CarryLane.h"

#include "AimTakeover.h"
#include "Haptics.h"
#include "InteractionLane.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "XrInput.h"
#include "preyvr/AnimIk.h"
#include "preyvr/EngineMap.h"
#include "preyvr/HandCarry.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/StereoCamera.h"

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace preyvr::dll {
namespace {

using carry::Add;
using carry::Finite;
using carry::Length;
using carry::Scale;
using carry::Sub;

void Log(const std::string& line) { lifecycle::Log("preyvr_carry " + line); }

// --- native contracts (Steam PreyDll.dll, SHA-256 7d6e322f...) ---------------
//
// ArkPlayerCarry, layout from the PDB-generated Chairloader header (EGS build)
// and every offset below confirmed in the Steam decompiles of Update/Stop/Start
// (docs/evidence/physical-grab-2026-10-04): m_grabber +0x3C (m_bConstrained
// +0x60), m_throwKickBacks +0x80, m_pickedUpEntityId +0xB0, constraint +0xB4,
// m_dragCorpse* +0xD8.., m_attachedEmitterSlot +0xF4, m_bThrowCarriedEntity +0xFC.
constexpr std::uintptr_t kUpdateRva = 0x125E400;   // bool Update(float frameTime)
constexpr std::array<std::uint8_t, 16> kUpdatePrologue{
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24};
constexpr std::uintptr_t kTargetRva = 0x125A750;   // QuatT* GetLerpTargetLocation(QuatT*, IEntity*, Quat*)
constexpr std::array<std::uint8_t, 18> kTargetPrologue{
    0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0x90};
constexpr std::uintptr_t kCorpseRva = 0x125A670;   // Vec3* GetDragCorpseConstraintPos(Vec3*)
constexpr std::array<std::uint8_t, 15> kCorpsePrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40};
constexpr std::uintptr_t kStopRva = 0x125CF70;     // void StopCarrying(float, bool angular, bool thrown, bool serialize)
constexpr std::array<std::uint8_t, 16> kStopPrologue{
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0xF3, 0x0F, 0x11, 0x48, 0x10, 0x55, 0x56, 0x57, 0x41};
constexpr std::uintptr_t kStartRva = 0x125C3D0;    // bool StartCarrying(IEntity*, bool remote, bool serialize)
constexpr std::array<std::uint8_t, 14> kStartPrologue{
    0x44, 0x88, 0x4C, 0x24, 0x20, 0x44, 0x88, 0x44, 0x24, 0x18, 0x55, 0x56, 0x41, 0x56};
// DropCarriedEntity: called, not hooked. Its guards (no collision-restore
// timer running, not just thrown, no leverage trail) are why it is used rather
// than calling StopCarrying directly.
constexpr std::uintptr_t kDropRva = 0x125A250;
constexpr std::array<std::uint8_t, 13> kDropPrologue{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x83, 0xB9, 0xB0, 0x00, 0x00, 0x00, 0x00};
// gEnv->pPhysicalWorld; its GetPhysVars (+0x78) holds the mass clamp the
// release's impulse producer (0x1231940) scales by: +0x1C8 min, +0x1CC max,
// +0x1D8 the articulated multiplier.
constexpr std::uintptr_t kPhysicalWorldRva = 0x224D9C8;
constexpr std::size_t kGetPhysVarsSlot = 0x78;
constexpr std::uintptr_t kMinMass = 0x1C8, kMaxMass = 0x1CC, kArticulatedScale = 0x1D8;
constexpr std::uintptr_t kGetPlayerRva = 0x157C990;
constexpr std::uintptr_t kEntitySystemRva = 0x224DA38;   // gEnv->pEntitySystem
// The physics queue's per-type struct sizes and payload descriptors (written at
// start-up by 0xD0D1D0): type 2 (impulse) is 0x38 at +2*4, which is what
// PhysicalInteractions verifies; type 10 (pe_action_set_velocity) must be 40
// bytes with no pointed-to payload, or the queued copy would be wrong.
constexpr std::uintptr_t kActionSizeTableRva = 0x2982CA0;
constexpr std::uintptr_t kActionPayloadTableRva = 0x2982DE0;

constexpr std::uintptr_t kCarryInInteraction = 0x28;
constexpr std::uintptr_t kGrabberConstrained = 0x60;
constexpr std::uintptr_t kKickBacks = 0x80;
constexpr std::uintptr_t kPickedUp = 0xB0;
constexpr std::uintptr_t kConstraint = 0xB4;
constexpr std::uintptr_t kDragHeight = 0xD8, kDragDistance = 0xDC, kDragBreakSq = 0xE0;
constexpr std::uintptr_t kJustThrown = 0xFF;

constexpr std::size_t kGetIdSlot = 0x08, kGetClassSlot = 0x18, kGetArchetypeSlot = 0x20, kGetNameSlot = 0x78;
constexpr std::size_t kWorldTmSlot = 0xE8, kWorldBoundsSlot = 0xF8, kLocalBoundsSlot = 0x100, kPhysicsSlot = 0x228;
constexpr std::size_t kNameSlot = 0x10;   // IEntityClass / IEntityArchetype GetName
constexpr std::size_t kPhysTypeSlot = 0x08, kPhysStatusSlot = 0x30, kPhysActionSlot = 0x38;
constexpr std::size_t kGetEntitySlot = 0x70;

using UpdateFn = std::uint64_t(__fastcall*)(std::uintptr_t carry, float frameTime);
using TargetFn = float*(__fastcall*)(std::uintptr_t carry, float* out, std::uintptr_t entity, const float* original);
using CorpseFn = float*(__fastcall*)(std::uintptr_t carry, float* out);
using StopFn = void(__fastcall*)(std::uintptr_t carry, float impulse, std::uint8_t angular, std::uint8_t thrown,
                                 std::uint8_t serialize);
using StartFn = bool(__fastcall*)(std::uintptr_t carry, std::uintptr_t entity, std::uint8_t remote, std::uint8_t serialize);
using DropFn = void(__fastcall*)(std::uintptr_t carry);

std::uintptr_t gBase = 0;
std::atomic<UpdateFn> gUpdate{nullptr};
std::atomic<TargetFn> gTarget{nullptr};
std::atomic<CorpseFn> gCorpse{nullptr};
std::atomic<StopFn> gStop{nullptr};
std::atomic<StartFn> gStart{nullptr};
std::mutex gInstallMutex;
bool gInstallTried = false, gInstalled = false, gVelocityQueueOk = false;

// Off until VR mode switches it on with the hooks installed.
std::atomic<bool> gHand{false}, gHoldToHold{true}, gTrace{false};
std::atomic<bool> gHolding{false};

// Tunables (carry.throw / carry.hold*), read on the game thread.
std::mutex gSettingsMutex;
carry::ThrowSettings gThrow{};
carry::HoldSettings gHoldSettings{};
carry::CorpseSettings gCorpseSettings{};
carry::KindSettings gKindSettings{};
float gHeavyTau = 0.10f, gCorpseTau = 0.12f, gBlendSeconds = 0.35f;
template <class T> T Get(const T& v)
{
    std::lock_guard lock(gSettingsMutex);
    return v;
}

// --- the hands' motion (XR thread writes, the release reads) -------------------
std::mutex gMotionMutex;
carry::MotionHistory gMotion[2];
std::uint64_t gMotionGeneration = 0;
std::atomic<unsigned long long> gMotionSamples{0}, gRuntimeVelocitySamples{0};

// --- the release request (input lane -> game thread) ----------------------------
struct Pending {
    bool active = false;
    std::uint64_t ns = 0;
    carry::Motion motion{};
    float yaw = 0;
    bool yawValid = false;
    unsigned attempts = 0;
};
std::mutex gPendingMutex;
Pending gPending;

// --- what the hand holds (game thread) --------------------------------------------
struct Grab {
    bool active = false;
    std::uint32_t entity = 0;
    carry::Kind kind = carry::Kind::Native;
    char label[64]{};
    char cls[32]{};
    carry::Box box{};
    float mass = 0, diagonal = 0, hold = -1;
    bool articulated = false;
    Quaternion handToObject{};
    // Light props: held by the point the pointing ray met them at (carry.grabpoint 1).
    bool haveGrabPoint = false;
    Vec3 grabLocal{};
    carry::HeavyGrab heavy{};
    carry::Follow follow{};
    std::uint64_t startNs = 0, lastTargetNs = 0;
    // Live, for the report.
    bool nativeValid = false;
    Vec3 native{};
    bool written = false;
    Vec3 target{};
    Quaternion targetRotation{};
    Vec3 wantCentre{};    // where the object's centre should be
    Vec3 grip{};
    float centreError = -1, gripDistance = -1, rotationError = -1;
    unsigned long long targets = 0, writes = 0, corpsePoints = 0;
};
Grab gGrab;
thread_local std::uintptr_t tInUpdate = 0;

// The release in flight between the Update hook's Drop and the Stop hook.
struct StopOverride {
    bool active = false;
    std::uint32_t entity = 0;
    carry::Release release{};
    bool setAngular = true;
    carry::Kind kind = carry::Kind::Native;
};
StopOverride gOverride;

// A released object, watched for a few frames: did it leave as it was told?
struct Watch {
    std::uint32_t entity = 0;
    unsigned frames = 0;
    Vec3 expected{};
    float firstSpeed = -1, laterSpeed = -1;
    Vec3 firstVelocity{};
    float startZ = 0, minZ = 0;
    bool restored = false;    // the native collision restore has cleared the carry
    unsigned restoredFrame = 0;
};
Watch gWatch;
// carry.impulse: what the release does with the game's own impulse (StopHook).
// 0 (default): kept, with a zero speed, and compensated; 1: kept, not
// compensated; 2: skipped through _bFromSerialize -- the object then falls
// through the floor (measured 2026-10-07, 2 of 2 and 3 of 3; with it kept,
// 0 of 5), so 1 and 2 are for A/B only.
std::atomic<int> gImpulseMode{0};
std::atomic<float> gLastScale{1.0f};
// carry.grabpoint 1 (default): a light prop is held by the point the pointing
// ray met it at (carry::LightHoldAt); 0: its centre in the palm, as before.
std::atomic<bool> gGrabPoint{true};

// Report snapshot.
struct Last {
    Grab grab{};
    bool releaseValid = false;
    carry::Release release{};
    carry::Motion motion{};
    Vec3 handWorld{}, spinWorld{}, player{};
    char how[24]{};
    std::uint32_t releasedEntity = 0;
    Watch watch{};
    char stop[160]{};
};
std::mutex gLastMutex;
Last gLast;

std::atomic<unsigned long long> gUpdates{0}, gStarts{0}, gStops{0}, gReleasesAsked{0}, gReleasesDone{0},
    gReleasesRefused{0}, gThrows{0}, gDrops{0}, gNativeThrows{0}, gVelocitySets{0}, gVelocityFaults{0}, gFaults{0},
    gStale{0};

// --- SEH helpers (POD only) --------------------------------------------------------

bool ReadBytes(void* destination, std::uintptr_t source, std::size_t size)
{
    __try {
        std::memcpy(destination, reinterpret_cast<const void*>(source), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <class T> bool Read(std::uintptr_t at, T& out) { return ReadBytes(&out, at, sizeof(T)); }
bool WriteBytes(std::uintptr_t destination, const void* source, std::size_t size)
{
    __try {
        std::memcpy(reinterpret_cast<void*>(destination), source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <class R, class... A> R VCall(std::uintptr_t object, std::size_t slot, A... args)
{
    const auto vtable = *reinterpret_cast<const std::uintptr_t*>(object);
    return (*reinterpret_cast<R(__fastcall* const*)(std::uintptr_t, A...)>(vtable + slot))(object, args...);
}

std::uintptr_t CallGetPlayer()
{
    __try {
        return reinterpret_cast<std::uintptr_t(__fastcall*)()>(gBase + kGetPlayerRva)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
std::uintptr_t CallGetEntity(std::uint32_t id)
{
    __try {
        const auto system = *reinterpret_cast<const std::uintptr_t*>(gBase + kEntitySystemRva);
        if (!system || !id) { return 0; }
        return VCall<std::uintptr_t>(system, kGetEntitySlot, id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
std::uint32_t CallEntityId(std::uintptr_t entity)
{
    __try {
        return entity ? VCall<std::uint32_t>(entity, kGetIdSlot) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
std::uintptr_t CallPhysics(std::uintptr_t entity)
{
    __try {
        return entity ? VCall<std::uintptr_t>(entity, kPhysicsSlot) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
int CallPhysicsType(std::uintptr_t physics)
{
    __try {
        return physics ? VCall<int>(physics, kPhysTypeSlot) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
bool CallWorldTm(std::uintptr_t entity, float* m12)
{
    __try {
        const float* m = VCall<const float*>(entity, kWorldTmSlot);
        if (!m) { return false; }
        std::memcpy(m12, m, 12 * sizeof(float));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CallBounds(std::uintptr_t entity, std::size_t slot, float* box6)
{
    __try {
        VCall<void>(entity, slot, box6);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
const char* CallName(std::uintptr_t entity, std::size_t slot)
{
    __try {
        const auto object = VCall<std::uintptr_t>(entity, slot);
        return object ? VCall<const char*>(object, kNameSlot) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
const char* CallEntityName(std::uintptr_t entity)
{
    __try {
        return VCall<const char*>(entity, kGetNameSlot);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
bool CopyName(const char* source, char* out, std::size_t size)
{
    __try {
        if (!source) { return false; }
        std::size_t n = 0;
        for (; n + 1 < size && source[n]; ++n) {
            const unsigned char c = static_cast<unsigned char>(source[n]);
            if (c < 32 || c > 126) { return false; }
            out[n] = static_cast<char>(c);
        }
        out[n] = 0;
        return n > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// pe_status_dynamics (Prey's physinterface.h): type 8; partid/ipart unused;
// v +0x0C, w +0x18, mass +0x4C; 92 bytes.
struct Dynamics {
    Vec3 v{}, w{};
    float mass = 0;
};
bool CallDynamics(std::uintptr_t physics, Dynamics& out)
{
    alignas(16) std::uint8_t status[128]{};
    const int type = 8, unused = INT_MIN;
    std::memcpy(status, &type, 4);
    std::memcpy(status + 4, &unused, 4);
    std::memcpy(status + 8, &unused, 4);
    __try {
        if (!physics || !VCall<int>(physics, kPhysStatusSlot, static_cast<void*>(status))) { return false; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    std::memcpy(&out.v, status + 0x0C, sizeof(Vec3));
    std::memcpy(&out.w, status + 0x18, sizeof(Vec3));
    std::memcpy(&out.mass, status + 0x4C, sizeof(float));
    return Finite(out.v) && Finite(out.w) && std::isfinite(out.mass);
}

// pe_action_set_velocity: type 10, ipart/partid unused (the whole entity),
// v, w (x = the engine's "unused" NaN keeps the current spin),
// bRotationAroundPivot 0 (about the centre of mass).
struct SetVelocityAction {
    int type = 10;
    int ipart = INT_MIN;
    int partid = INT_MIN;
    Vec3 v{}, w{};
    int aroundPivot = 0;
};
static_assert(sizeof(SetVelocityAction) == 40, "pe_action_set_velocity is 40 bytes in Prey");
bool CallSetVelocity(std::uintptr_t physics, const SetVelocityAction& action)
{
    __try {
        // bThreadSafe = 1, as the native release's own impulse: both queue (or
        // both apply now) in order, so this one lands after it.
        return physics && VCall<int>(physics, kPhysActionSlot, static_cast<const void*>(&action), 1) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// The velocity change the native release's impulse gives an object, per m/s
// of the vector it is handed: the producer scales by the mass CLAMPED to the
// physics vars' range (a 1 kg towel is pushed as if it were heavier), then the
// body divides by its real mass.
float NativeImpulseScale(std::uintptr_t physics, float mass)
{
    if (!(mass > 0)) { return 1.0f; }
    float minMass = 0, maxMass = 0, articulated = 1;
    __try {
        const auto world = *reinterpret_cast<const std::uintptr_t*>(gBase + kPhysicalWorldRva);
        const auto vars = world ? VCall<std::uintptr_t>(world, kGetPhysVarsSlot) : 0;
        if (!vars) { return 1.0f; }
        minMass = *reinterpret_cast<const float*>(vars + kMinMass);
        maxMass = *reinterpret_cast<const float*>(vars + kMaxMass);
        articulated = *reinterpret_cast<const float*>(vars + kArticulatedScale);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1.0f;
    }
    float scaled = mass;
    if (minMass > 0 && scaled < minMass) { scaled = minMass; }
    if (maxMass > 0 && maxMass <= scaled) { scaled = maxMass; }
    if (CallPhysicsType(physics) == 6 && std::isfinite(articulated) && articulated > 0) { scaled *= articulated; }
    const float k = scaled / mass;
    return std::isfinite(k) ? k : 1.0f;
}

void CallDrop(std::uintptr_t carry)
{
    __try {
        reinterpret_cast<DropFn>(gBase + kDropRva)(carry);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gFaults.fetch_add(1);
    }
}

std::string Fixed(float v, int decimals = 3)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, static_cast<double>(v));
    return text;
}
std::string Point(Vec3 v) { return Fixed(v.x) + "," + Fixed(v.y) + "," + Fixed(v.z); }

std::uintptr_t PlayerCarry(std::uintptr_t player)
{
    return player ? player + engine::ArkPlayerLayout::interaction + kCarryInInteraction : 0;
}
bool IsPlayerCarry(std::uintptr_t carry)
{
    return carry && carry == PlayerCarry(CallGetPlayer());
}

std::uintptr_t PlayerEntity(std::uintptr_t player)
{
    std::uintptr_t entity = 0;
    return player && Read(player + 0x38, entity) ? entity : 0;
}
Vec3 PlayerVelocity()
{
    Dynamics d{};
    const auto physics = CallPhysics(PlayerEntity(CallGetPlayer()));
    return physics && CallDynamics(physics, d) ? d.v : Vec3{};
}
bool PlayerFeet(Vec3& out)
{
    float m[12];
    const auto entity = PlayerEntity(CallGetPlayer());
    if (!entity || !CallWorldTm(entity, m)) { return false; }
    out = {m[3], m[7], m[11]};
    return Finite(out);
}

// --- the hand, in the world -----------------------------------------------------------

struct HandNow {
    bool valid = false;
    Pose grip{};          // engine world
    Vec3 palm{};          // out of the palm (left hand: grip +X)
    Vec3 forward{};       // the controller's pointing axis (grip -Z)
    Vec3 aimDirection{};  // the aim pose's ray
    Vec3 head{};          // camera centre
    float yaw = 0;
    std::uint64_t reference = 0;
};
HandNow LeftHandNow(const GameplayPoseFrame& f)
{
    HandNow h{};
    if (!f.cameraCentreValid || !f.headYawUsable || (f.referenceGeneration & 1) || !std::isfinite(f.yaw)) { return h; }
    const auto& left = f.tracking.hands[static_cast<unsigned>(Hand::left)];
    if (!IsPoseUsable(left.gripPose, left.gripValidity, 200000000ull)) { return h; }
    h.grip = animik::ControllerWorldFromHead(f.yaw, f.cameraCentre, f.tracking.head, left.gripPose);
    if (!Finite(h.grip.position) || !Finite(h.grip.orientation)) { return h; }
    h.palm = Rotate(h.grip.orientation, stereo::ToEngineSpace(Vec3{1, 0, 0}));
    h.forward = Rotate(h.grip.orientation, stereo::ToEngineSpace(Vec3{0, 0, -1}));
    if (IsPoseUsable(left.aimPose, left.aimValidity, 200000000ull)) {
        const Pose aim = animik::ControllerWorldFromHead(f.yaw, f.cameraCentre, f.tracking.head, left.aimPose);
        h.aimDirection = Rotate(aim.orientation, stereo::ToEngineSpace(Vec3{0, 0, -1}));
    } else {
        h.aimDirection = h.forward;
    }
    h.head = f.cameraCentre;
    h.yaw = f.yaw;
    h.reference = f.referenceGeneration;
    h.valid = true;
    return h;
}
HandNow LeftHandLatest()
{
    GameplayPoseFrame f{};
    if (!TryGetGameplayPoseFrame(f, true) || !FreshSample(MonotonicNanoseconds(), f.publishedNs, 150000000ull)) {
        return {};
    }
    return LeftHandNow(f);
}

// --- grab ---------------------------------------------------------------------------------

void Remember(const Grab& g)
{
    std::lock_guard lock(gLastMutex);
    gLast.grab = g;
}

void CaptureGrab(std::uintptr_t entity)
{
    Grab g{};
    g.entity = CallEntityId(entity);
    if (!g.entity) { return; }
    if (!CopyName(CallName(entity, kGetArchetypeSlot), g.label, sizeof(g.label)) &&
        !CopyName(CallEntityName(entity), g.label, sizeof(g.label))) {
        std::snprintf(g.label, sizeof(g.label), "?");
    }
    if (!CopyName(CallName(entity, kGetClassSlot), g.cls, sizeof(g.cls))) { std::snprintf(g.cls, sizeof(g.cls), "?"); }
    const auto physics = CallPhysics(entity);
    g.articulated = CallPhysicsType(physics) == 6;   // PE_ARTICULATED: a ragdoll
    Dynamics d{};
    if (CallDynamics(physics, d)) { g.mass = d.mass; }
    float box[6]{};
    if (CallBounds(entity, kLocalBoundsSlot, box)) {
        g.box.min = {box[0], box[1], box[2]};
        g.box.max = {box[3], box[4], box[5]};
        if (!Finite(g.box.min) || !Finite(g.box.max)) { g.box = {}; }
    }
    g.diagonal = Length(Sub(g.box.max, g.box.min));
    g.hold = UseCarryHoldSeconds(g.entity);
    const HandNow hand = LeftHandLatest();
    float tm[12];
    const bool haveTm = CallWorldTm(entity, tm);
    carry::ClassifyInput in{};
    in.articulated = g.articulated;
    // Turrets carry "safe": the game keeps them upright and sets them down
    // standing; its own placement is kept for them.
    in.safeCarry = std::strcmp(g.cls, "ArkTurret") == 0;
    in.handValid = hand.valid && haveTm && gHand.load();
    in.mass = g.mass;
    in.diagonal = g.diagonal;
    in.hold = g.hold;
    g.kind = carry::Classify(in, Get(gKindSettings));
    if (haveTm && hand.valid) {
        const Quaternion object = carry::FromMatrix34(tm);
        g.handToObject = Normalize(Multiply(carry::Inverse(hand.grip.orientation), object));
        g.heavy.handOffset = Sub(hand.grip.position, hand.head);
        g.heavy.yaw = hand.yaw;
        // The point the left hand's ray met it at, if this carry is what it pointed at.
        UsePointer pointer{};
        if (TryGetUsePointer(pointer) && pointer.valid && pointer.entity == g.entity &&
            MonotonicNanoseconds() - pointer.ns < 500000000ull &&
            carry::GrabPointLocal(tm, g.box, pointer.hand, pointer.direction, g.grabLocal)) {
            g.haveGrabPoint = true;
        }
    }
    g.active = true;
    g.startNs = MonotonicNanoseconds();
    gGrab = g;
    gHolding.store(gHand.load() && g.kind != carry::Kind::Native);
    Remember(g);
    gStarts.fetch_add(1);
    Log("start entity=" + std::to_string(g.entity) + " name=" + g.label + " class=" + g.cls + " kind=" +
        carry::KindName(g.kind) + " mass=" + Fixed(g.mass, 2) + " diag=" + Fixed(g.diagonal, 2) + " hold=" +
        Fixed(g.hold, 2) + " articulated=" + std::to_string(g.articulated) + " boxMin=" + Point(g.box.min) +
        " boxMax=" + Point(g.box.max) + " hand=" + std::to_string(hand.valid));
}

void ClearGrab(const char* why)
{
    if (gGrab.active && gTrace.load()) { Log(std::string("grab end why=") + why + " entity=" + std::to_string(gGrab.entity)); }
    gGrab.active = false;
    Remember(gGrab);
    gGrab = {};
    gHolding.store(false);
}

// --- the release ------------------------------------------------------------------------

Pending TakePending()
{
    std::lock_guard lock(gPendingMutex);
    Pending p = gPending;
    gPending = {};
    return p;
}
void Requeue(Pending p)
{
    std::lock_guard lock(gPendingMutex);
    if (!gPending.active) {
        ++p.attempts;
        gPending = p;
    }
}

void ExecuteRelease(std::uintptr_t carry, const Pending& p, std::uint32_t id)
{
    const auto yaw = p.yawValid ? p.yaw : 0.0f;
    const Vec3 hand = p.motion.valid && p.yawValid ? carry::TrackingToWorld(p.motion.velocity, yaw) : Vec3{};
    const Vec3 spin = p.motion.valid && p.yawValid ? carry::TrackingToWorld(p.motion.angular, yaw) : Vec3{};
    const Vec3 player = PlayerVelocity();
    const carry::Release r = carry::ComputeRelease(hand, spin, player, gGrab.kind, Get(gThrow));
    StopOverride o{};
    o.active = true;
    o.entity = id;
    o.release = r;
    o.kind = gGrab.kind;
    // A body is let go as the game drops one unless it is flung; then it
    // flies as one piece with the hand's velocity, without spin.
    if (gGrab.kind == carry::Kind::Corpse) {
        o.active = r.thrown;
        o.setAngular = false;
    }
    gOverride = o;
    {
        std::lock_guard lock(gLastMutex);
        gLast.releaseValid = true;
        gLast.release = r;
        gLast.motion = p.motion;
        gLast.handWorld = hand;
        gLast.spinWorld = spin;
        gLast.player = player;
        std::snprintf(gLast.how, sizeof(gLast.how), "%s", r.thrown ? "throw" : "drop");
        gLast.releasedEntity = id;
    }
    Log(std::string("release entity=") + std::to_string(id) + " kind=" + carry::KindName(gGrab.kind) +
        " thrown=" + std::to_string(r.thrown) + " handSpeed=" + Fixed(r.handSpeed, 2) + " hand=" + Point(hand) +
        " spin=" + Point(spin) + " player=" + Point(player) + " velocity=" + Point(r.velocity) + " angular=" +
        Point(r.angular) + " capped=" + std::to_string(r.capped) + " motion=" + std::to_string(p.motion.valid) +
        " runtime=" + std::to_string(p.motion.fromRuntime) + " samples=" + std::to_string(p.motion.samples) +
        " ageMs=" + std::to_string((MonotonicNanoseconds() - p.ns) / 1000000ull) + " attempt=" +
        std::to_string(p.attempts));
    CallDrop(carry);
}

void ApplyVelocity(std::uintptr_t physics, Vec3 v, const Vec3* w, std::uint32_t entity)
{
    SetVelocityAction a{};
    a.v = v;
    if (w) {
        a.w = *w;
    } else {
        const std::uint32_t unused = 0xFFBFFFFFu;
        std::memcpy(&a.w.x, &unused, 4);
    }
    if (gVelocityQueueOk && Finite(v) && CallSetVelocity(physics, a)) {
        gVelocitySets.fetch_add(1);
        gWatch = {};
        gWatch.entity = entity;
        gWatch.expected = v;
    } else {
        gVelocityFaults.fetch_add(1);
    }
}

// --- hooks -----------------------------------------------------------------------------------

std::uint64_t __fastcall UpdateHook(std::uintptr_t carry, float frameTime)
{
    const auto original = gUpdate.load(std::memory_order_acquire);
    if (!IsPlayerCarry(carry)) { return original(carry, frameTime); }
    gUpdates.fetch_add(1, std::memory_order_relaxed);
    std::uint32_t id = 0;
    Read(carry + kPickedUp, id);
    if (gGrab.active && gGrab.entity != id) {
        gStale.fetch_add(1);
        ClearGrab("entity changed");
    }
    Pending p = TakePending();
    if (p.active) {
        if (!id || !gGrab.active || gGrab.entity != id) {
            // Nothing (or something else) carried: nothing to let go.
        } else if (MonotonicNanoseconds() - p.ns > 500000000ull) {
            gReleasesRefused.fetch_add(1);
            Log("release refused why=stale");
        } else {
            ExecuteRelease(carry, p, id);
            std::uint32_t after = 0;
            Read(carry + kPickedUp, after);
            if (after == id) {
                // DropCarriedEntity refused (a collision-restore timer, a throw
                // still settling): try again next frame, for a while.
                gOverride = {};
                if (p.attempts < 30) { Requeue(p); }
                else {
                    gReleasesRefused.fetch_add(1);
                    Log("release refused why=drop_guard");
                }
            }
        }
    }
    tInUpdate = carry;
    const auto result = original(carry, frameTime);
    tInUpdate = 0;
    return result;
}

float* __fastcall TargetHook(std::uintptr_t carry, float* out, std::uintptr_t entity, const float* rotation)
{
    float* const result = gTarget.load(std::memory_order_acquire)(carry, out, entity, rotation);
    if (!out || tInUpdate != carry || !gGrab.active) { return result; }
    if (CallEntityId(entity) != gGrab.entity) { return result; }
    Grab& g = gGrab;
    ++g.targets;
    g.nativeValid = true;
    g.native = {out[4], out[5], out[6]};
    if (!gHand.load() || (g.kind != carry::Kind::Light && g.kind != carry::Kind::Heavy)) { return result; }
    const HandNow hand = LeftHandLatest();
    if (!hand.valid) {
        g.written = false;   // tracking lost: the game's own target until it returns
        return result;
    }
    const auto now = MonotonicNanoseconds();
    carry::EntityPose pose{};
    const carry::HoldSettings hold = Get(gHoldSettings);
    if (g.kind == carry::Kind::Light) {
        pose = g.haveGrabPoint && gGrabPoint.load() ? carry::LightHoldAt(hand.grip, g.handToObject, g.grabLocal)
                                                    : carry::LightHold(hand.grip, hand.palm, g.handToObject, g.box, hold);
    } else {
        // The game's own place for it, moved by the hand; eased in from zero
        // and smoothed a little on top of the grabber's lag: weight, not jitter.
        const Vec3 shift = carry::HeavyShift(hand.grip.position, hand.head, hand.yaw, g.heavy, hold);
        const float blend = std::clamp(static_cast<float>(now - g.startNs) * 1e-9f / Get(gBlendSeconds), 0.0f, 1.0f);
        const float dt = g.lastTargetNs ? static_cast<float>(now - g.lastTargetNs) * 1e-9f : 0.0f;
        if (!g.follow.Primed()) { g.follow.Reset({}); }
        const Vec3 eased = g.follow.Update(Scale(shift, blend), std::min(dt, 0.1f), Get(gHeavyTau));
        pose.position = Add(g.native, eased);
        pose.rotation = Quaternion{out[0], out[1], out[2], out[3]};
    }
    g.lastTargetNs = now;
    if (!Finite(pose.position) || !Finite(pose.rotation)) {
        gFaults.fetch_add(1);
        return result;
    }
    const Quaternion q = Normalize(pose.rotation);
    out[0] = q.x;
    out[1] = q.y;
    out[2] = q.z;
    out[3] = q.w;
    out[4] = pose.position.x;
    out[5] = pose.position.y;
    out[6] = pose.position.z;
    g.written = true;
    ++g.writes;
    g.target = pose.position;
    g.targetRotation = q;
    g.wantCentre = Add(pose.position, Rotate(q, carry::Centre(g.box)));
    g.grip = hand.grip.position;
    return result;
}

float* __fastcall CorpseHook(std::uintptr_t carry, float* out)
{
    float* const result = gCorpse.load(std::memory_order_acquire)(carry, out);
    // StartCarrying's own call (the constraint's first point) stays native.
    if (!out || tInUpdate != carry || !gGrab.active || gGrab.kind != carry::Kind::Corpse || !gHand.load()) {
        return result;
    }
    Grab& g = gGrab;
    const Vec3 native{out[0], out[1], out[2]};
    g.nativeValid = true;
    g.native = native;
    const HandNow hand = LeftHandLatest();
    Vec3 feet{};
    if (!hand.valid || !PlayerFeet(feet) || !Finite(native)) {
        g.written = false;
        return result;
    }
    const auto now = MonotonicNanoseconds();
    const Vec3 want = carry::CorpsePoint(hand.grip.position, feet, native, Get(gCorpseSettings));
    const float blend = std::clamp(static_cast<float>(now - g.startNs) * 1e-9f / Get(gBlendSeconds), 0.0f, 1.0f);
    const Vec3 aim = Add(native, Scale(Sub(want, native), blend));
    const float dt = g.lastTargetNs ? static_cast<float>(now - g.lastTargetNs) * 1e-9f : 0.0f;
    if (!g.follow.Primed()) { g.follow.Reset(native); }
    const Vec3 p = g.follow.Update(aim, std::min(dt, 0.1f), Get(gCorpseTau));
    g.lastTargetNs = now;
    if (!Finite(p)) { return result; }
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
    g.written = true;
    ++g.writes;
    ++g.corpsePoints;
    g.target = p;
    g.wantCentre = p;
    g.grip = hand.grip.position;
    return result;
}

void __fastcall StopHook(std::uintptr_t carry, float impulse, std::uint8_t angular, std::uint8_t thrown,
                         std::uint8_t serialize)
{
    const auto original = gStop.load(std::memory_order_acquire);
    if (!IsPlayerCarry(carry) || serialize) {
        original(carry, impulse, angular, thrown, serialize);
        if (IsPlayerCarry(carry)) { ClearGrab("serialize"); }
        return;
    }
    gStops.fetch_add(1);
    std::uint32_t id = 0;
    Read(carry + kPickedUp, id);
    const auto entity = CallGetEntity(id);
    const auto physics = CallPhysics(entity);
    StopOverride o = gOverride;
    gOverride = {};
    if (o.active && o.entity != id) { o = {}; }
    const float nativeImpulse = impulse;
    const bool nativeThrown = thrown != 0;
    if (o.active) {
        // The game's release adds its own impulse: the view direction times
        // the speed it is given plus the player's velocity, scaled by a CLAMPED
        // mass (a 1 kg towel gets ~4x) and applied after the next physics step,
        // so on top of any velocity set now (measured 2026-10-07: asked 7.0 m/s
        // with a speed of 7.0 handed over, flew 27.6). It is handed a zero
        // speed; what is left of it is compensated where the velocity is set.
        // `thrown` still marks the object thrown (its impact state) or dropped.
        thrown = o.release.thrown ? 1 : 0;
        impulse = 0;
        angular = 0;
        if (gImpulseMode.load() == 2) { serialize = 1; }
    }
    original(carry, impulse, angular, thrown, serialize);
    char line[160];
    std::snprintf(line, sizeof(line), "entity=%u impulse=%.2f angular=%u thrown=%u ours=%d sentThrown=%u",
                  id, static_cast<double>(nativeImpulse), static_cast<unsigned>(angular), nativeThrown ? 1u : 0u,
                  o.active ? 1 : 0, static_cast<unsigned>(thrown));
    Log(std::string("stop ") + line);
    {
        std::lock_guard lock(gLastMutex);
        std::snprintf(gLast.stop, sizeof(gLast.stop), "%s", line);
    }
    if (physics) {
        if (o.active) {
            // The game's impulse (kept: without it the object falls through
            // the floor until its collision is restored) was handed a zero
            // speed, so it adds only the player's velocity times the mass
            // clamp, after the next physics step. Set that much less now and
            // it lands on exactly the release velocity (a standing player:
            // exact; a moving one: one physics step of that difference).
            Vec3 v = o.release.velocity;
            if (gImpulseMode.load() == 0) {
                Dynamics d{};
                CallDynamics(physics, d);
                const float k = NativeImpulseScale(physics, d.mass);
                v = Sub(v, Scale(PlayerVelocity(), k));
                gLastScale.store(k);
            }
            ApplyVelocity(physics, v, o.setAngular ? &o.release.angular : nullptr, id);
            gReleasesDone.fetch_add(1);
            (o.release.thrown ? gThrows : gDrops).fetch_add(1);
            GameplayPoseFrame f{};
            if (o.release.thrown && TryGetGameplayPoseFrame(f, true)) {
                QueueHaptic(Hand::left, haptics::Event::ObjectThrown, f.tracking);
            }
        } else if (nativeThrown) {
            // The trigger: the game's own throw, along the right hand's aim
            // (the cached reticle ray the aim lane writes), at its own speed.
            gNativeThrows.fetch_add(1);
        }
    }
    ClearGrab("stop");
}

bool __fastcall StartHook(std::uintptr_t carry, std::uintptr_t entity, std::uint8_t remote, std::uint8_t serialize)
{
    const bool ok = gStart.load(std::memory_order_acquire)(carry, entity, remote, serialize);
    if (ok && !remote && !serialize && entity && IsPlayerCarry(carry)) {
        CaptureGrab(entity);
        GameplayPoseFrame f{};
        if (gHolding.load() && TryGetGameplayPoseFrame(f, true)) {
            QueueHaptic(Hand::left, haptics::Event::ObjectGrabbed, f.tracking);
        }
    }
    return ok;
}

template <class Fn, std::size_t N>
bool HookAt(std::uintptr_t rva, const std::array<std::uint8_t, N>& prologue, void* detour, std::atomic<Fn>& original,
            const char* name)
{
    auto* const target = reinterpret_cast<void*>(gBase + rva);
    if (std::memcmp(target, prologue.data(), prologue.size()) != 0) {
        Log(std::string("result=unavailable detail=prologue_mismatch target=") + name);
        return false;
    }
    Fn trampoline = nullptr;
    if (MH_CreateHook(target, detour, reinterpret_cast<void**>(&trampoline)) != MH_OK) {
        Log(std::string("result=failed detail=create_hook target=") + name);
        return false;
    }
    original.store(trampoline, std::memory_order_release);
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        Log(std::string("result=failed detail=enable_hook target=") + name);
        return false;
    }
    return true;
}

std::string GrabLine(const Grab& g)
{
    std::string out = " grab=" + std::to_string(g.active) + " entity=" + std::to_string(g.entity) + " name=" + g.label +
                      " class=" + g.cls + " kind=" + carry::KindName(g.kind) + " mass=" + Fixed(g.mass, 2) +
                      " diag=" + Fixed(g.diagonal, 2) + " hold=" + Fixed(g.hold, 2) +
                      " articulated=" + std::to_string(g.articulated) + " targets=" + std::to_string(g.targets) +
                      " writes=" + std::to_string(g.writes) + " corpsePoints=" + std::to_string(g.corpsePoints) +
                      " written=" + std::to_string(g.written) + " grabPoint=" + std::to_string(g.haveGrabPoint);
    if (g.haveGrabPoint) { out += " grabLocal=" + Point(g.grabLocal); }
    if (g.nativeValid) { out += " native=" + Point(g.native); }
    if (g.written) {
        out += " target=" + Point(g.target) + " wantCentre=" + Point(g.wantCentre) + " grip=" + Point(g.grip);
    }
    if (g.centreError >= 0) { out += " centreErrorM=" + Fixed(g.centreError) + " gripDistM=" + Fixed(g.gripDistance); }
    if (g.rotationError >= 0) { out += " rotErrDeg=" + Fixed(g.rotationError, 1); }
    return out;
}

std::string CarryFields(std::uintptr_t carry)
{
    if (!carry) { return " carry=none"; }
    float kick[4]{}, grabber[9]{}, dragH = 0, dragD = 0, dragBreak = 0;
    std::uint8_t constrained = 0, justThrown = 0;
    int constraint = 0;
    Read(carry + kKickBacks, kick);
    Read(carry + 0x3C, grabber);
    Read(carry + kGrabberConstrained, constrained);
    Read(carry + kJustThrown, justThrown);
    Read(carry + kConstraint, constraint);
    Read(carry + kDragHeight, dragH);
    Read(carry + kDragDistance, dragD);
    Read(carry + kDragBreakSq, dragBreak);
    std::string out = " kickBacks=" + Fixed(kick[0], 2) + "," + Fixed(kick[1], 2) + "," + Fixed(kick[2], 2) + "," +
                      Fixed(kick[3], 2) + " grabberBreakSq=" + Fixed(grabber[0], 2) + " lerpSpeed=" + Fixed(grabber[4], 2) +
                      " lerpObstructed=" + Fixed(grabber[5], 2) + " lerpAccel=" + Fixed(grabber[6], 2) +
                      " lerpObstructedAccel=" + Fixed(grabber[7], 2) + " maxTimeUnder=" + Fixed(grabber[8], 2) +
                      " constrained=" + std::to_string(constrained) + " justThrown=" + std::to_string(justThrown) +
                      " constraintId=" + std::to_string(constraint) + " dragHeight=" + Fixed(dragH, 2) +
                      " dragDistance=" + Fixed(dragD, 2) + " dragBreakSq=" + Fixed(dragBreak, 2);
    return out;
}

}  // namespace

// --- public ---------------------------------------------------------------------------------

bool EnsureCarryLane()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstallTried) { return gInstalled; }
    gInstallTried = true;
    gBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if (!gBase) {
        gInstallTried = false;
        return false;
    }
    if (std::memcmp(reinterpret_cast<const void*>(gBase + kDropRva), kDropPrologue.data(), kDropPrologue.size()) != 0) {
        Log("result=unavailable detail=prologue_mismatch target=drop");
        return false;
    }
    std::uint32_t size = 0;
    std::uintptr_t payload = 1;
    gVelocityQueueOk = Read(gBase + kActionSizeTableRva + 10 * 4, size) &&
                       Read(gBase + kActionPayloadTableRva + 10 * 8, payload) &&
                       size == sizeof(SetVelocityAction) && payload == 0;
    EnsureMinHook();
    gInstalled = HookAt(kUpdateRva, kUpdatePrologue, reinterpret_cast<void*>(&UpdateHook), gUpdate, "update") &&
                 HookAt(kTargetRva, kTargetPrologue, reinterpret_cast<void*>(&TargetHook), gTarget, "target") &&
                 HookAt(kCorpseRva, kCorpsePrologue, reinterpret_cast<void*>(&CorpseHook), gCorpse, "corpse") &&
                 HookAt(kStopRva, kStopPrologue, reinterpret_cast<void*>(&StopHook), gStop, "stop") &&
                 HookAt(kStartRva, kStartPrologue, reinterpret_cast<void*>(&StartHook), gStart, "start");
    Log(std::string("result=") + (gInstalled ? "0" : "unavailable") + " velocityQueue=" +
        std::to_string(gVelocityQueueOk) + " setVelocitySize=" + std::to_string(size) +
        " payload=" + std::to_string(payload));
    return gInstalled;
}

DWORD SetCarryHand(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    if (enabled && !EnsureCarryLane()) { return ERROR_NOT_SUPPORTED; }
    gHand.store(enabled != 0);
    if (!enabled) { gHolding.store(false); }
    Log("result=0 detail=hand value=" + std::to_string(enabled));
    return 0;
}
unsigned CarryHandEnabled() { return gHand.load() ? 1u : 0u; }
DWORD SetCarryHoldToHold(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gHoldToHold.store(enabled != 0);
    return 0;
}
unsigned CarryHoldToHold() { return gHand.load() && gHoldToHold.load() ? 1u : 0u; }
bool CarryHoldingInHand() { return gHolding.load(); }

void RecordCarryMotion(const TrackingFrame& frame)
{
    // Kept whether or not the carry hooks installed: the grenade lane throws
    // with the right hand's history too.
    std::lock_guard lock(gMotionMutex);
    for (unsigned h = 0; h < 2; ++h) {
        const auto& s = frame.hands[h];
        if (!IsPoseUsable(s.gripPose, s.gripValidity, 200000000ull)) { continue; }
        carry::MotionSample m{};
        m.time = frame.displayTime;
        m.position = s.gripPose.position;
        m.orientation = s.gripPose.orientation;
        m.velocityValid = s.gripVelocityValid;
        m.velocity = s.gripLinearVelocity;
        m.angular = s.gripAngularVelocity;
        gMotion[h].Push(m);
        gMotionSamples.fetch_add(1, std::memory_order_relaxed);
        if (m.velocityValid) { gRuntimeVelocitySamples.fetch_add(1, std::memory_order_relaxed); }
    }
}

bool EstimateHandMotion(unsigned hand, carry::Motion& out)
{
    if (hand > 1) { return false; }
    std::lock_guard lock(gMotionMutex);
    out = gMotion[hand].Estimate();
    return out.valid;
}

void RequestCarryRelease()
{
    gReleasesAsked.fetch_add(1);
    Pending p{};
    p.active = true;
    p.ns = MonotonicNanoseconds();
    {
        std::lock_guard lock(gMotionMutex);
        p.motion = gMotion[static_cast<unsigned>(Hand::left)].Estimate();
    }
    GameplayPoseFrame f{};
    if (TryGetGameplayPoseFrame(f, false) && std::isfinite(f.yaw)) {
        p.yaw = f.yaw;
        p.yawValid = true;
    }
    std::lock_guard lock(gPendingMutex);
    gPending = p;
}

void UpdateCarryLane(const GameplayPoseFrame& frame, bool)
{
    if (!gInstalled) { return; }
    const std::uintptr_t carry = PlayerCarry(frame.player);
    std::uint32_t id = 0;
    if (carry) { Read(carry + kPickedUp, id); }
    if (gGrab.active && id != gGrab.entity) {
        gStale.fetch_add(1);
        ClearGrab("carry ended");
    }
    // How well the object sits where it is wanted.
    if (gGrab.active && gGrab.written) {
        const auto entity = CallGetEntity(gGrab.entity);
        float box[6]{};
        if (entity && CallBounds(entity, kWorldBoundsSlot, box)) {
            const Vec3 centre{(box[0] + box[3]) * .5f, (box[1] + box[4]) * .5f, (box[2] + box[5]) * .5f};
            gGrab.centreError = Length(Sub(centre, gGrab.wantCentre));
            gGrab.gripDistance = Length(Sub(centre, gGrab.grip));
            float tm[12];
            if (gGrab.kind != carry::Kind::Corpse && CallWorldTm(entity, tm)) {
                const Quaternion q = carry::FromMatrix34(tm);
                const Quaternion d = Multiply(q, carry::Inverse(gGrab.targetRotation));
                const float s = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                gGrab.rotationError = 2.0f * std::atan2(s, std::fabs(d.w)) * 57.29578f;
            }
            if (gTrace.load()) {
                static std::uint64_t last = 0;
                const auto now = MonotonicNanoseconds();
                if (now - last > 66000000ull) {
                    last = now;
                    Log("hold entity=" + std::to_string(gGrab.entity) + " kind=" + carry::KindName(gGrab.kind) +
                        " centre=" + Point(centre) + " want=" + Point(gGrab.wantCentre) + " grip=" + Point(gGrab.grip) +
                        " native=" + Point(gGrab.native) + " errM=" + Fixed(gGrab.centreError) +
                        " gripDistM=" + Fixed(gGrab.gripDistance));
                }
            }
        }
        Remember(gGrab);
    }
    // The released object: did it leave with the velocity it was given, did
    // the game give its collision back, and did it stay in the world?
    if (gWatch.entity) {
        ++gWatch.frames;
        Dynamics d{};
        const auto watched = CallGetEntity(gWatch.entity);
        const auto physics = CallPhysics(watched);
        float wtm[12];
        if (watched && CallWorldTm(watched, wtm)) {
            if (gWatch.frames == 1) { gWatch.startZ = gWatch.minZ = wtm[11]; }
            gWatch.minZ = std::min(gWatch.minZ, wtm[11]);
        }
        if (!gWatch.restored && id != gWatch.entity) {
            gWatch.restored = true;
            gWatch.restoredFrame = gWatch.frames;
        }
        if (physics && CallDynamics(physics, d)) {
            if (gWatch.frames == 2) {
                gWatch.firstSpeed = Length(d.v);
                gWatch.firstVelocity = d.v;
            }
            if (gWatch.frames == 12) { gWatch.laterSpeed = Length(d.v); }
        }
        if (gWatch.frames == 2 || gWatch.frames == 12) {
            Log("flight entity=" + std::to_string(gWatch.entity) + " frame=" + std::to_string(gWatch.frames) +
                " v=" + Point(d.v) + " speed=" + Fixed(Length(d.v), 2) + " expected=" + Point(gWatch.expected) +
                " expectedSpeed=" + Fixed(Length(gWatch.expected), 2));
            std::lock_guard lock(gLastMutex);
            gLast.watch = gWatch;
        }
        if (gWatch.frames == 90) {
            Log("settled entity=" + std::to_string(gWatch.entity) + " startZ=" + Fixed(gWatch.startZ) + " minZ=" +
                Fixed(gWatch.minZ) + " drop=" + Fixed(gWatch.startZ - gWatch.minZ) + " restored=" +
                std::to_string(gWatch.restored) + " restoredFrame=" + std::to_string(gWatch.restoredFrame));
            std::lock_guard lock(gLastMutex);
            gLast.watch = gWatch;
        }
        if (gWatch.frames >= 90) { gWatch = {}; }
    }
}

std::string CarryReport()
{
    Last last{};
    {
        std::lock_guard lock(gLastMutex);
        last = gLast;
    }
    const auto player = CallGetPlayer();
    std::ostringstream out;
    out << " installed=" << gInstalled << " hand=" << gHand.load() << " holdToHold=" << gHoldToHold.load()
        << " grabPointMode=" << gGrabPoint.load()
        << " holding=" << gHolding.load() << " velocityQueue=" << gVelocityQueueOk << " updates=" << gUpdates.load()
        << " starts=" << gStarts.load() << " stops=" << gStops.load() << " releasesAsked=" << gReleasesAsked.load()
        << " releasesDone=" << gReleasesDone.load() << " refused=" << gReleasesRefused.load()
        << " throws=" << gThrows.load() << " drops=" << gDrops.load() << " nativeThrows=" << gNativeThrows.load()
        << " velocitySets=" << gVelocitySets.load() << " velocityFaults=" << gVelocityFaults.load()
        << " faults=" << gFaults.load() << " stale=" << gStale.load() << " motionSamples=" << gMotionSamples.load()
        << " runtimeVelocitySamples=" << gRuntimeVelocitySamples.load();
    {
        std::lock_guard lock(gSettingsMutex);
        out << " gain=" << Fixed(gThrow.gain, 2) << " throwSpeed=" << Fixed(gThrow.throwSpeed, 2)
            << " maxSpeed=" << Fixed(gThrow.maxSpeed, 2) << " heavyMaxSpeed=" << Fixed(gThrow.heavyMaxSpeed, 2);
    }
    out << GrabLine(last.grab);
    if (last.releaseValid) {
        out << " lastRelease=" << last.how << " releasedEntity=" << last.releasedEntity
            << " handSpeed=" << Fixed(last.release.handSpeed, 2) << " handWorld=" << Point(last.handWorld)
            << " spinWorld=" << Point(last.spinWorld) << " playerV=" << Point(last.player)
            << " velocity=" << Point(last.release.velocity) << " angular=" << Point(last.release.angular)
            << " capped=" << last.release.capped << " motionRuntime=" << last.motion.fromRuntime
            << " motionSamples=" << last.motion.samples;
    }
    if (last.watch.frames >= 90) {
        out << " settledDrop=" << Fixed(last.watch.startZ - last.watch.minZ) << " restored=" << last.watch.restored
            << " restoredFrame=" << last.watch.restoredFrame;
    }
    out << " impulseMode=" << gImpulseMode.load() << " nativeImpulseScale=" << Fixed(gLastScale.load(), 2);
    if (last.watch.firstSpeed >= 0) {
        out << " flightV2=" << Point(last.watch.firstVelocity) << " flightSpeed2=" << Fixed(last.watch.firstSpeed, 2)
            << " flightSpeed12=" << Fixed(last.watch.laterSpeed, 2) << " expected=" << Point(last.watch.expected);
    }
    if (last.stop[0]) { out << " lastStop={" << last.stop << "}"; }
    out << CarryFields(PlayerCarry(player));
    return out.str();
}

bool ExecuteCarryCommand(const std::vector<std::string>& args, std::ostringstream& out)
{
    const std::string& verb = args[0];
    if (verb.rfind("carry.", 0) != 0) { return false; }
    const auto arg = [&](std::size_t i, int fallback) {
        int value = fallback;
        if (i < args.size()) {
            try {
                value = std::stoi(args[i]);
            } catch (...) {
            }
        }
        return value;
    };
    DWORD result = 0;
    if (verb == "carry.hand") {
        if (args.size() > 1) { result = SetCarryHand(static_cast<unsigned>(arg(1, 1))); }
    } else if (verb == "carry.hold") {
        if (args.size() > 1) { result = SetCarryHoldToHold(static_cast<unsigned>(arg(1, 1))); }
    } else if (verb == "carry.impulse") {
        if (args.size() > 1) { gImpulseMode.store(std::clamp(arg(1, 0), 0, 2)); }
    } else if (verb == "carry.grabpoint") {
        if (args.size() > 1) { gGrabPoint.store(arg(1, 1) != 0); }
    } else if (verb == "carry.trace") {
        if (args.size() > 1) { gTrace.store(arg(1, 1) != 0); }
    } else if (verb == "carry.throw") {
        // carry.throw <gainPercent 140> <throwSpeedCm 160> <maxSpeedCm 1600> <heavyMaxSpeedCm 500>
        if (args.size() > 1) {
            std::lock_guard lock(gSettingsMutex);
            gThrow.gain = std::clamp(arg(1, 140), 50, 300) * .01f;
            gThrow.throwSpeed = std::clamp(arg(2, 160), 20, 1000) * .01f;
            gThrow.maxSpeed = std::clamp(arg(3, 1600), 100, 5000) * .01f;
            gThrow.heavyMaxSpeed = std::clamp(arg(4, 500), 50, 3000) * .01f;
        }
    } else if (verb == "carry.heavy") {
        // carry.heavy <massKg 25> <diagonalCm 120> <holdMs 500> <maxShiftCm 60> <tauMs 100>
        if (args.size() > 1) {
            std::lock_guard lock(gSettingsMutex);
            gKindSettings.heavyMass = static_cast<float>(std::clamp(arg(1, 25), 1, 1000));
            gKindSettings.heavyDiagonal = std::clamp(arg(2, 120), 20, 1000) * .01f;
            gKindSettings.heavyHold = std::clamp(arg(3, 500), 0, 5000) * .001f;
            gHoldSettings.heavyMaxShift = std::clamp(arg(4, 60), 0, 200) * .01f;
            gHeavyTau = std::clamp(arg(5, 100), 0, 2000) * .001f;
        }
    } else if (verb == "carry.corpse") {
        // carry.corpse <minDistanceCm 75> <maxFromNativeCm 110> <tauMs 120>
        if (args.size() > 1) {
            std::lock_guard lock(gSettingsMutex);
            gCorpseSettings.minDistance = std::clamp(arg(1, 75), 0, 300) * .01f;
            gCorpseSettings.maxFromNative = std::clamp(arg(2, 110), 0, 300) * .01f;
            gCorpseTau = std::clamp(arg(3, 120), 0, 2000) * .001f;
        }
    } else if (verb != "carry.report") {
        out << verb << " result=" << ERROR_INVALID_FUNCTION;
        return true;
    }
    out << verb << " result=" << result << CarryReport();
    return true;
}

}  // namespace preyvr::dll
