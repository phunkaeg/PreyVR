// A complete OpenXR session lifecycle, out of process.
//
// This is the code the DLL will eventually need -- instance, system, D3D11
// device on the runtime's required adapter, session, swapchains, the frame loop,
// and a submitted projection layer -- written as a standalone executable first.
//
// Standalone because it can then be *run*. Until xr-sim existed there was no way
// to execute any of this without a headset, and F-010 is the standing evidence
// that OpenXR code written without running it comes out confidently wrong. A
// probe that exercises the whole path against a simulated runtime turns ~400
// lines of hope into ~400 lines that have actually worked once.
//
// It also puts the project's pure policy modules under a real runtime for the
// first time: `xrswapchain::SelectFormat` chooses the format from what the
// runtime actually offers, and `xrframe::FrameContract` sequences the real
// calls rather than a test's imitation of them.

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "preyvr/StereoCamera.h"
#include "preyvr/StereoFrame.h"
#include "preyvr/XrFrameContract.h"
#include "preyvr/XrSwapchainFormat.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

void Fact(const char* format, ...)
{
    char line[1024]{};
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    std::printf("preyvr_xr_session %s\n", line);
}

const char* ResultName(XrResult r)
{
    switch (r) {
        case XR_SUCCESS: return "XR_SUCCESS";
        case XR_ERROR_VALIDATION_FAILURE: return "XR_ERROR_VALIDATION_FAILURE";
        case XR_ERROR_RUNTIME_FAILURE: return "XR_ERROR_RUNTIME_FAILURE";
        case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
        case XR_ERROR_INITIALIZATION_FAILED: return "XR_ERROR_INITIALIZATION_FAILED";
        case XR_ERROR_EXTENSION_NOT_PRESENT: return "XR_ERROR_EXTENSION_NOT_PRESENT";
        case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
        case XR_ERROR_GRAPHICS_DEVICE_INVALID: return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
        case XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED: return "XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED";
        case XR_ERROR_SESSION_NOT_RUNNING: return "XR_ERROR_SESSION_NOT_RUNNING";
        default: return "XrResult(other)";
    }
}

#define REQUIRE(expr, what)                                                    \
    do {                                                                       \
        const XrResult r_ = (expr);                                            \
        if (XR_FAILED(r_)) {                                                   \
            Fact("result=failed step=%s detail=%s code=%d", what,              \
                 ResultName(r_), static_cast<int>(r_));                        \
            return false;                                                      \
        }                                                                      \
    } while (false)

struct Probe {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSwapchain swapchain = XR_NULL_HANDLE;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    std::vector<ID3D11RenderTargetView*> rtvs;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    XrViewConfigurationType viewConfig = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

    preyvr::xrframe::FrameContract contract;

    bool CreateInstance();
    bool CreateDeviceOnRequiredAdapter();
    bool CreateSession();
    bool CreateSwapchain();
    bool RunFrames(int count);
    void Shutdown();
};

