// Answers the one Hurdle 2 question that a running Prey cannot answer: which
// GPU does the OpenXR runtime require us to render on, and what index is that
// in the order Prey's own adapter loop walks?
//
// R-025 selects its adapter with an EnumAdapters1 loop, and R-052 exposes
// r_overrideDXGIAdapter as an index into exactly that order. The live capture on
// 2026-08-29 found five adapters, four of them identical RTX 5070 Ti entries
// distinguishable only by LUID -- so the translation from "the LUID OpenXR wants"
// to "the index Prey understands" is necessary, not precautionary.
//
// Runs out of process. It never touches Prey, and it is the first code in this
// project to actually call into OpenXR rather than inspect its files.

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// One line per fact, greppable, matching the smoke log's shape. A probe whose
// output has to be read by eye is a probe whose result gets transcribed wrong.
void Fact(const char* line) {
    std::printf("preyvr_xr_adapter %s\n", line);
}

// The loader refuses xrResultToString without a live XrInstance -- which is
// precisely the case where the failure needs naming, since the most likely
// failures happen before or during instance creation. Observed 2026-08-29:
// a bare "XrResult_-4" is not something anyone should have to look up.
const char* KnownResultName(XrResult result) {
    switch (result) {
        case XR_ERROR_VALIDATION_FAILURE:      return "XR_ERROR_VALIDATION_FAILURE";
        case XR_ERROR_RUNTIME_FAILURE:         return "XR_ERROR_RUNTIME_FAILURE";
        case XR_ERROR_OUT_OF_MEMORY:           return "XR_ERROR_OUT_OF_MEMORY";
        case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
        case XR_ERROR_INITIALIZATION_FAILED:   return "XR_ERROR_INITIALIZATION_FAILED";
        case XR_ERROR_FUNCTION_UNSUPPORTED:    return "XR_ERROR_FUNCTION_UNSUPPORTED";
        case XR_ERROR_FEATURE_UNSUPPORTED:     return "XR_ERROR_FEATURE_UNSUPPORTED";
        case XR_ERROR_EXTENSION_NOT_PRESENT:   return "XR_ERROR_EXTENSION_NOT_PRESENT";
        case XR_ERROR_LIMIT_REACHED:           return "XR_ERROR_LIMIT_REACHED";
        case XR_ERROR_RUNTIME_UNAVAILABLE:     return "XR_ERROR_RUNTIME_UNAVAILABLE";
        case XR_ERROR_FORM_FACTOR_UNSUPPORTED: return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
        case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
        default:                               return nullptr;
    }
}

std::string ResultName(XrInstance instance, XrResult result) {
    if (instance != XR_NULL_HANDLE) {
        char buffer[XR_MAX_RESULT_STRING_SIZE]{};
        if (XR_SUCCEEDED(xrResultToString(instance, result, buffer))) {
            return buffer;
        }
    }
    if (const char* known = KnownResultName(result)) {
        return known;
    }
    return "XrResult_" + std::to_string(static_cast<int>(result));
}

std::string Narrow(const wchar_t* wide) {
    if (wide == nullptr) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

bool SameLuid(const LUID& a, const LUID& b) {
    return a.HighPart == b.HighPart && a.LowPart == b.LowPart;
}

std::string LuidText(const LUID& luid) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "0x%08lX:0x%08lX",
                  static_cast<unsigned long>(luid.HighPart),
                  static_cast<unsigned long>(luid.LowPart));
    return buffer;
}

// Walks adapters in the same order R-025's loop does, so the index printed here
// is the value r_overrideDXGIAdapter takes.
int ReportAdapters(const LUID* required) {
    IDXGIFactory1* factory = nullptr;
    const HRESULT created = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                               reinterpret_cast<void**>(&factory));
    if (FAILED(created) || factory == nullptr) {
        std::printf("preyvr_xr_adapter adapters status=factory_failed hr=0x%08lX\n",
                    static_cast<unsigned long>(created));
        return -1;
    }

    int matched = -1;
    UINT index = 0;
    IDXGIAdapter1* adapter = nullptr;
    while (factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            const bool isMatch = required != nullptr && SameLuid(desc.AdapterLuid, *required);
            if (isMatch) {
                matched = static_cast<int>(index);
            }
            std::printf("preyvr_xr_adapter adapter index=%u luid=%s vram_mb=%llu software=%s "
                        "required=%s description=\"%s\"\n",
                        index,
                        LuidText(desc.AdapterLuid).c_str(),
                        static_cast<unsigned long long>(desc.DedicatedVideoMemory / (1024ull * 1024ull)),
                        (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ? "yes" : "no",
                        isMatch ? "yes" : "no",
                        Narrow(desc.Description).c_str());
        }
        adapter->Release();
        adapter = nullptr;
        ++index;
    }

    factory->Release();
    std::printf("preyvr_xr_adapter adapters count=%u\n", index);
    return matched;
}

} // namespace

