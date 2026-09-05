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

#include "preyvr/MotionController.h"
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

    // --- input -------------------------------------------------------------
    //
    // The motion-controller half of the project has never touched a runtime.
    // preyvr::controller is well covered by unit tests, but every one of those
    // feeds it a pose we made up; nothing has ever checked that the action
    // system can be set up at all, or that a pose which came out of a real
    // xrLocateSpace survives the basis change intact.
    //
    // That gap is closable without a headset and without a human, which is why
    // it is closed here rather than being left for a headset day.
    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction aimPoseAction = XR_NULL_HANDLE;
    XrAction gripPoseAction = XR_NULL_HANDLE;
    XrAction triggerAction = XR_NULL_HANDLE;
    XrAction selectAction = XR_NULL_HANDLE;
    XrPath handPath[2]{};                     // 0 = left, 1 = right
    XrSpace aimSpace[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};
    XrSpace gripSpace[2]{XR_NULL_HANDLE, XR_NULL_HANDLE};
    bool inputReady = false;
    int invariantsChecked = 0;
    int invariantsFailed = 0;

    preyvr::xrframe::FrameContract contract;

    bool CreateInstance();
    bool CreateDeviceOnRequiredAdapter();
    bool CreateSession();
    bool CreateInput();
    bool CreateSwapchain();
    bool RunFrames(int count);
    // How often the controllers are read. 0 means "on the last frame only",
    // which is the cheap default; a driver that wants to command a pose and see
    // the result sets this so there is a fresh reading after each command.
    int inputEvery = 0;
    void ReadInput(XrTime displayTime, int frameIndex);
    void Expect(bool condition, const char* name, const char* detail);
    void Shutdown();
};

// One invariant, reported as its own line so a failure names itself rather than
// being buried in a summary.
void Probe::Expect(bool condition, const char* name, const char* detail)
{
    ++invariantsChecked;
    if (!condition) {
        ++invariantsFailed;
    }
    Fact("invariant name=%s result=%s detail=%s", name, condition ? "pass" : "FAIL", detail);
}

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

