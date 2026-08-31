#include "Logger.h"

#include <Windows.h>
#include <ShlObj.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace preyvr::lifecycle {
namespace {

std::mutex gLogMutex;

std::filesystem::path ResolveLogPath();

std::string Timestamp()
{
    SYSTEMTIME value{};
    GetSystemTime(&value);
    std::ostringstream stream;
    stream << std::setfill('0')
           << std::setw(4) << value.wYear << '-'
           << std::setw(2) << value.wMonth << '-'
           << std::setw(2) << value.wDay << 'T'
           << std::setw(2) << value.wHour << ':'
           << std::setw(2) << value.wMinute << ':'
           << std::setw(2) << value.wSecond << '.'
           << std::setw(3) << value.wMilliseconds << 'Z';
    return stream.str();
}

// Resolved once per process and cached.
//
// This is not a micro-optimisation. `LogPath` calls `SHGetFolderPathW`, and the
// original code called it on **every log line** -- including from Prey's render
// thread, where `ServiceFrameCapture`, `ServiceXrFrame` and the camera hook all
// log, and from any foreign thread a tool calls an export on.
//
// Observed 2026-08-31: inside Prey, every export that logs failed when called
// through Frida, while every export that does not log worked; nothing reached
// the log file after bootstrap; and the identical call succeeded in an isolated
// host. The bootstrap thread works and other threads do not, which is the shape
// of a shell API being invoked somewhere it is not safe rather than of a bug in
// the callers. F-009 attributed the same symptom to Frida reacting to MinHook;
// that attribution now looks wrong, because these exports touch no hook at all.
//
// The path cannot change during a process's life, so resolving it once removes
// the hazard entirely rather than working around it. Function-local static
// initialisation is thread-safe since C++11, so the first caller wins and the
// rest read a plain string.
const std::filesystem::path& CachedLogPath()
{
    static const std::filesystem::path path = [] {
        auto resolved = ResolveLogPath();
        // Created here too, for the same reason: it only needs doing once, and
        // doing it per line put a filesystem call on the render thread.
        std::error_code error;
        std::filesystem::create_directories(resolved.parent_path(), error);
        return resolved;
    }();
    return path;
}

std::filesystem::path ResolveLogPath()
{
    std::wstring overridePath(32768, L'\0');
    const DWORD overrideLength = GetEnvironmentVariableW(
        L"PREYVR_LOG_PATH", overridePath.data(), static_cast<DWORD>(overridePath.size()));
    if (overrideLength > 0 && overrideLength < overridePath.size()) {
        overridePath.resize(overrideLength);
        return std::filesystem::path(overridePath);
    }

    wchar_t documents[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents))) {
        return L"PreyVR.log";
    }
    return std::filesystem::path(documents) / L"PreyVR" / L"PreyVR.log";
}

} // namespace

std::filesystem::path LogPath()
{
    return CachedLogPath();
}

void ResetLog()
{
    std::lock_guard lock(gLogMutex);
    const auto& path = CachedLogPath();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
}

void Log(std::string_view message)
{
    const std::string line = Timestamp() + " " + std::string(message) + "\n";
    OutputDebugStringA(line.c_str());

    std::lock_guard lock(gLogMutex);
    const auto& path = CachedLogPath();
    std::ofstream output(path, std::ios::binary | std::ios::app);
    output << line;
}

} // namespace preyvr::lifecycle