int main() {
    Fact("probe version=1 purpose=xrGetD3D11GraphicsRequirementsKHR_luid");

    uint32_t extensionCount = 0;
    XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    if (XR_FAILED(result)) {
        std::printf("preyvr_xr_adapter result=no_runtime detail=%s\n",
                    ResultName(XR_NULL_HANDLE, result).c_str());
        std::printf("preyvr_xr_adapter hint=install_or_start_an_openxr_runtime\n");
        return 2;
    }

    std::vector<XrExtensionProperties> extensions(
        extensionCount, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES, nullptr, {}, 0});
    result = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount,
                                                    extensions.data());
    if (XR_FAILED(result)) {
        std::printf("preyvr_xr_adapter result=extension_enumeration_failed detail=%s\n",
                    ResultName(XR_NULL_HANDLE, result).c_str());
        return 2;
    }

    bool hasD3D11 = false;
    for (const auto& extension : extensions) {
        if (std::string(extension.extensionName) == XR_KHR_D3D11_ENABLE_EXTENSION_NAME) {
            hasD3D11 = true;
        }
    }
    std::printf("preyvr_xr_adapter extensions count=%u d3d11_enable=%s\n",
                extensionCount, hasD3D11 ? "yes" : "no");

    if (!hasD3D11) {
        // Worth failing loudly: Prey renders on D3D11, so a runtime without this
        // extension rules out the whole submission design, not just this probe.
        Fact("result=d3d11_extension_absent detail=runtime_cannot_accept_d3d11_swapchains");
        return 3;
    }

    // The pinned SDK is 1.1.60, but a runtime that only implements OpenXR 1.0
    // rejects XR_CURRENT_API_VERSION outright with XR_ERROR_API_VERSION_UNSUPPORTED
    // -- observed here on 2026-08-29. Which version this machine's runtime
    // actually accepts is a fact the mod needs, so try newest-first and report
    // the one that worked rather than hardcoding a guess.
    const XrVersion candidates[] = {XR_CURRENT_API_VERSION, XR_API_VERSION_1_0};

    XrInstance instance = XR_NULL_HANDLE;
    XrVersion acceptedVersion = 0;
    for (const XrVersion candidate : candidates) {
        const char* enabled[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
        XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.enabledExtensionCount = 1;
        instanceInfo.enabledExtensionNames = enabled;
        std::snprintf(instanceInfo.applicationInfo.applicationName,
                      sizeof(instanceInfo.applicationInfo.applicationName), "PreyVR adapter probe");
        instanceInfo.applicationInfo.applicationVersion = 1;
        std::snprintf(instanceInfo.applicationInfo.engineName,
                      sizeof(instanceInfo.applicationInfo.engineName), "PreyVR");
        instanceInfo.applicationInfo.engineVersion = 1;
        instanceInfo.applicationInfo.apiVersion = candidate;

        result = xrCreateInstance(&instanceInfo, &instance);
        std::printf("preyvr_xr_adapter attempt api_version=%llu.%llu.%llu result=%s\n",
                    static_cast<unsigned long long>(XR_VERSION_MAJOR(candidate)),
                    static_cast<unsigned long long>(XR_VERSION_MINOR(candidate)),
                    static_cast<unsigned long long>(XR_VERSION_PATCH(candidate)),
                    XR_SUCCEEDED(result) ? "ok" : ResultName(XR_NULL_HANDLE, result).c_str());
        if (XR_SUCCEEDED(result)) {
            acceptedVersion = candidate;
            break;
        }
        instance = XR_NULL_HANDLE;
        if (result != XR_ERROR_API_VERSION_UNSUPPORTED) {
            break; // a different failure will not be fixed by asking for less
        }
    }

    if (instance == XR_NULL_HANDLE) {
        // Enumerate anyway: the adapter list is half the answer and costs
        // nothing, so a run that cannot reach the runtime is not wasted.
        std::printf("preyvr_xr_adapter result=instance_creation_failed detail=%s\n",
                    ResultName(XR_NULL_HANDLE, result).c_str());
        if (result == XR_ERROR_INITIALIZATION_FAILED || result == XR_ERROR_RUNTIME_UNAVAILABLE) {
            Fact("hint=runtime_installed_but_not_running_start_the_streamer_and_headset");
        }
        ReportAdapters(nullptr);
        return 4;
    }

    std::printf("preyvr_xr_adapter api_version accepted=%llu.%llu.%llu sdk_built_against=%llu.%llu.%llu\n",
                static_cast<unsigned long long>(XR_VERSION_MAJOR(acceptedVersion)),
                static_cast<unsigned long long>(XR_VERSION_MINOR(acceptedVersion)),
                static_cast<unsigned long long>(XR_VERSION_PATCH(acceptedVersion)),
                static_cast<unsigned long long>(XR_VERSION_MAJOR(XR_CURRENT_API_VERSION)),
                static_cast<unsigned long long>(XR_VERSION_MINOR(XR_CURRENT_API_VERSION)),
                static_cast<unsigned long long>(XR_VERSION_PATCH(XR_CURRENT_API_VERSION)));

    XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(instance, &instanceProperties))) {
        std::printf("preyvr_xr_adapter runtime name=\"%s\" version=%llu.%llu.%llu\n",
                    instanceProperties.runtimeName,
                    static_cast<unsigned long long>(XR_VERSION_MAJOR(instanceProperties.runtimeVersion)),
                    static_cast<unsigned long long>(XR_VERSION_MINOR(instanceProperties.runtimeVersion)),
                    static_cast<unsigned long long>(XR_VERSION_PATCH(instanceProperties.runtimeVersion)));
    }

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    result = xrGetSystem(instance, &systemInfo, &systemId);
    if (XR_FAILED(result)) {
        // The expected outcome with no headset connected. Still enumerate the
        // adapters so the run is not wasted -- the list itself is half the answer.
        std::printf("preyvr_xr_adapter result=no_system detail=%s\n",
                    ResultName(instance, result).c_str());
        Fact("hint=connect_and_power_the_headset_then_rerun");
        ReportAdapters(nullptr);
        xrDestroyInstance(instance);
        return 5;
    }

    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystemProperties(instance, systemId, &systemProperties))) {
        std::printf("preyvr_xr_adapter system name=\"%s\" vendor=%u\n",
                    systemProperties.systemName, systemProperties.vendorId);
    }

    PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
    result = xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR",
                                   reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements));
    if (XR_FAILED(result) || getRequirements == nullptr) {
        std::printf("preyvr_xr_adapter result=proc_addr_failed detail=%s\n",
                    ResultName(instance, result).c_str());
        xrDestroyInstance(instance);
        return 6;
    }

    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    result = getRequirements(instance, systemId, &requirements);
    if (XR_FAILED(result)) {
        std::printf("preyvr_xr_adapter result=requirements_failed detail=%s\n",
                    ResultName(instance, result).c_str());
        xrDestroyInstance(instance);
        return 7;
    }

    std::printf("preyvr_xr_adapter required luid=%s min_feature_level=0x%04X\n",
                LuidText(requirements.adapterLuid).c_str(),
                static_cast<unsigned>(requirements.minFeatureLevel));

    const int matched = ReportAdapters(&requirements.adapterLuid);
    if (matched < 0) {
        Fact("result=luid_not_in_enumeration detail=required_adapter_absent_from_EnumAdapters1");
        xrDestroyInstance(instance);
        return 8;
    }

    // The actionable line. R-052's cvar takes this index directly; index 0 is
    // also what the engine picks unaided, so equality means no override needed.
    std::printf("preyvr_xr_adapter result=matched enum_index=%d r_overrideDXGIAdapter=%d "
                "override_needed=%s\n",
                matched, matched, matched == 0 ? "no" : "yes");

    xrDestroyInstance(instance);
    return 0;
}