// Sets up the OpenXR action system: an action set, pose/float/bool actions bound
// for both hands, and an action space per pose.
//
// **Bindings are suggested for the simple controller as well as Touch.** The
// simple controller profile is the one every conformant runtime must support, so
// binding only to Touch would make this probe pass on hardware that happens to be
// Oculus and fail everywhere else -- the same single-runtime trap that F-010
// recorded for the API version.
//
// Failure here is reported but is **not fatal to the run**. A runtime that
// declines the action system still has a valid stereo lifecycle worth measuring,
// and collapsing the whole probe because input was unavailable would lose that.
bool Probe::CreateInput()
{
    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::snprintf(setInfo.actionSetName, sizeof(setInfo.actionSetName), "preyvr");
    std::snprintf(setInfo.localizedActionSetName, sizeof(setInfo.localizedActionSetName),
                  "PreyVR");
    setInfo.priority = 0;
    REQUIRE(xrCreateActionSet(instance, &setInfo, &actionSet), "create_action_set");

    REQUIRE(xrStringToPath(instance, "/user/hand/left", &handPath[0]), "path_left");
    REQUIRE(xrStringToPath(instance, "/user/hand/right", &handPath[1]), "path_right");

    const auto makeAction = [&](XrActionType type, const char* name, const char* localized,
                                XrAction* out) -> XrResult {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        info.actionType = type;
        // Subaction paths on every action, so one action serves both hands and
        // the hand is chosen at query time. Two separate actions per input would
        // work too, and would double the binding table for no gain.
        info.countSubactionPaths = 2;
        info.subactionPaths = handPath;
        std::snprintf(info.actionName, sizeof(info.actionName), "%s", name);
        std::snprintf(info.localizedActionName, sizeof(info.localizedActionName), "%s", localized);
        return xrCreateAction(actionSet, &info, out);
    };

    REQUIRE(makeAction(XR_ACTION_TYPE_POSE_INPUT, "aim", "Aim", &aimPoseAction), "action_aim");
    REQUIRE(makeAction(XR_ACTION_TYPE_POSE_INPUT, "grip", "Grip", &gripPoseAction), "action_grip");
    REQUIRE(makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", &triggerAction),
            "action_trigger");
    REQUIRE(makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "select", "Select", &selectAction),
            "action_select");

    const auto path = [&](const char* text) {
        XrPath p{};
        xrStringToPath(instance, text, &p);
        return p;
    };

    // Suggested per profile. A runtime keeps whichever profiles it recognises and
    // rejects the rest, so a profile it refuses must not stop the others being
    // offered.
    struct Profile {
        const char* name;
        const char* trigger;
        const char* select;
    };
    const Profile profiles[] = {
        {"/interaction_profiles/khr/simple_controller", nullptr,
         "/input/select/click"},
        {"/interaction_profiles/oculus/touch_controller", "/input/trigger/value",
         "/input/trigger/value"},
    };

    int accepted = 0;
    for (const Profile& profile : profiles) {
        std::vector<XrActionSuggestedBinding> bindings;
        for (int hand = 0; hand < 2; ++hand) {
            const std::string base = hand == 0 ? "/user/hand/left" : "/user/hand/right";
            bindings.push_back({aimPoseAction, path((base + "/input/aim/pose").c_str())});
            bindings.push_back({gripPoseAction, path((base + "/input/grip/pose").c_str())});
            if (profile.trigger != nullptr) {
                bindings.push_back({triggerAction, path((base + profile.trigger).c_str())});
            }
            if (profile.select != nullptr) {
                bindings.push_back({selectAction, path((base + profile.select).c_str())});
            }
        }

        XrInteractionProfileSuggestedBinding suggested{
            XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggested.interactionProfile = path(profile.name);
        suggested.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
        suggested.suggestedBindings = bindings.data();

        const XrResult r = xrSuggestInteractionProfileBindings(instance, &suggested);
        Fact("suggest_bindings profile=%s count=%zu result=%s", profile.name, bindings.size(),
             ResultName(r));
        if (XR_SUCCEEDED(r)) {
            ++accepted;
        }
    }
    if (accepted == 0) {
        Fact("result=failed step=suggest_bindings detail=no_profile_accepted");
        return false;
    }

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &actionSet;
    REQUIRE(xrAttachSessionActionSets(session, &attach), "attach_action_sets");

    for (int hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo aimInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        aimInfo.action = aimPoseAction;
        aimInfo.subactionPath = handPath[hand];
        aimInfo.poseInActionSpace.orientation.w = 1.0f;
        REQUIRE(xrCreateActionSpace(session, &aimInfo, &aimSpace[hand]), "create_aim_space");

        XrActionSpaceCreateInfo gripInfo = aimInfo;
        gripInfo.action = gripPoseAction;
        REQUIRE(xrCreateActionSpace(session, &gripInfo, &gripSpace[hand]), "create_grip_space");
    }

    inputReady = true;
    Fact("input result=ok profiles_accepted=%d detail=action_sets_attached", accepted);
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

// Reads the controllers and runs them through preyvr::controller, checking the
// properties that must hold whatever the runtime happens to report.
//
// **The invariants are chosen to be independent of the commanded pose**, because
// a check that only holds for one input is a check that passes by luck. A basis
// change between two right-handed frames is a rigid motion, so it must preserve
// distances and angles; if it did not, the failure would be a mirrored or scaled
// world, which is exactly the bug that is hard to see and easy to ship.
void Probe::ReadInput(XrTime displayTime, int frameIndex)
{
    if (!inputReady) {
        return;
    }

    XrActiveActionSet active{actionSet, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    const XrResult synced = xrSyncActions(session, &sync);
    // XR_SESSION_NOT_FOCUSED is a success code, not an error: the session is
    // running but not receiving input. Worth naming, because it produces
    // untracked poses that would otherwise look like a conversion bug.
    Fact("sync_actions result=%s", ResultName(synced));
    if (XR_FAILED(synced)) {
        return;
    }

    for (int hand = 0; hand < 2; ++hand) {
        // Both hands, because the profile is per subaction path. Reporting only
        // the left made a run look controller-less while the right hand was live
        // and tracked.
        const char* label = hand == 0 ? "left" : "right";
        XrInteractionProfileState profileState{XR_TYPE_INTERACTION_PROFILE_STATE};
        if (!XR_SUCCEEDED(xrGetCurrentInteractionProfile(session, handPath[hand], &profileState))) {
            continue;
        }
        char name[XR_MAX_PATH_LENGTH]{};
        std::uint32_t written = 0;
        if (profileState.interactionProfile != XR_NULL_PATH &&
            XR_SUCCEEDED(xrPathToString(instance, profileState.interactionProfile, sizeof(name),
                                        &written, name))) {
            Fact("interaction_profile hand=%s path=%s", label, name);
        } else {
            Fact("interaction_profile hand=%s path=none detail=no_profile_bound", label);
        }
    }

    const preyvr::stereo::ReferenceFrame reference{};
    preyvr::Vec3 xrOrigin[2]{};
    preyvr::Vec3 engineOrigin[2]{};
    bool tracked[2] = {false, false};

    for (int hand = 0; hand < 2; ++hand) {
        const char* label = hand == 0 ? "left" : "right";

        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = aimPoseAction;
        get.subactionPath = handPath[hand];
        XrActionStatePose poseState{XR_TYPE_ACTION_STATE_POSE};
        xrGetActionStatePose(session, &get, &poseState);

        get.action = triggerAction;
        XrActionStateFloat triggerState{XR_TYPE_ACTION_STATE_FLOAT};
        xrGetActionStateFloat(session, &get, &triggerState);

        get.action = selectAction;
        XrActionStateBoolean selectState{XR_TYPE_ACTION_STATE_BOOLEAN};
        xrGetActionStateBoolean(session, &get, &selectState);

        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        const XrResult located = xrLocateSpace(aimSpace[hand], space, displayTime, &location);

        // **Valid and tracked are different bits and mean different things.**
        // Valid says the pose field holds a number worth reading; tracked says
        // the runtime is actually observing the device rather than extrapolating
        // from its last known position. PoseValidity carries all four for that
        // reason, and filling only half of it is how a coasting controller gets
        // treated as a live one.
        const auto flag = [&](XrSpaceLocationFlags bit) {
            return XR_SUCCEEDED(located) && (location.locationFlags & bit) != 0;
        };
        preyvr::PoseValidity validity{};
        validity.positionValid = flag(XR_SPACE_LOCATION_POSITION_VALID_BIT);
        validity.orientationValid = flag(XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
        validity.positionTracked = flag(XR_SPACE_LOCATION_POSITION_TRACKED_BIT);
        validity.orientationTracked = flag(XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT);
        const bool valid = validity.positionValid && validity.orientationValid;

        Fact("controller frame=%d hand=%s active=%d valid=%d tracked=%d trigger=%.3f select=%d",
             frameIndex, label, poseState.isActive ? 1 : 0, valid ? 1 : 0,
             (validity.positionTracked && validity.orientationTracked) ? 1 : 0,
             triggerState.currentState, selectState.currentState ? 1 : 0);
        if (!valid) {
            continue;
        }
        tracked[hand] = true;

        const preyvr::Pose xrPose{
            {location.pose.orientation.x, location.pose.orientation.y,
             location.pose.orientation.z, location.pose.orientation.w},
            {location.pose.position.x, location.pose.position.y, location.pose.position.z}};
        xrOrigin[hand] = xrPose.position;

        const preyvr::Pose enginePose =
            preyvr::controller::ControllerPoseInWorld(reference, xrPose);
        engineOrigin[hand] = enginePose.position;

        Fact("controller_pose frame=%d hand=%s xr=%.4f,%.4f,%.4f engine=%.4f,%.4f,%.4f",
             frameIndex, label,
             xrPose.position.x, xrPose.position.y, xrPose.position.z,
             enginePose.position.x, enginePose.position.y, enginePose.position.z);

        // The whole point of the module: a tracked hand becoming an aim ray.
        // The validity comes from the runtime's own flags above, not from an
        // assertion here that the pose must be good.
        const auto ray = preyvr::controller::AimFromController(reference, xrPose, validity, 0);

        // **Valid but not tracked must yield nothing, and that is the contract
        // being tested here -- not a failure.** A runtime reports this while it
        // is extrapolating from a controller's last known position: the numbers
        // are readable but the device is not being observed. AimFromController
        // refuses such a pose on purpose, because a plausible wrong aim is worse
        // than no aim -- it fires.
        //
        // The first version of this probe asserted a ray whenever the pose was
        // valid, and so counted six correct refusals as failures on the first
        // real-hardware run. xr-sim always reports tracked, so only a real
        // runtime could surface it.
        const bool fullyTracked = validity.positionTracked && validity.orientationTracked;
        if (!fullyTracked) {
            Expect(!ray.has_value(), "untracked_pose_yields_no_ray",
                   "a coasting controller must not produce an aim the game would shoot along");
            continue;
        }
        if (!ray) {
            Expect(false, "aim_ray_produced", "a fully tracked pose yielded no ray");
            continue;
        }
        Expect(true, "aim_ray_produced", "a tracked pose produced a ray");
        Fact("aim_ray frame=%d hand=%s origin=%.4f,%.4f,%.4f direction=%.4f,%.4f,%.4f",
             frameIndex, label,
             ray->origin.x, ray->origin.y, ray->origin.z,
             ray->direction.x, ray->direction.y, ray->direction.z);

        const float length = std::sqrt(ray->direction.x * ray->direction.x +
                                       ray->direction.y * ray->direction.y +
                                       ray->direction.z * ray->direction.z);
        Expect(std::fabs(length - 1.0f) < 1e-3f, "aim_direction_unit_length",
               "a non-unit ray scales every distance the trace reports");

        const preyvr::Vec3 forward = preyvr::controller::ForwardOf(enginePose);
        const float agreement = forward.x * ray->direction.x + forward.y * ray->direction.y +
                                forward.z * ray->direction.z;
        Expect(agreement > 0.999f, "aim_matches_pose_forward",
               "the ray must be the pose's own forward, not a second opinion");

        // **The weapon-mount lane, driven by a real located pose.**
        //
        // `MountRotationFromControllerDelta` is well covered by unit tests, but
        // every one of those feeds it a quaternion we invented -- the same gap
        // the action system had before this probe existed. What is untested is
        // the *pipeline*: a runtime-located aim reaching the shipping function
        // and carrying the rotation that was actually commanded.
        //
        // The authored mount is deliberately **not identity**, so the
        // composition is real rather than a pass-through. The check the script
        // then performs is the angle between the returned mount and that
        // authored baseline, which must equal the commanded rotation -- and is
        // invariant under the basis change, so it does not quietly depend on
        // whether this is measured in OpenXR or engine space.
        static std::optional<preyvr::Quaternion> calibrationAim[2];
        if (!calibrationAim[hand].has_value()) {
            calibrationAim[hand] = enginePose.orientation;
            Fact("mount_calibrated frame=%d hand=%s", frameIndex, label);
        }
        // 30 degrees about engine X.
        const preyvr::Quaternion authoredMount{0.258819f, 0.0f, 0.0f, 0.9659258f};
        const auto mount = preyvr::controller::MountRotationFromControllerDelta(
            authoredMount, *calibrationAim[hand], enginePose.orientation);
        if (mount) {
            Fact("mount frame=%d hand=%s mount=%.6f,%.6f,%.6f,%.6f "
                 "authored=%.6f,%.6f,%.6f,%.6f",
                 frameIndex, label, mount->x, mount->y, mount->z, mount->w,
                 authoredMount.x, authoredMount.y, authoredMount.z, authoredMount.w);
        } else {
            // A located, fully-tracked pose must compose. Silence here would be
            // the weapon keeping its animated mount forever with nothing said.
            Expect(false, "mount_composed", "a tracked aim yielded no mount rotation");
        }

        // The basis change, stated as the axis images it is defined by. OpenXR is
        // right-handed Y-up with -Z forward; CryEngine is right-handed Z-up with
        // +Y forward. Rx(+90) is the only rotation that carries one to the other,
        // and a sign slip here mirrors the world.
        const preyvr::Vec3 xrRight = preyvr::Rotate(xrPose.orientation, {1.0f, 0.0f, 0.0f});
        const preyvr::Vec3 xrUp = preyvr::Rotate(xrPose.orientation, {0.0f, 1.0f, 0.0f});
        const preyvr::Vec3 engineRight =
            preyvr::Rotate(enginePose.orientation, {1.0f, 0.0f, 0.0f});
        const preyvr::Vec3 engineUp = preyvr::Rotate(enginePose.orientation, {0.0f, 0.0f, 1.0f});
        if (hand == 0) {
            // Compared component-wise under the expected relabelling: OpenXR
            // (x, y, z) becomes engine (x, -z, y).
            Expect(std::fabs(engineRight.x - xrRight.x) < 1e-3f &&
                   std::fabs(engineRight.y + xrRight.z) < 1e-3f &&
                   std::fabs(engineRight.z - xrRight.y) < 1e-3f,
                   "basis_maps_right_to_right", "OpenXR +X must land on engine +X");
            Expect(std::fabs(engineUp.x - xrUp.x) < 1e-3f &&
                   std::fabs(engineUp.y + xrUp.z) < 1e-3f &&
                   std::fabs(engineUp.z - xrUp.y) < 1e-3f,
                   "basis_maps_up_to_up", "OpenXR +Y must land on engine +Z");
        }
    }

    // Rigidity. This is the strongest check available without knowing what was
    // commanded: a proper rotation preserves the distance between two points, so
    // if the hands are further apart in engine space than in OpenXR space the
    // conversion is not a rotation at all.
    if (tracked[0] && tracked[1]) {
        const auto distance = [](const preyvr::Vec3& a, const preyvr::Vec3& b) {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            const float dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };
        const float xrGap = distance(xrOrigin[0], xrOrigin[1]);
        const float engineGap = distance(engineOrigin[0], engineOrigin[1]);
        Fact("hand_separation xr_m=%.5f engine_m=%.5f", xrGap, engineGap);
        Expect(std::fabs(xrGap - engineGap) < 1e-3f, "basis_change_preserves_distance",
               "a basis change that changes lengths is a scale or a mirror, not a rotation");
    }
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

        // Read the controllers on the last frame only. Not for tidiness: a
        // session takes a few frames to reach focus, and input read before then
        // comes back untracked -- which would look exactly like a conversion bug.
        const bool lastFrame = submitted + 1 >= count;
        const bool periodic = inputEvery > 0 && (submitted % inputEvery) == 0;
        if (lastFrame || periodic) {
            Fact("head_pose frame=%d xr=%.4f,%.4f,%.4f quat=%.4f,%.4f,%.4f,%.4f valid=%d",
                 submitted, xrViews[0].pose.position.x, xrViews[0].pose.position.y,
                 xrViews[0].pose.position.z, xrViews[0].pose.orientation.x,
                 xrViews[0].pose.orientation.y, xrViews[0].pose.orientation.z,
                 xrViews[0].pose.orientation.w, posesValid ? 1 : 0);
            ReadInput(frameState.predictedDisplayTime, submitted);
        }

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
    // Action spaces before the action set: they reference the actions the set
    // owns, and destroying the set first leaves them dangling.
    for (int hand = 0; hand < 2; ++hand) {
        if (aimSpace[hand]) xrDestroySpace(aimSpace[hand]);
        if (gripSpace[hand]) xrDestroySpace(gripSpace[hand]);
    }
    if (actionSet) xrDestroyActionSet(actionSet);
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
    int inputEvery = 0;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--input-every" && i + 1 < argc) {
            inputEvery = std::atoi(argv[++i]);
            if (inputEvery < 1) inputEvery = 1;
        } else if (positional == 0) {
            frames = std::atoi(argv[i]);
            if (frames < 1) frames = 1;
            ++positional;
        } else {
            // Second positional is the sampling interval. The dashed spelling
            // above is kept, but this is the one to prefer: a flag that gets
            // silently dropped in transit produces a run that looks fine and
            // measures once.
            inputEvery = std::atoi(argv[i]);
            if (inputEvery < 1) inputEvery = 1;
            ++positional;
        }
    }

    Fact("probe version=2 purpose=full_d3d11_session_lifecycle frames=%d input_every=%d",
         frames, inputEvery);

    Probe probe;
    probe.inputEvery = inputEvery;
    int code = 0;
    if (!probe.CreateInstance()) {
        code = 2;
    } else if (!probe.CreateDeviceOnRequiredAdapter()) {
        code = 3;
    } else if (!probe.CreateSession()) {
        code = 4;
    } else if (!probe.CreateSwapchain()) {
        code = 5;
    } else if (!probe.CreateInput()) {
        // Not fatal on its own. A runtime without the action system still has a
        // stereo lifecycle worth measuring, and the input result is reported
        // separately so a partial answer is not mistaken for a total failure.
        Fact("input result=unavailable detail=continuing_without_controllers");
        if (!probe.RunFrames(frames)) {
            code = 6;
        }
    } else if (!probe.RunFrames(frames)) {
        code = 6;
    } else {
        Fact("result=ok detail=session_lifecycle_complete");
    }

    if (probe.invariantsChecked > 0) {
        Fact("invariants checked=%d failed=%d", probe.invariantsChecked, probe.invariantsFailed);
        // A failed invariant is a failed run even if every OpenXR call succeeded.
        // The calls succeeding is what makes a silent maths error dangerous.
        if (probe.invariantsFailed > 0 && code == 0) {
            code = 7;
        }
    } else if (probe.inputReady) {
        Fact("invariants checked=0 detail=controllers_never_tracked");
    }
    if (code != 0) {
        Fact("result=failed exit=%d", code);
    }
    probe.Shutdown();
    return code;
}
