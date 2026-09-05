#include "RenderFrame.h"

#include "Logger.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cmath>
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

constexpr unsigned int kSlots = 8;

struct Slot {
    std::atomic<unsigned long long> character{0};
    float matrix[12]{};
    std::atomic<bool> valid{false};
};

std::array<Slot, kSlots> gSlots;
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

void* __fastcall RenderCharacterObserved(void* character, void* params,
                                         const float* matrix, void* pass)
{
    if (gEnabled.load(std::memory_order_acquire) && character != nullptr && matrix != nullptr) {
        __try {
            if (LooksLikeMatrix(matrix)) {
                const auto key = reinterpret_cast<unsigned long long>(character);
                // Reuse this character's slot, else take a free one. A full table
                // simply stops tracking new characters rather than evicting a
                // live one out from under a reader.
                int chosen = -1;
                for (unsigned int i = 0; i < kSlots; ++i) {
                    if (gSlots[i].character.load(std::memory_order_relaxed) == key) {
                        chosen = static_cast<int>(i);
                        break;
                    }
                }
                if (chosen < 0) {
                    for (unsigned int i = 0; i < kSlots; ++i) {
                        unsigned long long expected = 0;
                        if (gSlots[i].character.compare_exchange_strong(
                                expected, key, std::memory_order_acq_rel)) {
                            chosen = static_cast<int>(i);
                            break;
                        }
                    }
                }
                if (chosen >= 0) {
                    Slot& slot = gSlots[static_cast<std::size_t>(chosen)];
                    // Cleared first so a reader cannot see a half-written matrix
                    // as valid. This is a torn-read guard, not a full seqlock --
                    // a reader may still miss an update, which is correct here
                    // because a stale frame is better than a spliced one.
                    slot.valid.store(false, std::memory_order_release);
                    std::memcpy(slot.matrix, matrix, sizeof(slot.matrix));
                    slot.valid.store(true, std::memory_order_release);
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

const Slot* FindSlot(unsigned long long character)
{
    for (unsigned int i = 0; i < kSlots; ++i) {
        if (gSlots[i].character.load(std::memory_order_acquire) == character &&
            gSlots[i].valid.load(std::memory_order_acquire)) {
            return &gSlots[i];
        }
    }
    return nullptr;
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
    const Slot* const slot = FindSlot(character);
    if (slot == nullptr) {
        return false;
    }
    std::memcpy(out, slot->matrix, sizeof(float) * 12);
    return true;
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

unsigned long long RenderFrameCaptureCount() { return gCaptures.load(std::memory_order_relaxed); }

unsigned int RenderFrameTrackedCharacters()
{
    unsigned int count = 0;
    for (unsigned int i = 0; i < kSlots; ++i) {
        if (gSlots[i].valid.load(std::memory_order_acquire)) {
            ++count;
        }
    }
    return count;
}

int RenderFrameBasisMicro(unsigned long long character, unsigned int index)
{
    float m[12]{};
    if (index >= 12 || !TryGetRenderMatrix(character, m)) {
        return 0;
    }
    return static_cast<int>(m[index] * 1.0e6f);
}

} // namespace preyvr::dll
