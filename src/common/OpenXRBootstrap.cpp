#include "preyvr/OpenXRBootstrap.h"

namespace preyvr {

OpenXRBootstrapStatus EvaluateOpenXRBootstrap(const OpenXRBootstrapInputs& inputs)
{
    if (!inputs.enabled) {
        return OpenXRBootstrapStatus::disabled;
    }
    if (!inputs.runtimeRegistered) {
        return OpenXRBootstrapStatus::runtimeNotRegistered;
    }
    if (!inputs.runtimeManifestPresent) {
        return OpenXRBootstrapStatus::runtimeManifestMissing;
    }
    if (!inputs.runtimeManifestLooksValid) {
        return OpenXRBootstrapStatus::runtimeManifestInvalid;
    }
    if (!inputs.loaderPresent) {
        return OpenXRBootstrapStatus::loaderMissing;
    }
    if (!inputs.loaderIsX64) {
        return OpenXRBootstrapStatus::loaderWrongArchitecture;
    }
    if (!inputs.loaderIsDll) {
        return OpenXRBootstrapStatus::loaderNotDll;
    }
    if (!inputs.loaderExportsEntryPoint) {
        return OpenXRBootstrapStatus::loaderEntryPointMissing;
    }
    return OpenXRBootstrapStatus::ready;
}

std::string_view ToString(OpenXRBootstrapStatus status)
{
    switch (status) {
    case OpenXRBootstrapStatus::disabled:
        return "disabled";
    case OpenXRBootstrapStatus::runtimeNotRegistered:
        return "runtime_not_registered";
    case OpenXRBootstrapStatus::runtimeManifestMissing:
        return "runtime_manifest_missing";
    case OpenXRBootstrapStatus::runtimeManifestInvalid:
        return "runtime_manifest_invalid";
    case OpenXRBootstrapStatus::loaderMissing:
        return "loader_missing";
    case OpenXRBootstrapStatus::loaderWrongArchitecture:
        return "loader_wrong_architecture";
    case OpenXRBootstrapStatus::loaderNotDll:
        return "loader_not_dll";
    case OpenXRBootstrapStatus::loaderEntryPointMissing:
        return "loader_entry_point_missing";
    case OpenXRBootstrapStatus::ready:
        return "ready";
    }
    return "unknown";
}

} // namespace preyvr
