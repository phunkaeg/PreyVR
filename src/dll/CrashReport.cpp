#include "CrashReport.h"

#include "Logger.h"

#include <windows.h>
#include <DbgHelp.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace preyvr::dll {
namespace {

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

// Everything the reporter needs is resolved at install: nothing is allocated or
// loaded while a thread is dying.
std::wstring gTextPath, gDumpPath, gLogPath;
MiniDumpWriteDumpFn gWriteDump = nullptr;
HANDLE gRequest = nullptr, gDone = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER gPrevious = nullptr;
std::atomic<bool> gTaken{false};
EXCEPTION_POINTERS* gPointers = nullptr;
DWORD gThreadId = 0;

void Append(HANDLE file, const char* text)
{
    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
}

// "PreyVR.dll+0x1A2B3C", or the bare address outside every module.
void Describe(DWORD64 address, char* out, std::size_t size)
{
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH]{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module) &&
        GetModuleFileNameW(module, path, MAX_PATH) != 0) {
        const wchar_t* name = std::wcsrchr(path, L'\\');
        std::snprintf(out, size, "%ls+0x%llX", name ? name + 1 : path,
                      address - reinterpret_cast<DWORD64>(module));
    } else {
        std::snprintf(out, size, "0x%llX", address);
    }
}

// One frame up the faulting thread's stack; false at its end or on unreadable memory.
bool Unwind(CONTEXT& context)
{
    __try {
        DWORD64 imageBase = 0;
        const PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (entry == nullptr) {   // a leaf: the return address is on top of the stack
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += 8;
        } else {
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry, &context,
                             &handlerData, &establisher, nullptr);
        }
        return context.Rip != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The first 64 frames, then the last 32: an overflow is thousands of the same few
// frames, and what started the recursion is at the bottom.
void WriteStack(HANDLE file, CONTEXT context)
{
    constexpr int kHead = 64, kTail = 32;
    char tail[kTail][192]{};
    char frame[160], line[192];
    int count = 0;
    do {
        Describe(context.Rip, frame, sizeof(frame));
        std::snprintf(line, sizeof(line), "#%d %s rsp=0x%llX\r\n", count, frame, context.Rsp);
        if (count < kHead) { Append(file, line); }
        else { std::memcpy(tail[count % kTail], line, sizeof(line)); }
        ++count;
    } while (count < 200000 && Unwind(context));
    if (count > kHead) {
        std::snprintf(line, sizeof(line), "... %d frames in all\r\n", count);
        Append(file, line);
        for (int i = (count > kHead + kTail ? count - kTail : kHead); i < count; ++i) { Append(file, tail[i % kTail]); }
    }
}

DWORD WINAPI Reporter(LPVOID)
{
    WaitForSingleObject(gRequest, INFINITE);
    const EXCEPTION_RECORD* record = gPointers->ExceptionRecord;
    char where[160], summary[384];
    Describe(reinterpret_cast<DWORD64>(record->ExceptionAddress), where, sizeof(where));
    std::snprintf(summary, sizeof(summary), "preyvr_crash code=0x%08lX at=%s thread=%lu report=crash-%lu.txt\r\n",
                  record->ExceptionCode, where, gThreadId, GetCurrentProcessId());

    const HANDLE text = CreateFileW(gTextPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (text != INVALID_HANDLE_VALUE) {
        Append(text, summary);
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            char access[96];
            const ULONG_PTR kind = record->ExceptionInformation[0];
            std::snprintf(access, sizeof(access), "access=%s address=0x%llX\r\n",
                          kind == 0 ? "read" : kind == 1 ? "write" : "execute",
                          static_cast<unsigned long long>(record->ExceptionInformation[1]));
            Append(text, access);
        }
        WriteStack(text, *gPointers->ContextRecord);
        CloseHandle(text);
    }
    // The same line in PreyVR.log, written raw: the dying thread may hold the logger's mutex.
    const HANDLE log = CreateFileW(gLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
    if (log != INVALID_HANDLE_VALUE) { Append(log, summary); CloseHandle(log); }

    if (gWriteDump != nullptr) {
        const HANDLE dump = CreateFileW(gDumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (dump != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION info{gThreadId, gPointers, FALSE};
            gWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                       static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules),
                       &info, nullptr, nullptr);
            CloseHandle(dump);
        }
    }
    SetEvent(gDone);
    return 0;
}

void Report(EXCEPTION_POINTERS* pointers)
{
    if (gTaken.exchange(true)) { return; }   // one report per process
    gPointers = pointers;
    gThreadId = GetCurrentThreadId();
    SetEvent(gRequest);
    WaitForSingleObject(gDone, 30000);
}

LONG CALLBACK OnFirstChance(EXCEPTION_POINTERS* pointers)
{
    if (pointers->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) { Report(pointers); }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* pointers)
{
    Report(pointers);
    return gPrevious != nullptr ? gPrevious(pointers) : EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void InstallCrashReport()
{
    if (gRequest != nullptr) { return; }
    const auto log = lifecycle::LogPath();
    const auto stem = L"crash-" + std::to_wstring(GetCurrentProcessId());
    gLogPath = log.wstring();
    gTextPath = (log.parent_path() / (stem + L".txt")).wstring();
    gDumpPath = (log.parent_path() / (stem + L".dmp")).wstring();
    if (const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll")) {
        gWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    }
    gRequest = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    gDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const HANDLE thread = gRequest && gDone ? CreateThread(nullptr, 256 * 1024, &Reporter, nullptr, 0, nullptr) : nullptr;
    if (thread == nullptr) {
        lifecycle::Log("preyvr_crash_report result=1 detail=unavailable");
        return;
    }
    CloseHandle(thread);
    AddVectoredExceptionHandler(1, &OnFirstChance);
    gPrevious = SetUnhandledExceptionFilter(&OnUnhandled);
    lifecycle::Log(std::string("preyvr_crash_report result=0 minidump=") + (gWriteDump ? "1" : "0"));
}

} // namespace preyvr::dll