// Same newest-first policy the adapter probe uses. It is not defensive padding:
// xr-sim accepts 1.1 and VirtualDesktopXR rejects it, so one binary works
// against both only because of this.
bool Probe::CreateInstance()
{
    const char* enabled[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    const XrVersion candidates[] = {XR_CURRENT_API_VERSION, XR_API_VERSION_1_0};

    XrResult result = XR_ERROR_RUNTIME_FAILURE;
    for (const XrVersion candidate : candidates) {
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        info.enabledExtensionCount = 1;
        info.enabledExtensionNames = enabled;
        std::snprintf(info.applicationInfo.applicationName,
                      sizeof(info.applicationInfo.applicationName), "PreyVR session probe");
        std::snprintf(info.applicationInfo.engineName,
                      sizeof(info.applicationInfo.engineName), "PreyVR");
        info.applicationInfo.applicationVersion = 1;
        info.applicationInfo.engineVersion = 1;
        info.applicationInfo.apiVersion = candidate;

        result = xrCreateInstance(&info, &instance);
        Fact("attempt api_version=%llu.%llu.%llu result=%s",
             static_cast<unsigned long long>(XR_VERSION_MAJOR(candidate)),
             static_cast<unsigned long long>(XR_VERSION_MINOR(candidate)),
             static_cast<unsigned long long>(XR_VERSION_PATCH(candidate)),
             XR_SUCCEEDED(result) ? "ok" : ResultName(result));
        if (XR_SUCCEEDED(result)) {
            break;
        }
        instance = XR_NULL_HANDLE;
        if (result != XR_ERROR_API_VERSION_UNSUPPORTED) {
            break;
        }
    }
    if (instance == XR_NULL_HANDLE) {
        Fact("result=failed step=create_instance detail=%s", ResultName(result));
        return false;
    }

    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(instance, &props))) {
        Fact("runtime name=\"%s\"", props.runtimeName);
    }

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    const XrResult sys = xrGetSystem(instance, &systemInfo, &systemId);
    if (XR_FAILED(sys)) {
        Fact("result=no_system detail=%s", ResultName(sys));
        return false;
    }

    // Queried for two reasons. It tells xr-tape's `layer_budget` check what the
    // system's maximum layer count is -- without it that check can only SKIP,
    // and a SKIP is not a pass. And `maxSwapchainImageWidth/Height` is a real
    // precondition: requesting a swapchain larger than the system allows fails
    // at creation, and it is much easier to read that here than from an
    // XR_ERROR_LIMIT_REACHED three calls later.
    XrSystemProperties systemProps{XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystemProperties(instance, systemId, &systemProps))) {
        Fact("system name=\"%s\" vendor=%u maxLayers=%u maxSwapchain=%ux%u",
             systemProps.systemName, systemProps.vendorId,
             systemProps.graphicsProperties.maxLayerCount,
             systemProps.graphicsProperties.maxSwapchainImageWidth,
             systemProps.graphicsProperties.maxSwapchainImageHeight);
    } else {
        // Not fatal -- the session can still run -- but it is worth saying so,
        // because a downstream "unknown" is otherwise indistinguishable from a
        // value that happened to be fine.
        Fact("system properties=unavailable detail=layer_budget_cannot_be_checked");
        systemProps.graphicsProperties.maxSwapchainImageWidth = 0;
    }

    // The recommended per-eye size is what the swapchain must be built at.
    std::uint32_t viewCount = 0;
    REQUIRE(xrEnumerateViewConfigurationViews(instance, systemId, viewConfig, 0, &viewCount, nullptr),
            "enumerate_view_configs");
    std::vector<XrViewConfigurationView> views(
        viewCount, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
    REQUIRE(xrEnumerateViewConfigurationViews(instance, systemId, viewConfig, viewCount,
                                              &viewCount, views.data()),
            "enumerate_view_configs");
    if (viewCount != 2) {
        Fact("result=failed step=view_count detail=expected_stereo got=%u", viewCount);
        return false;
    }
    width = views[0].recommendedImageRectWidth;
    height = views[0].recommendedImageRectHeight;
    Fact("views count=%u recommended=%ux%u samples=%u", viewCount, width, height,
         views[0].recommendedSwapchainSampleCount);

    // A runtime is not obliged to keep its own recommendation inside its own
    // limit, and the failure if it does not lands at swapchain creation rather
    // than here.
    const std::uint32_t maxWidth = systemProps.graphicsProperties.maxSwapchainImageWidth;
    const std::uint32_t maxHeight = systemProps.graphicsProperties.maxSwapchainImageHeight;
    if (maxWidth != 0 && (width > maxWidth || height > maxHeight)) {
        Fact("result=failed step=swapchain_size detail=recommended_exceeds_system_max "
             "recommended=%ux%u max=%ux%u", width, height, maxWidth, maxHeight);
        return false;
    }
    return true;
}

