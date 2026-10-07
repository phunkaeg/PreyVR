#include "InteractionLane.h"

#include "AimTakeover.h"
#include "CarryLane.h"
#include "HeadTrackingHook.h"
#include "HudBridge.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "preyvr/EngineMap.h"
#include "preyvr/InteractionUse.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/PsiTargeting.h"
#include "preyvr/StereoCamera.h"

#include <MinHook.h>

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

void Log(const std::string& line) { lifecycle::Log("preyvr_use " + line); }

// --- native contracts (Steam PreyDll.dll, SHA-256 7d6e322f...) ---------------
//
// ArkPlayerTargetSelector::UpdateCandidates (R-022): RCX = selector, which is
// ArkPlayerInteraction+0x190, which is ArkPlayer+0xAC8. Reads the cached ray
// once, through IArkPlayer::GetReticleViewPositionAndDir (call at 0x159A708).
constexpr std::uintptr_t kUpdateCandidatesRva = 0x159A660;
constexpr std::array<std::uint8_t, 22> kUpdateCandidatesPrologue{
    0x40, 0x55, 0x41, 0x54, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0x80,
    0x48, 0x81, 0xEC, 0x80, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x51, 0x28};
// ArkPlayerInteraction::Interact (R-023): (interaction, mode) -> bool.
constexpr std::uintptr_t kInteractRva = 0x1593690;
constexpr std::array<std::uint8_t, 16> kInteractPrologue{
    0x40, 0x53, 0x55, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x68, 0x48, 0x8B, 0xE9, 0x48, 0x63, 0xF2};
// ArkPlayerInteraction::PerformInteraction: (interaction, type, mode, IEntity*,
// float delay). The native action dispatcher (PHYSICAL-GRAB-SEAMS-2026-10-04).
constexpr std::uintptr_t kPerformRva = 0x1593980;
constexpr std::array<std::uint8_t, 18> kPerformPrologue{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56};
constexpr std::uintptr_t kGetPlayerRva = 0x157C990;
// gEnv (SSystemGlobalEnvironment, static at 0x224D980) + 0xB8 = IEntitySystem*.
constexpr std::uintptr_t kEntitySystemRva = 0x224DA38;
// Virtual slots. IEntitySystem::GetEntity: +0x70 is by far the most common
// slot called on that pointer in the binary (1228 sites; the next is 134).
// IEntity/IEntityClass slots follow the PDB-generated Chairloader headers and
// are validated on every read: a name must be printable, bounds finite.
constexpr std::size_t kGetEntitySlot = 0x70;
constexpr std::size_t kEntityGetClassSlot = 0x18;
constexpr std::size_t kEntityGetArchetypeSlot = 0x20;   // both overloads return [entity+0x30]
constexpr std::size_t kArchetypeGetNameSlot = 0x10;
constexpr std::size_t kEntityGetNameSlot = 0x78;
constexpr std::size_t kEntityGetWorldBoundsSlot = 0xF8;
constexpr std::size_t kClassGetNameSlot = 0x10;
// ArkPlayerInteraction members. The carry is ArkPlayerCarry at +0x28; its
// picked-up entity id at +0xB0 (PHYSICAL-GRAB-SEAMS, carry_target.txt).
constexpr std::uintptr_t kCarry = 0x28;
constexpr std::uintptr_t kCarryEntity = 0xB0;
// ArkPlayerTargetSelector: m_bIsHoovering +1, inner/outer aim +4/+8.
constexpr std::uintptr_t kHoovering = 0x01, kInnerAim = 0x04, kOuterAim = 0x08;
constexpr std::size_t kInfoSize = 0x18;   // ArkInteractionInfo: type, string text, bool, float hold
// Suppression points the selector at an empty point of the world.
constexpr float kSkyMetres = 20000.0f;

using UpdateCandidatesFn = void(__fastcall*)(std::uintptr_t selector);
using InteractFn = bool(__fastcall*)(std::uintptr_t interaction, int mode);
using PerformFn = void(__fastcall*)(std::uintptr_t interaction, int type, int mode, std::uintptr_t entity, float delay);

std::uintptr_t gBase = 0;
std::atomic<UpdateCandidatesFn> gUpdateCandidates{nullptr};
std::atomic<InteractFn> gInteract{nullptr};
std::atomic<PerformFn> gPerform{nullptr};
std::mutex gInstallMutex;
bool gInstallTried = false, gInstalled = false, gObserversInstalled = false;

// Off until VR mode switches it on with the selector hook installed: without
// the hook, the left grip must not take over the use button.
std::atomic<bool> gLeftHand{false}, gHold{true}, gTrace{false};
std::atomic<bool> gFilterOn{true};
std::atomic<float> gMinCutoff{1.0f}, gBeta{2.0f}, gBacklash{use::FilterSettings{}.backlashDegrees};
std::atomic<unsigned> gBackMm{100};
std::atomic<bool> gSuppress{false};

