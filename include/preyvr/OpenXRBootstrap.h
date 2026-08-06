#pragma once

#include <string_view>

namespace preyvr {

struct OpenXRBootstrapInputs {
    bool enabled = false;
    bool runtimeRegistered = false;
    bool runtimeManifestPresent = false;
    bool runtimeManifestLooksValid = false;
    bool loaderPresent = false;
    bool loaderIsX64 = false;
    bool loaderIsDll = false;
    bool loaderExportsEntryPoint = false;
};

enum class OpenXRBootstrapStatus {
    disabled,
    runtimeNotRegistered,
    runtimeManifestMissing,
    runtimeManifestInvalid,
    loaderMissing,
    loaderWrongArchitecture,
    loaderNotDll,
    loaderEntryPointMissing,
    ready,
};

OpenXRBootstrapStatus EvaluateOpenXRBootstrap(const OpenXRBootstrapInputs& inputs);
std::string_view ToString(OpenXRBootstrapStatus status);

} // namespace preyvr
