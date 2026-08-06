#include <Windows.h>

#include <filesystem>
#include <iostream>

namespace {

using GetSmokeStatusFn = DWORD (*)();
using SetFrameObserverEnabledFn = DWORD (*)(DWORD);
using GetFrameObserverStatusFn = DWORD (*)();
using GetObservedFrameCountFn = ULONGLONG (*)();
using GetOpenXRPreflightStatusFn = DWORD (*)();
using GetModulePinStatusFn = DWORD (*)();

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) {
        std::cerr << "usage: preyvr_dll_smoke_host <PreyVR.dll>\n";
        return 2;
    }

    const std::filesystem::path dllPath = std::filesystem::absolute(argv[1]);
    const HMODULE module = LoadLibraryW(dllPath.c_str());
    if (module == nullptr) {
        std::cerr << "dll_smoke_host status=load_failed error=" << GetLastError() << '\n';
        return 1;
    }

    const auto getStatus = reinterpret_cast<GetSmokeStatusFn>(
        GetProcAddress(module, "PreyVR_GetSmokeStatus"));
    const auto setObserver = reinterpret_cast<SetFrameObserverEnabledFn>(
        GetProcAddress(module, "PreyVR_SetFrameObserverEnabled"));
    const auto getObserverStatus = reinterpret_cast<GetFrameObserverStatusFn>(
        GetProcAddress(module, "PreyVR_GetFrameObserverStatus"));
    const auto getFrameCount = reinterpret_cast<GetObservedFrameCountFn>(
        GetProcAddress(module, "PreyVR_GetObservedFrameCount"));
    const auto getOpenXRStatus = reinterpret_cast<GetOpenXRPreflightStatusFn>(
        GetProcAddress(module, "PreyVR_GetOpenXRPreflightStatus"));
    const auto getModulePinStatus = reinterpret_cast<GetModulePinStatusFn>(
        GetProcAddress(module, "PreyVR_GetModulePinStatus"));
    if (getStatus == nullptr || setObserver == nullptr ||
        getObserverStatus == nullptr || getFrameCount == nullptr ||
        getOpenXRStatus == nullptr || getModulePinStatus == nullptr) {
        std::cerr << "dll_smoke_host status=export_missing\n";
        FreeLibrary(module);
        return 1;
    }

    DWORD status = 0;
    for (unsigned int attempt = 0; attempt < 500 && status == 0; ++attempt) {
        Sleep(10);
        status = getStatus();
    }

    if (status != 1) {
        std::cerr << "dll_smoke_host status=unexpected result=" << status << '\n';
        FreeLibrary(module);
        return 1;
    }

    if (getObserverStatus() != 0 || setObserver(1) != 0 ||
        getFrameCount() != 0 || getOpenXRStatus() != 0 ||
        getModulePinStatus() != 0) {
        std::cerr << "dll_smoke_host status=unsupported_exports_failed_closed_check_failed\n";
        FreeLibrary(module);
        return 1;
    }

    std::cout << "dll_smoke_host status=unsupported_as_expected hooks=default_off exports=fail_closed\n";
    FreeLibrary(module);
    return 0;
}
