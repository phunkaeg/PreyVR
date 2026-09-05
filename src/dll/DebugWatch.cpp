#include "DebugWatch.h"

#include "Logger.h"

#include "preyvr/DebugRegisters.h"

#include <tlhelp32.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_debug_watch " + line);
}

constexpr unsigned int kSlots = 4;
// Fixed and preallocated: the writer runs inside an exception handler on a game
// thread, where allocating is a bad idea and blocking would be a worse one.
constexpr unsigned int kRingSize = 256;

struct SlotState {
    std::atomic<bool> armed{false};
    std::atomic<unsigned long long> address{0};
    std::atomic<unsigned int> kind{0};
    std::atomic<unsigned long long> hits{0};
    std::atomic<unsigned int> maxCaptures{0};
    std::atomic<unsigned int> stored{0};

    // Apply mode: when this slot traps and the trapping RIP is `applyMatchRip`,
    // add `applyOffset` to the Vec3 at `applyVec3`.
    //
    // **Why in the handler and not in a hook.** The site that owns the final value
    // (R-082, `0x87BC36`) is mid-function, so MinHook cannot take it without
    // patching exact instruction bytes. A data write watch already traps there,
    // and traps *after the store retires* -- which is precisely where an override
    // has to land to survive. The mechanism we built to find the writer turns out
    // to be the mechanism for overriding it.
    std::atomic<bool> applyEnabled{false};
    std::atomic<unsigned long long> applyMatchRip{0};
    std::atomic<unsigned long long> applyVec3{0};
    std::atomic<float> applyOffsetX{0.0f};
    std::atomic<float> applyOffsetY{0.0f};
    std::atomic<float> applyOffsetZ{0.0f};
    // Bounded by construction: an override left armed would be a permanent edit
    // to someone's animation with no way to notice.
    std::atomic<long long> applyDeadlineQpc{0};
};

std::atomic<unsigned long long> gApplyApplied{0};
std::atomic<unsigned long long> gApplySkipped{0};
std::atomic<unsigned long long> gApplyExpired{0};

long long QpcNowRaw()
{
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

std::array<SlotState, kSlots> gSlots;
std::array<WatchCapture, kRingSize> gRing;
std::atomic<unsigned int> gRingCount{0};
std::atomic<unsigned long long> gSequence{0};
std::atomic<unsigned long long> gForeignTraps{0};
std::atomic<unsigned int> gArmedThreads{0};
std::atomic<unsigned int> gMissedThreads{0};

void* gHandler = nullptr;
CRITICAL_SECTION gLock;
std::atomic<bool> gLockReady{false};

void EnsureLock()
{
    if (!gLockReady.load(std::memory_order_acquire)) {
        InitializeCriticalSection(&gLock);
        gLockReady.store(true, std::memory_order_release);
    }
}

void ApplySlotToContext(CONTEXT& context, unsigned int slot, unsigned long long address,
                        WatchKind kind, bool enable)
{
    switch (slot) {
    case 0: context.Dr0 = enable ? address : 0; break;
    case 1: context.Dr1 = enable ? address : 0; break;
    case 2: context.Dr2 = enable ? address : 0; break;
    case 3: context.Dr3 = enable ? address : 0; break;
    default: return;
    }
    context.Dr7 = debugreg::ApplyDr7(context.Dr7, slot, kind, enable);
}

bool SlotIsFull(const SlotState& state)
{
    return state.stored.load(std::memory_order_relaxed) >=
           state.maxCaptures.load(std::memory_order_relaxed);
}

// Writes the currently-armed slot table into one thread debug-register set.
bool ApplyToThread(HANDLE thread)
{
    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &context) == 0) {
        return false;
    }
    for (unsigned int slot = 0; slot < kSlots; ++slot) {
        // A full slot is deliberately not re-armed. Trapping costs a kernel
        // round-trip per hit, and once the quota is met every further trap buys
        // nothing -- so a watch left armed on a per-frame site stops taxing the
        // game instead of doing so until someone remembers to disarm it.
        const bool armed = gSlots[slot].armed.load(std::memory_order_acquire) &&
                           !SlotIsFull(gSlots[slot]);
        ApplySlotToContext(
            context, slot,
            gSlots[slot].address.load(std::memory_order_relaxed),
            static_cast<WatchKind>(gSlots[slot].kind.load(std::memory_order_relaxed)),
            armed);
    }
    context.Dr6 = 0;
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    return SetThreadContext(thread, &context) != 0;
}

