#include "GrenadeLane.h"

#include "AimTakeover.h"
#include "CarryLane.h"
#include "Haptics.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "SceneQuery.h"
#include "WeaponAttachment.h"
#include "XrInput.h"
#include "preyvr/AnimIk.h"
#include "preyvr/GrenadeThrow.h"
#include "preyvr/HandCarry.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/StereoCamera.h"

#include <MinHook.h>
#include <intrin.h>

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
using carry::Dot;
using carry::Finite;
using carry::Length;
using carry::Scale;
using carry::Sub;

void Log(const std::string& line) { lifecycle::Log("preyvr_grenade " + line); }

// --- native contracts (Steam PreyDll.dll, SHA-256 7d6e322f...) ---------------
//
// CArkWeaponGrenade (no RTTI in the Steam build; identified by its methods):
//   fire action handler 0x16A5C90 (vtable +0x88 of the action listener): mode 1
//   press -> 0x16A6150 starts the charge (+0x521 = 1, timer +0x524 started, the
//   charge fragment); mode 2 release -> vtable +0xE8 = 0x16A6040, which plays
//   the throw fragment (or the deploy fragment when +0x4F9, a surface within
//   reach of the VIEW, is set). The fragment's "Throw" event (handler 0x169DD20,
//   lower-case CRC of "Throw" at 0x1E11130) calls vtable +0x118 = ThrowGrenade
//   0x16A6300 from 0x169DDA9; "Deploy" calls +0x120 = DeployGrenade 0x16A4B20.
// ThrowGrenade: position = GetFiringPosition 0x1694BC0 (out +4), direction =
// normalize(GetReticlePosition (vtable +0x148) - position); the projectile from
// 0x168F2B0 (RCX archetype [w+0x4F0], RDX &position, R8 &direction, R9 owner
// entity id, six more on the stack; returns the projectile, entity at +0x38);
// then, if the charge timer's duration [w+0x528] > 0, pe_action_set_velocity
// (type 10, queued) = direction of its current velocity x lerp(fMinSpeed
// [w+0x4E0], fMaxSpeed [w+0x4E4], elapsed/duration); then ammo ([w+0x2B0]
// vtable +0x88 (1)) and vtable +0x1F8.
constexpr std::uintptr_t kThrowRva = 0x16A6300;
constexpr std::array<std::uint8_t, 18> kThrowPrologue{
    0x48, 0x8B, 0xC4, 0x55, 0x56, 0x41, 0x56, 0x48, 0x8D, 0x68, 0x98, 0x48, 0x81, 0xEC, 0x50, 0x01, 0x00, 0x00};
constexpr std::uintptr_t kThrowEventReturnRva = 0x169DDAF;
constexpr std::uintptr_t kCreateRva = 0x168F2B0;
constexpr std::array<std::uint8_t, 15> kCreatePrologue{
    0x4C, 0x8B, 0xDC, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x49, 0x89, 0x73, 0x10};
constexpr std::uintptr_t kReleaseRva = 0x16A6040;
constexpr std::array<std::uint8_t, 17> kReleasePrologue{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80, 0xB9, 0x21, 0x05, 0x00, 0x00, 0x00};
// The per-frame update 0x16A67B0 (secondary vtable at weapon+8, slot +0x98;
// RCX = weapon+8): with [weapon+0x4C0] > 0 it raycasts that far along the VIEW
// and, on a surface, sets [weapon+0x4F9] -- "deploy": the press then sticks the
// charge there instead of charging a throw. A head-gaze decision in VR; with
// the range 0 during the call the raycast is skipped and the flag stays clear
// (it is cleared only by a raycast that misses, so a set flag is left to that).
constexpr std::uintptr_t kUpdateRva = 0x16A67B0;
constexpr std::array<std::uint8_t, 18> kUpdatePrologue{
    0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0x90, 0x48, 0x81, 0xEC, 0x70, 0x01, 0x00, 0x00};
constexpr std::uintptr_t kDeployRange = 0x4C0;
constexpr std::size_t kThrowSlot = 0x118;
constexpr std::uintptr_t kFiringPositionRva = 0x1694BC0;   // (weapon, out16, flags, overrideEntity)
constexpr std::uintptr_t kWeaponEntity = 0x40;
constexpr std::uintptr_t kMinSpeed = 0x4E0, kMaxSpeed = 0x4E4, kArchetype = 0x4F0, kDeployReady = 0x4F9;
constexpr std::uintptr_t kCharging = 0x521, kChargeRemaining = 0x524, kChargeDuration = 0x528;
constexpr std::uintptr_t kProjectileEntity = 0x38;
// The EMP charge's centre comes to rest ~7 cm above the floor (measured contact
// heights 2026-10-07): ranges are compared at that height.
constexpr float kGrenadeRadius = 0.07f;

// i_giveitem's own path (handler 0x146F2C0): the archetype from the entity
// system (+0x1A0), then 0x146E6D0 (manager, &std::vector<EntityId> out,
// receiver 0x7777 = the local player, archetype, count); the manager is
// 0x16FDDA0([0x2C16840]); the vector's buffer goes back through 0x1B772CC.
constexpr std::uintptr_t kGiveRva = 0x146E6D0;
constexpr std::array<std::uint8_t, 15> kGivePrologue{
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54};
constexpr std::uintptr_t kItemManagerGetterRva = 0x16FDDA0, kItemManagerGlobalRva = 0x2C16840;
constexpr std::uintptr_t kFreeRva = 0x1B772CC;
constexpr std::uintptr_t kEntitySystemRva = 0x224DA38;
constexpr std::size_t kArchetypeSlot = 0x1A0, kGetEntitySlot = 0x70;
// Equip (ArmsLane.cpp): ArkPlayerWeaponComponent = player+0x14B8.
constexpr std::uintptr_t kEquipRva = 0x1274820, kCanEquipRva = 0x1273EB0, kWeaponComponent = 0x14B8;
constexpr std::uintptr_t kGetPlayerRva = 0x157C990;

constexpr std::size_t kWorldTmSlot = 0xE8, kPhysicsSlot = 0x228, kGetIdSlot = 0x08, kGetNameSlot = 0x78;
constexpr std::size_t kGetArchetypeSlot = 0x20, kNameSlot = 0x10;
constexpr std::size_t kPhysGetParamsSlot = 0x28, kPhysStatusSlot = 0x30, kPhysActionSlot = 0x38;

using ThrowFn = void(__fastcall*)(std::uintptr_t weapon);
using CreateFn = std::uintptr_t(__fastcall*)(std::uintptr_t archetype, float* position, float* direction,
                                             std::uint64_t owner, std::uint64_t a5, std::uint64_t a6,
                                             std::uint64_t a7, std::uint64_t a8, std::uint64_t a9, std::uint64_t a10);
using ReleaseFn = bool(__fastcall*)(std::uintptr_t weapon);
using UpdateFn = void(__fastcall*)(std::uintptr_t weaponPlus8, std::uintptr_t context);
using FiringPositionFn = void*(__fastcall*)(std::uintptr_t weapon, void* out16, std::uint32_t flags, void* entity);