// The ray this frame. Produced by the aim hook (R-011) and consumed by the
// selector hook (R-022); both run on the game thread in every capture so far,
// but the lock costs nothing and makes that an observation, not a requirement.
// A blocking lock, not a try-lock: a refused read would hand the selector the
// weapon's ray for one frame and flicker the pick.
struct FrameRay {
    bool valid = false;
    std::uintptr_t player = 0;
    Vec3 origin{}, direction{}, hand{};
    std::uint64_t ns = 0;
};
FrameRay gRay;
std::mutex gRayMutex;
use::RayFilter gFilter;                  // game thread only
std::atomic<bool> gFilterReset{false};   // settings changed: restart it there
long long gLastDisplayTime = 0;

std::atomic<unsigned long long> gSelectorCalls{0}, gSteered{0}, gSuppressed{0}, gUnsteered{0}, gForeign{0},
    gFaults{0};
std::atomic<unsigned long long> gInteracts{0}, gPerforms{0}, gTargetChanges{0};
std::atomic<unsigned long long> gPresses{0}, gReleases{0}, gNoTarget{0}, gGaveUp{0}, gBusy{0};
std::atomic<std::uint32_t> gUsable{0}, gCarry{0};
std::atomic<bool> gTapOnly{false};
std::atomic<bool> gButtonDown{false};
std::atomic<unsigned> gButtonOwner{0};
LatestSnapshot<UsePointer> gPointer;

// The last few things the game did, for the report and the overlay.
struct Event {
    std::uint64_t ns = 0;
    char text[96]{};
};
std::mutex gEventMutex;
std::array<Event, 8> gEvents{};
unsigned gEventNext = 0;
void Remember(const std::string& text)
{
    std::lock_guard lock(gEventMutex);
    auto& e = gEvents[gEventNext++ % gEvents.size()];
    e.ns = MonotonicNanoseconds();
    std::snprintf(e.text, sizeof(e.text), "%s", text.c_str());
}

