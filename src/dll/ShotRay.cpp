#include "ShotRay.h"

#include "AimTakeover.h"
#include "HeadTrackingHook.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "SceneQuery.h"
#include "WeaponAttachment.h"
#include "preyvr/AnimIk.h"
#include "preyvr/EngineMap.h"
#include "preyvr/FiringPosition.h"
#include "preyvr/LatestSnapshot.h"

#include <MinHook.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>

namespace preyvr::dll {
namespace {

void Log(const std::string& line) { lifecycle::Log("preyvr_shot " + line); }

// --- native contracts (Steam PreyDll.dll, SHA-256 7d6e322f...) ---------------
//
// GetReticleInfoForFiring 0x1694890 (R-014): RCX weapon, RDX caller-owned
// ReticleInfo {IEntity* +0, Vec3 target +8}; reads the cached ray through the
// player's getter at call time, raycasts origin + direction * weapon+0x344.
constexpr std::uintptr_t kQueryRva = 0x1694890;
constexpr std::array<std::uint8_t, 24> kQueryPrologue{
    0x48, 0x89, 0x74, 0x24, 0x18, 0x55, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x6C,
    0x24, 0xB0, 0x48, 0x81, 0xEC, 0x50, 0x01, 0x00, 0x00, 0x45, 0x33, 0xF6};
// GetFiringPosition 0x1694BC0: (weapon, out16, flags, overrideEntity). Called
// through its hooked entry so the VR spawn test applies to our call too.
constexpr std::uintptr_t kFiringPositionRva = 0x1694BC0;
// Helper world TM 0x11A5CF0: (Matrix34* out, IEntity*, slot, const char* name,
// bool, bool) -> out. GetFiringPosition calls it with (entity = weapon+0x40,
// slot 0, name = weapon+0x2F0, 1, 0); we make exactly that call.
constexpr std::uintptr_t kHelperWorldRva = 0x11A5CF0;
constexpr std::array<std::uint8_t, 22> kHelperWorldPrologue{
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x70, 0x18, 0x55, 0x57, 0x41, 0x54,
    0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xA8, 0xE8, 0xFE, 0xFF, 0xFF};
constexpr std::size_t kWeaponEntity = 0x40;
constexpr std::size_t kWeaponAmmoHelperName = 0x2F0;
// Passive projectile observers, to measure the shot that really left:
// the projectile spawner 0x1677040 (RCX launcher, RDX &position, R8 &direction,
// six more): the GLOO calls it at 0x169F5A2 right after its query, and the
// pellet function below calls it once per projectile (the crossbow's dart).
constexpr std::uintptr_t kProjectileSpawnRva = 0x1677040;
constexpr std::array<std::uint8_t, 23> kProjectileSpawnPrologue{
    0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x40, 0x18, 0x55, 0x57, 0x48, 0x8D, 0xA8,
    0x48, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xA8, 0x01, 0x00, 0x00};
// Shotgun-family pellet spawn 0x16AAB70 (RCX weapon, RDX &firing position,
// R8 &muzzle rotation, R9 &target, three more), called at 0x16AB451.
constexpr std::uintptr_t kPelletsRva = 0x16AAB70;
constexpr std::array<std::uint8_t, 23> kPelletsPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x4C, 0x89,
    0x44, 0x24, 0x18, 0x55, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57};

// IArkPlayer::GetReticleViewPositionAndDir 0x157CBB0 (R-013): RCX = player+0x40,
// RDX = 6 floats out (origin, direction). Every native reader of the cached ray
// calls it; the probe below records who, to find firing routes that do not
// go through GetReticleInfoForFiring.
constexpr std::uintptr_t kGetterRva = 0x157CBB0;
constexpr std::array<std::uint8_t, 14> kGetterPrologue{
    0x8B, 0x81, 0x94, 0x17, 0x00, 0x00, 0x89, 0x02, 0x8B, 0x81, 0x98, 0x17, 0x00, 0x00};
using GetterFn = void*(__fastcall*)(void* player, void* out);
using QueryFn = void*(__fastcall*)(void* weapon, void* out);
using FiringPositionFn = void*(__fastcall*)(void*, void*, std::uint32_t, void*);
using HelperWorldFn = void*(__fastcall*)(void* out, void* entity, int slot, const char* name, bool, bool);
using ProjectileSpawnFn = void*(__fastcall*)(void*, void*, void*, std::uint64_t, std::uint64_t, std::uint64_t,
                                       std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
using PelletsFn = void*(__fastcall*)(void*, void*, void*, void*, std::uint64_t, std::uint64_t, std::uint64_t);

std::uintptr_t gBase = 0;
std::atomic<QueryFn> gQueryOriginal{nullptr};
std::atomic<ProjectileSpawnFn> gProjectileOriginal{nullptr};
std::atomic<PelletsFn> gPelletsOriginal{nullptr};
std::atomic<GetterFn> gGetterOriginal{nullptr};
bool gGetterHooked = false;
std::atomic<bool> gProbe{false};
struct CallerSlot { std::atomic<std::uint32_t> rva{0}; std::atomic<unsigned long long> count{0}; };
std::array<CallerSlot, 64> gCallers{};
std::atomic<unsigned long long> gCallersOverflow{0};
// **The shot scope.** The pistol/shotgun/toy-gun route reads the cached ray a
// second time while spawning: the pellet function centres its pattern on
// CArkWeapon::GetReticlePosition (0x1694A20 = eye + direction * reticle range)
// unless aim assist or a direct hit replaces it (0x16A7050). That point is on
// the EYE's ray, so the crossbow's dart still converged from the eye, ~1.2
// degrees at 2 m (measured 2026-10-06). Between the steered query and the
// return of the pellet function, on that thread, every read of the cached ray
// gets the shot's origin instead. Nothing outside the shot sees it.
struct Scope { bool active = false; Vec3 origin{}; std::uint64_t ns = 0; };
thread_local Scope tScope;
std::atomic<unsigned long long> gScopedReads{0};
HelperWorldFn gHelperWorld = nullptr;
std::mutex gInstallMutex;
std::atomic<bool> gInstallTried{false};
bool gQueryHooked = false, gProjectileHooked = false, gPelletsHooked = false, gHelperOk = false;

std::atomic<bool> gEnabled{false};
std::atomic<unsigned long long> gFramesMuzzle{0}, gFramesNoMuzzle{0};
std::atomic<unsigned long long> gQueries{0}, gSteered{0}, gSkipNoFrame{0}, gSkipNotEquipped{0}, gSkipNoAim{0},
    gSkipNoFiring{0}, gSkipDirection{0};
std::atomic<unsigned long long> gSpawnNative{0}, gSpawnRestored{0}, gSpawnBlocked{0};
std::atomic<unsigned long long> gProjectiles{0}, gProjectilesUnseen{0};
// Per-frame diagnostics, mm and millidegrees.
std::atomic<int> gMuzzleOffAimMm{-1}, gMuzzleAlongAimMm{0}, gBarrelAxisMdeg{-1}, gMuzzleFromGripMm{-1};
LatestSnapshot<ShotTrace> gLastShot;
std::atomic<unsigned long long> gShotIndex{0};

bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Len(Vec3 a) { return std::sqrt(Dot(a, a)); }
std::string Point(Vec3 v)
{
    char text[64];
    std::snprintf(text, sizeof(text), "%.3f,%.3f,%.3f", v.x, v.y, v.z);
    return text;
}
std::string Fixed(float v, int decimals)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, static_cast<double>(v));
    return text;
}