// The whole reason R-052 exists: the device must sit on the adapter the runtime
// names, and OpenXR names it by LUID while DXGI addresses it by index.
bool Probe::CreateDeviceOnRequiredAdapter()
{
    PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
    REQUIRE(xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR",
                                  reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements)),
            "get_d3d11_requirements_proc");

    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    REQUIRE(getRequirements(instance, systemId, &requirements), "d3d11_graphics_requirements");
    Fact("required luid=0x%08lX:0x%08lX min_feature_level=0x%04X",
         static_cast<unsigned long>(requirements.adapterLuid.HighPart),
         static_cast<unsigned long>(requirements.adapterLuid.LowPart),
         static_cast<unsigned>(requirements.minFeatureLevel));

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        Fact("result=failed step=create_dxgi_factory");
        return false;
    }

    IDXGIAdapter1* chosen = nullptr;
    int chosenIndex = -1;
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            desc.AdapterLuid.HighPart == requirements.adapterLuid.HighPart &&
            desc.AdapterLuid.LowPart == requirements.adapterLuid.LowPart) {
            chosen = adapter;
            chosenIndex = static_cast<int>(i);
            break;
        }
        adapter->Release();
    }
    factory->Release();

    if (chosen == nullptr) {
        // Refused rather than falling back to the default adapter: creating the
        // device on the wrong GPU produces a session that submits frames the
        // runtime silently never shows, which is far harder to diagnose than
        // failing here.
        Fact("result=failed step=adapter_match detail=required_luid_not_enumerated");
        return false;
    }
    Fact("adapter matched enum_index=%d r_overrideDXGIAdapter=%d", chosenIndex, chosenIndex);

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL achieved{};
    const HRESULT created = D3D11CreateDevice(
        chosen, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, 1, D3D11_SDK_VERSION,
        &device, &achieved, &context);
    chosen->Release();
    if (FAILED(created) || device == nullptr) {
        Fact("result=failed step=create_device hr=0x%08lX", static_cast<unsigned long>(created));
        return false;
    }
    Fact("device created feature_level=0x%04X", static_cast<unsigned>(achieved));
    return true;
}

bool Probe::CreateSession()
{
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = device;

    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
    info.next = &binding;
    info.systemId = systemId;
    REQUIRE(xrCreateSession(instance, &info, &session), "create_session");

    // LOCAL rather than STAGE: seated and standing both work, and it does not
    // require the runtime to have a configured play area.
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    REQUIRE(xrCreateReferenceSpace(session, &spaceInfo, &space), "create_reference_space");
    Fact("session created space=LOCAL");
    return true;
}