// --- SEH helpers (POD only) ---------------------------------------------------

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
        const auto vtable = *reinterpret_cast<const std::uintptr_t*>(system);
        const auto fn = *reinterpret_cast<std::uintptr_t(__fastcall* const*)(std::uintptr_t, std::uint32_t)>(
            vtable + kGetEntitySlot);
        return fn(system, id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Copies a printable C string; false for anything else.
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

const char* CallEntityName(std::uintptr_t entity)
{
    __try {
        const auto vtable = *reinterpret_cast<const std::uintptr_t*>(entity);
        return (*reinterpret_cast<const char* (__fastcall* const*)(std::uintptr_t)>(vtable + kEntityGetNameSlot))(entity);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
const char* CallClassName(std::uintptr_t entity)
{
    __try {
        const auto vtable = *reinterpret_cast<const std::uintptr_t*>(entity);
        const auto cls = (*reinterpret_cast<std::uintptr_t(__fastcall* const*)(std::uintptr_t)>(
            vtable + kEntityGetClassSlot))(entity);
        if (!cls) { return nullptr; }
        const auto classVtable = *reinterpret_cast<const std::uintptr_t*>(cls);
        return (*reinterpret_cast<const char* (__fastcall* const*)(std::uintptr_t)>(classVtable + kClassGetNameSlot))(cls);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
// Spawned pickups have no entity name; their archetype names what they are.
const char* CallArchetypeName(std::uintptr_t entity)
{
    __try {
        const auto vtable = *reinterpret_cast<const std::uintptr_t*>(entity);
        const auto archetype = (*reinterpret_cast<std::uintptr_t(__fastcall* const*)(std::uintptr_t)>(
            vtable + kEntityGetArchetypeSlot))(entity);
        if (!archetype) { return nullptr; }
        const auto archetypeVtable = *reinterpret_cast<const std::uintptr_t*>(archetype);
        return (*reinterpret_cast<const char* (__fastcall* const*)(std::uintptr_t)>(archetypeVtable + kArchetypeGetNameSlot))(
            archetype);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool CallWorldBounds(std::uintptr_t entity, float* box)
{
    __try {
        const auto vtable = *reinterpret_cast<const std::uintptr_t*>(entity);
        (*reinterpret_cast<void(__fastcall* const*)(std::uintptr_t, float*)>(vtable + kEntityGetWorldBoundsSlot))(
            entity, box);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct EntityInfo {
    std::uint32_t id = 0;
    std::uintptr_t entity = 0;
    char name[40]{};        // the entity's own name, or "" for spawned pickups
    char archetype[64]{};
    char cls[32]{};
    char label[64]{};       // the most descriptive of the three
    bool bounds = false;
    Vec3 min{}, max{};
};

bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Len(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

bool DescribeEntity(std::uint32_t id, EntityInfo& out)
{
    out = {};
    out.id = id;
    out.entity = CallGetEntity(id);
    if (!out.entity) { return false; }
    if (!CopyName(CallEntityName(out.entity), out.name, sizeof(out.name))) { out.name[0] = 0; }
    if (!CopyName(CallArchetypeName(out.entity), out.archetype, sizeof(out.archetype))) { out.archetype[0] = 0; }
    if (!CopyName(CallClassName(out.entity), out.cls, sizeof(out.cls))) { std::snprintf(out.cls, sizeof(out.cls), "?"); }
    std::snprintf(out.label, sizeof(out.label), "%s",
                  out.archetype[0] ? out.archetype : out.name[0] ? out.name : out.cls);
    float box[6]{};
    if (CallWorldBounds(out.entity, box)) {
        out.min = {box[0], box[1], box[2]};
        out.max = {box[3], box[4], box[5]};
        // A real prop: finite, ordered, smaller than a room.
        out.bounds = Finite(out.min) && Finite(out.max) && out.min.x <= out.max.x && out.min.y <= out.max.y &&
                     out.min.z <= out.max.z && Len(Sub(out.max, out.min)) < 40.0f;
    }
    return true;
}

// One interaction mode's record: type, prompt text, hold seconds.
struct InfoRecord {
    int type = 0;
    char text[32]{};
    float hold = 0;
};
void ReadInfo(std::uintptr_t interaction, std::array<InfoRecord, 4>& out)
{
    for (int mode = 0; mode < 4; ++mode) {
        const std::uintptr_t at = interaction + engine::ArkPlayerInteractionLayout::interactionInfo + mode * kInfoSize;
        auto& r = out[mode];
        r = {};
        Read(at, r.type);
        std::uintptr_t text = 0;
        if (Read(at + 8, text) && text) { CopyName(reinterpret_cast<const char*>(text), r.text, sizeof(r.text)); }
        Read(at + 0x14, r.hold);
    }
}

std::string Fixed(float v, int decimals)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, static_cast<double>(v));
    return text;
}
std::string Point(Vec3 v) { return Fixed(v.x, 3) + "," + Fixed(v.y, 3) + "," + Fixed(v.z, 3); }

// --- the selector seam ---------------------------------------------------------

// Runs the original with the cached ray replaced; restored even if it faults.
void CallWithRay(UpdateCandidatesFn original, std::uintptr_t selector, std::uintptr_t player, const Vec3* origin,
                 const Vec3* direction)
{
    auto* const originAt = reinterpret_cast<float*>(player + engine::ArkPlayerLayout::cachedReticleOrigin);
    auto* const directionAt = reinterpret_cast<float*>(player + engine::ArkPlayerLayout::cachedReticleDirection);
    float savedOrigin[3], savedDirection[3];
    std::memcpy(savedOrigin, originAt, sizeof(savedOrigin));
    std::memcpy(savedDirection, directionAt, sizeof(savedDirection));
    std::memcpy(originAt, origin, sizeof(savedOrigin));
    std::memcpy(directionAt, direction, sizeof(savedDirection));
    __try {
        original(selector);
    } __finally {
        std::memcpy(originAt, savedOrigin, sizeof(savedOrigin));
        std::memcpy(directionAt, savedDirection, sizeof(savedDirection));
    }
}

void __fastcall UpdateCandidatesHook(std::uintptr_t selector)
{
    const auto original = gUpdateCandidates.load(std::memory_order_acquire);
    gSelectorCalls.fetch_add(1, std::memory_order_relaxed);
    if (!gLeftHand.load(std::memory_order_acquire)) { original(selector); return; }
    const std::uintptr_t player = CallGetPlayer();
    if (!player || selector != player + engine::ArkPlayerLayout::interaction +
                                    engine::ArkPlayerInteractionLayout::targetSelector) {
        gForeign.fetch_add(1, std::memory_order_relaxed);
        original(selector);
        return;
    }
    Vec3 origin{}, direction{};
    if (gSuppress.load(std::memory_order_acquire)) {
        Vec3 eye{};
        if (!Read(player + engine::ArkPlayerLayout::cachedReticleOrigin, eye) || !Finite(eye)) {
            gFaults.fetch_add(1, std::memory_order_relaxed);
            original(selector);
            return;
        }
        origin = {eye.x, eye.y, eye.z + kSkyMetres};
        direction = {0, 0, 1};
        gSuppressed.fetch_add(1, std::memory_order_relaxed);
    } else if (const FrameRay ray = [] { std::lock_guard lock(gRayMutex); return gRay; }();
               ray.valid && ray.player == player && FreshSample(MonotonicNanoseconds(), ray.ns, 100000000ull)) {
        origin = ray.origin;
        direction = ray.direction;
        gSteered.fetch_add(1, std::memory_order_relaxed);
    } else {
        // No usable left hand this frame (tracking lost, a menu): the game's own
        // ray, which is the weapon's. Counted, not hidden.
        gUnsteered.fetch_add(1, std::memory_order_relaxed);
        original(selector);
        return;
    }
    CallWithRay(original, selector, player, &origin, &direction);
}

bool __fastcall InteractHook(std::uintptr_t interaction, int mode)
{
    std::uint32_t usable = 0;
    Read(interaction + engine::ArkPlayerInteractionLayout::usableEntityId, usable);
    const bool result = gInteract.load(std::memory_order_acquire)(interaction, mode);
    gInteracts.fetch_add(1, std::memory_order_relaxed);
    EntityInfo info{};
    DescribeEntity(usable, info);
    const std::string line = "interact mode=" + std::to_string(mode) + " entity=" + std::to_string(usable) + " name=" +
                             info.label + " class=" + info.cls + " result=" + std::to_string(result ? 1 : 0);
    Log(line);
    Remember("interact m" + std::to_string(mode) + " " + info.label + (result ? " ok" : " refused"));
    return result;
}

void __fastcall PerformHook(std::uintptr_t interaction, int type, int mode, std::uintptr_t entity, float delay)
{
    char name[64] = "?";
    if (entity && !CopyName(CallArchetypeName(entity), name, sizeof(name))) { CopyName(CallEntityName(entity), name, sizeof(name)); }
    gPerform.load(std::memory_order_acquire)(interaction, type, mode, entity, delay);
    gPerforms.fetch_add(1, std::memory_order_relaxed);
    Log("perform type=" + std::to_string(type) + " mode=" + std::to_string(mode) + " name=" + name +
        " delay=" + Fixed(delay, 2));
    Remember("perform t" + std::to_string(type) + " m" + std::to_string(mode) + " " + name);
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

// --- per-frame state ---------------------------------------------------------

struct Selection {
    std::uint32_t usable = 0, carry = 0;
    EntityInfo target{};
    std::array<InfoRecord, 4> info{};
    float inner = 0, outer = 0, reach = 0;
    bool hoovering = false;
    std::uint32_t forced = 0;
    unsigned candidates = 0;
    std::array<std::uint32_t, 8> candidateIds{};
    std::array<float, 8> candidatePriority{}, candidateAim{};
    std::array<std::array<char, 24>, 8> candidateName{};
    std::array<Vec3, 8> candidateCentre{};
    std::array<bool, 8> candidateBounds{};
    // The target against the pointer: angle off the line and distance, to its bounds centre.
    bool measured = false;
    float angle = 0, distance = 0;
    // The ray the selector used, for the report.
    bool ray = false;
    Vec3 rayOrigin{}, rayDirection{};
};
// use.find: where an entity is, as the controller angles that point the left
// hand at it (test harness). Requested by the command thread, answered by the
// game thread on its next frame -- the engine is only called from there.
struct FindResult {
    std::uint32_t id = 0;
    bool found = false, bounds = false;
    char label[64]{};
    Vec3 centre{};
    float yawDeg = 0, pitchDeg = 0, distance = 0;
    std::uint64_t ns = 0;
};
std::atomic<std::uint32_t> gFindId{0};
std::mutex gFindMutex;
FindResult gFind;

Selection gLast;           // game thread
std::mutex gLastMutex;     // for the report
Selection gReported;
bool gMenuWasOpen = false;

void ReadSelection(std::uintptr_t player, Selection& s)
{
    const std::uintptr_t interaction = player + engine::ArkPlayerLayout::interaction;
    const std::uintptr_t selector = interaction + engine::ArkPlayerInteractionLayout::targetSelector;
    Read(interaction + engine::ArkPlayerInteractionLayout::usableEntityId, s.usable);
    Read(interaction + kCarry + kCarryEntity, s.carry);
    std::uint8_t hoover = 0;
    Read(selector + kHoovering, hoover);
    s.hoovering = hoover != 0;
    Read(selector + kInnerAim, s.inner);
    Read(selector + kOuterAim, s.outer);
    Read(selector + engine::ArkPlayerTargetSelectorLayout::interactDistance, s.reach);
    Read(selector + engine::ArkPlayerTargetSelectorLayout::forceSelectEntityId, s.forced);
    std::uintptr_t begin = 0, end = 0;
    Read(selector + engine::ArkPlayerTargetSelectorLayout::candidatesBegin, begin);
    Read(selector + engine::ArkPlayerTargetSelectorLayout::candidatesEnd, end);
    s.candidates = 0;
    if (begin && end > begin && (end - begin) % engine::ArkPlayerTargetSelectorLayout::candidateRecordSize == 0) {
        const auto count = (end - begin) / engine::ArkPlayerTargetSelectorLayout::candidateRecordSize;
        for (std::size_t i = 0; i < count && s.candidates < s.candidateIds.size(); ++i) {
            const auto record = begin + i * engine::ArkPlayerTargetSelectorLayout::candidateRecordSize;
            std::uint32_t id = 0;
            float priority = 0, aim = 0;
            if (!Read(record + engine::ArkPlayerTargetSelectorLayout::candidateEntityId, id) || !Read(record, priority) ||
                !Read(record + 4, aim)) { break; }
            const unsigned n = s.candidates++;
            s.candidateIds[n] = id;
            s.candidatePriority[n] = priority;
            s.candidateAim[n] = aim;
            EntityInfo c{};
            DescribeEntity(id, c);
            std::snprintf(s.candidateName[n].data(), s.candidateName[n].size(), "%s", c.label);
            s.candidateBounds[n] = c.bounds;
            if (c.bounds) { s.candidateCentre[n] = Scale(Add(c.min, c.max), .5f); }
        }
    }
    if (s.usable) {
        DescribeEntity(s.usable, s.target);
        ReadInfo(interaction, s.info);
    }
}

// EArkInteractionType values, named from what PerformInteraction did with
// them in the mock (docs/INTERACTION-LEFT-HAND-2026-10-06.md); the rest stay numbers.
const char* TypeName(int type)
{
    switch (type) {
    case 3: return "search";
    case 4: return "take";
    case 6: return "carry";
    default: return nullptr;
    }
}

// What the use button does on this target: the prompt text the entity
// specified, else the mode-0 type's name.
std::string ActionText(const Selection& s)
{
    for (const auto& r : s.info) {
        if (r.text[0]) { return r.text[0] == '@' ? r.text + 1 : r.text; }
    }
    if (const char* name = TypeName(s.info[0].type)) { return name; }
    return s.info[0].type ? "type " + std::to_string(s.info[0].type) : "";
}

std::string DescribeInfo(const Selection& s)
{
    std::string out;
    for (int m = 0; m < 4; ++m) {
        const auto& r = s.info[m];
        if (!r.type && !r.text[0]) { continue; }
        out += " m" + std::to_string(m) + "=" + std::to_string(r.type) + ":" + (r.text[0] ? r.text : "-");
        if (r.hold > 0) { out += "/hold" + Fixed(r.hold, 2); }
    }
    return out;
}

}  // namespace

DWORD SetUseHand(unsigned left)
{
    if (left > 1) { return ERROR_INVALID_PARAMETER; }
    if (left && !EnsureInteractionLane()) { return ERROR_NOT_SUPPORTED; }
    gLeftHand.store(left != 0, std::memory_order_release);
    gFilterReset.store(true);
    Log("result=0 detail=hand value=" + std::string(left ? "left" : "right"));
    return 0;
}
unsigned UseHandLeft() { return gLeftHand.load() ? 1u : 0u; }
DWORD SetUseHold(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gHold.store(enabled != 0);
    return 0;
}
unsigned UseHoldEnabled() { return gHold.load() ? 1u : 0u; }
DWORD SetUseFilter(unsigned enabled, float minCutoff, float beta, float backlashDegrees)
{
    if (enabled > 1 || !(minCutoff > 0) || minCutoff > 30 || !(beta >= 0) || beta > 100 || !(backlashDegrees >= 0) ||
        backlashDegrees > 5) { return ERROR_INVALID_PARAMETER; }
    gFilterOn.store(enabled != 0);
    gMinCutoff.store(minCutoff);
    gBeta.store(beta);
    gBacklash.store(backlashDegrees);
    gFilterReset.store(true);
    return 0;
}
DWORD SetUseBackMillimetres(unsigned mm)
{
    if (mm > 400) { return ERROR_INVALID_PARAMETER; }
    gBackMm.store(mm);
    return 0;
}
DWORD SetUseTrace(unsigned enabled)
{
    if (enabled > 1) { return ERROR_INVALID_PARAMETER; }
    gTrace.store(enabled != 0);
    return 0;
}

bool EnsureInteractionLane()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstallTried) { return gInstalled; }
    gInstallTried = true;
    gBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll"));
    if (!gBase) { gInstallTried = false; return false; }
    EnsureMinHook();
    gInstalled = HookAt(kUpdateCandidatesRva, kUpdateCandidatesPrologue, reinterpret_cast<void*>(&UpdateCandidatesHook),
                        gUpdateCandidates, "update_candidates");
    // Observers: the lane works without them, it only reports less.
    gObserversInstalled = HookAt(kInteractRva, kInteractPrologue, reinterpret_cast<void*>(&InteractHook), gInteract, "interact") &&
                          HookAt(kPerformRva, kPerformPrologue, reinterpret_cast<void*>(&PerformHook), gPerform, "perform");
    Log(std::string("result=") + (gInstalled ? "0" : "unavailable") + " selector=" + std::to_string(gInstalled) +
        " observers=" + std::to_string(gObserversInstalled));
    return gInstalled;
}

void UpdateInteractionLane(const GameplayPoseFrame& frame, bool tracking)
{
    if (!gInstalled) { return; }
    const auto now = MonotonicNanoseconds();
    // --- the left hand's pointing ray -------------------------------------------
    FrameRay next{};
    if (gFilterReset.exchange(false)) { gFilter.Reset(); }
    const auto& left = frame.tracking.hands[static_cast<unsigned>(Hand::left)];
    UsePointer pointer{};
    if (gLeftHand.load() && tracking && frame.cameraCentreValid && frame.headYawUsable && !(frame.referenceGeneration & 1)) {
        const auto ray = psi::BuildRay(psi::Mode::LeftController, frame.yaw, frame.cameraCentre, frame.tracking.head,
                                       left.aimPose, left.aimValidity);
        if (ray) {
            const float dt = gLastDisplayTime && frame.tracking.displayTime > gLastDisplayTime
                                 ? static_cast<float>(frame.tracking.displayTime - gLastDisplayTime) * 1e-9f
                                 : 0.0f;
            use::FilterSettings settings{};
            settings.enabled = gFilterOn.load();
            settings.minCutoff = gMinCutoff.load();
            settings.beta = gBeta.load();
            settings.backlashDegrees = gBacklash.load();
            const auto filtered = gFilter.Update({ray->origin, ray->direction}, dt, settings);
            const float back = static_cast<float>(gBackMm.load()) * .001f;
            next.player = frame.player;
            next.hand = filtered.origin;
            next.direction = filtered.direction;
            // Started behind the controller so a prop the hand touches is still
            // in front of the ray, where the native cone can see it.
            next.origin = Sub(filtered.origin, Scale(filtered.direction, back));
            next.ns = now;
            next.valid = true;
            pointer.valid = true;
            pointer.hand = filtered.origin;
            pointer.direction = filtered.direction;
        }
    } else {
        gFilter.Reset();
    }
    gLastDisplayTime = frame.tracking.displayTime;
    {
        std::lock_guard lock(gRayMutex);
        gRay = next;
    }

    // --- use.find ------------------------------------------------------------------
    if (const auto id = gFindId.exchange(0)) {
        FindResult f{};
        f.id = id;
        EntityInfo e{};
        f.found = DescribeEntity(id, e);
        f.bounds = e.bounds;
        std::snprintf(f.label, sizeof(f.label), "%s", e.label);
        if (f.found && e.bounds && next.valid) {
            f.centre = Scale(Add(e.min, e.max), .5f);
            const Vec3 d = Sub(f.centre, next.hand);
            f.distance = Len(d);
            // Engine world -> the app's OpenXR space (the inverse of what the
            // controller pose went through), then OpenXR yaw/pitch: -Z forward,
            // +Y up, yaw positive to the left.
            const Quaternion yaw = stereo::YawQuaternion(frame.yaw);
            const Quaternion toXr = stereo::kOpenXrToEngine;
            const Vec3 x = Rotate(Quaternion{-toXr.x, -toXr.y, -toXr.z, toXr.w},
                                  Rotate(Quaternion{-yaw.x, -yaw.y, -yaw.z, yaw.w}, d));
            const float h = std::sqrt(x.x * x.x + x.z * x.z);
            f.yawDeg = std::atan2(-x.x, -x.z) * 57.29578f;
            f.pitchDeg = std::atan2(x.y, h) * 57.29578f;
        }
        f.ns = MonotonicNanoseconds();
        std::lock_guard lock(gFindMutex);
        gFind = f;
    }

    // --- what the game selected ----------------------------------------------------
    Selection s{};
    if (frame.player) { ReadSelection(frame.player, s); }
    gUsable.store(s.usable);
    gCarry.store(s.carry);
    // EArkInteractionType (the game's name table at 0x1E582F0): 4 Pickup, 12 Hoover.
    gTapOnly.store(s.usable && s.info[0].type == 4 && s.info[1].type == 12);
    if (s.usable && s.target.bounds && pointer.valid) {
        const Vec3 centre = Scale(Add(s.target.min, s.target.max), .5f);
        s.measured = true;
        s.angle = use::AngleDegrees(Sub(centre, pointer.hand), pointer.direction);
        s.distance = Len(Sub(centre, pointer.hand));
    }
    const bool menuOpen = !HudGameplayInputAllowed();
    if (s.usable != gLast.usable) {
        gTargetChanges.fetch_add(1, std::memory_order_relaxed);
        if (gTrace.load()) {
            std::string line = "target=" + std::to_string(s.usable);
            if (s.usable) {
                line += std::string(" name=") + s.target.label + " class=" + s.target.cls + DescribeInfo(s);
                if (s.measured) { line += " angleDeg=" + Fixed(s.angle, 1) + " distM=" + Fixed(s.distance, 2); }
                line += " candidates=" + std::to_string(s.candidates);
            }
            Log(line);
        }
    }
    if (s.carry != gLast.carry) {
        EntityInfo carried{};
        DescribeEntity(s.carry, carried);
        Log("carry entity=" + std::to_string(s.carry) + " name=" + carried.label + " class=" + carried.cls);
        Remember(s.carry ? std::string("carry ") + carried.label : std::string("carry end"));
    }
    if (menuOpen != gMenuWasOpen) {
        Log(std::string("menu ") + (menuOpen ? "open" : "closed") + " epoch=" + std::to_string(HudMenuEpoch()));
        Remember(menuOpen ? "menu open" : "menu closed");
        gMenuWasOpen = menuOpen;
    }
    s.ray = next.valid;
    s.rayOrigin = next.origin;
    s.rayDirection = next.direction;
    gLast = s;
    {
        std::lock_guard lock(gLastMutex);
        gReported = s;
    }

    // --- the pointer ----------------------------------------------------------------
    // A hand holding what it carries points at nothing: no ray out of it.
    if (s.carry && CarryHoldingInHand()) { pointer.valid = false; }
    if (pointer.valid) {
        pointer.suppressed = gSuppress.load();
        pointer.held = gButtonDown.load();
        pointer.entity = s.usable;
        const float reach = s.reach > .1f && s.reach < 10.0f ? s.reach : 2.0f;
        pointer.end = Add(pointer.hand, Scale(pointer.direction, reach));
        if (s.usable && s.target.bounds) {
            pointer.target = true;
            pointer.end = use::PointerEnd(pointer.hand, pointer.direction, s.target.min, s.target.max, &pointer.onLine);
        } else if (s.usable) {
            pointer.target = true;   // selected but no bounds: the line still says so
        }
        std::snprintf(pointer.name, sizeof(pointer.name), "%s", s.usable ? s.target.label : "");
        std::snprintf(pointer.action, sizeof(pointer.action), "%s", s.usable ? ActionText(s).c_str() : "");
        for (unsigned i = 0; i < s.candidates && pointer.candidateCount < pointer.candidates.size(); ++i) {
            if (s.candidateIds[i] != s.usable && s.candidateBounds[i]) {
                pointer.candidates[pointer.candidateCount++] = s.candidateCentre[i];
            }
        }
        pointer.ns = now;
        pointer.reference = frame.referenceGeneration;
        gPointer.Publish(pointer);
    } else {
        gPointer.Clear();
    }
}

bool UseTargetPresent() { return gUsable.load(std::memory_order_relaxed) != 0; }
std::uint32_t UseTargetId() { return gUsable.load(std::memory_order_relaxed); }
bool UseTargetTapOnly() { return gTapOnly.load(std::memory_order_relaxed); }
bool UseCarrying() { return gCarry.load(std::memory_order_relaxed) != 0; }

float UseCarryHoldSeconds(std::uint32_t entity)
{
    // gLast is the game thread's; so is the caller (StartCarrying).
    if (!entity || gLast.usable != entity) { return -1.0f; }
    float hold = -1.0f;
    for (const auto& r : gLast.info) {
        if (r.type == 6) { hold = std::max(hold, r.hold); }
    }
    return hold;
}

void RequestUseTargetSuppression(bool suppress) { gSuppress.store(suppress, std::memory_order_release); }

void NoteUseButton(bool down, unsigned owner, bool pressed, bool released, unsigned events)
{
    gButtonDown.store(down);
    gButtonOwner.store(owner);
    // events: 1 no target, 2 gave up, 4 busy
    if (pressed) { gPresses.fetch_add(1); }
    if (released) { gReleases.fetch_add(1); }
    if (events & 1) { gNoTarget.fetch_add(1); }
    if (events & 2) { gGaveUp.fetch_add(1); }
    if (events & 4) { gBusy.fetch_add(1); }
    if (pressed || released || events) {
        const char* names[] = {"none", "use", "reload", "legacy", "hold"};
        const std::string ownerName = owner < 5 ? names[owner] : "?";
        std::string line = std::string("button ") + (pressed ? "press" : released ? "release" : "event") + " owner=" +
                           ownerName + " usable=" + std::to_string(gUsable.load());
        if (events & 1) { line += " noTarget=1"; }
        if (events & 2) { line += " gaveUp=1"; }
        if (events & 4) { line += " busy=1"; }
        Log(line);
        if (pressed) { Remember("press " + ownerName); }
        if (events & 1) { Remember("grip: nothing selected"); }
        if (events & 2) { Remember("reload: selection stuck"); }
    }
}

bool TryGetUsePointer(UsePointer& out)
{
    return gPointer.TryRead(out) && FreshSample(MonotonicNanoseconds(), out.ns, 150000000ull);
}

std::string UseFind(std::uint32_t id)
{
    const auto asked = MonotonicNanoseconds();
    gFindId.store(id);
    for (int i = 0; i < 50; ++i) {
        Sleep(10);
        std::lock_guard lock(gFindMutex);
        if (gFind.ns > asked && gFind.id == id) {
            const auto& f = gFind;
            return " id=" + std::to_string(id) + " found=" + std::to_string(f.found) + " name=" + f.label +
                   " bounds=" + std::to_string(f.bounds) + " centre=" + Point(f.centre) + " lhandYaw=" +
                   Fixed(f.yawDeg, 2) + " lhandPitch=" + Fixed(f.pitchDeg, 2) + " distM=" + Fixed(f.distance, 2);
        }
    }
    return " id=" + std::to_string(id) + " found=timeout";
}

std::string UseStatusLine()
{
    Selection s{};
    {
        std::lock_guard lock(gLastMutex);
        s = gReported;
    }
    std::ostringstream out;
    out << "USE " << (gLeftHand.load() ? "L" : "R") << "  ";
    if (s.usable) { out << s.target.label << " [" << s.target.cls << "] " << ActionText(s); }
    else { out << "-"; }
    out << "  btn " << (gButtonDown.load() ? "DOWN" : "up") << (gSuppress.load() ? " (reload: no target)" : "");
    if (s.carry) { out << "  carry " << s.carry; }
    std::lock_guard lock(gEventMutex);
    const auto now = MonotonicNanoseconds();
    for (unsigned i = 0; i < 2; ++i) {
        const auto& e = gEvents[(gEventNext + gEvents.size() - 1 - i) % gEvents.size()];
        if (e.ns && now - e.ns < 4000000000ull) { out << "  | " << e.text; }
    }
    return out.str();
}

std::string UseReport()
{
    Selection s{};
    {
        std::lock_guard lock(gLastMutex);
        s = gReported;
    }
    std::ostringstream out;
    out << " installed=" << gInstalled << " observers=" << gObserversInstalled << " hand=" << (gLeftHand.load() ? "left" : "right")
        << " hold=" << gHold.load() << " filter=" << gFilterOn.load() << "," << gMinCutoff.load() << "," << gBeta.load() << "," << gBacklash.load()
        << " backMm=" << gBackMm.load() << " selectorCalls=" << gSelectorCalls.load() << " steered=" << gSteered.load()
        << " suppressed=" << gSuppressed.load() << " unsteered=" << gUnsteered.load() << " foreign=" << gForeign.load()
        << " faults=" << gFaults.load() << " targetChanges=" << gTargetChanges.load() << " interacts=" << gInteracts.load()
        << " performs=" << gPerforms.load() << " presses=" << gPresses.load() << " releases=" << gReleases.load()
        << " noTarget=" << gNoTarget.load() << " gaveUp=" << gGaveUp.load() << " busy=" << gBusy.load()
        << " button=" << (gButtonDown.load() ? 1 : 0) << " owner=" << gButtonOwner.load()
        << " suppress=" << gSuppress.load() << " reach=" << Fixed(s.reach, 2) << " innerAim=" << Fixed(s.inner, 3)
        << " outerAim=" << Fixed(s.outer, 3) << " hoovering=" << s.hoovering << " forced=" << s.forced
        << " usable=" << s.usable;
    if (s.usable) {
        out << " name=" << s.target.label << " class=" << s.target.cls << DescribeInfo(s);
        if (s.measured) { out << " angleDeg=" << Fixed(s.angle, 1) << " distM=" << Fixed(s.distance, 2); }
        if (s.target.bounds) { out << " boundsMin=" << Point(s.target.min) << " boundsMax=" << Point(s.target.max); }
        if (s.target.entity) {
            std::uintptr_t vtable = 0;
            Read(s.target.entity, vtable);
            char text[32];
            std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(vtable - gBase));
            out << " entityVtableRva=" << text;
        }
    }
    out << " carry=" << s.carry << " candidates=" << s.candidates;
    for (unsigned i = 0; i < s.candidates; ++i) {
        out << " [" << s.candidateIds[i] << ":" << s.candidateName[i].data() << " p=" << Fixed(s.candidatePriority[i], 3)
            << " aim=" << Fixed(s.candidateAim[i], 3) << "]";
    }
    if (s.ray) { out << " rayOrigin=" << Point(s.rayOrigin) << " rayDir=" << Point(s.rayDirection); }
    std::lock_guard lock(gEventMutex);
    out << " events=";
    for (unsigned i = 0; i < gEvents.size(); ++i) {
        const auto& e = gEvents[(gEventNext + i) % gEvents.size()];
        if (e.ns) { out << "{" << e.text << "}"; }
    }
    return out.str();
}

}  // namespace preyvr::dll