// The equipped weapon's ammo helper in world space, by the same native call the
// firing position makes. False for a weapon without one (the wrench).
bool ReadMuzzleRaw(std::uintptr_t weapon, float* matrix)
{
    __try {
        void* const entity = *reinterpret_cast<void* const*>(weapon + kWeaponEntity);
        const char* const name = *reinterpret_cast<const char* const*>(weapon + kWeaponAmmoHelperName);
        if (!entity || !name || !name[0]) { return false; }
        const auto* const result = static_cast<const float*>(gHelperWorld(matrix, entity, 0, name, true, false));
        if (!result) { return false; }
        if (result != matrix) { std::memcpy(matrix, result, 12 * sizeof(float)); }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool ReadMuzzle(std::uintptr_t weapon, Vec3& position, Vec3& axis)
{
    if (!gHelperOk || !weapon) { return false; }
    alignas(16) float m[16]{};
    if (!ReadMuzzleRaw(weapon, m)) { return false; }
    // Matrix34, row-major: translation in column 3, the authored +Y in column 1.
    position = {m[3], m[7], m[11]};
    axis = {m[1], m[5], m[9]};
    return Finite(position) && Finite(axis);
}

bool CopyBytes(void* destination, const void* source, std::size_t size)
{
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool FiringPositionNow(void* weapon, std::uint32_t flags, FiringPosition& out)
{
    alignas(16) std::array<std::uint8_t, 32> buffer{};
    const auto call = reinterpret_cast<FiringPositionFn>(gBase + kFiringPositionRva);
    void* result = nullptr;
    __try {
        result = call(weapon, buffer.data(), flags, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (result != buffer.data()) { return false; }
    const auto decoded = DecodeFiringPosition(std::span<const std::uint8_t>(buffer.data(), 16));
    if (!decoded) { return false; }
    out = *decoded;
    return true;
}

// The right controller's aim pose in engine world space, on the anchor the IK
// places the drawn hand with.
bool AimWorld(const GameplayPoseFrame& frame, Pose& out)
{
    const auto& hand = frame.tracking.hands[static_cast<unsigned>(Hand::right)];
    if (!frame.cameraCentreValid || !IsPoseUsable(hand.aimPose, hand.aimValidity, 200000000ull)) { return false; }
    out = animik::ControllerWorldFromHead(frame.yaw, frame.cameraCentre, frame.tracking.head, hand.aimPose);
    return Finite(out.position);
}
bool GripWorld(const GameplayPoseFrame& frame, Vec3& out)
{
    const auto& hand = frame.tracking.hands[static_cast<unsigned>(Hand::right)];
    if (!frame.cameraCentreValid || !IsPoseUsable(hand.gripPose, hand.gripValidity, 200000000ull)) { return false; }
    out = animik::ControllerWorldFromHead(frame.yaw, frame.cameraCentre, frame.tracking.head, hand.gripPose).position;
    return Finite(out);
}

// The aim lane's last frame and sample, kept whole for the shot hooks: a
// try-lock read of the published snapshots can miss under contention, and a
// shot must not go unsteered for that.
struct ShotContext { GameplayPoseFrame frame{}; aim::Sample sample{}; std::uint64_t ns = 0; };
std::mutex gContextMutex;
ShotContext gContext;
bool ReadContext(ShotContext& out)
{
    std::lock_guard lock(gContextMutex);
    if (!gContext.ns || !FreshSample(MonotonicNanoseconds(), gContext.ns, 150000000ull)) { return false; }
    out = gContext;
    return true;
}

// A shot between its query and its projectile, on the thread that fires it.
struct Pending {
    bool active = false;
    ShotTrace trace{};
    Vec3 aimOrigin{};       // controller aim point, for "how far off the drawn line"
    bool haveAim = false;
    Vec3 queryOrigin{};     // the origin the native query raycast from (eye, or the spawn when steered)
    Vec3 projectileTarget{};
    bool haveProjectileTarget = false;
    Vec3 projectileDirection{};   // as the spawn received it (before any spread)
    bool haveProjectileDirection = false;
    std::uint64_t ns = 0;
};
thread_local Pending tPending;
// The last firing-position decision on this thread. The GLOO asks for its firing
// position before its query, the other routes after, so it is kept apart.
struct SpawnRecord { shot::Spawn kind = shot::Spawn::Native; std::uint64_t ns = 0; };
thread_local SpawnRecord tSpawn;

void Finish(Pending& p, bool seen)
{
    if (!p.active) { return; }
    p.active = false;
    auto& t = p.trace;
    t.projectileSeen = seen;
    if (tSpawn.ns && tSpawn.ns + 50000000ull > p.ns) { t.spawnKind = tSpawn.kind; }
    (seen ? gProjectiles : gProjectilesUnseen).fetch_add(1, std::memory_order_relaxed);
    if (p.haveAim) { t.spawnOffLineMm = shot::DistanceToLine(t.spawn, p.aimOrigin, t.direction) * 1000.0f; }
    gLastShot.Publish(t);
    const float distance = Len(Sub(t.target, t.spawn));
    std::ostringstream line;
    line << "n=" << gShotIndex.fetch_add(1) + 1 << " route=" << shot::RouteName(t.route)
         << " steered=" << (gEnabled.load() ? 1 : 0) << " spawnKind=" << shot::SpawnName(t.spawnKind)
         << " projectile=" << (seen ? "seen" : "unseen") << " spawn=" << Point(t.spawn)
         << " dir=" << Point(t.direction) << " target=" << Point(t.target) << " hit=" << t.entityHit
         << " dist=" << Fixed(distance, 2) << " angleDeg=" << Fixed(t.angleDegrees, 3)
         << " spawnOffAimMm=" << Fixed(t.spawnOffLineMm, 1) << " queryOrigin=" << Point(p.queryOrigin)
         << " spawnFromQueryMm=" << Fixed(Len(Sub(t.spawn, p.queryOrigin)) * 1000.0f, 1);
    if (p.haveProjectileDirection && Len(p.projectileDirection) > 1e-6f) {
        // Split the projectile's deviation from the aim into up and sideways,
        // CryEngine Z-up: a weapon that lobs to compensate gravity tilts UP only.
        const Vec3 f = Scale(p.projectileDirection, 1.0f / Len(p.projectileDirection));
        const Vec3 d = t.direction;
        Vec3 side{d.y, -d.x, 0};
        const float sideLength = Len(side);
        if (sideLength > 1e-4f) {
            side = Scale(side, 1.0f / sideLength);
            const Vec3 up{side.y * d.z, -side.x * d.z, side.x * d.y - side.y * d.x};   // side x d: up for d=+Y, side=+X
            line << " projDir=" << Point(f) << " upDeg=" << Fixed(std::asin(std::clamp(Dot(f, up), -1.f, 1.f)) * 57.2958f, 3)
                 << " sideDeg=" << Fixed(std::asin(std::clamp(Dot(f, side), -1.f, 1.f)) * 57.2958f, 3);
        }
    }
    if (p.haveProjectileTarget) {
        // A projectile route may move the target after the query (pellets get
        // it through the point-blank handler first).
        line << " projTarget=" << Point(p.projectileTarget)
             << " projTargetMovedMm=" << Fixed(Len(Sub(p.projectileTarget, t.target)) * 1000.0f, 1);
    }
    Log(line.str());
}

void* __fastcall QueryHook(void* weapon, void* out)
{
    const auto original = gQueryOriginal.load(std::memory_order_acquire);
    const auto rva = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - gBase);
    // A query whose projectile never reported (an unobserved route) closes now.
    Finish(tPending, false);
    gQueries.fetch_add(1, std::memory_order_relaxed);
    const bool steer = gEnabled.load(std::memory_order_acquire);

    ShotContext ctx{};
    EquippedRig rig{};
    Pending p{};
    p.trace.route = shot::RouteFromReturnRva(rva);
    p.ns = MonotonicNanoseconds();
    const char* skip = nullptr;
    if (!ReadContext(ctx)) { gSkipNoFrame.fetch_add(1); skip = "no_frame"; }
    else if (!TryGetEquippedRig(ctx.frame.player, rig) || rig.weapon != reinterpret_cast<std::uintptr_t>(weapon)) {
        gSkipNotEquipped.fetch_add(1); skip = "not_equipped";
    } else if (!aim::Usable(ctx.sample, WeaponEquipGeneration(), HeadTrackingReferenceGeneration(), p.ns)) {
        gSkipNoAim.fetch_add(1); skip = "no_aim";
    }
    if (skip) {
        Log(std::string("skip reason=") + skip + " route=" + shot::RouteName(p.trace.route) +
            " thread=" + std::to_string(GetCurrentThreadId()));
        return original(weapon, out);
    }
    const GameplayPoseFrame& frame = ctx.frame;
    const aim::Sample& sample = ctx.sample;

    auto* const originAt = reinterpret_cast<std::uint8_t*>(frame.player + engine::ArkPlayerLayout::cachedReticleOrigin);
    const auto* const directionAt =
        reinterpret_cast<const std::uint8_t*>(frame.player + engine::ArkPlayerLayout::cachedReticleDirection);
    Vec3 eye{}, direction{};
    std::memcpy(&eye, originAt, sizeof(eye));
    std::memcpy(&direction, directionAt, sizeof(direction));
    p.trace.direction = direction;
    Pose aim{};
    p.haveAim = AimWorld(frame, aim);
    p.aimOrigin = aim.position;

    FiringPosition fire{};
    bool steered = false;
    void* result = nullptr;
    if (steer) {
        // The direction the query is about to use must be the aim lane's: a frame
        // the lane refused leaves the head's ray in the cache.
        if (shot::AngleDegrees(direction, sample.direction) > .5f) {
            gSkipDirection.fetch_add(1);
        } else if (!FiringPositionNow(weapon, shot::FiringPositionFlags(p.trace.route), fire)) {
            gSkipNoFiring.fetch_add(1);
        } else {
            std::memcpy(originAt, &fire.origin, sizeof(fire.origin));
            result = original(weapon, out);
            std::memcpy(originAt, &eye, sizeof(eye));
            steered = true;
            if (p.trace.route == shot::Route::Ballistic) { tScope = {true, fire.origin, MonotonicNanoseconds()}; }
            gSteered.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (!steered) { result = original(weapon, out); }

    std::uint8_t info[0x14]{};
    if (result && CopyBytes(info, result, sizeof(info))) {
        void* entity = nullptr;
        std::memcpy(&entity, info, sizeof(entity));
        std::memcpy(&p.trace.target, info + 8, sizeof(Vec3));
        p.trace.entityHit = entity != nullptr;
        // Until a projectile reports, the query's own spawn stands for it.
        p.queryOrigin = steered ? fire.origin : eye;
        p.trace.spawn = p.queryOrigin;
        p.trace.angleDegrees = shot::AngleDegrees(Sub(p.trace.target, p.trace.spawn), direction);
        p.trace.ns = p.ns;
        p.trace.valid = Finite(p.trace.target);
        p.active = p.trace.valid;
        tPending = p;
    }
    return result;
}

// Projectile observers: they only read, and only finish a shot this thread's
// query opened moments ago.
void ObserveProjectile(Vec3 spawn, Vec3 flight, const Vec3* aimedTarget = nullptr)
{
    auto& p = tPending;
    if (!p.active || MonotonicNanoseconds() - p.ns > 20000000ull) { return; }
    if (!Finite(spawn) || !Finite(flight) || !(Len(flight) > 1e-6f)) { return; }
    p.trace.spawn = spawn;
    p.projectileDirection = flight;
    p.haveProjectileDirection = true;
    if (aimedTarget) { p.projectileTarget = *aimedTarget; p.haveProjectileTarget = true; }
    p.trace.angleDegrees = shot::AngleDegrees(flight, p.trace.direction);
    Finish(p, true);
}

void* __fastcall ProjectileSpawnHook(void* launcher, void* position, void* direction, std::uint64_t a4, std::uint64_t a5,
                               std::uint64_t a6, std::uint64_t a7, std::uint64_t a8, std::uint64_t a9,
                               std::uint64_t a10)
{
    Vec3 spawn{}, flight{};
    const bool read = CopyBytes(&spawn, position, sizeof(spawn)) && CopyBytes(&flight, direction, sizeof(flight));
    void* result = gProjectileOriginal.load(std::memory_order_acquire)(launcher, position, direction, a4, a5, a6, a7, a8,
                                                                 a9, a10);
    if (read) { ObserveProjectile(spawn, flight); }
    return result;
}

void* __fastcall PelletsHook(void* weapon, void* position, void* rotation, void* target, std::uint64_t a5,
                             std::uint64_t a6, std::uint64_t a7)
{
    Vec3 spawn{}, aimed{};
    const bool read = CopyBytes(&spawn, position, sizeof(spawn)) && CopyBytes(&aimed, target, sizeof(aimed));
    void* result = gPelletsOriginal.load(std::memory_order_acquire)(weapon, position, rotation, target, a5, a6, a7);
    tScope.active = false;
    // Pellets fly from the firing position toward the target, then the
    // weapon's own spread; the centre line is what is aimed.
    if (read) { ObserveProjectile(spawn, Sub(aimed, spawn), &aimed); }
    return result;
}

void RecordCaller(std::uint32_t rva)
{
    for (auto& slot : gCallers) {
        std::uint32_t current = slot.rva.load(std::memory_order_acquire);
        if (current == 0) {
            std::uint32_t expected = 0;
            if (slot.rva.compare_exchange_strong(expected, rva)) { current = rva; }
            else { current = expected; }
        }
        if (current == rva) { slot.count.fetch_add(1, std::memory_order_relaxed); return; }
    }
    gCallersOverflow.fetch_add(1, std::memory_order_relaxed);
}

void* __fastcall GetterHook(void* player, void* out)
{
    void* const result = gGetterOriginal.load(std::memory_order_acquire)(player, out);
    if (tScope.active) {
        if (result == out && MonotonicNanoseconds() - tScope.ns < 20000000ull &&
            CopyBytes(out, &tScope.origin, sizeof(tScope.origin))) {
            gScopedReads.fetch_add(1, std::memory_order_relaxed);
        } else {
            tScope.active = false;   // stale: never outlive the shot
        }
    }
    if (gProbe.load(std::memory_order_relaxed)) {
        RecordCaller(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - gBase));
    }
    return result;
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

bool Install()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstallTried) { return gQueryHooked && gHelperOk; }
    gInstallTried = true;
    gBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if (!gBase) { gInstallTried = false; return false; }
    EnsureMinHook();
    gHelperOk = std::memcmp(reinterpret_cast<const void*>(gBase + kHelperWorldRva), kHelperWorldPrologue.data(),
                            kHelperWorldPrologue.size()) == 0;
    if (gHelperOk) { gHelperWorld = reinterpret_cast<HelperWorldFn>(gBase + kHelperWorldRva); }
    // The firing-position hook carries the VR spawn test; without it shots are
    // still steered, only the camera fallback stays the engine's.
    const bool firing = EnsureFiringPositionHook();
    gQueryHooked = HookAt(kQueryRva, kQueryPrologue, reinterpret_cast<void*>(&QueryHook), gQueryOriginal, "query");
    gProjectileHooked = HookAt(kProjectileSpawnRva, kProjectileSpawnPrologue, reinterpret_cast<void*>(&ProjectileSpawnHook), gProjectileOriginal,
                         "projectile_spawn");
    gPelletsHooked = HookAt(kPelletsRva, kPelletsPrologue, reinterpret_cast<void*>(&PelletsHook), gPelletsOriginal,
                            "pellets");
    gGetterHooked = HookAt(kGetterRva, kGetterPrologue, reinterpret_cast<void*>(&GetterHook), gGetterOriginal,
                           "ray_getter");
    Log(std::string("result=") + (gQueryHooked && gHelperOk ? "0" : "unavailable") +
        " query=" + std::to_string(gQueryHooked) + " helper=" + std::to_string(gHelperOk) +
        " firing=" + std::to_string(firing) + " projectileSpawn=" + std::to_string(gProjectileHooked) +
        " pellets=" + std::to_string(gPelletsHooked));
    return gQueryHooked && gHelperOk;
}

}  // namespace

DWORD SetShotRay(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    // Installing is also how `aim.shot 0` measures the native behaviour: the
    // hooks observe either way and only steer when enabled.
    if (!Install()) { return ERROR_NOT_SUPPORTED; }
    gEnabled.store(enabled != 0, std::memory_order_release);
    Log(std::string("result=0 detail=enabled value=") + (enabled ? "1" : "0"));
    return 0;
}

unsigned ShotRayEnabled() { return gEnabled.load(std::memory_order_relaxed) ? 1u : 0u; }

bool ShotRayFrame(const GameplayPoseFrame& frame, aim::Sample& sample)
{
    // A shot whose projectile never reported closes on the next frame.
    if (tPending.active && MonotonicNanoseconds() - tPending.ns > 50000000ull) { Finish(tPending, false); }
    tScope.active = false;
    struct Store {
        const GameplayPoseFrame& frame; const aim::Sample& sample;
        ~Store()
        {
            // The aim lane stamps its sample after this; the copy is stamped here.
            const auto now = MonotonicNanoseconds();
            std::lock_guard lock(gContextMutex);
            gContext = {frame, sample, now};
            gContext.sample.publishedNs = now;
        }
    } store{frame, sample};   // after any edit below
    if (!gEnabled.load(std::memory_order_acquire)) { return false; }
    EquippedRig rig{};
    Vec3 muzzle{}, axis{};
    if (!TryGetEquippedRig(frame.player, rig) || !ReadMuzzle(rig.weapon, muzzle, axis)) {
        gFramesNoMuzzle.fetch_add(1, std::memory_order_relaxed);
        gMuzzleOffAimMm.store(-1, std::memory_order_relaxed);
        return false;
    }
    sample.origin = muzzle;
    sample.muzzleOrigin = true;
    gFramesMuzzle.fetch_add(1, std::memory_order_relaxed);
    Pose aim{};
    if (AimWorld(frame, aim)) {
        gMuzzleOffAimMm.store(static_cast<int>(shot::DistanceToLine(muzzle, aim.position, sample.direction) * 1000.0f),
                              std::memory_order_relaxed);
        gMuzzleAlongAimMm.store(static_cast<int>(Dot(Sub(muzzle, aim.position), sample.direction) * 1000.0f),
                                std::memory_order_relaxed);
    }
    Vec3 grip{};
    if (GripWorld(frame, grip)) {
        gMuzzleFromGripMm.store(static_cast<int>(Len(Sub(muzzle, grip)) * 1000.0f), std::memory_order_relaxed);
    }
    gBarrelAxisMdeg.store(static_cast<int>(shot::AngleDegrees(axis, sample.direction) * 1000.0f),
                          std::memory_order_relaxed);
    return true;
}

bool ShotRayAdjustFiringPosition(void* weapon, void* result, std::uint32_t, void* overrideEntity)
{
    if (overrideEntity || !result || !gInstallTried) { return false; }
    std::array<std::uint8_t, 16> bytes{};
    if (!CopyBytes(bytes.data(), result, bytes.size())) { return false; }
    const auto native = DecodeFiringPosition(bytes);
    if (!native) { return false; }
    const auto now = MonotonicNanoseconds();
    shot::SpawnInput in{};
    in.nativeFallback = native->cameraFallback != 0;
    if (!in.nativeFallback) {
        tSpawn = {shot::Spawn::Native, now};
        if (gEnabled.load(std::memory_order_relaxed)) { gSpawnNative.fetch_add(1, std::memory_order_relaxed); }
        return false;
    }
    // Lane off: record that the engine fell back, change nothing.
    if (!gEnabled.load(std::memory_order_acquire)) {
        tSpawn = {shot::Spawn::Blocked, now};
        return false;
    }
    ShotContext ctx{};
    EquippedRig rig{};
    if (!ReadContext(ctx) || !TryGetEquippedRig(ctx.frame.player, rig) ||
        rig.weapon != reinterpret_cast<std::uintptr_t>(weapon)) {
        return false;   // not the local player's weapon: the engine's answer stands
    }
    const GameplayPoseFrame& frame = ctx.frame;
    Vec3 muzzle{}, axis{}, hand{};
    in.haveMuzzle = ReadMuzzle(rig.weapon, muzzle, axis);
    in.haveHand = GripWorld(frame, hand);
    if (in.haveMuzzle && in.haveHand) {
        scene::Hit hit{};
        in.eyeToHandClear = QuerySegment(frame, frame.cameraCentre, hand, hit) && hit.distance < 0;
        if (in.eyeToHandClear) {
            const Vec3 from = shot::SegmentStart(hand, muzzle, .6f);
            in.handToMuzzleClear = QuerySegment(frame, from, muzzle, hit) && hit.distance < 0;
        }
    }
    const auto decision = shot::DecideSpawn(in);
    tSpawn = {decision, now};
    if (decision != shot::Spawn::Restored) {
        gSpawnBlocked.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    auto* const out = static_cast<std::uint8_t*>(result);
    const std::uint8_t muzzleKind = 0;
    if (!CopyBytes(out, &muzzleKind, 1) || !CopyBytes(out + 4, &muzzle, sizeof(muzzle))) { return false; }
    gSpawnRestored.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool TryGetLastShot(ShotTrace& out) { return gLastShot.TryRead(out); }

DWORD SetShotProbe(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    if (!Install() || !gGetterHooked) { return ERROR_NOT_SUPPORTED; }
    if (enabled) {
        for (auto& slot : gCallers) { slot.count.store(0); slot.rva.store(0); }
        gCallersOverflow.store(0);
    }
    gProbe.store(enabled != 0);
    return 0;
}

std::string ShotProbeReport()
{
    std::ostringstream out;
    out << " probe=" << gProbe.load() << " overflow=" << gCallersOverflow.load();
    for (const auto& slot : gCallers) {
        const auto rva = slot.rva.load();
        if (!rva) { continue; }
        char text[48];
        std::snprintf(text, sizeof(text), " 0x%X:%llu", rva, slot.count.load());
        out << text;
    }
    return out.str();
}

std::string ShotRayReport()
{
    std::ostringstream out;
    out << " shotRay=" << ShotRayEnabled() << " shotHooks=" << gQueryHooked << gHelperOk << gProjectileHooked
        << gPelletsHooked << " shotFramesMuzzle=" << gFramesMuzzle.load()
        << " shotFramesNoMuzzle=" << gFramesNoMuzzle.load() << " muzzleOffAimMm=" << gMuzzleOffAimMm.load()
        << " muzzleAlongAimMm=" << gMuzzleAlongAimMm.load() << " muzzleFromGripMm=" << gMuzzleFromGripMm.load()
        << " barrelAxisMdeg=" << gBarrelAxisMdeg.load() << " shotQueries=" << gQueries.load()
        << " shotSteered=" << gSteered.load() << " shotSkip=" << gSkipNoFrame.load() << ","
        << gSkipNotEquipped.load() << "," << gSkipNoAim.load() << "," << gSkipNoFiring.load() << ","
        << gSkipDirection.load() << " spawnNative=" << gSpawnNative.load()
        << " spawnRestored=" << gSpawnRestored.load() << " spawnBlocked=" << gSpawnBlocked.load()
        << " projectiles=" << gProjectiles.load() << " projectilesUnseen=" << gProjectilesUnseen.load()
        << " scopedReads=" << gScopedReads.load() << " getterHooked=" << gGetterHooked;
    ShotTrace t{};
    if (gLastShot.TryRead(t) && t.valid) {
        out << " lastShot=" << shot::RouteName(t.route) << " lastSpawnKind=" << shot::SpawnName(t.spawnKind)
            << " lastAngleDeg=" << Fixed(t.angleDegrees, 3) << " lastSpawnOffAimMm=" << Fixed(t.spawnOffLineMm, 1)
            << " lastHit=" << t.entityHit << " lastDist=" << Fixed(Len(Sub(t.target, t.spawn)), 2)
            << " lastAgeMs=" << (MonotonicNanoseconds() - t.ns) / 1000000ull;
    }
    return out.str();
}

}  // namespace preyvr::dll
