#include "AimRayProbe.h"

#include "Logger.h"
#include "preyvr/AimState.h"
#include "preyvr/EngineMap.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <sstream>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_aim_probe " + line);
}

using GetArkPlayerInstanceFn = void*(__fastcall*)();

// Resolved through the landmark table rather than a hardcoded RVA, so this
// inherits the same fail-closed gate as every other engine address: the table is
// byte-validated at bootstrap, and a build that does not match cannot reach here
// with a plausible-looking address.
std::uintptr_t GetArkPlayerInstanceRva()
{
    for (const auto& landmark : engine::Landmarks()) {
        if (landmark.id == "player.get_instance") {
            return landmark.rva;
        }
    }
    return 0;
}

// Full angular separation, not yaw. AimState offers DirectionYawDeltaDegrees,
// which would silently miss the vertical component -- and a per-eye camera
// displaces the ray horizontally, so a yaw-only measure would happen to look
// right here while being wrong in general.
float AngleBetweenDegrees(const Vec3& a, const Vec3& b)
{
    const float lengthA = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    const float lengthB = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    if (lengthA <= 0.0f || lengthB <= 0.0f) {
        return 0.0f;
    }
    float cosine = (a.x * b.x + a.y * b.y + a.z * b.z) / (lengthA * lengthB);
    cosine = cosine > 1.0f ? 1.0f : (cosine < -1.0f ? -1.0f : cosine);
    return std::acos(cosine) * 57.29577951f;
}

std::atomic<bool> gEnabled{false};
std::atomic<unsigned long long> gSamples{0};
std::atomic<unsigned long long> gUnreadable{0};

// Spread rather than variance: the question is "how far apart do consecutive
// frames put the ray", and the largest gap answers it directly without needing a
// second pass over the data. Stored as micrometres and millidegrees so they can
// cross the export boundary as integers.
std::atomic<unsigned long long> gMaxOriginGapMicrometres{0};
std::atomic<unsigned long long> gMaxAngleGapMillidegrees{0};

bool gHavePrevious = false;
Vec3 gPreviousOrigin{};
Vec3 gPreviousDirection{};

float Length(const Vec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

} // namespace

DWORD SetAimRayProbeEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on) {
        gSamples.store(0, std::memory_order_relaxed);
        gUnreadable.store(0, std::memory_order_relaxed);
        gMaxOriginGapMicrometres.store(0, std::memory_order_relaxed);
        gMaxAngleGapMillidegrees.store(0, std::memory_order_relaxed);
        gHavePrevious = false;
    }
    gEnabled.store(on, std::memory_order_release);
    Log(std::string("result=0 detail=enabled value=") + (on ? "1" : "0"));
    return 0;
}

void SampleAimRay()
{
    if (!gEnabled.load(std::memory_order_acquire)) {
        return;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);

    // The landmark is gated exactly like every other engine address here, so a
    // wrong build cannot silently produce plausible numbers.
    const std::uintptr_t rva = GetArkPlayerInstanceRva();
    if (rva == 0) {
        gUnreadable.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto getInstance = reinterpret_cast<GetArkPlayerInstanceFn>(base + rva);
    void* const player = getInstance();
    if (player == nullptr) {
        gUnreadable.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto playerAddress = reinterpret_cast<std::uintptr_t>(player);

    Vec3 origin{};
    Vec3 direction{};
    std::memcpy(&origin,
                reinterpret_cast<const void*>(playerAddress + engine::ArkPlayerLayout::cachedReticleOrigin),
                sizeof(origin));
    std::memcpy(&direction,
                reinterpret_cast<const void*>(playerAddress + engine::ArkPlayerLayout::cachedReticleDirection),
                sizeof(direction));

    const float directionLength = Length(direction);
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(directionLength) || directionLength < 0.5f) {
        gUnreadable.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (gHavePrevious) {
        const Vec3 delta{origin.x - gPreviousOrigin.x,
                         origin.y - gPreviousOrigin.y,
                         origin.z - gPreviousOrigin.z};
        const auto gapMicrometres =
            static_cast<unsigned long long>(Length(delta) * 1.0e6f);
        unsigned long long previousMax = gMaxOriginGapMicrometres.load(std::memory_order_relaxed);
        while (gapMicrometres > previousMax &&
               !gMaxOriginGapMicrometres.compare_exchange_weak(previousMax, gapMicrometres)) {
        }

        const float angleDegrees = AngleBetweenDegrees(gPreviousDirection, direction);
        if (std::isfinite(angleDegrees)) {
            const auto milli = static_cast<unsigned long long>(angleDegrees * 1000.0f);
            unsigned long long previousAngle = gMaxAngleGapMillidegrees.load(std::memory_order_relaxed);
            while (milli > previousAngle &&
                   !gMaxAngleGapMillidegrees.compare_exchange_weak(previousAngle, milli)) {
            }
        }
    }

    gPreviousOrigin = origin;
    gPreviousDirection = direction;
    gHavePrevious = true;
    gSamples.fetch_add(1, std::memory_order_relaxed);
}

unsigned long long AimRaySampleCount()
{
    return gSamples.load(std::memory_order_relaxed);
}

unsigned long long AimRayUnreadableCount()
{
    return gUnreadable.load(std::memory_order_relaxed);
}

unsigned long long AimRayMaxOriginGapMicrometres()
{
    return gMaxOriginGapMicrometres.load(std::memory_order_relaxed);
}

unsigned long long AimRayMaxAngleGapMillidegrees()
{
    return gMaxAngleGapMillidegrees.load(std::memory_order_relaxed);
}

} // namespace preyvr::dll
