#pragma once

#include "preyvr/OpenXRBootstrap.h"

#include <filesystem>
#include <string>

namespace preyvr::dll {

struct OpenXRPreflightReport {
    OpenXRBootstrapStatus status = OpenXRBootstrapStatus::disabled;
    std::string runtimeSource = "none";
    std::filesystem::path runtimeManifest;
    std::filesystem::path loader;
};

struct OpenXRFileInspection {
    bool runtimeManifestPresent = false;
    bool runtimeManifestLooksValid = false;
    bool loaderPresent = false;
    bool loaderIsX64 = false;
    bool loaderIsDll = false;
    bool loaderExportsEntryPoint = false;
};

OpenXRFileInspection InspectOpenXRFiles(
    const std::filesystem::path& runtimeManifest,
    const std::filesystem::path& loader);

OpenXRPreflightReport CollectOpenXRPreflight(
    const std::filesystem::path& modulePath);

} // namespace preyvr::dll
