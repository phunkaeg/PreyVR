#include "ConsoleBridgeWin32.h"

#include "Logger.h"
#include "preyvr/SettingReadback.h"
#include "preyvr/ConsolePolicy.h"
#include "preyvr/EngineMap.h"

#include <array>
#include <atomic>
#include <chrono>
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

// Timed for the control side, try_lock for the render side. ServiceConsoleQueue
// runs on Prey's render thread, so a poisoned lock here would not merely fail --
// it would freeze the game.
std::timed_mutex gQueueMutex;
constexpr auto kQueueLockTimeout = std::chrono::milliseconds(100);
std::string gQueued;
const char* gVerifyName=nullptr;
int gVerifyValue=0;
bool gAwaitingReadback=false;
SettingReadback gReadback;
std::atomic<unsigned long long> gVerifiedCompleted{0};
std::atomic<DWORD> gVerifiedResult{0};

std::atomic<bool> gHasQueued{false};
std::atomic<DWORD> gLastResult{static_cast<DWORD>(ConsoleBridgeResult::ok)};
std::atomic<unsigned long long> gSubmitted{0};

DWORD Finish(ConsoleBridgeResult result, const char* detail, const std::string& command)
{
    gLastResult.store(static_cast<DWORD>(result), std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_console result=" << static_cast<DWORD>(result)
         << " detail=" << detail
         << " command=\"" << command << '"';
    lifecycle::Log(line.str());
    return static_cast<DWORD>(result);
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

// R-052, freshly checked in the Steam binary: F50A1A calls console vtable
// +B8; F50A2F tail-calls the returned ICVar's +10 (GetIVal). The live
// 2026-08-29 capture independently exercised both slots. Never use header order.
bool ReadRendererSetting(const char* name,int& value) {
    __try {
        auto* console=ResolveConsole();
        if(!console) return false;
        auto get=(*reinterpret_cast<void***>(console))[0xB8/sizeof(void*)];
        MEMORY_BASIC_INFORMATION m{};
        const auto executable=[](void* address,MEMORY_BASIC_INFORMATION& info) {
            return VirtualQuery(address,&info,sizeof(info))==sizeof(info) &&
                info.State==MEM_COMMIT && info.AllocationBase==GetModuleHandleW(L"PreyDll.dll") &&
                (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
        };
        if(!executable(get,m)) return false;
        auto* variable=reinterpret_cast<void*(__fastcall*)(void*,const char*)>(get)(console,name);
        if(!variable) return false;
        auto read=(*reinterpret_cast<void***>(variable))[0x10/sizeof(void*)];
        if(!executable(read,m)) return false;
        value=reinterpret_cast<int(__fastcall*)(void*)>(read)(variable);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
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

    std::unique_lock lock(gQueueMutex, kQueueLockTimeout);
    if (!lock.owns_lock() || gHasQueued.load(std::memory_order_acquire)) {
        return static_cast<DWORD>(ConsoleBridgeResult::busy);
    }
    gVerifyName=nullptr;gAwaitingReadback=false;
    gQueued = text;
    gHasQueued.store(true, std::memory_order_release);
    return static_cast<DWORD>(ConsoleBridgeResult::ok);
}

DWORD QueueVerifiedRendererSetting(const char* command) {
    const char* name=nullptr;int expected=0;
    if(command && std::strcmp(command,"r_MotionBlur 0")==0) name="r_MotionBlur";
    else if(command && std::strcmp(command,"r_AntialiasingMode 1")==0) {name="r_AntialiasingMode";expected=1;}
    else return static_cast<DWORD>(ConsoleBridgeResult::denied);
    std::unique_lock lock(gQueueMutex,kQueueLockTimeout);
    if(!lock.owns_lock() || gHasQueued.load()) return static_cast<DWORD>(ConsoleBridgeResult::busy);
    gQueued=command;gVerifyName=name;gVerifyValue=expected;gAwaitingReadback=false;
    gHasQueued.store(true,std::memory_order_release);return 0;
}

unsigned long long VerifiedRendererSettingCount() {return gVerifiedCompleted.load(std::memory_order_acquire);}
DWORD VerifiedRendererSettingResult() {return gVerifiedResult.load(std::memory_order_acquire);}

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

    std::unique_lock lock(gQueueMutex,std::try_to_lock);
    if(!lock.owns_lock() || !gHasQueued.load()) return;
    const std::string command=gQueued;
    if(gAwaitingReadback) {
        int value=0;
        const bool readable=ReadRendererSetting(gVerifyName,value);
        const auto state=gReadback.Observe(readable,value,GetTickCount64());
        if(state==ReadbackResult::verified) {
            Finish(ConsoleBridgeResult::ok,"verified_readback",command);
        } else if(state==ReadbackResult::pending) return;
        else Finish(ConsoleBridgeResult::unavailable,"readback_timeout_or_mismatch",command);
        gVerifiedResult.store(static_cast<DWORD>(state==ReadbackResult::verified ? ConsoleBridgeResult::ok : ConsoleBridgeResult::unavailable));
        gVerifiedCompleted.fetch_add(1,std::memory_order_release);
        gAwaitingReadback=false;gHasQueued.store(false);gQueued.clear();
        gSubmitted.fetch_add(1,std::memory_order_release);return;
    }
    // Count refused dequeues too so a waiting owner sees failure immediately.
    struct Completion {
        bool deferred=false;
        DWORD result=static_cast<DWORD>(ConsoleBridgeResult::unavailable);
        ~Completion() {if(!deferred) {if(gVerifyName) {gVerifiedResult.store(result);gVerifiedCompleted.fetch_add(1,std::memory_order_release);} gHasQueued=false;gQueued.clear();gSubmitted.fetch_add(1,std::memory_order_release);}}
    } completion;

    // Re-checked after dequeue rather than trusting the enqueue-time decision:
    // the buffer left the caller's hands in between, and a gate that is only
    // consulted once is a gate that can be raced.
    if (console::Classify(command) == console::Classification::denied) {
        completion.result=Finish(ConsoleBridgeResult::denied, "not_allowlisted_on_dequeue", command);
        return;
    }

    void* console = ResolveConsole();
    if (console == nullptr) {
        completion.result=Finish(ConsoleBridgeResult::unavailable, "console_null", command);
        return;
    }
    const ExecuteStringFn execute = ResolveExecuteString();
    if (execute == nullptr) {
        completion.result=Finish(ConsoleBridgeResult::signatureMismatch, "prologue_or_module_mismatch", command);
        return;
    }

    // silentMode = false so the command still appears in the game's own console
    // log; deferExecution = true so the engine drains it on its own update
    // instead of running it here.
    execute(console, command.c_str(), false, true);

    if(gVerifyName) {
        gAwaitingReadback=true;gReadback={gVerifyValue,GetTickCount64()+10000};
        completion.deferred=true;
    }
    Finish(ConsoleBridgeResult::ok, "queued_to_engine", command);
}

} // namespace preyvr::dll
