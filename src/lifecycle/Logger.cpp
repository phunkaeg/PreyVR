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

} // namespace

std::filesystem::path LogPath()
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

void ResetLog()
{
    std::lock_guard lock(gLogMutex);
    const auto path = LogPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
}

void Log(std::string_view message)
{
    const std::string line = Timestamp() + " " + std::string(message) + "\n";
    OutputDebugStringA(line.c_str());

    std::lock_guard lock(gLogMutex);
    const auto path = LogPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::app);
    output << line;
}

} // namespace preyvr::lifecycle