unsigned long long ReadStackTop(unsigned long long rsp)
{
    if (rsp == 0) {
        return 0;
    }
    // Guarded: a stack pointer at a trap is almost always readable, but "almost"
    // inside an exception handler is not good enough.
    __try {
        return *reinterpret_cast<const unsigned long long*>(rsp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
{
    if (info == nullptr || info->ExceptionRecord == nullptr || info->ContextRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (info->ExceptionRecord->ExceptionCode != static_cast<DWORD>(EXCEPTION_SINGLE_STEP)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    CONTEXT& context = *info->ContextRecord;
    const unsigned long long which = debugreg::FiredSlotsFromDr6(context.Dr6);
    if (which == 0) {
        // A single-step that no debug register claims -- someone elses, or a trap
        // flag. Counting these is what distinguishes "nothing wrote it" from
        // "something else owns the debug registers".
        gForeignTraps.fetch_add(1, std::memory_order_relaxed);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    bool anyExecute = false;
    for (unsigned int slot = 0; slot < kSlots; ++slot) {
        if ((which & (1ull << slot)) == 0) {
            continue;
        }
        SlotState& state = gSlots[slot];
        if (!state.armed.load(std::memory_order_acquire)) {
            continue;
        }
        if (static_cast<WatchKind>(state.kind.load(std::memory_order_relaxed)) ==
            WatchKind::execute) {
            anyExecute = true;
        }
        state.hits.fetch_add(1, std::memory_order_relaxed);

        // Apply runs before the capture bookkeeping and is independent of the
        // capture quota: an override must keep working after the ring is full,
        // or it would stop silently mid-test.
        if (state.applyEnabled.load(std::memory_order_acquire)) {
            if (QpcNowRaw() > state.applyDeadlineQpc.load(std::memory_order_relaxed)) {
                state.applyEnabled.store(false, std::memory_order_release);
                gApplyExpired.fetch_add(1, std::memory_order_relaxed);
            } else if (context.Rip != state.applyMatchRip.load(std::memory_order_relaxed)) {
                // A write from some other site in the cycle. Left alone -- only
                // the LAST writer's result is worth overriding, and editing an
                // intermediate one just gets overwritten again.
                gApplySkipped.fetch_add(1, std::memory_order_relaxed);
            } else {
                auto* const v = reinterpret_cast<float*>(
                    static_cast<std::uintptr_t>(state.applyVec3.load(std::memory_order_relaxed)));
                // Read-modify-write against what the engine just stored, so the
                // hand keeps following the animation and is only displaced from it.
                const float nx = v[0] + state.applyOffsetX.load(std::memory_order_relaxed);
                const float ny = v[1] + state.applyOffsetY.load(std::memory_order_relaxed);
                const float nz = v[2] + state.applyOffsetZ.load(std::memory_order_relaxed);
                if (std::isfinite(nx) && std::isfinite(ny) && std::isfinite(nz)) {
                    v[0] = nx; v[1] = ny; v[2] = nz;
                    gApplyApplied.fetch_add(1, std::memory_order_relaxed);
                } else {
                    gApplySkipped.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        // **An applying slot must never self-disarm on the capture quota.** The
        // quota exists so a hot path cannot fill memory; an override has nothing
        // to do with memory and needs the trap to keep firing. Without this the
        // takeover would work for the first 16 traps and then stop, with every
        // counter still looking healthy -- the exact failure shape this project
        // keeps meeting.
        if (SlotIsFull(state) && !state.applyEnabled.load(std::memory_order_acquire)) {
            // Quota met. Clear this slot out of *this* thread debug registers so
            // the trap stops costing anything here; other threads drop it as they
            // hit it, and RearmThreads will not put it back.
            ApplySlotToContext(context, slot, 0, WatchKind::execute, false);
            continue;
        }
        const unsigned int index = gRingCount.fetch_add(1, std::memory_order_relaxed);
        if (index >= kRingSize) {
            gRingCount.store(kRingSize, std::memory_order_relaxed);
            continue;
        }
        state.stored.fetch_add(1, std::memory_order_relaxed);

        WatchCapture& out = gRing[index];
        out.rip = context.Rip;
        out.rax = context.Rax; out.rcx = context.Rcx;
        out.rdx = context.Rdx; out.rbx = context.Rbx;
        out.rsp = context.Rsp; out.rbp = context.Rbp;
        out.rsi = context.Rsi; out.rdi = context.Rdi;
        out.r8  = context.R8;  out.r9  = context.R9;
        out.r10 = context.R10; out.r11 = context.R11;
        out.r12 = context.R12; out.r13 = context.R13;
        out.r14 = context.R14; out.r15 = context.R15;
        out.stackTop = ReadStackTop(context.Rsp);
        out.threadId = GetCurrentThreadId();
        out.slot = slot;
        out.sequence = gSequence.fetch_add(1, std::memory_order_relaxed);
    }

    // Clear the status bits, or the next trap cannot be told apart from this one:
    // Dr6 is sticky, the CPU never clears it, and stale bits would misattribute
    // the next trap to a slot that did not fire.
    context.Dr6 = 0;
    // **Both that clear and the self-disarm above are only honoured if the resume
    // actually restores debug registers**, and the context handed to a vectored
    // handler does not ask for them by default. Without this line the writes above
    // are silently dropped and everything still *looks* like it works -- the same
    // failure shape as a misaligned watch. Ask for them explicitly.
    context.ContextFlags |= CONTEXT_DEBUG_REGISTERS;
    if (anyExecute) {
        // An execute breakpoint is a *fault*: it reports before the instruction
        // runs, so resuming without the Resume Flag traps on the same byte again,
        // forever. RF suppresses exactly one re-trigger. Data breakpoints are
        // traps -- they report after the write -- and must not set it.
        context.EFlags |= 0x00010000u;
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

bool EnsureHandler()
{
    if (gHandler != nullptr) {
        return true;
    }
    gHandler = AddVectoredExceptionHandler(1, &OnException);
    if (gHandler == nullptr) {
        Log("result=failed detail=add_veh");
        return false;
    }
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

DWORD RearmThreads()
{
    EnsureLock();
    EnterCriticalSection(&gLock);
    unsigned int armed = 0;
    unsigned int missed = 0;
    const DWORD self = GetCurrentThreadId();
    const DWORD process = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&gLock);
        Log("result=failed detail=thread_snapshot");
        return 1;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry) != 0) {
        do {
            if (entry.th32OwnerProcessID != process) {
                continue;
            }
            if (entry.th32ThreadID == self) {
                // Skipped deliberately: suspending the thread doing the arming
                // would deadlock, and the producer being hunted is a game thread.
                // Counted as missed so the result is never read as full coverage.
                ++missed;
                continue;
            }
            const HANDLE thread = OpenThread(
                THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                FALSE, entry.th32ThreadID);
            if (thread == nullptr) {
                ++missed;
                continue;
            }
            if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
                CloseHandle(thread);
                ++missed;
                continue;
            }
            if (ApplyToThread(thread)) {
                ++armed;
            } else {
                ++missed;
            }
            ResumeThread(thread);
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry) != 0);
    }
    CloseHandle(snapshot);
    gArmedThreads.store(armed, std::memory_order_relaxed);
    gMissedThreads.store(missed, std::memory_order_relaxed);
    LeaveCriticalSection(&gLock);
    Log("result=0 detail=threads armed=" + std::to_string(armed) +
        " missed=" + std::to_string(missed));
    return 0;
}

DWORD ArmWatch(unsigned int slot, unsigned long long address, WatchKind kind,
               unsigned int maxCaptures)
{
    if (slot >= kSlots) {
        Log("result=refused detail=slot_out_of_range slot=" + std::to_string(slot));
        return 1;
    }
    if (address == 0) {
        Log("result=refused detail=null_address");
        return 1;
    }
    if (!debugreg::AddressIsAligned(address, kind)) {
        // **A misaligned data breakpoint never fires.** Arming one would produce a
        // confident zero -- the exact failure shape this project keeps being bitten
        // by, a measurement that cannot show the thing it claims to test. Refuse.
        Log("result=refused detail=misaligned address=" + Hex(address) +
            " required_alignment=" +
            std::to_string(debugreg::EncodingFor(kind).alignMask + 1));
        return 2;
    }
    if (gSlots[slot].armed.load(std::memory_order_acquire)) {
        Log("result=refused detail=slot_busy slot=" + std::to_string(slot));
        return 3;
    }
    if (!EnsureHandler()) {
        return 4;
    }
    gSlots[slot].address.store(address, std::memory_order_relaxed);
    gSlots[slot].kind.store(static_cast<unsigned int>(kind), std::memory_order_relaxed);
    gSlots[slot].hits.store(0, std::memory_order_relaxed);
    gSlots[slot].stored.store(0, std::memory_order_relaxed);
    gSlots[slot].maxCaptures.store(maxCaptures == 0 ? 1u : maxCaptures,
                                   std::memory_order_relaxed);
    gSlots[slot].armed.store(true, std::memory_order_release);
    const DWORD result = RearmThreads();
    if (result != 0 || gArmedThreads.load(std::memory_order_relaxed) == 0) {
        gSlots[slot].armed.store(false, std::memory_order_release);
        Log("result=failed detail=no_threads_armed slot=" + std::to_string(slot));
        return 5;
    }
    static const char* const kKindNames[] = {"execute", "write4", "write8"};
    Log(std::string("result=0 detail=armed slot=") + std::to_string(slot) +
        " kind=" + kKindNames[static_cast<unsigned int>(kind)] +
        " address=" + Hex(address) +
        " max_captures=" + std::to_string(maxCaptures));
    return 0;
}

DWORD ArmApplyOffset(unsigned int slot, unsigned long long vec3Address,
                     unsigned long long matchRip, float dx, float dy, float dz,
                     unsigned int seconds)
{
    if (slot >= kSlots) {
        return 1;
    }
    // The Vec3 must be 4-aligned for the same reason the watch address must be:
    // an unaligned float write is not a crash, it is a wrong number.
    if (vec3Address == 0 || (vec3Address % 4) != 0 || matchRip == 0) {
        return 2;
    }
    // A hand does not move ten metres. A larger request is a caller error, and
    // this is memory belonging to a running game.
    const float magnitude = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(magnitude) || magnitude > 10.0f) {
        return 3;
    }
    if (seconds == 0 || seconds > 120) {
        return 4;
    }
    SlotState& state = gSlots[slot];
    if (!state.armed.load(std::memory_order_acquire)) {
        return 5;   // nothing is trapping, so nothing would ever apply
    }
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    state.applyVec3.store(vec3Address, std::memory_order_relaxed);
    state.applyMatchRip.store(matchRip, std::memory_order_relaxed);
    state.applyOffsetX.store(dx, std::memory_order_relaxed);
    state.applyOffsetY.store(dy, std::memory_order_relaxed);
    state.applyOffsetZ.store(dz, std::memory_order_relaxed);
    state.applyDeadlineQpc.store(
        QpcNowRaw() + static_cast<long long>(seconds) * (freq.QuadPart ? freq.QuadPart : 1),
        std::memory_order_relaxed);
    gApplyApplied.store(0, std::memory_order_relaxed);
    gApplySkipped.store(0, std::memory_order_relaxed);
    gApplyExpired.store(0, std::memory_order_relaxed);
    state.applyEnabled.store(true, std::memory_order_release);
    return 0;
}

DWORD DisarmApplyOffset(unsigned int slot)
{
    if (slot >= kSlots) {
        return 1;
    }
    gSlots[slot].applyEnabled.store(false, std::memory_order_release);
    return 0;
}

unsigned long long ApplyOffsetAppliedCount() { return gApplyApplied.load(std::memory_order_relaxed); }
unsigned long long ApplyOffsetSkippedCount() { return gApplySkipped.load(std::memory_order_relaxed); }
unsigned long long ApplyOffsetExpiredCount() { return gApplyExpired.load(std::memory_order_relaxed); }

DWORD DisarmWatch(unsigned int slot)
{
    if (slot >= kSlots) {
        return 1;
    }
    gSlots[slot].armed.store(false, std::memory_order_release);
    gSlots[slot].address.store(0, std::memory_order_relaxed);
    RearmThreads();
    Log("result=0 detail=disarmed slot=" + std::to_string(slot));
    return 0;
}

DWORD DisarmAllWatches()
{
    for (unsigned int slot = 0; slot < kSlots; ++slot) {
        gSlots[slot].armed.store(false, std::memory_order_release);
        gSlots[slot].address.store(0, std::memory_order_relaxed);
    }
    RearmThreads();
    if (gHandler != nullptr) {
        RemoveVectoredExceptionHandler(gHandler);
        gHandler = nullptr;
    }
    Log("result=0 detail=disarmed_all");
    return 0;
}

unsigned long long WatchHitCount(unsigned int slot)
{
    if (slot >= kSlots) {
        return 0;
    }
    return gSlots[slot].hits.load(std::memory_order_relaxed);
}

unsigned int WatchCaptureCount()
{
    const unsigned int count = gRingCount.load(std::memory_order_relaxed);
    return count > kRingSize ? kRingSize : count;
}

bool ReadWatchCapture(unsigned int index, WatchCapture& out)
{
    if (index >= WatchCaptureCount()) {
        return false;
    }
    out = gRing[index];
    return true;
}

void ClearWatchCaptures()
{
    gRingCount.store(0, std::memory_order_relaxed);
    for (unsigned int slot = 0; slot < kSlots; ++slot) {
        gSlots[slot].stored.store(0, std::memory_order_relaxed);
    }
}

DWORD WatchArmedMask()
{
    DWORD mask = 0;
    for (unsigned int slot = 0; slot < kSlots; ++slot) {
        if (gSlots[slot].armed.load(std::memory_order_relaxed)) {
            mask |= (1u << slot);
        }
    }
    return mask;
}

DWORD WatchArmedThreadCount() { return gArmedThreads.load(std::memory_order_relaxed); }
DWORD WatchMissedThreadCount() { return gMissedThreads.load(std::memory_order_relaxed); }
DWORD WatchHandlerInstalled() { return gHandler != nullptr ? 1u : 0u; }
unsigned long long WatchForeignTrapCount()
{
    return gForeignTraps.load(std::memory_order_relaxed);
}

} // namespace preyvr::dll