std::uintptr_t gBase = 0;
std::atomic<ThrowFn> gThrow{nullptr};
std::atomic<CreateFn> gCreate{nullptr};
std::atomic<ReleaseFn> gRelease{nullptr};
std::atomic<UpdateFn> gUpdate{nullptr};
std::mutex gInstallMutex;
bool gInstallTried = false, gInstalled = false, gGiveOk = false;

// grenade.hand: off until VR mode switches it on. grenade.early 1: the throw
// leaves at the release (0: at the animation's "Throw" event, as natively).
std::atomic<bool> gHand{false}, gEarly{true}, gTrace{false};
// grenade.spawn 0: where the grenade is drawn (its entity's centre; the fist
// if that is unreadable or off the hand); 1: the fist; 2: the native firing
// position (the camera when the game falls back to it).
std::atomic<int> gSpawnMode{0};
// grenade.deploy 0 (default with grenade.hand): no sticking to what the view
// is on; 1: the native deploy stays.
std::atomic<bool> gDeploy{false};

std::mutex gSettingsMutex;
grenade::ThrowSettings gSettings{};
grenade::ThrowSettings Settings()
{
    std::lock_guard lock(gSettingsMutex);
    return gSettings;
}

// --- the trigger (input lane) --------------------------------------------------
struct TriggerRelease {
    bool valid = false;
    std::uint64_t ns = 0, pressNs = 0;
    carry::Motion motion{};
    float yaw = 0;
    bool yawValid = false;
};
std::mutex gTriggerMutex;
TriggerRelease gTrigger;
std::uint64_t gPressNs = 0;

// --- one throw, on the thread that throws ------------------------------------------
struct Scope {
    std::uintptr_t weapon = 0;
    bool early = false, fromEvent = false;
    bool steer = false;              // the VR throw applies
    TriggerRelease release{};
    GameplayPoseFrame frame{};
    bool haveFrame = false;
    // Filled by the projectile creator.
    bool created = false;
    std::uintptr_t projectile = 0;
    Vec3 nativePosition{}, nativeDirection{}, position{}, direction{};
    Vec3 grip{}, head{};
    float pulledBack = 0;
    grenade::Release plan{};
    Vec3 handWorld{}, spinWorld{}, player{};
    float nativeMin = 0, nativeMax = 0, chargeElapsed = 0, chargeDuration = 0;
    float floorZ = 0;
    bool haveFloor = false;
    int spawnFrom = -1;   // 0 the drawn grenade, 1 the fist, 2 native
    grenade::ReleaseInput input{};
    // The projectile's own free-flight physics, read once it exists; the
    // speed is re-solved with it (the direction does not change).
    bool physicsRead = false;
    float gravity = 0, damping = 0;
};
thread_local Scope* tScope = nullptr;
thread_local bool tEarlyCall = false;

// The animation's Throw event still to come for a throw that already left.
struct Skip {
    std::uintptr_t weapon = 0;
    std::uint64_t ns = 0;
};
Skip gSkip;

// --- the flight, watched (game thread) ------------------------------------------------
struct Flight {
    std::uint32_t entity = 0;
    char name[64]{};
    std::uint64_t startNs = 0;
    unsigned frames = 0;
    Vec3 spawn{}, expected{};
    Vec3 v2{};
    bool haveV2 = false;
    // Free flight, measured: gravity and damping from each frame's change of
    // velocity over the physics time it covered (displacement / speed).
    float gravitySum = 0, dampingSum = 0;
    unsigned fits = 0;
    Vec3 last{}, lastV{};
    std::uint64_t lastNs = 0;
    bool contact = false;
    Vec3 contactAt{};
    float contactSeconds = 0;
    // On the floor: where the model says it comes down, and where a real ball
    // thrown with the hand's speed x vrFactor would have.
    float floorZ = 0;
    bool haveFloor = false;
    Vec3 heading{};         // horizontal unit direction of the launch
    float gameRange = -1, realRange = -1, range = -1, rangeError = -1, lateral = -1;
    bool onFloor = false;
    float modelGravity = 0, modelDamping = 0;   // the projectile's own (read), else the settings'
    bool rest = false;
    Vec3 restAt{};
    float restSeconds = 0;
    unsigned still = 0;
    bool gone = false;
    float goneSeconds = 0;
    float maxHeight = 0;
    float distance = 0;     // horizontal, spawn -> contact
};
Flight gFlight;

// --- report snapshot ------------------------------------------------------------------
struct Last {
    bool valid = false;
    Scope scope{};
    std::uint64_t ns = 0;
    float delayMs = -1;     // trigger release -> the projectile
    float pressedMs = -1;   // how long the trigger was held
    Flight flight{};
};
std::mutex gLastMutex;
Last gLast;

struct GiveRequest {
    bool active = false;
    char archetype[96]{};
    int count = 1;
    bool equip = true;
};
std::mutex gGiveMutex;
GiveRequest gGive;
char gGiveResult[160] = "none";
// The equip after a give, retried while the game is still putting a spent
// grenade away (Equip refuses then): the given item, or the grenade weapon it
// merged into (the last equipped one).
struct PendingEquip {
    std::uint32_t id = 0;
    std::uint64_t until = 0;
    unsigned tries = 0;
};
PendingEquip gPendingEquip;

std::atomic<unsigned long long> gDeploySuppressed{0}, gThrows{0}, gSteered{0}, gEarlyThrows{0}, gEventThrows{0}, gEventsSkipped{0},
    gNativeThrows{0}, gReleases{0}, gCreates{0}, gVelocitySets{0}, gFaults{0}, gNoRelease{0}, gPulledBack{0};

