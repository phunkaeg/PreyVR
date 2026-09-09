#include "NearFovOverride.h"

#include "Logger.h"
#include "preyvr/EngineMap.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

namespace preyvr::dll {
namespace {

// R-069: `MOVSS [RBX+0x95B4],XMM0` in `CD3D9Renderer::RT_BeginFrame` is the
// latch. The offset is static-only; the acceptance test is that the value read
// back here is a plausible field of view before we ever write one.
constexpr std::uintptr_t kDrawNearFovLatched = 0x95B4;

std::atomic<unsigned int> gDeciDegrees{0};
std::atomic<unsigned long long> gApplied{0}, gRefused{0};
std::atomic<unsigned int> gObserved{0};

float* LatchedNearFov()
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) { return nullptr; }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    auto* const renderer =
        *reinterpret_cast<std::uint8_t**>(base + engine::RendererLayout::singletonPointerRva);
    if (renderer == nullptr) { return nullptr; }
    return reinterpret_cast<float*>(renderer + kDrawNearFovLatched);
}

} // namespace

DWORD SetNearFovDeciDegrees(unsigned int deciDegrees)
{
    if (deciDegrees > 1790u) {
        lifecycle::Log("preyvr_near_fov result=refused detail=out_of_range");
        return 1;
    }
    gDeciDegrees.store(deciDegrees, std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_near_fov result=0 detail=near_fov deciDegrees=" << deciDegrees
         << " applied=" << gApplied.load(std::memory_order_relaxed)
         << " refused=" << gRefused.load(std::memory_order_relaxed);
    lifecycle::Log(line.str());
    return 0;
}

DWORD NearFovDeciDegrees() { return gDeciDegrees.load(std::memory_order_acquire); }

void AssertNearFovForRender()
{
    const unsigned int wanted = gDeciDegrees.load(std::memory_order_acquire);
    if (wanted == 0) { return; }

    float* const latched = LatchedNearFov();
    if (latched == nullptr) {
        gRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Read before writing. Without this the premise is unfalsifiable: a lane
    // that only ever writes cannot show that anything else was writing too.
    const float observed = *latched;
    if (std::isfinite(observed) && observed > 0.0f && observed < 180.0f) {
        gObserved.store(static_cast<unsigned int>(observed * 10.0f + 0.5f),
                        std::memory_order_relaxed);
    } else {
        // Not a field of view. Refuse rather than write into whatever this is:
        // the offset is static-only, and an implausible read is the cheapest
        // sign that it does not point where R-069 says it does.
        gRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    *latched = static_cast<float>(wanted) / 10.0f;
    gApplied.fetch_add(1, std::memory_order_relaxed);
}

unsigned long long NearFovAppliedCount() { return gApplied.load(std::memory_order_relaxed); }
unsigned long long NearFovRefusedCount() { return gRefused.load(std::memory_order_relaxed); }
DWORD NearFovObservedDeciDegrees() { return gObserved.load(std::memory_order_relaxed); }

} // namespace preyvr::dll
