#include "InputPathProbe.h"

#include "Logger.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_input_path " + line);
}

// R-044: gEnv, the SSystemGlobalEnvironment instance. Verified 24 times over by
// the gEnv-relaying ISystem accessors, and confirmed live 2026-08-29.
constexpr std::uintptr_t kGEnvRva = 0x224D980;

// SYSTEM_VTABLE.md: slot 84 GetIInput relays gEnv->pInput at gEnv+0x58. This is
// **our own verified receipt**, not a guess from the CryEngine 5 headers --
// H-006 recorded this slot as open because that work happened separately and the
// two were never connected.
constexpr std::uintptr_t kPInputOffset = 0x58;

// Candidate vtable index for IInput::PostInputEvent, counted from the CryEngine 5
// public IInput.h with the MSVC scalar-deleting destructor at slot 0:
//
//   0 ~IInput           4 RemoveConsoleEventListener   8 GetExclusiveListener
//   1 AddEventListener  5 AddTouchEventListener        9 AddInputDevice
//   2 RemoveEventListener 6 RemoveTouchEventListener  10 EnableEventPosting
//   3 AddConsoleEventListener 7 SetExclusiveListener  11 IsEventPostingEnabled
//                                                     12 PostInputEvent
//
// **This is INFERENCE and must not be trusted on its own.** Prey is an Arkane
// fork of a much older CryEngine, and the touch-event pair at slots 5 and 6 is
// exactly the kind of thing a older fork will not have -- which would shift
// PostInputEvent to slot 10. The playbook's rule applies directly: an open
// ancestor can name things correctly and still be the wrong bytes.
constexpr unsigned int kPostInputEventSlotGuess = 12;
constexpr unsigned int kSlotsToDump = 20;

std::atomic<unsigned long long> gGEnv{0};
std::atomic<unsigned long long> gInput{0};
std::atomic<unsigned long long> gVtable{0};
std::atomic<unsigned int> gAlignmentSlot{0};
std::atomic<unsigned int> gAlignmentFound{0};
std::atomic<unsigned long long> gPostInputEvent{0};
std::atomic<unsigned long long> gSlots[kSlotsToDump];