bool Probe::CreateSwapchain()
{
    std::uint32_t formatCount = 0;
    REQUIRE(xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr), "enumerate_formats");
    std::vector<std::int64_t> formats(formatCount);
    REQUIRE(xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data()),
            "enumerate_formats");

    std::string offered;
    for (const std::int64_t f : formats) {
        offered += std::to_string(f) + " ";
    }
    Fact("formats count=%u offered=[ %s]", formatCount, offered.c_str());

    // The project's own policy, exercised against a real runtime's list for the
    // first time. Prey's backbuffer is R8G8B8A8_UNORM (28).
    const auto choice = preyvr::xrswapchain::SelectFormat(formats);
    if (!choice) {
        Fact("result=failed step=select_format detail=no_acceptable_format_offered");
        return false;
    }
    Fact("format chosen=%lld match=%d colour_conversion=%s",
         static_cast<long long>(choice->format), static_cast<int>(choice->match),
         preyvr::xrswapchain::InvolvesColourConversion(*choice) ? "yes" : "no");

    // One texture-array swapchain with a slice per eye, rather than two
    // swapchains. It is the layout Prey would most cheaply produce from a single
    // render target, and xr-sim's capture selects the submitted imageArrayIndex
    // so each eye is still captured separately.
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = choice->format;
    info.sampleCount = 1;
    info.width = width;
    info.height = height;
    info.faceCount = 1;
    info.arraySize = 2;
    info.mipCount = 1;
    REQUIRE(xrCreateSwapchain(session, &info, &swapchain), "create_swapchain");

    std::uint32_t imageCount = 0;
    REQUIRE(xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr), "enumerate_images");
    std::vector<XrSwapchainImageD3D11KHR> images(
        imageCount, XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    REQUIRE(xrEnumerateSwapchainImages(
                swapchain, imageCount, &imageCount,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
            "enumerate_images");
    Fact("swapchain created %ux%u arraySize=2 images=%u", width, height, imageCount);

    // One RTV per (image, slice) so a frame can clear each eye distinctly --
    // which is what makes the captured pair prove the eyes were addressed
    // separately rather than both written by accident.
    for (std::uint32_t i = 0; i < imageCount; ++i) {
        for (std::uint32_t slice = 0; slice < 2; ++slice) {
            D3D11_RENDER_TARGET_VIEW_DESC desc{};
            desc.Format = static_cast<DXGI_FORMAT>(choice->format);
            desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice = 0;
            desc.Texture2DArray.FirstArraySlice = slice;
            desc.Texture2DArray.ArraySize = 1;

            ID3D11RenderTargetView* rtv = nullptr;
            if (FAILED(device->CreateRenderTargetView(images[i].texture, &desc, &rtv))) {
                Fact("result=failed step=create_rtv image=%u slice=%u", i, slice);
                return false;
            }
            rtvs.push_back(rtv);
        }
    }
    return true;
}

bool Probe::RunFrames(int count)
{
    const std::uint32_t thisThread = GetCurrentThreadId();
    // The probe is single-threaded, so it cannot honour XR-005's game/render
    // thread split. Reported rather than worked around: the contract's
    // wrongThread result is correct here and the DLL will satisfy it for real.
    const std::uint32_t pretendRenderThread = thisThread + 1;

    int submitted = 0;
    bool sessionBegun = false;

    for (int spin = 0; spin < count * 20 && submitted < count; ++spin) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(instance, &event) == XR_SUCCESS) {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto* changed =
                    reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                Fact("session_state %d", static_cast<int>(changed->state));
                if (changed->state == XR_SESSION_STATE_READY && !sessionBegun) {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType = viewConfig;
                    REQUIRE(xrBeginSession(session, &begin), "begin_session");
                    sessionBegun = true;
                    contract.SetSessionRunning(true);
                } else if (changed->state == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(session);
                    contract.SetSessionRunning(false);
                    sessionBegun = false;
                }
            }
            event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (!sessionBegun) {
            Sleep(5);
            continue;
        }

        XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        REQUIRE(xrWaitFrame(session, &waitInfo, &frameState), "wait_frame");

        const auto waited = contract.OnWaited(
            frameState.predictedDisplayTime, frameState.shouldRender != XR_FALSE, thisThread);
        if (waited != preyvr::xrframe::Result::ok) {
            Fact("result=failed step=contract_wait detail=%d", static_cast<int>(waited));
            return false;
        }

        // Both eyes located at the cached predicted time, in one call. Locating
        // them separately is how the two eyes end up from different instants.
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = viewConfig;
        locate.displayTime = frameState.predictedDisplayTime;
        locate.space = space;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        std::uint32_t located = 0;
        XrView xrViews[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        REQUIRE(xrLocateViews(session, &locate, &viewState, 2, &located, xrViews), "locate_views");

        const bool posesValid =
            (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
            (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
        contract.OnViewsLocated(frameState.predictedDisplayTime, posesValid, posesValid);

        XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        REQUIRE(xrBeginFrame(session, &beginInfo), "begin_frame");
        contract.OnBegun();

        XrCompositionLayerProjectionView projViews[2]{};
        bool rendered = false;

        if (contract.ShouldRenderThisFrame()) {
            std::uint32_t imageIndex = 0;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            REQUIRE(xrAcquireSwapchainImage(swapchain, &acquire, &imageIndex), "acquire_image");
            XrSwapchainImageWaitInfo waitImage{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitImage.timeout = XR_INFINITE_DURATION;
            REQUIRE(xrWaitSwapchainImage(swapchain, &waitImage), "wait_image");

            // Distinct colours per eye so the captured pair proves the two
            // slices were addressed separately. A single clear colour would look
            // identical whether or not the array indexing worked.
            const float left[4] = {0.85f, 0.15f, 0.15f, 1.0f};
            const float right[4] = {0.15f, 0.35f, 0.85f, 1.0f};
            context->ClearRenderTargetView(rtvs[imageIndex * 2 + 0], left);
            context->ClearRenderTargetView(rtvs[imageIndex * 2 + 1], right);
            context->Flush();

            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            REQUIRE(xrReleaseSwapchainImage(swapchain, &release), "release_image");

            for (int eye = 0; eye < 2; ++eye) {
                projViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                projViews[eye].pose = xrViews[eye].pose;
                projViews[eye].fov = xrViews[eye].fov;
                projViews[eye].subImage.swapchain = swapchain;
                projViews[eye].subImage.imageArrayIndex = static_cast<std::uint32_t>(eye);
                projViews[eye].subImage.imageRect.offset = {0, 0};
                projViews[eye].subImage.imageRect.extent = {static_cast<std::int32_t>(width),
                                                            static_cast<std::int32_t>(height)};
            }
            rendered = true;

            if (submitted == 0) {
                // Report the first frame's geometry through the project's own
                // conversion, so the maths modules are exercised against a real
                // runtime's poses rather than only against unit-test inputs.
                const preyvr::Pose xrLeft{
                    {xrViews[0].pose.orientation.x, xrViews[0].pose.orientation.y,
                     xrViews[0].pose.orientation.z, xrViews[0].pose.orientation.w},
                    {xrViews[0].pose.position.x, xrViews[0].pose.position.y,
                     xrViews[0].pose.position.z}};
                const preyvr::Pose xrRight{
                    {xrViews[1].pose.orientation.x, xrViews[1].pose.orientation.y,
                     xrViews[1].pose.orientation.z, xrViews[1].pose.orientation.w},
                    {xrViews[1].pose.position.x, xrViews[1].pose.position.y,
                     xrViews[1].pose.position.z}};
                const preyvr::Pose engineLeft =
                    preyvr::stereo::EyePoseInWorld(preyvr::stereo::ReferenceFrame{}, xrLeft);
                const preyvr::Pose engineRight =
                    preyvr::stereo::EyePoseInWorld(preyvr::stereo::ReferenceFrame{}, xrRight);
                const float ipd = engineRight.position.x - engineLeft.position.x;
                Fact("engine_space left=%.4f,%.4f,%.4f right=%.4f,%.4f,%.4f ipd_m=%.4f",
                     engineLeft.position.x, engineLeft.position.y, engineLeft.position.z,
                     engineRight.position.x, engineRight.position.y, engineRight.position.z, ipd);
                Fact("fov_left l=%.4f r=%.4f u=%.4f d=%.4f (radians)",
                     xrViews[0].fov.angleLeft, xrViews[0].fov.angleRight,
                     xrViews[0].fov.angleUp, xrViews[0].fov.angleDown);

                preyvr::stereoframe::EyeView probeEye{};
                probeEye.tanLeft = std::tanf(xrViews[0].fov.angleLeft);
                probeEye.tanRight = std::tanf(xrViews[0].fov.angleRight);
                probeEye.tanDown = std::tanf(xrViews[0].fov.angleDown);
                probeEye.tanUp = std::tanf(xrViews[0].fov.angleUp);
                const auto projection =
                    preyvr::stereoframe::ProjectionFromTangents(probeEye, 0.1f);
                if (projection) {
                    Fact("prey_projection fov=%.6f ratio=%.6f asym=%.6f,%.6f,%.6f,%.6f",
                         projection->fov, projection->projectionRatio,
                         projection->asymmetry.left, projection->asymmetry.right,
                         projection->asymmetry.bottom, projection->asymmetry.top);
                } else {
                    Fact("prey_projection result=refused detail=runtime_fov_not_convertible");
                }
            }
        }

        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = space;
        layer.viewCount = 2;
        layer.views = projViews;

        const XrCompositionLayerBaseHeader* layers[] = {
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};

        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime = frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount = rendered ? 1u : 0u;
        endInfo.layers = rendered ? layers : nullptr;
        REQUIRE(xrEndFrame(session, &endInfo), "end_frame");

        contract.OnSubmitted(pretendRenderThread);
        if (rendered) {
            ++submitted;
        }
    }

    Fact("frames submitted=%d completed=%llu skipped=%llu protocol_errors=%llu",
         submitted,
         static_cast<unsigned long long>(contract.CompletedFrames()),
         static_cast<unsigned long long>(contract.SkippedFrames()),
         static_cast<unsigned long long>(contract.ProtocolErrors()));
    return submitted > 0;
}

void Probe::Shutdown()
{
    for (auto* rtv : rtvs) {
        if (rtv) rtv->Release();
    }
    rtvs.clear();
    if (swapchain) xrDestroySwapchain(swapchain);
    if (space) xrDestroySpace(space);
    if (session) xrDestroySession(session);
    if (context) context->Release();
    if (device) device->Release();
    if (instance) xrDestroyInstance(instance);
}

} // namespace

int main(int argc, char** argv)
{
    int frames = 30;
    if (argc > 1) {
        frames = std::atoi(argv[1]);
        if (frames < 1) frames = 1;
    }

    Fact("probe version=1 purpose=full_d3d11_session_lifecycle frames=%d", frames);

    Probe probe;
    int code = 0;
    if (!probe.CreateInstance()) {
        code = 2;
    } else if (!probe.CreateDeviceOnRequiredAdapter()) {
        code = 3;
    } else if (!probe.CreateSession()) {
        code = 4;
    } else if (!probe.CreateSwapchain()) {
        code = 5;
    } else if (!probe.RunFrames(frames)) {
        code = 6;
    } else {
        Fact("result=ok detail=session_lifecycle_complete");
    }
    if (code != 0) {
        Fact("result=failed exit=%d", code);
    }
    probe.Shutdown();
    return code;
}
