#include "OpenXRPreflightWin32.h"

#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    Require(static_cast<bool>(output), "temporary manifest write succeeds");
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) {
        std::cerr << "usage: preyvr_openxr_preflight_tests <openxr_loader.dll> <non-loader.dll>\n";
        return 2;
    }

    const auto root = std::filesystem::temp_directory_path() /
        (L"preyvr-openxr-preflight-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    Require(!error, "temporary preflight directory is created");

    const auto validManifest = root / L"valid-runtime.json";
    const auto invalidManifest = root / L"invalid-runtime.json";
    WriteText(
        validManifest,
        R"({"file_format_version":"1.0.0","runtime":{"library_path":"runtime.dll"}})");
    WriteText(invalidManifest, R"({"runtime":{}})");

    const std::filesystem::path loader = std::filesystem::absolute(argv[1]);
    const std::filesystem::path nonLoaderDll = std::filesystem::absolute(argv[2]);
    const auto missing = root / L"missing.dll";

    auto inspection = preyvr::dll::InspectOpenXRFiles(validManifest, loader);
    Require(inspection.runtimeManifestPresent && inspection.runtimeManifestLooksValid,
        "bounded manifest inspection recognizes the OpenXR runtime shape");
    Require(inspection.loaderPresent && inspection.loaderIsX64 && inspection.loaderIsDll &&
            inspection.loaderExportsEntryPoint,
        "packaged loader is an x64 DLL exporting xrGetInstanceProcAddr");

    inspection = preyvr::dll::InspectOpenXRFiles(invalidManifest, loader);
    Require(inspection.runtimeManifestPresent && !inspection.runtimeManifestLooksValid,
        "malformed runtime manifest fails closed");

    inspection = preyvr::dll::InspectOpenXRFiles(validManifest, nonLoaderDll);
    Require(inspection.loaderPresent && inspection.loaderIsX64 && inspection.loaderIsDll &&
            !inspection.loaderExportsEntryPoint,
        "an arbitrary x64 DLL is not accepted as the OpenXR loader");

    inspection = preyvr::dll::InspectOpenXRFiles(validManifest, missing);
    Require(!inspection.loaderPresent && !inspection.loaderIsX64 &&
            !inspection.loaderIsDll && !inspection.loaderExportsEntryPoint,
        "missing loader fails closed without throwing");

    std::filesystem::remove_all(root, error);
    std::cout << "PreyVR Win32 OpenXR-preflight tests passed\n";
    return 0;
}