bool ReadPointer(std::uintptr_t address, std::uintptr_t& out)
{
    __try {
        out = *reinterpret_cast<const std::uintptr_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadBytes(std::uintptr_t address, std::uint8_t* out, std::size_t count)
{
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string Hex(unsigned long long value)
{
    static const char* const digits = "0123456789abcdef";
    std::string out = "0x";
    bool leading = true;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const unsigned int nibble = static_cast<unsigned int>((value >> shift) & 0xFull);
        if (nibble == 0 && leading && shift != 0) {
            continue;
        }
        leading = false;
        out.push_back(digits[nibble]);
    }
    return out;
}

// The alignment test, and the reason this probe is worth building rather than
// guessing an index.
//
// `EnableEventPosting(bool)` and `IsEventPostingEnabled() const` are adjacent in
// every version of this interface and act on the **same** member. So a correctly
// aligned vtable shows, at some slot n and n+1:
//
//     n     MOV [RCX+d], DL          ; 88 51 dd
//     n+1   MOVZX EAX, BYTE [RCX+d]  ; 0F B6 41 dd  RET   (or MOV AL, 8A 41 dd)
//
// with the *same* displacement `d` -- which is the only load-bearing part; see
// the encodings note in the body, which cost a test cycle to learn. That is the identical trick R-043 used to
// confirm the CSystem vtable and R-054 used for C3DEngine -- a setter and getter
// on one member, predicted by name and matching independently. Finding that pair
// fixes the whole table, and PostInputEvent is then n+2 by construction.
//
// Returns the slot index of the setter, or 0 if the pair was not found.
unsigned int FindEventPostingPair(std::uintptr_t vtable)
{
    for (unsigned int slot = 1; slot + 1 < kSlotsToDump; ++slot) {
        std::uintptr_t setter = 0;
        std::uintptr_t getter = 0;
        if (!ReadPointer(vtable + slot * sizeof(std::uintptr_t), setter) ||
            !ReadPointer(vtable + (slot + 1) * sizeof(std::uintptr_t), getter)) {
            continue;
        }
        std::uint8_t s[8]{};
        std::uint8_t g[8]{};
        if (!ReadBytes(setter, s, sizeof(s)) || !ReadBytes(getter, g, sizeof(g))) {
            continue;
        }
        // **Both shapes are wider than the obvious guess, measured live
        // 2026-09-05.** The first version of this test looked for
        // `MOV [RCX+d],DL; RET` against `MOV AL,[RCX+d]; RET` and found nothing,
        // while the pair was sitting at slots 10/11 the whole time. Two wrong
        // assumptions, both about the compiler rather than the interface:
        //
        //   * the setter does **not** return immediately -- Prey's
        //     `EnableEventPosting` continues into a global check, so requiring a
        //     `RET` at byte 3 rejected it;
        //   * the getter is `MOVZX EAX, BYTE PTR [RCX+d]` (`0F B6 41 dd`), not
        //     `MOV AL, [RCX+d]` (`8A 41 dd`). Returning a bool as a zero-extended
        //     int is the normal thing for MSVC to emit; `MOV AL` was the guess.
        //
        // So: match the setter's store only, and accept either getter encoding.
        // The load-bearing part was always the **shared displacement**, and that
        // is what is still required.
        const bool setterShape = s[0] == 0x88 && s[1] == 0x51;      // mov [rcx+d], dl
        const std::uint8_t setterDisp = s[2];

        bool getterShape = false;
        std::uint8_t getterDisp = 0;
        if (g[0] == 0x0F && g[1] == 0xB6 && g[2] == 0x41) {          // movzx eax, byte [rcx+d]
            getterShape = g[4] == 0xC3;
            getterDisp = g[3];
        } else if (g[0] == 0x8A && g[1] == 0x41) {                   // mov al, [rcx+d]
            getterShape = g[3] == 0xC3;
            getterDisp = g[2];
        }

        if (setterShape && getterShape && setterDisp == getterDisp) {
            return slot;
        }
    }
    return 0;
}

} // namespace

DWORD ResolveInputPath()
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        Log("result=unavailable detail=no_preydll");
        return 1;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    const std::uintptr_t gEnv = base + kGEnvRva;
    gGEnv.store(gEnv, std::memory_order_relaxed);

    std::uintptr_t input = 0;
    if (!ReadPointer(gEnv + kPInputOffset, input) || input == 0) {
        // A null pInput is a real answer, not a failure: it means the input system
        // is not up yet, and the caller should retry once in a level.
        Log("result=0 detail=pinput_null gEnv=" + Hex(gEnv));
        return 2;
    }
    gInput.store(input, std::memory_order_relaxed);

    std::uintptr_t vtable = 0;
    if (!ReadPointer(input, vtable) || vtable == 0) {
        Log("result=failed detail=vtable_unreadable pInput=" + Hex(input));
        return 3;
    }
    gVtable.store(vtable, std::memory_order_relaxed);

    std::string dump;
    for (unsigned int slot = 0; slot < kSlotsToDump; ++slot) {
        std::uintptr_t entry = 0;
        ReadPointer(vtable + slot * sizeof(std::uintptr_t), entry);
        const unsigned long long rva = (entry > base) ? (entry - base) : 0ull;
        gSlots[slot].store(rva, std::memory_order_relaxed);
        dump += " " + std::to_string(slot) + "=" + Hex(rva);
    }

    const unsigned int setterSlot = FindEventPostingPair(vtable);
    gAlignmentSlot.store(setterSlot, std::memory_order_relaxed);
    gAlignmentFound.store(setterSlot != 0 ? 1u : 0u, std::memory_order_relaxed);

    // Prefer the measured alignment over the header-derived guess, always. If the
    // pair was not found the guess is published anyway, but flagged unconfirmed --
    // so the number is never mistaken for a verified one.
    const unsigned int resolvedSlot =
        setterSlot != 0 ? setterSlot + 2 : kPostInputEventSlotGuess;
    std::uintptr_t postInputEvent = 0;
    ReadPointer(vtable + resolvedSlot * sizeof(std::uintptr_t), postInputEvent);
    gPostInputEvent.store(
        (postInputEvent > base) ? (postInputEvent - base) : 0ull,
        std::memory_order_relaxed);

    Log("result=0 detail=resolved gEnv=" + Hex(gEnv) + " pInput=" + Hex(input) +
        " vtable=" + Hex(vtable) +
        " alignment_pair_slot=" + std::to_string(setterSlot) +
        " confirmed=" + (setterSlot != 0 ? "1" : "0") +
        " post_input_event_slot=" + std::to_string(resolvedSlot) +
        " post_input_event_rva=" + Hex(gPostInputEvent.load(std::memory_order_relaxed)));
    Log("result=0 detail=vtable_slots" + dump);
    return 0;
}

unsigned long long InputPathGEnv() { return gGEnv.load(std::memory_order_relaxed); }
unsigned long long InputPathInputPointer() { return gInput.load(std::memory_order_relaxed); }
unsigned long long InputPathVtable() { return gVtable.load(std::memory_order_relaxed); }
DWORD InputPathAlignmentSlot() { return gAlignmentSlot.load(std::memory_order_relaxed); }
DWORD InputPathAlignmentConfirmed() { return gAlignmentFound.load(std::memory_order_relaxed); }
unsigned long long InputPathPostInputEventRva()
{
    return gPostInputEvent.load(std::memory_order_relaxed);
}

unsigned long long InputPathSlotRva(unsigned int slot)
{
    if (slot >= kSlotsToDump) {
        return 0;
    }
    return gSlots[slot].load(std::memory_order_relaxed);
}

} // namespace preyvr::dll
