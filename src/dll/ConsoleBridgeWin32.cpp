#include "ConsoleBridgeWin32.h"

#include "Logger.h"
#include "preyvr/ConsolePolicy.h"
#include "preyvr/EngineMap.h"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

namespace preyvr::dll {
namespace {

// CXConsole::ExecuteString(const char*, bool bSilentMode, bool bDeferExecution)
//
// RVA and prologue read from the installed PreyDll.dll on 2026-08-30. Located by
// following the "Unknown command: %s" string to its dispatcher and taking that
// function's sole thin caller; the decompilation matches CryEngine's
// implementation, including the `exec` special case and the deferred list.
//
// The `80 B9 28 01 00 00 00` in the middle is `cmp byte ptr [rcx+0x128], 0` --
// the deferred-execution flag test -- which makes this signature distinctive
// rather than a generic MSVC prologue.
constexpr std::uintptr_t kExecuteStringRva = 0xE1FBE0;
constexpr std::array<std::uint8_t, 24> kExecuteStringPrologue{
    0x40, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x80, 0xB9,
    0x28, 0x01, 0x00, 0x00, 0x00, 0x45, 0x0F, 0xB6, 0xF8, 0x48, 0x8B, 0xF2,
};

using ExecuteStringFn = void(__fastcall*)(
    void* console, const char* command, bool silentMode, bool deferExecution);

std::mutex gQueueMutex;
std::string gQueued;
std::atomic<bool> gHasQueued{false};
std::atomic<DWORD> gLastResult{static_cast<DWORD>(ConsoleBridgeResult::ok)};
std::atomic<unsigned long long> gSubmitted{0};

void Finish(ConsoleBridgeResult result, const char* detail, const std::string& command)
{
    gLastResult.store(static_cast<DWORD>(result), std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_console result=" << static_cast<DWORD>(result)
         << " detail=" << detail
         << " command=\"" << command << '"';
    lifecycle::Log(line.str());
}

// Verified locally rather than added to the load-time landmark table: that table
// is the gate on whether the DLL may attach at all, and its count is part of the
// smoke contract. A feature-local precondition belongs with the feature.
ExecuteStringFn ResolveExecuteString()
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return nullptr;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    const auto target = base + kExecuteStringRva;

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(target), &memory, sizeof(memory)) !=
            sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY)) == 0) {
        return nullptr;
    }
    if (std::memcmp(reinterpret_cast<const void*>(target), kExecuteStringPrologue.data(),
                    kExecuteStringPrologue.size()) != 0) {
        return nullptr;
    }
    return reinterpret_cast<ExecuteStringFn>(target);
}

void* ResolveConsole()
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return nullptr;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    const auto gEnv = base + engine::GlobalEnvironmentLayout::baseRva;
    auto* console = *reinterpret_cast<void**>(gEnv + engine::GlobalEnvironmentLayout::console);
    return console;
}

} // namespace

DWORD QueueConsoleCommand(const char* command)
{
    if (command == nullptr) {
        return static_cast<DWORD>(ConsoleBridgeResult::denied);
    }
    const std::size_t length = ::strnlen(command, console::kMaxCommandLength + 1);
    if (length > console::kMaxCommandLength) {
        return static_cast<DWORD>(ConsoleBridgeResult::tooLong);
    }

    const std::string text(command, length);
    if (console::Classify(text) == console::Classification::denied) {
        Finish(ConsoleBridgeResult::denied, "not_allowlisted", text);
        return static_cast<DWORD>(ConsoleBridgeResult::denied);
    }

    std::lock_guard lock(gQueueMutex);
    if (gHasQueued.load(std::memory_order_acquire)) {
        return static_cast<DWORD>(ConsoleBridgeResult::busy);
    }
    gQueued = text;
    gHasQueued.store(true, std::memory_order_release);
    return static_cast<DWORD>(ConsoleBridgeResult::ok);
}

DWORD LastConsoleBridgeResult()
{
    return gLastResult.load(std::memory_order_acquire);
}

unsigned long long SubmittedConsoleCommandCount()
{
    return gSubmitted.load(std::memory_order_acquire);
}

void ServiceConsoleQueue()
{
    if (!gHasQueued.load(std::memory_order_acquire)) {
        return; // the hot path
    }

    std::string command;
    {
        std::lock_guard lock(gQueueMutex);
        command = gQueued;
        gQueued.clear();
        gHasQueued.store(false, std::memory_order_release);
    }

    // Re-checked after dequeue rather than trusting the enqueue-time decision:
    // the buffer left the caller's hands in between, and a gate that is only
    // consulted once is a gate that can be raced.
    if (console::Classify(command) == console::Classification::denied) {
        Finish(ConsoleBridgeResult::denied, "not_allowlisted_on_dequeue", command);
        return;
    }

    void* console = ResolveConsole();
    if (console == nullptr) {
        Finish(ConsoleBridgeResult::unavailable, "console_null", command);
        return;
    }
    const ExecuteStringFn execute = ResolveExecuteString();
    if (execute == nullptr) {
        Finish(ConsoleBridgeResult::signatureMismatch, "prologue_or_module_mismatch", command);
        return;
    }

    // silentMode = false so the command still appears in the game's own console
    // log; deferExecution = true so the engine drains it on its own update
    // instead of running it here.
    execute(console, command.c_str(), false, true);

    gSubmitted.fetch_add(1, std::memory_order_relaxed);
    Finish(ConsoleBridgeResult::ok, "queued_to_engine", command);
}

} // namespace preyvr::dll
