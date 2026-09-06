#include "RenderFrame.h"
#include "MinHookInit.h"

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
using RenderCharacterFn = void*(__fastcall*)(void*, void*, float*, void*);

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

// --- the per-frame transform override ---------------------------------------
//
// The character is named explicitly rather than inferred from the near flag.
// Holding a weapon produces **two** near objects -- the viewmodel arms and the
// weapon -- so "drive the near one" would pick one of them by draw order, which
// is exactly the kind of silent wrong choice the near predicate exists to end.
std::atomic<unsigned long long> gOverrideCharacter{0};
std::atomic<bool> gOverrideEnabled{false};
std::atomic<int> gOverrideMm[3]{};
std::atomic<unsigned long long> gOverrideApplied{0}, gOverrideRefused{0};

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
// The decoding lives in `preyvr_core` and is fixture-tested; this adds only the
// fault guard, which cannot live in a translation unit a portable test links.
bool NearestFromArguments(const void* params, const void* character)
{
    __try {
        return renderframe::NearestFromRenderArguments(params, character);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* __fastcall RenderCharacterObserved(void* character, void* params,
                                         float* matrix, void* pass)
{
    if (gEnabled.load(std::memory_order_acquire) && character != nullptr && matrix != nullptr) {
        __try {
            const bool nearest = NearestFromArguments(params, character);
            if (LooksLikeMatrix(matrix)) {
                const auto key = reinterpret_cast<unsigned long long>(character);
                if (gTable.Capture(key, matrix, nearest)) {
                    gCaptures.fetch_add(1, std::memory_order_relaxed);
                }
                // **Edited before forwarding, which is the whole point.**
                // `RenderCHR` copies these twelve floats into `CRenderObject+0x00`
                // further down its own body, so a write here is a per-frame
                // transform rather than an attachment default (H-017).
                if (gOverrideEnabled.load(std::memory_order_acquire) &&
                    gOverrideCharacter.load(std::memory_order_acquire) == key) {
                    const float x = static_cast<float>(
                        gOverrideMm[0].load(std::memory_order_relaxed)) / 1000.0f;
                    const float y = static_cast<float>(
                        gOverrideMm[1].load(std::memory_order_relaxed)) / 1000.0f;
                    const float z = static_cast<float>(
                        gOverrideMm[2].load(std::memory_order_relaxed)) / 1000.0f;
                    if (renderframe::ApplyRenderMatrixOverride(matrix, x, y, z,
                                                               0.0f, 0.0f, 0.0f, 1.0f)) {
                        gOverrideApplied.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        gOverrideRefused.fetch_add(1, std::memory_order_relaxed);
                    }
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
    // Shared and idempotent: without it MH_CreateHook returns
    // MH_ERROR_NOT_INITIALIZED and the feature reports "unavailable" for a
    // reason unrelated to itself. See MinHookInit.h.
    EnsureMinHook();
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

DWORD SetRenderFrameOverrideCharacter(unsigned long long character)
{
    gOverrideCharacter.store(character, std::memory_order_release);
    Log("result=0 detail=override_character value=" + std::to_string(character));
    return 0;
}

DWORD SetRenderFrameOffsetMillimetres(int x, int y, int z)
{
    gOverrideMm[0].store(x, std::memory_order_relaxed);
    gOverrideMm[1].store(y, std::memory_order_relaxed);
    gOverrideMm[2].store(z, std::memory_order_relaxed);
    Log("result=0 detail=override_offset_mm x=" + std::to_string(x) +
        " y=" + std::to_string(y) + " z=" + std::to_string(z));
    return 0;
}

DWORD SetRenderFrameOverrideEnabled(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on && gOverrideCharacter.load(std::memory_order_acquire) == 0) {
        // Refused rather than defaulted: an override with no subject would
        // either do nothing or, worse, be pointed at whatever came first.
        Log("result=refused detail=no_override_character");
        return 1;
    }
    gOverrideEnabled.store(on, std::memory_order_release);
    Log(std::string("result=0 detail=override_enabled value=") + (on ? "1" : "0"));
    return 0;
}

unsigned long long RenderFrameOverrideAppliedCount()
{
    return gOverrideApplied.load(std::memory_order_relaxed);
}

unsigned long long RenderFrameOverrideRefusedCount()
{
    return gOverrideRefused.load(std::memory_order_relaxed);
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
