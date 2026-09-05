#include "RenderFrame.h"

#include "Logger.h"

#include "preyvr/RenderFrameTable.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_render_frame " + line);
}

// RenderCHR: RCX character, RDX SRendParams*, R8 Matrix34*, R9 pass info.
using RenderCharacterFn = void*(__fastcall*)(void*, void*, const float*, void*);

constexpr std::uintptr_t kRenderCharacterRva = 0x81D0D0;

// **18 bytes, stopping before the `mov rcx,[rip+disp32]` at offset 18.** A
// signature carrying a RIP-relative displacement encodes a distance to a global
// and is exact for one build only -- RE-010, and this project already has three
// signatures recorded as mistakes for exactly that.
constexpr std::array<std::uint8_t, 18> kRenderCharacterPrologue{
    0x40, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xF9, 0x48,
    0x8B, 0xF2,
};

// The slot table and its seqlock live in `preyvr_core` so they can be tested.
// They used to sit here, where a threaded test could not reach them because
// this translation unit is bound to MinHook and the game module -- and the
// first version of the publication was wrong in a way that reads as entirely
// plausible numbers. See `preyvr/RenderFrameTable.h`.
renderframe::MatrixTable gTable;
std::atomic<unsigned long long> gCaptures{0};

void* gTarget = nullptr;
std::atomic<RenderCharacterFn> gOriginal{nullptr};
bool gInstalled = false;
std::atomic<bool> gEnabled{false};

bool LooksLikeMatrix(const float* m)
{
    for (int i = 0; i < 12; ++i) {
        if (!std::isfinite(m[i])) {
            return false;
        }
    }
    // Basis vectors are COLUMNS. A degenerate basis means this is not the matrix
    // we think it is, and using it would silently produce nonsense rather than an
    // error.
    const float xLen = std::sqrt(m[0]*m[0] + m[4]*m[4] + m[8]*m[8]);
    return xLen > 0.001f && xLen < 1000.0f;
}

// RenderCHR's own near predicate, read from arguments it already has (R-089).
// This is the test the function itself performs at `0x81D127`/`0x81D141` before
// setting or clearing `FOB_NEAREST` on the render object, so it needs no
// mid-function marker and **no injector** -- which is the whole point, given that
// `frida-agent.dll` crashed the host four times in one session.
//
// The false path *clears* the bit rather than leaving it, so a stale flag on a
// pooled render object cannot make this read wrong.
bool NearestFromArguments(const std::uint8_t* params, const std::uint8_t* character)
{
    __try {
        if (params != nullptr &&
            (*reinterpret_cast<const std::uint32_t*>(params + 0x80) & 0x00800000u) != 0u) {
            return true;
        }
        if (character != nullptr && (*(character + 0xAC8) & 0x02u) != 0u) {
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

void* __fastcall RenderCharacterObserved(void* character, void* params,
                                         const float* matrix, void* pass)
{
    if (gEnabled.load(std::memory_order_acquire) && character != nullptr && matrix != nullptr) {
        __try {
            const bool nearest = NearestFromArguments(
                static_cast<const std::uint8_t*>(params),
                static_cast<const std::uint8_t*>(character));
            if (LooksLikeMatrix(matrix)) {
                const auto key = reinterpret_cast<unsigned long long>(character);
                if (gTable.Capture(key, matrix, nearest)) {
                    gCaptures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    const RenderCharacterFn original = gOriginal.load(std::memory_order_acquire);
    return original != nullptr ? original(character, params, matrix, pass) : nullptr;
}

bool Install()
{
    if (gInstalled) {
        return true;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto target = reinterpret_cast<std::uintptr_t>(preyDll) + kRenderCharacterRva;
    if (std::memcmp(reinterpret_cast<const void*>(target),
                    kRenderCharacterPrologue.data(), kRenderCharacterPrologue.size()) != 0) {
        Log("result=unavailable detail=render_chr_prologue_mismatch");
        return false;
    }
    gTarget = reinterpret_cast<void*>(target);
    RenderCharacterFn original = nullptr;
    if (MH_CreateHook(gTarget, reinterpret_cast<void*>(&RenderCharacterObserved),
                      reinterpret_cast<void**>(&original)) != MH_OK) {
        Log("result=failed detail=create_hook");
        return false;
    }
    gOriginal.store(original, std::memory_order_release);
    if (MH_EnableHook(gTarget) != MH_OK) {
        Log("result=failed detail=enable_hook");
        MH_RemoveHook(gTarget);
        return false;
    }
    gInstalled = true;
    Log("result=0 detail=hook_installed target=RenderCHR rva=0x81D0D0");
    return true;
}

} // namespace

DWORD SetRenderFrameCapture(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on && !Install()) {
        return 1;
    }
    gEnabled.store(on, std::memory_order_release);
    Log(std::string("result=0 detail=enabled value=") + (on ? "1" : "0"));
    return 0;
}

bool TryGetRenderMatrix(unsigned long long character, float out[12])
{
    return gTable.Read(character, out, nullptr);
}

bool RenderMatrixIsNear(unsigned long long character)
{
    float ignored[12]{};
    bool nearest = false;
    return gTable.Read(character, ignored, &nearest) && nearest;
}

bool RenderMatrixBasisIsOrthonormal(unsigned long long character)
{
    float m[12]{};
    if (!TryGetRenderMatrix(character, m)) {
        return false;
    }
    // Columns.
    const float x[3] = {m[0], m[4], m[8]};
    const float y[3] = {m[1], m[5], m[9]};
    const float z[3] = {m[2], m[6], m[10]};
    const auto length = [](const float v[3]) {
        return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    };
    const auto dot = [](const float a[3], const float b[3]) {
        return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
    };
    const float unit = 0.01f;
    if (std::fabs(length(x) - 1.0f) > unit || std::fabs(length(y) - 1.0f) > unit ||
        std::fabs(length(z) - 1.0f) > unit) {
        return false;   // scale present: an inverse-by-transpose would be wrong
    }
    const float square = 0.01f;
    return std::fabs(dot(x, y)) < square && std::fabs(dot(x, z)) < square &&
           std::fabs(dot(y, z)) < square;
}

bool RenderFrameSlot(unsigned int index, unsigned long long* character, bool* nearest)
{
    return gTable.SlotAt(index, character, nearest);
}

unsigned long long RenderFrameCaptureCount() { return gCaptures.load(std::memory_order_relaxed); }

unsigned int RenderFrameTrackedCharacters() { return gTable.Tracked(); }

int RenderFrameBasisMicro(unsigned long long character, unsigned int index)
{
    float m[12]{};
    if (index >= 12 || !TryGetRenderMatrix(character, m)) {
        return 0;
    }
    return static_cast<int>(m[index] * 1.0e6f);
}

} // namespace preyvr::dll