// --- SEH helpers (POD only) ---------------------------------------------------------

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
bool CallWorldPosition(std::uintptr_t entity, Vec3& out)
{
    __try {
        const float* m = entity ? VCall<const float*>(entity, kWorldTmSlot) : nullptr;
        if (!m) { return false; }
        out = {m[3], m[7], m[11]};
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return Finite(out);
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
const char* CallArchetypeName(std::uintptr_t entity)
{
    __try {
        const auto archetype = entity ? VCall<std::uintptr_t>(entity, kGetArchetypeSlot) : 0;
        return archetype ? VCall<const char*>(archetype, kNameSlot) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// pe_status_dynamics: type 8; v +0x0C, w +0x18, mass +0x4C (CarryLane.cpp).
bool CallDynamics(std::uintptr_t physics, Vec3& v, Vec3& w)
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
    std::memcpy(&v, status + 0x0C, sizeof(Vec3));
    std::memcpy(&w, status + 0x18, sizeof(Vec3));
    return Finite(v) && Finite(w);
}
// pe_action_set_velocity, 40 bytes (CarryLane.cpp verifies the queue's size for type 10).
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
        return physics && VCall<int>(physics, kPhysActionSlot, static_cast<const void*>(&action), 1) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// pe_simulation_params (type 10 of the params family, Prey's physinterface.h):
// damping +0x10, gravity +0x14, dampingFreefall +0x24, gravityFreefall +0x28 --
// the free-flight pair is what a thrown grenade flies with. False if neither
// pair reads as set.
bool CallFreeFlight(std::uintptr_t physics, float& gravity, float& damping)
{
    alignas(16) std::uint8_t p[0x60]{};
    const int type = 10;
    std::memcpy(p, &type, 4);
    __try {
        if (!physics || !VCall<int>(physics, kPhysGetParamsSlot, static_cast<void*>(p))) { return false; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    float k = 0, kf = 0;
    Vec3 g{}, gf{};
    std::memcpy(&k, p + 0x10, 4);
    std::memcpy(&g, p + 0x14, sizeof(Vec3));
    std::memcpy(&kf, p + 0x24, 4);
    std::memcpy(&gf, p + 0x28, sizeof(Vec3));
    const auto usable = [](float damp, Vec3 grav) {
        return std::isfinite(damp) && damp >= 0.0f && damp < 20.0f && Finite(grav) && grav.z < -0.5f && grav.z > -100.0f;
    };
    if (usable(kf, gf)) {
        gravity = -gf.z;
        damping = kf;
        return true;
    }
    if (usable(k, g)) {
        gravity = -g.z;
        damping = k;
        return true;
    }
    return false;
}

bool CallFiringPosition(std::uintptr_t weapon, Vec3& out, bool& cameraFallback)
{
    alignas(16) std::uint8_t result[16]{};
    __try {
        reinterpret_cast<FiringPositionFn>(gBase + kFiringPositionRva)(weapon, result, 0, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    cameraFallback = result[0] != 0;
    std::memcpy(&out, result + 4, sizeof(Vec3));
    return Finite(out);
}

std::string Fixed(float v, int decimals = 3)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, static_cast<double>(v));
    return text;
}
std::string Point(Vec3 v) { return Fixed(v.x) + "," + Fixed(v.y) + "," + Fixed(v.z); }
Vec3 Normalized(Vec3 v)
{
    const float l = Length(v);
    return l > 1e-6f ? Scale(v, 1.0f / l) : Vec3{};
}

// A grenade weapon is one whose +0x118 is ThrowGrenade.
bool IsGrenadeWeapon(std::uintptr_t weapon)
{
    std::uintptr_t vtable = 0, slot = 0;
    return weapon && Read(weapon, vtable) && vtable && Read(vtable + kThrowSlot, slot) && slot == gBase + kThrowRva;
}
std::uintptr_t PlayerGrenade(std::uintptr_t player)
{
    EquippedRig rig{};
    if (!player || !TryGetEquippedRig(player, rig)) { return 0; }
    return IsGrenadeWeapon(rig.weapon) ? rig.weapon : 0;
}
bool IsPlayerGrenade(std::uintptr_t weapon)
{
    return weapon && weapon == PlayerGrenade(CallGetPlayer());
}

bool PlayerFeet(Vec3& out)
{
    const auto player = CallGetPlayer();
    std::uintptr_t entity = 0;
    return player && Read(player + 0x38, entity) && CallWorldPosition(entity, out);
}

Vec3 PlayerVelocity()
{
    const auto player = CallGetPlayer();
    std::uintptr_t entity = 0;
    if (!player || !Read(player + 0x38, entity)) { return {}; }
    Vec3 v{}, w{};
    return CallDynamics(CallPhysics(entity), v, w) ? v : Vec3{};
}

bool RightGrip(const GameplayPoseFrame& f, Pose& out)
{
    const auto& hand = f.tracking.hands[static_cast<unsigned>(Hand::right)];
    if (!f.cameraCentreValid || !std::isfinite(f.yaw) ||
        !IsPoseUsable(hand.gripPose, hand.gripValidity, 200000000ull)) {
        return false;
    }
    out = animik::ControllerWorldFromHead(f.yaw, f.cameraCentre, f.tracking.head, hand.gripPose);
    return Finite(out.position) && Finite(out.orientation);
}
// The middle of the closed right fist: out of the grip origin through the palm
// (the right hand's palm faces grip -X), where a held grenade's centre sits.
Vec3 FistPoint(const Pose& grip)
{
    return Add(grip.position, Rotate(grip.orientation, stereo::ToEngineSpace(Vec3{-0.02f, 0, 0})));
}

void Remember(const Scope& s, float delayMs, float pressedMs)
{
    std::lock_guard lock(gLastMutex);
    gLast.valid = true;
    gLast.scope = s;
    gLast.ns = MonotonicNanoseconds();
    gLast.delayMs = delayMs;
    gLast.pressedMs = pressedMs;
}

// --- the throw ---------------------------------------------------------------------

// Inside the creator: where it spawns and which way it faces, and the velocity
// it will leave with (applied once the native throw is done with it).
void Steer(Scope& s, Vec3& position, Vec3& direction)
{
    s.nativePosition = position;
    s.nativeDirection = direction;
    s.position = position;
    s.direction = direction;
    if (!s.steer) { return; }
    Pose grip{};
    const bool haveGrip = s.haveFrame && RightGrip(s.frame, grip);
    if (haveGrip) { s.grip = grip.position; }
    s.head = s.frame.cameraCentre;
    Vec3 want = position;
    const int mode = gSpawnMode.load();
    // The drawn grenade: the game's own firing position, unless it fell back
    // to the camera (it does while the grenade idles) or is off the hand (the
    // arm at full stretch short of a controller held further out).
    Vec3 firing{};
    bool fallback = true;
    const bool drawn = haveGrip && CallFiringPosition(s.weapon, firing, fallback) && !fallback &&
                       Length(Sub(position, grip.position)) < 0.20f;
    if (mode == 0 && drawn) {
        s.spawnFrom = 0;
    } else if (mode != 2 && haveGrip) {
        want = FistPoint(grip);
        s.spawnFrom = 1;
    } else {
        s.spawnFrom = 2;
    }
    // Never through a wall the hand has gone into: the segment from the head.
    if (s.haveFrame && s.frame.cameraCentreValid) {
        scene::Hit hit{};
        if (QuerySegment(s.frame, s.head, want, hit) && hit.distance >= 0) {
            const float l = Length(Sub(want, s.head));
            const Vec3 pulled = grenade::PullBack(s.head, want, l > 1e-4f ? hit.distance / l : 0.0f, 0.08f);
            s.pulledBack = Length(Sub(want, pulled));
            want = pulled;
            gPulledBack.fetch_add(1);
        }
    }
    // The release's velocity, in the world, from the hand's motion at the trigger.
    const auto& m = s.release.motion;
    const float yaw = s.release.yawValid ? s.release.yaw : s.frame.yaw;
    s.handWorld = m.valid ? carry::TrackingToWorld(m.velocity, yaw) : Vec3{};
    s.spinWorld = m.valid ? carry::TrackingToWorld(m.angular, yaw) : Vec3{};
    s.player = PlayerVelocity();
    grenade::ReleaseInput in{};
    in.handVelocity = s.handWorld;
    in.handAngular = s.spinWorld;
    in.player = s.player;
    in.nativeMax = s.nativeMax;
    Vec3 feet{};
    if (PlayerFeet(feet)) {
        s.floorZ = feet.z;
        s.haveFloor = true;
        // To the height its centre stops at on the floor (its radius).
        in.height = std::clamp(want.z - feet.z - kGrenadeRadius, 0.0f, 3.0f);
    }
    // The lever is the grenade's offset from the grip origin, only when the
    // spawn really is in the fist (a camera fallback or a pulled-back spawn
    // must not turn the wrist's spin into speed).
    if (haveGrip && s.pulledBack <= 0.0f) {
        const Vec3 lever = Sub(want, grip.position);
        if (Length(lever) <= 0.15f) { in.lever = lever; }
    }
    s.input = in;
    s.plan = grenade::ComputeRelease(in, Settings());
    position = want;
    s.position = want;
    // Facing the way it flies; a drop keeps the native facing.
    const Vec3 own = Sub(s.plan.velocity, s.player);
    if (Length(own) > 0.5f) { direction = Normalized(own); }
    s.direction = direction;
}

std::uintptr_t __fastcall CreateHook(std::uintptr_t archetype, float* position, float* direction, std::uint64_t owner,
                                     std::uint64_t a5, std::uint64_t a6, std::uint64_t a7, std::uint64_t a8,
                                     std::uint64_t a9, std::uint64_t a10)
{
    const auto original = gCreate.load(std::memory_order_acquire);
    Scope* const s = tScope;
    if (!s || s->created || !position || !direction) {
        return original(archetype, position, direction, owner, a5, a6, a7, a8, a9, a10);
    }
    gCreates.fetch_add(1);
    s->created = true;
    Vec3 p{position[0], position[1], position[2]};
    Vec3 d{direction[0], direction[1], direction[2]};
    if (!Finite(p) || !Finite(d)) {
        s->steer = false;
        return original(archetype, position, direction, owner, a5, a6, a7, a8, a9, a10);
    }
    Steer(*s, p, d);
    float pos[3]{p.x, p.y, p.z}, dir[3]{d.x, d.y, d.z};
    s->projectile = original(archetype, pos, dir, owner, a5, a6, a7, a8, a9, a10);
    return s->projectile;
}

void StartFlight(const Scope& s)
{
    std::uintptr_t entity = 0;
    if (!s.projectile || !Read(s.projectile + kProjectileEntity, entity) || !entity) { return; }
    Flight f{};
    f.entity = CallEntityId(entity);
    if (!f.entity) { return; }
    if (!CopyName(CallArchetypeName(entity), f.name, sizeof(f.name))) { std::snprintf(f.name, sizeof(f.name), "?"); }
    f.startNs = MonotonicNanoseconds();
    f.spawn = s.position;
    f.expected = s.steer ? s.plan.velocity : Vec3{};
    f.last = s.position;
    f.lastNs = f.startNs;
    f.maxHeight = s.position.z;
    {
        const auto settings = Settings();
        f.modelGravity = settings.gravity;
        f.modelDamping = settings.damping;
        float g = 0, k = 0;
        if (s.physicsRead) {
            f.modelGravity = s.gravity;
            f.modelDamping = s.damping;
        } else if (CallFreeFlight(CallPhysics(entity), g, k)) {
            f.modelGravity = g;
            f.modelDamping = k;
        }
    }
    Vec3 feet{};
    if (s.haveFloor) {
        f.floorZ = s.floorZ;
        f.haveFloor = true;
    } else if (PlayerFeet(feet)) {
        f.floorZ = feet.z;
        f.haveFloor = true;
    }
    if (s.steer) {
        // Where a real ball would have come down: the grenade's own velocity x
        // vrFactor (the speed parity aims for), plus the player's.
        const Vec3 real = Add(Scale(Sub(s.plan.velocity, s.player), s.plan.speed > 1e-4f
                                                                        ? s.plan.objectSpeed * Settings().vrFactor / s.plan.speed
                                                                        : 0.0f),
                              s.player);
        const float speed = Length(real);
        if (f.haveFloor && speed > 1e-3f) {
            f.realRange = grenade::RangeOnFloor(speed, std::asin(std::clamp(real.z / speed, -1.0f, 1.0f)),
                                                std::max(s.position.z - f.floorZ - kGrenadeRadius, 0.0f), 9.81f, 0.0f);
        }
    }
    gFlight = f;
}

void __fastcall ThrowHook(std::uintptr_t weapon)
{
    const auto original = gThrow.load(std::memory_order_acquire);
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const bool early = tEarlyCall;
    const bool fromEvent = caller == gBase + kThrowEventReturnRva;
    if (!IsPlayerGrenade(weapon)) {
        original(weapon);
        return;
    }
    const auto now = MonotonicNanoseconds();
    // The animation's Throw event of a grenade that already left at the release.
    if (fromEvent && gSkip.weapon == weapon && now - gSkip.ns < 3000000000ull) {
        gSkip = {};
        gEventsSkipped.fetch_add(1);
        Log("event_skipped");
        return;
    }
    gThrows.fetch_add(1);
    (early ? gEarlyThrows : fromEvent ? gEventThrows : gNativeThrows).fetch_add(1);
    Scope s{};
    s.weapon = weapon;
    s.early = early;
    s.fromEvent = fromEvent;
    {
        std::lock_guard lock(gTriggerMutex);
        s.release = gTrigger;
    }
    Read(weapon + kMinSpeed, s.nativeMin);
    Read(weapon + kMaxSpeed, s.nativeMax);
    float remaining = 0;
    Read(weapon + kChargeDuration, s.chargeDuration);
    if (Read(weapon + kChargeRemaining, remaining) && remaining >= 0) { s.chargeElapsed = s.chargeDuration - remaining; }
    s.haveFrame = TryGetGameplayPoseFrame(s.frame, true);
    const bool freshRelease = s.release.valid && now - s.release.ns < 1500000000ull;
    if (!freshRelease) { gNoRelease.fetch_add(1); }
    s.steer = gHand.load() && s.haveFrame && freshRelease;
    tScope = &s;
    original(weapon);
    tScope = nullptr;
    // After the native throw, whose own set_velocity (a charged throw) is
    // queued before this one: this is the velocity it leaves with.
    std::uintptr_t entity = 0;
    if (s.steer && s.projectile && Read(s.projectile + kProjectileEntity, entity) && entity) {
        auto settings = Settings();
        if (CallFreeFlight(CallPhysics(entity), s.gravity, s.damping)) {
            s.physicsRead = true;
            settings.gravity = s.gravity;
            settings.damping = s.damping;
            s.plan = grenade::ComputeRelease(s.input, settings);
        }
        SetVelocityAction a{};
        a.v = s.plan.velocity;
        a.w = s.plan.angular;
        if (CallSetVelocity(CallPhysics(entity), a)) {
            gVelocitySets.fetch_add(1);
            gSteered.fetch_add(1);
        } else {
            gFaults.fetch_add(1);
        }
        if (s.haveFrame && s.plan.thrown) { QueueHaptic(Hand::right, haptics::Event::ObjectThrown, s.frame.tracking); }
    }
    StartFlight(s);
    const float delayMs = s.release.valid ? static_cast<float>(now - s.release.ns) * 1e-6f : -1.0f;
    const float pressedMs = s.release.valid && s.release.pressNs && s.release.ns > s.release.pressNs
                                ? static_cast<float>(s.release.ns - s.release.pressNs) * 1e-6f
                                : -1.0f;
    Remember(s, delayMs, pressedMs);
    Log(std::string("throw how=") + (early ? "early" : fromEvent ? "event" : "other") + " steer=" +
        std::to_string(s.steer) + " created=" + std::to_string(s.created) + " delayMs=" + Fixed(delayMs, 1) +
        " pressedMs=" + Fixed(pressedMs, 1) + " nativePos=" + Point(s.nativePosition) + " nativeDir=" +
        Point(s.nativeDirection) + " pos=" + Point(s.position) + " dir=" + Point(s.direction) + " spawnFrom=" +
        std::to_string(s.spawnFrom) + " grip=" + Point(s.grip) + " head=" + Point(s.head) + " pulledBack=" + Fixed(s.pulledBack) + " hand=" +
        Point(s.handWorld) + " handSpeed=" + Fixed(s.plan.handSpeed, 2) + " objectSpeed=" +
        Fixed(s.plan.objectSpeed, 2) + " gain=" + Fixed(s.plan.gain, 2) + " speed=" + Fixed(s.plan.speed, 2) +
        " limit=" + Fixed(s.plan.limit, 2) + " limited=" + std::to_string(s.plan.limited) + " thrown=" +
        std::to_string(s.plan.thrown) + " player=" + Point(s.player) + " velocity=" + Point(s.plan.velocity) +
        " angular=" + Point(s.plan.angular) + " nativeMin=" + Fixed(s.nativeMin, 2) + " nativeMax=" +
        Fixed(s.nativeMax, 2) + " charge=" + Fixed(s.chargeElapsed, 2) + "/" + Fixed(s.chargeDuration, 2) +
        " motion=" + std::to_string(s.release.motion.valid) + " runtime=" +
        std::to_string(s.release.motion.fromRuntime) + " samples=" + std::to_string(s.release.motion.samples) +
        " physics=" + std::to_string(s.physicsRead) + " gravity=" + Fixed(s.gravity, 2) + " damping=" +
        Fixed(s.damping, 3) + " entity=" + std::to_string(gFlight.entity) + " name=" + gFlight.name);
}

void __fastcall UpdateHook(std::uintptr_t weaponPlus8, std::uintptr_t context)
{
    const auto original = gUpdate.load(std::memory_order_acquire);
    const std::uintptr_t weapon = weaponPlus8 - 8;
    std::uint8_t ready = 1;
    float range = 0;
    if (!gHand.load() || gDeploy.load() || !Read(weapon + kDeployReady, ready) || ready ||
        !Read(weapon + kDeployRange, range) || !(range > 0.0f) || !IsPlayerGrenade(weapon)) {
        original(weaponPlus8, context);
        return;
    }
    float* const field = reinterpret_cast<float*>(weapon + kDeployRange);
    *field = 0.0f;
    original(weaponPlus8, context);
    *field = range;
    gDeploySuppressed.fetch_add(1, std::memory_order_relaxed);
}

bool __fastcall ReleaseHook(std::uintptr_t weapon)
{
    const auto original = gRelease.load(std::memory_order_acquire);
    std::uint8_t deploy = 0, charging = 0;
    Read(weapon + kDeployReady, deploy);
    Read(weapon + kCharging, charging);
    const bool started = original(weapon);
    gReleases.fetch_add(1);
    if (gTrace.load()) {
        Log("release started=" + std::to_string(started) + " deploy=" + std::to_string(deploy) + " charging=" +
            std::to_string(charging));
    }
    if (!started || deploy || !gHand.load() || !gEarly.load() || !IsPlayerGrenade(weapon)) { return started; }
    bool fresh = false;
    {
        std::lock_guard lock(gTriggerMutex);
        fresh = gTrigger.valid && MonotonicNanoseconds() - gTrigger.ns < 500000000ull;
    }
    if (!fresh) { return started; }
    // The throw leaves the hand now; the wind-up animation plays on, and its
    // Throw event is skipped (ThrowHook).
    tEarlyCall = true;
    reinterpret_cast<ThrowFn>(gBase + kThrowRva)(weapon);   // through the hook
    tEarlyCall = false;
    gSkip = {weapon, MonotonicNanoseconds()};
    return started;
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

// --- give (test support; game thread) ---------------------------------------------------

struct EntityIdVector {
    std::uint32_t* begin = nullptr;
    std::uint32_t* end = nullptr;
    std::uint32_t* capacity = nullptr;
};
using GiveFn = void(__fastcall*)(std::uintptr_t manager, EntityIdVector* out, std::uint32_t receiver,
                                 std::uintptr_t archetype, int count);

int CallGive(const char* name, int count, std::uint32_t& first, unsigned& given)
{
    first = 0;
    given = 0;
    EntityIdVector ids{};
    __try {
        const auto system = *reinterpret_cast<const std::uintptr_t*>(gBase + kEntitySystemRva);
        if (!system) { return 2; }
        const auto archetype = VCall<std::uintptr_t>(system, kArchetypeSlot, name);
        if (!archetype) { return 3; }
        const auto global = *reinterpret_cast<const std::uintptr_t*>(gBase + kItemManagerGlobalRva);
        const auto manager = reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t)>(gBase + kItemManagerGetterRva)(global);
        if (!manager) { return 4; }
        reinterpret_cast<GiveFn>(gBase + kGiveRva)(manager, &ids, 0x7777, archetype, count);
        if (ids.begin && ids.end > ids.begin) {
            given = static_cast<unsigned>(ids.end - ids.begin);
            first = ids.begin[0];
        }
        if (ids.begin) {
            // As the console handler frees it: big buffers keep their real
            // allocation 8 bytes before the aligned data.
            auto* block = reinterpret_cast<void*>(ids.begin);
            const auto bytes = reinterpret_cast<std::uintptr_t>(ids.capacity) - reinterpret_cast<std::uintptr_t>(ids.begin);
            if (bytes >= 0x1000) { block = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(ids.begin) - 8); }
            reinterpret_cast<void(__cdecl*)(void*)>(gBase + kFreeRva)(block);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 5;
    }
    return given ? 0 : 6;
}
int CallEquip(std::uintptr_t player, std::uint32_t id)
{
    __try {
        const auto component = player + kWeaponComponent;
        if (!reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(gBase + kCanEquipRva)(component)) { return 3; }
        return reinterpret_cast<bool(__fastcall*)(std::uintptr_t, std::uint32_t)>(gBase + kEquipRva)(component, id) ? 0 : 4;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 5;
    }
}

void RunGive(std::uintptr_t player)
{
    GiveRequest g{};
    {
        std::lock_guard lock(gGiveMutex);
        g = gGive;
        gGive = {};
    }
    if (!g.active) { return; }
    std::uint32_t id = 0;
    unsigned given = 0;
    const int r = gGiveOk ? CallGive(g.archetype, g.count, id, given) : 1;
    const int e = r == 0 && g.equip && player ? CallEquip(player, id) : -1;
    if (r == 0 && g.equip && e != 0) { gPendingEquip = {id, MonotonicNanoseconds() + 3000000000ull, 0}; }
    std::snprintf(gGiveResult, sizeof(gGiveResult), "give=%d given=%u id=%u equip=%d archetype=%s", r, given, id, e,
                  g.archetype);
    Log(std::string("give ") + gGiveResult);
}

void RetryEquip(std::uintptr_t player)
{
    PendingEquip& p = gPendingEquip;
    if (!p.id || !player) { return; }
    if (MonotonicNanoseconds() > p.until || PlayerGrenade(player)) {
        Log("equip_retry done=" + std::to_string(PlayerGrenade(player) != 0) + " tries=" + std::to_string(p.tries));
        p = {};
        return;
    }
    if (++p.tries % 10) { return; }   // every 10th frame
    std::uint32_t last = 0;
    Read(player + kWeaponComponent + 0x5C, last);
    if (CallEquip(player, p.id) != 0 && last && last != p.id) { CallEquip(player, last); }
}

// --- the flight watch (game thread) -------------------------------------------------------

void WatchFlight()
{
    Flight& f = gFlight;
    if (!f.entity) { return; }
    const auto now = MonotonicNanoseconds();
    const float t = static_cast<float>(now - f.startNs) * 1e-9f;
    const auto entity = CallGetEntity(f.entity);
    Vec3 p{}, v{}, w{};
    if (!entity || !CallWorldPosition(entity, p) || !CallDynamics(CallPhysics(entity), v, w)) {
        // Gone: it went off (the EMP pulses and removes itself) or was removed.
        f.gone = true;
        f.goneSeconds = t;
        Log("gone entity=" + std::to_string(f.entity) + " t=" + Fixed(t, 2) + " last=" + Point(f.last));
        std::lock_guard lock(gLastMutex);
        gLast.flight = f;
        f = {};
        return;
    }
    ++f.frames;
    f.maxHeight = std::max(f.maxHeight, p.z);
    if (f.frames == 1) {
        const float speed = Length(v);
        const Vec3 h = carry::Horizontal(v);
        f.heading = Length(h) > 1e-4f ? Scale(h, 1.0f / Length(h)) : Vec3{};
        if (f.haveFloor && speed > 1e-3f) {
            f.gameRange = grenade::RangeOnFloor(speed, std::asin(std::clamp(v.z / speed, -1.0f, 1.0f)),
                                                std::max(p.z - f.floorZ - kGrenadeRadius, 0.0f), f.modelGravity,
                                                f.modelDamping);
        }
    }
    if (gTrace.load() && f.frames <= 90) {
        Log("path entity=" + std::to_string(f.entity) + " frame=" + std::to_string(f.frames) + " t=" + Fixed(t, 4) +
            " p=" + Point(p) + " v=" + Point(v) + " w=" + Point(w));
    }
    if (f.frames == 2) {
        f.v2 = v;
        f.haveV2 = true;
        Log("flight entity=" + std::to_string(f.entity) + " v=" + Point(v) + " speed=" + Fixed(Length(v), 2) +
            " expected=" + Point(f.expected) + " expectedSpeed=" + Fixed(Length(f.expected), 2) + " pos=" + Point(p));
    }
    // Free flight against the measured model: each frame's velocity follows
    // dv = -(k v + g z) dt over the physics time the frame covered; a jump
    // away from that is the first contact.
    if (!f.contact && f.frames > 2) {
        const float speed = Length(f.lastV);
        const float dt = speed > 0.3f ? Length(Sub(p, f.last)) / speed : 0.0f;
        const Vec3 predicted =
            Sub(f.lastV, Scale(Add(Scale(f.lastV, f.modelDamping), Vec3{0, 0, f.modelGravity}), dt));
        if (dt > 0.0f && Length(Sub(v, predicted)) > 0.6f) {
            f.contact = true;
            f.contactAt = f.last;
            f.contactSeconds = static_cast<float>(f.lastNs - f.startNs) * 1e-9f;
            f.distance = Length(carry::Horizontal(Sub(f.contactAt, f.spawn)));
            f.onFloor = f.haveFloor && std::fabs(f.contactAt.z - f.floorZ) < 0.35f;
            if (f.onFloor) {
                const Vec3 d = carry::Horizontal(Sub(f.contactAt, f.spawn));
                f.range = Dot(d, f.heading);
                f.lateral = Length(Sub(d, Scale(f.heading, f.range)));
                if (f.gameRange >= 0) { f.rangeError = f.range - f.gameRange; }
            }
            Log("contact entity=" + std::to_string(f.entity) + " t=" + Fixed(f.contactSeconds, 3) + " at=" +
                Point(f.contactAt) + " distance=" + Fixed(f.distance, 2) + " onFloor=" + std::to_string(f.onFloor) +
                " range=" + Fixed(f.range, 2) + " gameRange=" + Fixed(f.gameRange, 2) + " realRange=" +
                Fixed(f.realRange, 2) + " rangeError=" + Fixed(f.rangeError, 3) + " lateral=" + Fixed(f.lateral, 3) +
                (f.fits ? " gravity=" + Fixed(f.gravitySum / f.fits, 2) + " damping=" +
                              Fixed(f.dampingSum / f.fits, 3) : std::string()) +
                " maxRise=" + Fixed(f.maxHeight - f.spawn.z, 2));
        } else if (dt > 0.002f && std::fabs(v.x) + std::fabs(v.y) > 0.5f) {
            // k from the horizontal decay, g from the vertical change net of it.
            const Vec3 h0 = carry::Horizontal(f.lastV), h1 = carry::Horizontal(v);
            const float k = (Length(h0) - Length(h1)) / (Length(h0) * dt);
            const float g = -(v.z - f.lastV.z) / dt - k * f.lastV.z;
            if (std::isfinite(k) && std::isfinite(g)) {
                f.dampingSum += k;
                f.gravitySum += g;
                ++f.fits;
            }
        }
    }
    if (!f.rest) {
        f.still = Length(v) < 0.05f ? f.still + 1 : 0;
        if (f.still >= 10) {
            f.rest = true;
            f.restAt = p;
            f.restSeconds = t;
            Log("rest entity=" + std::to_string(f.entity) + " t=" + Fixed(t, 2) + " at=" + Point(p) +
                " fromSpawn=" + Fixed(Length(carry::Horizontal(Sub(p, f.spawn))), 2));
        }
    }
    f.last = p;
    f.lastV = v;
    f.lastNs = now;
    {
        std::lock_guard lock(gLastMutex);
        gLast.flight = f;
    }
    if (t > 12.0f) { f = {}; }
}

std::string FlightLine(const Flight& f)
{
    std::string out = " flightEntity=" + std::to_string(f.entity) + " flightName=" + f.name +
                      " flightFrames=" + std::to_string(f.frames) + " spawn=" + Point(f.spawn) +
                      " expectedV=" + Point(f.expected);
    if (f.haveV2) { out += " v2=" + Point(f.v2) + " speed2=" + Fixed(Length(f.v2), 2); }
    if (f.fits) {
        out += " gravity=" + Fixed(f.gravitySum / f.fits, 2) + " damping=" + Fixed(f.dampingSum / f.fits, 3);
    }
    out += " modelGravity=" + Fixed(f.modelGravity, 2) + " modelDamping=" + Fixed(f.modelDamping, 3) +
           " gameRange=" + Fixed(f.gameRange, 2) + " realRange=" + Fixed(f.realRange, 2);
    if (f.contact) {
        out += " contactAt=" + Point(f.contactAt) + " contactT=" + Fixed(f.contactSeconds, 3) +
               " distance=" + Fixed(f.distance, 2) + " onFloor=" + std::to_string(f.onFloor);
        if (f.onFloor) {
            out += " range=" + Fixed(f.range, 2) + " rangeError=" + Fixed(f.rangeError, 3) + " lateral=" +
                   Fixed(f.lateral, 3);
        }
    }
    if (f.rest) { out += " restAt=" + Point(f.restAt) + " restT=" + Fixed(f.restSeconds, 2); }
    if (f.gone) { out += " goneT=" + Fixed(f.goneSeconds, 2); }
    out += " maxRise=" + Fixed(f.maxHeight - f.spawn.z, 2);
    return out;
}

// The equipped grenade, now: where the game would spawn it against the hand.
std::string HoldLine()
{
    const auto player = CallGetPlayer();
    const auto weapon = PlayerGrenade(player);
    if (!weapon) { return " equipped=none"; }
    std::string out = " equipped=grenade";
    std::uintptr_t entity = 0;
    char name[64]{};
    if (Read(weapon + kWeaponEntity, entity) && CopyName(CallArchetypeName(entity), name, sizeof(name))) {
        out += std::string(" weaponArchetype=") + name;
    }
    float minS = 0, maxS = 0, remaining = 0, duration = 0;
    std::uint8_t charging = 0, deploy = 0;
    Read(weapon + kMinSpeed, minS);
    Read(weapon + kMaxSpeed, maxS);
    Read(weapon + kChargeRemaining, remaining);
    Read(weapon + kChargeDuration, duration);
    Read(weapon + kCharging, charging);
    Read(weapon + kDeployReady, deploy);
    out += " minSpeed=" + Fixed(minS, 2) + " maxSpeed=" + Fixed(maxS, 2) + " chargeRemaining=" + Fixed(remaining, 2) +
           " chargeDuration=" + Fixed(duration, 2) + " charging=" + std::to_string(charging) + " deployReady=" +
           std::to_string(deploy);
    float deployRange = 0;
    Read(weapon + kDeployRange, deployRange);
    out += " deployRange=" + Fixed(deployRange, 2);
    Vec3 firing{};
    bool fallback = false;
    GameplayPoseFrame f{};
    Pose grip{};
    if (CallFiringPosition(weapon, firing, fallback)) {
        out += " firing=" + Point(firing) + " firingFallback=" + std::to_string(fallback);
        if (TryGetGameplayPoseFrame(f, true) && RightGrip(f, grip)) {
            // The firing position in the grip's frame (OpenXR axes: +X right,
            // +Y up, -Z forward along the handle), metres.
            const Vec3 d = Sub(firing, grip.position);
            const Vec3 local = Rotate(stereo::Conjugate(stereo::kOpenXrToEngine), Rotate(carry::Inverse(grip.orientation), d));
            out += " grip=" + Point(grip.position) + " firingFromGripM=" + Fixed(Length(d)) +
                   " firingInGrip=" + Point(local) + " fist=" + Point(FistPoint(grip)) +
                   " firingFromFistM=" + Fixed(Length(Sub(firing, FistPoint(grip))));
        }
    }
    return out;
}

}  // namespace

// --- public ---------------------------------------------------------------------------------

bool EnsureGrenadeLane()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstallTried) { return gInstalled; }
    gInstallTried = true;
    gBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if (!gBase) {
        gInstallTried = false;
        return false;
    }
    gGiveOk = std::memcmp(reinterpret_cast<const void*>(gBase + kGiveRva), kGivePrologue.data(), kGivePrologue.size()) == 0;
    EnsureMinHook();
    gInstalled = HookAt(kThrowRva, kThrowPrologue, reinterpret_cast<void*>(&ThrowHook), gThrow, "throw") &&
                 HookAt(kCreateRva, kCreatePrologue, reinterpret_cast<void*>(&CreateHook), gCreate, "create") &&
                 HookAt(kReleaseRva, kReleasePrologue, reinterpret_cast<void*>(&ReleaseHook), gRelease, "release") &&
                 HookAt(kUpdateRva, kUpdatePrologue, reinterpret_cast<void*>(&UpdateHook), gUpdate, "update");
    Log(std::string("result=") + (gInstalled ? "0" : "unavailable") + " give=" + std::to_string(gGiveOk));
    return gInstalled;
}

DWORD SetGrenadeHand(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    if (enabled && !EnsureGrenadeLane()) { return ERROR_NOT_SUPPORTED; }
    gHand.store(enabled != 0);
    Log("result=0 detail=hand value=" + std::to_string(enabled));
    return 0;
}
unsigned GrenadeHandEnabled() { return gHand.load() ? 1u : 0u; }
bool GrenadeHeldByHand(std::uintptr_t weapon) { return gHand.load() && gInstalled && IsGrenadeWeapon(weapon); }

void NoteGrenadeTrigger(bool pressed)
{
    const auto now = MonotonicNanoseconds();
    std::lock_guard lock(gTriggerMutex);
    if (pressed) {
        gPressNs = now;
        return;
    }
    TriggerRelease r{};
    r.ns = now;
    r.pressNs = gPressNs;
    // The release counts even without a motion estimate (tracking lost): it
    // is still WHEN the grenade leaves; without motion it is dropped.
    EstimateHandMotion(static_cast<unsigned>(Hand::right), r.motion);
    r.valid = true;
    GameplayPoseFrame f{};
    if (TryGetGameplayPoseFrame(f, false) && std::isfinite(f.yaw)) {
        r.yaw = f.yaw;
        r.yawValid = true;
    }
    gTrigger = r;
}

void UpdateGrenadeLane(const GameplayPoseFrame& frame, bool)
{
    if (!gInstallTried) { return; }
    RunGive(frame.player);
    RetryEquip(frame.player);
    WatchFlight();
}

std::string GrenadeReport()
{
    Last last{};
    {
        std::lock_guard lock(gLastMutex);
        last = gLast;
    }
    std::ostringstream out;
    const auto s = Settings();
    out << " installed=" << gInstalled << " hand=" << gHand.load() << " early=" << gEarly.load()
        << " deploy=" << gDeploy.load() << " deploySuppressedFrames=" << gDeploySuppressed.load()
        << " spawnMode=" << gSpawnMode.load() << " giveOk=" << gGiveOk << " throws=" << gThrows.load()
        << " steered=" << gSteered.load() << " earlyThrows=" << gEarlyThrows.load()
        << " eventThrows=" << gEventThrows.load() << " otherThrows=" << gNativeThrows.load()
        << " eventsSkipped=" << gEventsSkipped.load() << " releases=" << gReleases.load()
        << " creates=" << gCreates.load() << " velocitySets=" << gVelocitySets.load() << " faults=" << gFaults.load()
        << " noRelease=" << gNoRelease.load() << " pulledBackCount=" << gPulledBack.load()
        << " vrFactor=" << Fixed(s.vrFactor, 2) << " parity=" << s.parity << " fixedGain=" << Fixed(s.fixedGain, 2)
        << " modelGravity=" << Fixed(s.gravity, 2) << " modelDamping=" << Fixed(s.damping, 2)
        << " dropSpeed=" << Fixed(s.dropSpeed, 2)
        << " throwSpeed=" << Fixed(s.throwSpeed, 2) << " maxOverNative=" << Fixed(s.maxOverNative, 2)
        << " lever=" << s.lever << " lastGive={" << gGiveResult << "}";
    Vec3 feet{};
    if (PlayerFeet(feet)) { out << " feet=" << Point(feet) << " playerV=" << Point(PlayerVelocity()); }
    out << HoldLine();
    if (last.valid) {
        const Scope& c = last.scope;
        out << " lastHow=" << (c.early ? "early" : c.fromEvent ? "event" : "other") << " lastSteer=" << c.steer
            << " delayMs=" << Fixed(last.delayMs, 1) << " pressedMs=" << Fixed(last.pressedMs, 1)
            << " nativePos=" << Point(c.nativePosition) << " nativeDir=" << Point(c.nativeDirection)
            << " pos=" << Point(c.position) << " dir=" << Point(c.direction) << " lastGrip=" << Point(c.grip)
            << " pulledBack=" << Fixed(c.pulledBack) << " handWorld=" << Point(c.handWorld)
            << " spinWorld=" << Point(c.spinWorld) << " handSpeed=" << Fixed(c.plan.handSpeed, 2)
            << " objectSpeed=" << Fixed(c.plan.objectSpeed, 2) << " appliedGain=" << Fixed(c.plan.gain, 2)
            << " speed=" << Fixed(c.plan.speed, 2) << " limit=" << Fixed(c.plan.limit, 2)
            << " limited=" << c.plan.limited << " thrown=" << c.plan.thrown << " playerV=" << Point(c.player)
            << " velocity=" << Point(c.plan.velocity) << " angular=" << Point(c.plan.angular)
            << " nativeMin=" << Fixed(c.nativeMin, 2) << " nativeMax=" << Fixed(c.nativeMax, 2)
            << " charge=" << Fixed(c.chargeElapsed, 2) << "/" << Fixed(c.chargeDuration, 2)
            << " motionValid=" << c.release.motion.valid << " motionRuntime=" << c.release.motion.fromRuntime
            << " motionSamples=" << c.release.motion.samples;
    }
    out << FlightLine(last.flight);
    return out.str();
}

bool ExecuteGrenadeCommand(const std::vector<std::string>& args, std::ostringstream& out)
{
    const std::string& verb = args[0];
    if (verb.rfind("grenade.", 0) != 0) { return false; }
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
    if (verb == "grenade.hand") {
        if (args.size() > 1) { result = SetGrenadeHand(static_cast<unsigned>(arg(1, 1))); }
    } else if (verb == "grenade.early") {
        if (args.size() > 1) { gEarly.store(arg(1, 1) != 0); }
    } else if (verb == "grenade.spawn") {
        if (args.size() > 1) { gSpawnMode.store(std::clamp(arg(1, 0), 0, 2)); }
    } else if (verb == "grenade.deploy") {
        if (args.size() > 1) { gDeploy.store(arg(1, 0) != 0); }
    } else if (verb == "grenade.trace") {
        if (args.size() > 1) { gTrace.store(arg(1, 1) != 0); }
    } else if (verb == "grenade.throw") {
        // grenade.throw <vrFactor% 130> <parity 1> <dropCm/s 80> <throwCm/s 200> <maxOverNative% 115>
        //               <lever 1> <fixedGain% 150>
        if (args.size() > 1) {
            std::lock_guard lock(gSettingsMutex);
            gSettings.vrFactor = std::clamp(arg(1, 130), 50, 400) * .01f;
            gSettings.parity = arg(2, 1) != 0;
            gSettings.dropSpeed = std::clamp(arg(3, 80), 0, 1000) * .01f;
            gSettings.throwSpeed = std::max(gSettings.dropSpeed, std::clamp(arg(4, 200), 0, 1000) * .01f);
            gSettings.maxOverNative = std::clamp(arg(5, 115), 50, 400) * .01f;
            gSettings.lever = arg(6, 1) != 0;
            gSettings.fixedGain = std::clamp(arg(7, 150), 50, 400) * .01f;
        }
    } else if (verb == "grenade.physics") {
        // grenade.physics <gravity cm/s2 1270> <damping % per s 85>: the grenade's own, for parity.
        if (args.size() > 1) {
            std::lock_guard lock(gSettingsMutex);
            gSettings.gravity = std::clamp(arg(1, 1270), 100, 5000) * .01f;
            gSettings.damping = std::clamp(arg(2, 85), 0, 500) * .01f;
        }
    } else if (verb == "grenade.give") {
        // grenade.give <emp|recycler|nullwave|lure|archetype> [count 3] [equip 1] (test support)
        if (!EnsureGrenadeLane() || !gGiveOk) {
            result = ERROR_NOT_SUPPORTED;
        } else {
            std::string name = args.size() > 1 ? args[1] : "emp";
            if (name == "emp") { name = "ArkSpecialWeapons.EMPGrenadeWeapon"; }
            else if (name == "recycler") { name = "ArkSpecialWeapons.RecyclerGrenadeWeapon"; }
            else if (name == "nullwave") { name = "ArkSpecialWeapons.NullwaveTransmitterWeapon"; }
            else if (name == "lure") { name = "ArkSpecialWeapons.LureGrenadeWeapon"; }
            std::lock_guard lock(gGiveMutex);
            gGive = {};
            gGive.active = true;
            std::snprintf(gGive.archetype, sizeof(gGive.archetype), "%s", name.c_str());
            gGive.count = std::clamp(arg(2, 3), 1, 20);
            gGive.equip = arg(3, 1) != 0;
            out << verb << " result=0 queued=" << gGive.archetype;
            return true;
        }
    } else if (verb != "grenade.report") {
        out << verb << " result=" << ERROR_INVALID_FUNCTION;
        return true;
    }
    out << verb << " result=" << result << GrenadeReport();
    return true;
}

}  // namespace preyvr::dll
