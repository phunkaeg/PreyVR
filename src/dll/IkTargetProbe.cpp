#include "IkTargetProbe.h"

#include "DebugWatch.h"
#include "Logger.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_ik_probe " + line);
}

// R-077. The LEA that loads the LHand_IKTarget format string; the transform it is
// about to print lives in r12.
constexpr std::uintptr_t kIkLogLeaRva = 0x878744;
// The byte gating that log path. Set to 1 for a capture, restored afterwards.
constexpr std::uintptr_t kIkLogGateRva = 0x2257810;

// `LEA RAX, [RIP+0x014AE875]` -- read out of the shipped Steam PreyDll.dll
// (SHA-256 7D6E322F...B05311A7) on 2026-09-04, not copied from a listing.
//
// **Why this is checked here rather than added to the landmark table.** The
// landmark gate is fail-closed for the *whole mod*: a mismatch there stops PreyVR
// loading at all. This RVA is a probe target on a build we have one hash for, and
// an unrecognised binary should cost the user a probe, not the mod. So the blast
// radius is kept local -- same standard, right scope.
//
// Without this the execute watch would arm on whatever sits at the offset in a
// different build, and a breakpoint landing mid-instruction traps somewhere
// meaningless or never fires at all.
constexpr std::uint8_t kIkLogLeaBytes[] = {0x48, 0x8D, 0x05, 0x75, 0xE8, 0x4A, 0x01};

bool LeaBytesMatch(const std::uint8_t* base)
{
    __try {
        return std::memcmp(base + kIkLogLeaRva, kIkLogLeaBytes, sizeof(kIkLogLeaBytes)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// R-078: QuatT = Quat(16) then Vec3(12). pos.x is the first float of the position.
constexpr std::uintptr_t kQuatTPositionOffset = 0x10;

constexpr unsigned int kCaptureSlot = 0;
constexpr unsigned int kProducerSlot = 1;
constexpr unsigned int kMaxRows = 32;
constexpr unsigned int kMaxProducers = 16;

std::atomic<bool> gCaptureArmed{false};
std::atomic<unsigned int> gGateOriginal{0xFFFFFFFFu};   // sentinel: never read
std::atomic<bool> gGateWritten{false};
std::atomic<unsigned long long> gProducerAddress{0};

std::uint8_t* PreyDllBase()
{
    return reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"PreyDll.dll"));
}

bool ReadableFloats(const void* address, float* out, std::size_t count)
{
    __try {
        std::memcpy(out, address, count * sizeof(float));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteGate(std::uint8_t value, std::uint8_t& previous)
{
    std::uint8_t* const base = PreyDllBase();
    if (base == nullptr) {
        return false;
    }
    void* const gate = base + kIkLogGateRva;
    DWORD oldProtect = 0;
    if (VirtualProtect(gate, 1, PAGE_EXECUTE_READWRITE, &oldProtect) == 0) {
        return false;
    }
    previous = *static_cast<volatile std::uint8_t*>(gate);
    *static_cast<volatile std::uint8_t*>(gate) = value;
    DWORD restored = 0;
    VirtualProtect(gate, 1, oldProtect, &restored);
    return true;
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

} // namespace

DWORD ArmIkCapture(unsigned int maxCaptures)
{
    std::uint8_t* const base = PreyDllBase();
    if (base == nullptr) {
        Log("result=unavailable detail=no_preydll");
        return 1;
    }
    if (gCaptureArmed.load(std::memory_order_acquire)) {
        Log("result=refused detail=already_armed");
        return 2;
    }
    if (!LeaBytesMatch(base)) {
        // Refuse rather than arm on an offset that means something else in this
        // build. A breakpoint on the wrong byte is not an error anywhere -- it
        // just never fires, and the run reports an absence that was never tested.
        Log("result=refused detail=lea_bytes_mismatch rva=" + Hex(kIkLogLeaRva));
        return 6;
    }
    ClearWatchCaptures();

    std::uint8_t previous = 0;
    if (!WriteGate(1, previous)) {
        Log("result=failed detail=gate_write");
        return 3;
    }
    gGateOriginal.store(previous, std::memory_order_relaxed);
    gGateWritten.store(true, std::memory_order_release);

    const unsigned long long target =
        reinterpret_cast<unsigned long long>(base) + kIkLogLeaRva;
    const DWORD armed = ArmWatch(kCaptureSlot, target, WatchKind::execute,
                                 maxCaptures == 0 ? 64u : maxCaptures);
    if (armed != 0) {
        // Put the gate back rather than leaving the engine in a state we changed
        // for a capture that never started.
        std::uint8_t ignored = 0;
        WriteGate(previous, ignored);
        gGateWritten.store(false, std::memory_order_release);
        Log("result=failed detail=arm_watch code=" + std::to_string(armed));
        return 4;
    }
    gCaptureArmed.store(true, std::memory_order_release);
    Log("result=0 detail=armed lea=" + Hex(target) +
        " gate_was=" + std::to_string(previous));
    return 0;
}

DWORD DisarmIkCapture()
{
    DisarmWatch(kCaptureSlot);
    if (gGateWritten.load(std::memory_order_acquire)) {
        const unsigned int original = gGateOriginal.load(std::memory_order_relaxed);
        std::uint8_t ignored = 0;
        // Restore, then read back. A restore that silently failed would leave the
        // shipped game logging every frame, which is exactly the class of change
        // this project promises never to leave behind.
        const bool wrote = WriteGate(static_cast<std::uint8_t>(original), ignored);
        gGateWritten.store(false, std::memory_order_release);
        Log(std::string("result=0 detail=gate_restored ok=") + (wrote ? "1" : "0") +
            " value=" + std::to_string(original) +
            " readback=" + std::to_string(IkLogGateCurrentValue()));
    }
    gCaptureArmed.store(false, std::memory_order_release);
    Log("result=0 detail=disarmed hits=" + std::to_string(WatchHitCount(kCaptureSlot)));
    return 0;
}

unsigned int IkTargetRowCount()
{
    // Folded on demand rather than maintained live, because the folding must not
    // happen inside the exception handler.
    std::array<unsigned long long, kMaxRows> seen{};
    unsigned int rows = 0;
    const unsigned int captures = WatchCaptureCount();
    for (unsigned int i = 0; i < captures && rows < kMaxRows; ++i) {
        WatchCapture capture{};
        if (!ReadWatchCapture(i, capture) || capture.slot != kCaptureSlot) {
            continue;
        }
        bool known = false;
        for (unsigned int r = 0; r < rows; ++r) {
            if (seen[r] == capture.r12) {
                known = true;
                break;
            }
        }
        if (!known) {
            seen[rows++] = capture.r12;
        }
    }
    return rows;
}

bool ReadIkTargetRow(unsigned int index, IkTargetRow& out)
{
    std::array<unsigned long long, kMaxRows> seen{};
    unsigned int rows = 0;
    const unsigned int captures = WatchCaptureCount();
    for (unsigned int i = 0; i < captures && rows <= index && rows < kMaxRows; ++i) {
        WatchCapture capture{};
        if (!ReadWatchCapture(i, capture) || capture.slot != kCaptureSlot) {
            continue;
        }
        bool known = false;
        for (unsigned int r = 0; r < rows; ++r) {
            if (seen[r] == capture.r12) {
                known = true;
                break;
            }
        }
        if (known) {
            continue;
        }
        if (rows == index) {
            out = IkTargetRow{};
            out.targetAddress = capture.r12;
            out.limbId = capture.rsi;
            out.owner = capture.r13;
            out.skeleton = capture.rdi;
            for (unsigned int j = 0; j < captures; ++j) {
                WatchCapture other{};
                if (ReadWatchCapture(j, other) && other.slot == kCaptureSlot &&
                    other.r12 == capture.r12) {
                    ++out.hits;
                }
            }
            float values[7]{};
            if (capture.r12 != 0 &&
                ReadableFloats(reinterpret_cast<const void*>(capture.r12), values, 7)) {
                out.readable = 1;
                const float magnitude = std::sqrt(values[0] * values[0] +
                                                  values[1] * values[1] +
                                                  values[2] * values[2] +
                                                  values[3] * values[3]);
                out.quatMagnitude =
                    static_cast<unsigned int>(magnitude * 1000.0f + 0.5f);
                out.posX = static_cast<int>(values[4] * 1000.0f);
                out.posY = static_cast<int>(values[5] * 1000.0f);
                out.posZ = static_cast<int>(values[6] * 1000.0f);
            }
            return true;
        }
        seen[rows++] = capture.r12;
    }
    return false;
}

DWORD ArmIkProducerWatch(unsigned long long targetAddress, unsigned int maxCaptures)
{
    if (targetAddress == 0) {
        Log("result=refused detail=null_target");
        return 1;
    }
    const unsigned long long watched = targetAddress + kQuatTPositionOffset;
    const DWORD armed = ArmWatch(kProducerSlot, watched, WatchKind::write4,
                                 maxCaptures == 0 ? 16u : maxCaptures);
    if (armed != 0) {
        Log("result=failed detail=arm_watch code=" + std::to_string(armed) +
            " address=" + Hex(watched));
        return armed;
    }
    gProducerAddress.store(watched, std::memory_order_release);
    Log("result=0 detail=producer_watch_armed address=" + Hex(watched));
    return 0;
}

DWORD DisarmIkProducerWatch()
{
    DisarmWatch(kProducerSlot);
    gProducerAddress.store(0, std::memory_order_release);
    Log("result=0 detail=producer_watch_disarmed hits=" +
        std::to_string(WatchHitCount(kProducerSlot)));
    return 0;
}

namespace {

// Distinct producer RIPs, folded the same way the target rows are.
unsigned int FoldProducers(std::array<unsigned long long, kMaxProducers>& rips,
                           std::array<unsigned int, kMaxProducers>& hits)
{
    unsigned int count = 0;
    const unsigned int captures = WatchCaptureCount();
    const std::uint8_t* const base = PreyDllBase();
    for (unsigned int i = 0; i < captures; ++i) {
        WatchCapture capture{};
        if (!ReadWatchCapture(i, capture) || capture.slot != kProducerSlot) {
            continue;
        }
        unsigned long long rva = capture.rip;
        if (base != nullptr && capture.rip >= reinterpret_cast<unsigned long long>(base)) {
            rva = capture.rip - reinterpret_cast<unsigned long long>(base);
        }
        bool known = false;
        for (unsigned int r = 0; r < count; ++r) {
            if (rips[r] == rva) {
                ++hits[r];
                known = true;
                break;
            }
        }
        if (!known && count < kMaxProducers) {
            rips[count] = rva;
            hits[count] = 1;
            ++count;
        }
    }
    return count;
}

} // namespace

unsigned int IkProducerCount()
{
    std::array<unsigned long long, kMaxProducers> rips{};
    std::array<unsigned int, kMaxProducers> hits{};
    return FoldProducers(rips, hits);
}

unsigned long long IkProducerRva(unsigned int index)
{
    std::array<unsigned long long, kMaxProducers> rips{};
    std::array<unsigned int, kMaxProducers> hits{};
    const unsigned int count = FoldProducers(rips, hits);
    return index < count ? rips[index] : 0ull;
}

unsigned int IkProducerHits(unsigned int index)
{
    std::array<unsigned long long, kMaxProducers> rips{};
    std::array<unsigned int, kMaxProducers> hits{};
    const unsigned int count = FoldProducers(rips, hits);
    return index < count ? hits[index] : 0u;
}

DWORD IkProducerRipIsAfterWrite() { return 1; }

DWORD IkLogGateOriginalValue() { return gGateOriginal.load(std::memory_order_relaxed); }

DWORD IkLogGateCurrentValue()
{
    const std::uint8_t* const base = PreyDllBase();
    if (base == nullptr) {
        return 0xFFFFFFFFu;
    }
    std::uint8_t value = 0;
    __try {
        value = *reinterpret_cast<const volatile std::uint8_t*>(base + kIkLogGateRva);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFFFFFFu;
    }
    return value;
}

DWORD IkCaptureArmed() { return gCaptureArmed.load(std::memory_order_relaxed) ? 1u : 0u; }

} // namespace preyvr::dll
