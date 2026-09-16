#include "XrSessionHost.h"

#include "preyvr/DepthSubmission.h"
#include "preyvr/FrameTiming.h"
#include "preyvr/FrustumCoverage.h"

#include "Logger.h"
#include "preyvr/EngineMap.h"
#include "preyvr/XrFrameContract.h"
#include "CameraEditHook.h"
#include "HeadTrackingHook.h"
#include "XrInput.h"
#include "HudBridge.h"
#include "UiGuide.h"
#include "UiPointer.h"
#include "HudLayer.h"
#include "InventorySwapchain.h"
#include "preyvr/UiPanel.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/HudCapturePair.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/StereoFrame.h"
#include "preyvr/XrSwapchainFormat.h"

#include <d3d11.h>
#include <dxgi1_2.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <chrono>
#include <mutex>
#include <memory>
#include <string>
#include <sstream>
#include <vector>

namespace preyvr::dll {
namespace {

// A **timed** mutex, and nothing ever blocks on it indefinitely.
//
// Diagnosed the hard way on 2026-08-31: a plain std::mutex here deadlocked. An
// external tool calls a control export while the render thread services frames,
// and a blocking lock between those two turns any mishap into a hang -- the
// export never returns, the caller reports a vague error, and the real cause is
// invisible. A hang is strictly worse than a failure, because a failure says
// what happened.
//
// So the control path waits briefly and then *refuses*, and the render path uses
// try_lock and simply skips the frame. Neither can ever wait on the other.
std::timed_mutex gMutex;
constexpr auto kControlLockTimeout = std::chrono::milliseconds(250);
std::atomic<DWORD> gStatus{static_cast<DWORD>(XrSessionStatus::idle)};
std::atomic<bool> gStopRequested{false};
std::atomic<bool> gStopAcknowledged{false};
std::atomic<unsigned long long> gSubmitted{0};
std::atomic<unsigned int> gUiPanelMode{0};
std::atomic<unsigned long long> gUiPanelFrames{0};
std::atomic<unsigned long long> gInventoryLayerFrames{0};
std::atomic<unsigned int> gUiCurveDegrees{35};
std::atomic<float> gRuntimeIpd{0};
struct BackbufferSample { unsigned width=0,height=0; std::uint64_t changed=0,stamp=0; unsigned frames=0; };
LatestSnapshot<BackbufferSample> gBackbufferSample;

struct Host {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;
    XrSwapchain guideSwapchain = XR_NULL_HANDLE;
    XrSwapchain pointerSwapchain = XR_NULL_HANDLE;
    bool pointerReady=false,pointerAttempted=false,cylinderSupported=false;
    XrSwapchain hudSwapchain = XR_NULL_HANDLE;
    std::unique_ptr<InventorySwapchain> inventorySwapchain;
    std::unique_ptr<InventorySwapchain> inventoryRightSwapchain;
    std::vector<XrSwapchainImageD3D11KHR> hudImages;
    D3D11_TEXTURE2D_DESC hudDesc{};
    bool guideReady = false, guideAttempted = false;
    std::vector<ID3D11Texture2D*> images;

    ID3D11Device* device = nullptr;   // Prey's, borrowed -- never released here
    // The resolution chain, measured rather than assumed. The submitted size is
    // Prey's backbuffer by construction (see CreateSessionAndSwapchain), so the
    // runtime's own recommendation was never consulted and the gap between them
    // was never a number. RE-HEADSET-RESOLUTION-2026-09-08 asks for exactly this
    // before any resolution work: measure the chain first.
    std::uint32_t recommendedWidth = 0, recommendedHeight = 0;
    std::uint32_t maxWidth = 0, maxHeight = 0;
    std::uint32_t recommendedSamples = 0, maxSamples = 0;
    std::uint32_t viewCount = 0;
    bool viewsDiffer = false;          // per-eye sizes are allowed to differ
    std::uint32_t heldWidth = 0, heldHeight = 0, heldFormat = 0;
    // Depth submission (XR_KHR_composition_layer_depth). The swapchain is built
    // only when the runtime offers the game's own depth format, because the copy
    // is a straight CopyResource and a format mismatch there fails at the API
    // rather than converting.
    bool depthSupported = false;                    // extension enabled
    XrSwapchain depthSwapchain = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> depthImages;
    std::uint32_t depthFormat = 0;                  // DXGI format actually created
    std::uint32_t submittedWidth = 0, submittedHeight = 0;
    // Set when Prey's backbuffer no longer matches the size this session was
    // built at, which makes every copy invalid until a restart.
    bool sizeMismatch = false;
    bool loggedSizeMismatch = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool sessionBegun = false;
    bool started = false;
    std::optional<ui::Panel> menuPanel;
    std::optional<ui::Panel> guidePanel;
    unsigned long long panelReference = 0;
    bool panelWasActive = false;
    float panelAngle=0;
    unsigned long long surfaceSerial=0;

    // Stereo submission: one held image per eye.
    //
    // Prey renders one eye per frame, so the backbuffer is never both. These two
    // textures hold the most recent image of each eye so that every submitted
    // frame can carry a complete pair -- one of them a few frames older than the
    // other. That staleness is the price of alternate-eye and it is invisible in
    // a frozen scene, which is how the first depth test is run.
    ID3D11Texture2D* eyeImage[2] = {nullptr, nullptr};
    bool eyeImageValid[2] = {false, false};
    HudCapturePair hudCapturePair;

    // **The contract that rendered each eye's pixels, held with them.**
    //
    // FAIL-STR-044, raised by the playbook session from Crysis 2 VR's runtime
    // records (author-grade, not independently replayed -- treated as a design
    // warning rather than a measurement). Carrying the eye identity is necessary
    // and not sufficient: the two held images are captured on *different* frames,
    // so labelling both with the pose and FOV read at submission time pairs older
    // pixels with a newer contract. "Latest left plus latest right is not a pair,
    // and a fresh ticket does not make stale pixels fresh."
    //
    // Invisible today, because the rendered image does not yet follow the head, so
    // a newer pose describes the same picture. It becomes shear and rubber-banding
    // the moment the view seam lands -- which is why this is fixed before that
    // rather than after.
    XrPosef eyePose[2]{};
    XrFovf eyeFov[2]{};
    // What the runtime asked for, kept beside what we declared. The two are
    // different frusta and the gap between them is spent pixels: comparing them
    // is the only way to say how much of the render the headset can show.
    XrFovf requestedFov[2]{};
    bool requestedFovValid[2]{};
    XrTime eyeDisplayTime[2]{};
    xrframe::FrameContract contract;
};

Host gHost;
LatestSnapshot<std::array<DWORD,15>> gResolution;
LatestSnapshot<std::array<XrFovf,2>> gOptics;
void PublishResolution() {
    gResolution.Publish({gHost.recommendedWidth,gHost.recommendedHeight,gHost.maxWidth,gHost.maxHeight,
        gHost.width,gHost.height,gHost.heldWidth,gHost.heldHeight,gHost.submittedWidth,gHost.submittedHeight,
        gHost.recommendedSamples,gHost.viewsDiffer?1u:0u,gHost.heldFormat,gHost.viewCount,gHost.sizeMismatch?1u:0u});
}

// Stereo submission, off by default.
//
// With this clear the path is the original flat mirror: the same backbuffer into
// both eyes, declaring the runtime's own FOV. That is a mono image and a false
// declaration, but it is a *known* one that proved the plumbing, and it stays
// reachable so a regression here can be bisected against it.
std::atomic<bool> gStereoSubmission{false};

// Sends each eye's image to the other eye's socket.
//
// **Insurance, not a feature.** Everything says the mapping is already right --
// eye 0 takes the negative offset in BuildSyntheticEye, and the red-left /
// blue-right check through the optics confirmed slice 0 is the left eye. But
// inverted stereo does not look broken, it looks subtly wrong: depth that will
// not settle, and discomfort that is easy to blame on judder or on the frustum.
// If that is what a session reports, this settles it in one call instead of
// costing a rebuild -- and a rebuild is not always available.
std::atomic<bool> gSwapEyes{false};

// Ask for the sRGB swapchain format instead of the exact match -- FAIL-STR-033.
// Read once when the swapchain is created, so it must be set before StartXrSession.
std::atomic<bool> gPreferSrgb{false};

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_xr_session " + line);
}

void LogResult(const char* step, XrResult result)
{
    std::ostringstream line;
    line << "result=failed step=" << step << " code=" << static_cast<int>(result);
    Log(line.str());
}

// Prey's renderer holds the device it created at startup (R-007).
ID3D11Device* PreyDevice(void* renderer)
{
    if (renderer == nullptr) {
        return nullptr;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(renderer);
    return *reinterpret_cast<ID3D11Device**>(address + engine::RendererLayout::device);
}

// Which adapter is a device actually on? Answered through the device rather than
// assumed from an index, because the index is exactly what is in question.
bool AdapterLuidOf(ID3D11Device* device, LUID& out)
{
    IDXGIDevice* dxgiDevice = nullptr;
    if (device == nullptr ||
        FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)))) {
        return false;
    }
    IDXGIAdapter* adapter = nullptr;
    const bool got = SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter != nullptr;
    dxgiDevice->Release();
    if (!got) {
        return false;
    }
    DXGI_ADAPTER_DESC desc{};
    const bool described = SUCCEEDED(adapter->GetDesc(&desc));
    adapter->Release();
    if (!described) {
        return false;
    }
    out = desc.AdapterLuid;
    return true;
}

// The EnumAdapters1 order is the order r_overrideDXGIAdapter indexes into, so
// this is the number to report.
int AdapterIndexOfLuid(const LUID& luid)
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        return -1;
    }
    int found = -1;
    for (UINT i = 0; found < 0; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            desc.AdapterLuid.HighPart == luid.HighPart &&
            desc.AdapterLuid.LowPart == luid.LowPart) {
            found = static_cast<int>(i);
        }
        adapter->Release();
    }
    factory->Release();
    return found;
}

// openxr_loader.dll is delay-loaded, so it must be brought in explicitly before
// the first XR call -- otherwise the delay-load helper raises a structured
// exception on a missing module, which is a far worse way to learn it is absent.
//
// Loaded from **this DLL's own directory** rather than by bare name, so the
// result does not depend on the host process's search path. That matters here:
// we are injected, and Prey's working directory is not ours.
bool EnsureLoaderPresent()
{
    static const wchar_t* kLoader = L"openxr_loader.dll";
    if (GetModuleHandleW(kLoader) != nullptr) {
        return true;
    }

    HMODULE self = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&EnsureLoaderPresent), &self) == 0 || self == nullptr) {
        return false;
    }
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(self, path, MAX_PATH) == 0) {
        return false;
    }
    std::wstring beside(path);
    const std::size_t slash = beside.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return false;
    }
    beside.resize(slash + 1);
    beside += kLoader;

    if (LoadLibraryW(beside.c_str()) != nullptr) {
        return true;
    }
    // Last resort: the normal search path, in case it was deployed elsewhere.
    return LoadLibraryW(kLoader) != nullptr;
}

void EnsurePointerTexture()
{
    if(gHost.pointerAttempted)return;
    gHost.pointerAttempted=true;
    std::uint32_t count=0;
    if(XR_FAILED(xrEnumerateSwapchainFormats(gHost.session,0,&count,nullptr)))return;
    std::vector<std::int64_t> formats(count);
    if(XR_FAILED(xrEnumerateSwapchainFormats(gHost.session,count,&count,formats.data())))return;
    auto format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if(std::find(formats.begin(),formats.end(),format)==formats.end())format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    if(std::find(formats.begin(),formats.end(),format)==formats.end())return;
    std::array<std::uint8_t,32*32*4> pixels{};
    for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
        const float dx=static_cast<float>(x)-15.5f,dy=static_cast<float>(y)-15.5f;
        const auto alpha=static_cast<std::uint8_t>(std::clamp(14.5f-std::sqrt(dx*dx+dy*dy),0.f,1.f)*255);
        const auto offset=static_cast<std::size_t>((y*32+x)*4);
        pixels[offset]=format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB?90:255;
        pixels[offset+1]=235;pixels[offset+2]=format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB?255:90;pixels[offset+3]=alpha;
    }
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.createFlags=XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    create.usageFlags=XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    create.format=format;create.sampleCount=1;create.width=32;create.height=32;
    create.faceCount=1;create.arraySize=1;create.mipCount=1;
    if(XR_FAILED(xrCreateSwapchain(gHost.session,&create,&gHost.pointerSwapchain)))return;
    if(XR_FAILED(xrEnumerateSwapchainImages(gHost.pointerSwapchain,0,&count,nullptr)))return;
    std::vector<XrSwapchainImageD3D11KHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if(XR_FAILED(xrEnumerateSwapchainImages(gHost.pointerSwapchain,count,&count,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))))return;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
    std::uint32_t index=0;
    if(XR_FAILED(xrAcquireSwapchainImage(gHost.pointerSwapchain,&acquire,&index)))return;
    if(XR_FAILED(xrWaitSwapchainImage(gHost.pointerSwapchain,&wait))){gStopRequested.store(true);return;}
    if(index<images.size()) {
        ID3D11DeviceContext* context=nullptr;gHost.device->GetImmediateContext(&context);
        if(context){context->UpdateSubresource(images[index].texture,0,nullptr,pixels.data(),32*4,0);context->Release();gHost.pointerReady=true;}
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(XR_FAILED(xrReleaseSwapchainImage(gHost.pointerSwapchain,&release)))gHost.pointerReady=false;
    Log(std::string("ui_pointer texture_ready=")+(gHost.pointerReady?"1":"0"));
}

bool CreateInstanceAndSystem()
{
    std::vector<const char*> enabled{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    std::uint32_t extensionCount=0;
    if(XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr,0,&extensionCount,nullptr))) {
        std::vector<XrExtensionProperties> extensions(extensionCount,{XR_TYPE_EXTENSION_PROPERTIES});
        if(XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr,extensionCount,&extensionCount,extensions.data())))
            // Braced deliberately: this loop was a single unbraced statement, and
            // adding a second test to it silently moved that test outside the loop.
            for(const auto& extension:extensions) {
                if(std::strcmp(extension.extensionName,XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME)==0) {
                    enabled.push_back(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME);gHost.depthSupported=true;
                }
                if(std::strcmp(extension.extensionName,XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME)==0) {
                    enabled.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);gHost.cylinderSupported=true;
                }
            }
    }
    Log(std::string("xr_depth extension_supported=")+(gHost.depthSupported?"1":"0"));
    Log(std::string("ui_cylinder supported=")+(gHost.cylinderSupported?"1":"0 flat_fallback=1"));
    // Newest-first with a fallback: xr-sim accepts 1.1, VirtualDesktopXR does not
    // (F-010). One build has to work against both.
    const XrVersion candidates[] = {XR_CURRENT_API_VERSION, XR_API_VERSION_1_0};

    XrResult result = XR_ERROR_RUNTIME_FAILURE;
    for (const XrVersion candidate : candidates) {
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        info.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
        info.enabledExtensionNames = enabled.data();
        std::snprintf(info.applicationInfo.applicationName,
                      sizeof(info.applicationInfo.applicationName), "PreyVR");
        std::snprintf(info.applicationInfo.engineName,
                      sizeof(info.applicationInfo.engineName), "CryEngine");
        info.applicationInfo.applicationVersion = 1;
        info.applicationInfo.engineVersion = 1;
        info.applicationInfo.apiVersion = candidate;

        result = xrCreateInstance(&info, &gHost.instance);
        if (XR_SUCCEEDED(result)) {
            std::ostringstream line;
            line << "instance created api_version=" << XR_VERSION_MAJOR(candidate) << '.'
                 << XR_VERSION_MINOR(candidate) << '.' << XR_VERSION_PATCH(candidate);
            Log(line.str());
            break;
        }
        gHost.instance = XR_NULL_HANDLE;
        if (result != XR_ERROR_API_VERSION_UNSUPPORTED) {
            break;
        }
    }
    if (gHost.instance == XR_NULL_HANDLE) {
        LogResult("create_instance", result);
        return false;
    }

    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(gHost.instance, &props))) {
        Log(std::string("runtime name=\"") + props.runtimeName + "\"");
    }

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    result = xrGetSystem(gHost.instance, &systemInfo, &gHost.systemId);
    if (XR_FAILED(result)) {
        LogResult("get_system", result);
        return false;
    }
    return true;
}

// The check this whole module exists for.
bool AdapterAgrees()
{
    PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
    if (XR_FAILED(xrGetInstanceProcAddr(
            gHost.instance, "xrGetD3D11GraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements))) ||
        getRequirements == nullptr) {
        Log("result=failed step=d3d11_requirements_proc");
        return false;
    }

    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    const XrResult got = getRequirements(gHost.instance, gHost.systemId, &requirements);
    if (XR_FAILED(got)) {
        LogResult("d3d11_graphics_requirements", got);
        return false;
    }

    LUID preyLuid{};
    if (!AdapterLuidOf(gHost.device, preyLuid)) {
        Log("result=failed step=prey_adapter_luid");
        return false;
    }

    const int requiredIndex = AdapterIndexOfLuid(requirements.adapterLuid);
    const int preyIndex = AdapterIndexOfLuid(preyLuid);

    std::ostringstream line;
    line << "adapter prey_luid=0x" << std::hex << preyLuid.HighPart << ':' << preyLuid.LowPart
         << " required_luid=0x" << requirements.adapterLuid.HighPart << ':'
         << requirements.adapterLuid.LowPart << std::dec
         << " prey_index=" << preyIndex << " required_index=" << requiredIndex;
    Log(line.str());

    if (preyLuid.HighPart == requirements.adapterLuid.HighPart &&
        preyLuid.LowPart == requirements.adapterLuid.LowPart) {
        Log("adapter result=agree");
        return true;
    }

    // Cannot be fixed from here: r_overrideDXGIAdapter is read once during device
    // creation, long before this DLL loaded. So say precisely what to set and
    // refuse, rather than starting a session that fails somewhere less obvious.
    std::ostringstream fix;
    fix << "adapter result=mismatch detail=set_before_launch"
        << " r_overrideDXGIAdapter=" << requiredIndex
        << " note=cvar_is_read_once_during_device_creation";
    Log(fix.str());
    return false;
}

bool CreateSessionAndSwapchain()
{
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = gHost.device;

    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
    info.next = &binding;
    info.systemId = gHost.systemId;
    XrResult result = xrCreateSession(gHost.instance, &info, &gHost.session);
    if (XR_FAILED(result)) {
        LogResult("create_session", result);
        return false;
    }

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    result = xrCreateReferenceSpace(gHost.session, &spaceInfo, &gHost.space);
    if (XR_FAILED(result)) {
        LogResult("create_reference_space", result);
        return false;
    }

    // Controller input, created with the session because OpenXR permits
    // xrAttachSessionActionSets exactly once per session. A failure here is
    // logged and tolerated: the result is a session with no hands, which is far
    // better than no session at all.
    if (!CreateXrInput(gHost.instance, gHost.session)) {
        Log("result=degraded detail=no_controller_input");
    }

    std::uint32_t formatCount = 0;
    if (XR_FAILED(xrEnumerateSwapchainFormats(gHost.session, 0, &formatCount, nullptr))) {
        Log("result=failed step=enumerate_formats");
        return false;
    }
    std::vector<std::int64_t> formats(formatCount);
    xrEnumerateSwapchainFormats(gHost.session, formatCount, &formatCount, formats.data());

    const auto choice = xrswapchain::SelectFormat(
        formats, xrswapchain::kR8G8B8A8Unorm, gPreferSrgb.load(std::memory_order_acquire));
    if (!choice) {
        Log("result=failed step=select_format detail=no_acceptable_format");
        return false;
    }

    // Measure what the runtime asks for before building at a different size, so
    // the difference is recorded rather than discovered later in a headset. This
    // does not change what is submitted; it makes the current policy visible.
    {
        std::uint32_t viewCount = 0;
        const XrViewConfigurationType viewConfig = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(
                gHost.instance, gHost.systemId, viewConfig, 0, &viewCount, nullptr)) &&
            viewCount > 0) {
            std::vector<XrViewConfigurationView> views(
                viewCount, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
            if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(
                    gHost.instance, gHost.systemId, viewConfig, viewCount, &viewCount,
                    views.data())) && viewCount > 0) {
                gHost.viewCount = viewCount;
                gHost.recommendedWidth = views[0].recommendedImageRectWidth;
                gHost.recommendedHeight = views[0].recommendedImageRectHeight;
                gHost.maxWidth = views[0].maxImageRectWidth;
                gHost.maxHeight = views[0].maxImageRectHeight;
                gHost.recommendedSamples = views[0].recommendedSwapchainSampleCount;
                gHost.maxSamples = views[0].maxSwapchainSampleCount;
                for (std::uint32_t i = 1; i < viewCount; ++i) {
                    if (views[i].recommendedImageRectWidth != gHost.recommendedWidth ||
                        views[i].recommendedImageRectHeight != gHost.recommendedHeight) {
                        gHost.viewsDiffer = true;
                    }
                }
                std::ostringstream line;
                line << "result=0 detail=view_configuration views=" << viewCount
                     << " recommended=" << gHost.recommendedWidth << "x" << gHost.recommendedHeight
                     << " max=" << gHost.maxWidth << "x" << gHost.maxHeight
                     << " recommendedSamples=" << gHost.recommendedSamples
                     << " maxSamples=" << gHost.maxSamples
                     << " viewsDiffer=" << (gHost.viewsDiffer ? 1 : 0)
                     << " submitting=" << gHost.width << "x" << gHost.height
                     << " note=submitted_size_is_preys_backbuffer_not_the_recommendation";
                Log(line.str());
            }
        }
    }

    // Built at **Prey's backbuffer size**, not the runtime's recommendation, so
    // the first light can CopyResource straight across with no scaling blit. The
    // runtime scales for display. Matching the recommendation is a later
    // optimisation and a different problem.
    XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.usageFlags =
        XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    swapchainInfo.format = choice->format;
    gHost.colorFormat = static_cast<DXGI_FORMAT>(choice->format);
    swapchainInfo.sampleCount = 1;
    swapchainInfo.width = gHost.width;
    swapchainInfo.height = gHost.height;
    swapchainInfo.faceCount = 1;
    swapchainInfo.arraySize = 2;
    swapchainInfo.mipCount = 1;
    result = xrCreateSwapchain(gHost.session, &swapchainInfo, &gHost.swapchain);
    if (XR_FAILED(result)) {
        LogResult("create_swapchain", result);
        return false;
    }

    std::uint32_t imageCount = 0;
    xrEnumerateSwapchainImages(gHost.swapchain, 0, &imageCount, nullptr);
    std::vector<XrSwapchainImageD3D11KHR> images(
        imageCount, XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (XR_FAILED(xrEnumerateSwapchainImages(
            gHost.swapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))) {
        Log("result=failed step=enumerate_images");
        return false;
    }
    gHost.images.clear();
    for (const auto& image : images) {
        gHost.images.push_back(image.texture);
    }

    std::ostringstream line;
    line << "swapchain created " << gHost.width << 'x' << gHost.height
         << " format=" << choice->format << " arraySize=2 images=" << imageCount
         << " colour_conversion=" << (xrswapchain::InvolvesColourConversion(*choice) ? "yes" : "no");
    Log(line.str());
    PublishResolution();
    return true;
}

void Teardown()
{
    gResolution.Clear();
    gOptics.Clear();
    SetHudLayerPresentation(false);
    SetInventoryConsumerReady(nullptr);
    gHost.inventorySwapchain.reset(); // XR images die before their session.
    gHost.inventoryRightSwapchain.reset();
    DestroyXrInput();
    for (auto*& image : gHost.eyeImage) {
        if (image != nullptr) {
            image->Release();
            image = nullptr;
        }
    }
    if (gHost.guideSwapchain) xrDestroySwapchain(gHost.guideSwapchain);
    if (gHost.pointerSwapchain) xrDestroySwapchain(gHost.pointerSwapchain);
    ClearUiPointer();
    if (gHost.hudSwapchain) xrDestroySwapchain(gHost.hudSwapchain);
    if (gHost.swapchain) xrDestroySwapchain(gHost.swapchain);
    if (gHost.space) xrDestroySpace(gHost.space);
    if (gHost.session) xrDestroySession(gHost.session);
    if (gHost.instance) xrDestroyInstance(gHost.instance);
    gHost = Host{};
    Log("result=0 detail=torn_down");
}

// Logs a line at most once, so a per-frame refusal cannot fill the log.
void LogOnce(const std::string& line)
{
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true, std::memory_order_acq_rel)) {
        Log(line);
    }
}

// Reads Prey's live view camera and returns the frustum to declare.
//
// The same code path the verified PreyVR_ReadDeclaredFovPtr uses, so what is
// submitted is what was measured on 2026-09-02 -- 120 degrees horizontal by
// 88.507 vertical, zero asymmetry, confirmed against the player's FOV slider.
//
// Returns nothing rather than guessing. Every caller must treat that as "do not
// submit a layer".
// **Off by default, because a profiler that is always on is a profiler nobody
// trusts.** Recording is a store and an increment, but the honest way to answer
// "did measuring change the number" is to be able to turn it off.
std::atomic<bool> gTimingEnabled{false};
preyvr::timing::IntervalSeries gServiceInterval;
preyvr::timing::DurationSeries gWaitFrame, gAcquireWait, gEndFrame, gServiceTotal;
// The runtime's OWN display period, from XrFrameState, reported beside the
// configured budget so nobody has to assume the two agree. `xr.timing 1 90`
// configures 11111 us; if the runtime is at 72 Hz that budget is simply wrong,
// and only this number says so.

// **Off by default, and it stays that way until a wearer has judged it.** A
// wrong depth layer degrades the image everywhere and looks like a subtle warp
// under head motion rather than an obvious fault, so this is not a default.
// **Eye skew: how far apart in time the two submitted eyes are.**
//
// Prey renders one eye per frame, so a submitted pair is always one older image
// and one newer one. `eyeDisplayTime[2]` was already being written at the
// publication unit and never read by anything -- this reads it. The skew is the
// difference between the two eyes' display times, which is the temporal
// disparity a wearer perceives as the pair not agreeing.
//
// Reuses DurationSeries so the distribution comes from tested code: a p50 says
// what the pair normally looks like, and the max says whether an eye ever
// starved for much longer than one frame.
preyvr::timing::DurationSeries gEyeSkew;
std::atomic<int> gStaleEye{-1};              // which eye currently holds the older image
std::atomic<unsigned long long> gEyeSkewSamples{0};

// **A wearer's dial on panel size, in percent.** FitPanel's 60/40 caps are a
// comfort default and the corner test uses the frustum the runtime REPORTS,
// which on a Quest 3 is wider than the lenses show. Where the real edge sits is
// not derivable -- only a wearer can find it -- so this is a knob rather than a
// constant. Applied to the menu panel and the HUD alike, because they are the
// same presentation and one dial is easier to reason about than two.
std::atomic<unsigned int> gUiScalePercent{100};
// **The two things that actually cap panel size, both reported as `ui.scale`
// doing nothing above ~130.**
//
// gUiFitMargin: the fraction of the reported frustum a panel must stay inside.
// Once it binds, the fit loops shrink away every further increase in scale, so
// the dial appears to have a maximum it does not have. See kDefaultFitMargin.
//
// gUiGuide: the instruction card stacked under the menu. It is ours, added for
// onboarding -- not a native Prey element -- and it is charged to the same
// vertical budget, so the menu above it is roughly a sixth smaller than it
// would be alone.
//
// **Off by default from 2026-09-11**, at the wearer's request after measuring
// what it costs: the card is read once and then paid for on every menu, and a
// wearer who already knows the controls was giving up a sixth of the panel to
// keep it. `ui.guide 1` brings it back for the session, and UiGuide in
// PreyVR.json brings it back for good -- which is what to set when handing the
// package to someone who has not seen the controls before.
std::atomic<unsigned int> gUiFitMarginPercent{72};
std::atomic<bool> gUiGuide{false};
float UiFitMargin() {
    return static_cast<float>(gUiFitMarginPercent.load(std::memory_order_relaxed))*.01f;
}

std::atomic<bool> gDepthEnabled{false};
std::atomic<unsigned long long> gDepthSubmitted{0}, gDepthRefused{0};
std::atomic<int> gDepthNearMilli{0}, gDepthFarMilli{0};
std::atomic<bool> gDepthReversed{false};

// R-130: the renderer holds its depth-stencil view at +0x9970, proved by the
// OMSetRenderTargets call that passes it in the pDepthStencilView slot. A view
// is not a resource, so the texture behind it has to be fetched -- which is also
// the check that the offset still points at a DSV, because GetResource on
// anything else does not survive the QueryInterface below.
ID3D11Texture2D* GameDepthTexture(D3D11_TEXTURE2D_DESC& desc)
{
    constexpr std::uintptr_t kNativeZSurface = 0x9970;
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) { return nullptr; }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    auto* const renderer =
        *reinterpret_cast<std::uint8_t**>(base + engine::RendererLayout::singletonPointerRva);
    if (renderer == nullptr) { return nullptr; }
    auto* const view =
        *reinterpret_cast<ID3D11DepthStencilView**>(renderer + kNativeZSurface);
    if (view == nullptr) { return nullptr; }

    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    if (resource == nullptr) { return nullptr; }
    ID3D11Texture2D* texture = nullptr;
    resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
    resource->Release();
    if (texture == nullptr) { return nullptr; }
    texture->GetDesc(&desc);
    return texture;   // caller releases
}

// The engine's own near and far, live, in engine units. Same receiver as
// DeclaredFovFromLiveCamera, so the planes and the frustum come from one camera
// rather than two reads that could straddle a frame.
bool GameDepthPlanes(float& nearPlane, float& farPlane)
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) { return false; }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    auto* const systemPtr =
        *reinterpret_cast<std::uint8_t**>(base + engine::SystemLayout::pointerRva);
    if (systemPtr == nullptr) { return false; }
    const auto* const camera =
        reinterpret_cast<const std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(systemPtr) + engine::SystemLayout::viewCamera);
    // GetNearPlane()/GetFarPlane() are the .y of the edge vectors, per the
    // camera layout -- +4 into each, not the vector base.
    std::memcpy(&nearPlane, camera + engine::CameraLayout::edgeNearLeftTop + 4, sizeof(float));
    std::memcpy(&farPlane, camera + engine::CameraLayout::edgeFarLeftTop + 4, sizeof(float));
    return true;
}

std::atomic<std::uint32_t> gRuntimePeriodUs{0};

std::atomic<unsigned long long> gFovAgree{0};
std::atomic<unsigned long long> gFovDiverge{0};
std::atomic<unsigned int> gWorstDivergenceMilliTan{0};
std::atomic<bool> gDivergenceLogged{false};

// Compares what we are about to DECLARE against what we actually RENDERED with.
//
// **No tool outside this process can perform this check.** xr-tape sees the FOV
// the runtime located and the FOV we declared; the engine's own projection never
// crosses the OpenXR boundary. So `submitted_fov_matches_located` can be in its
// correct state -- failing, which for an injector is the pass -- while the image
// is still a lie, because the mod itself replaced the projection and left the
// declaration behind. That is exactly what happened on 2026-09-05.
//
// Compared in **tangent** space, not degrees: every correct operation on a
// frustum is linear in tangents, and a degrees-space comparison mis-weights the
// edges where the error actually shows.
//
// Read-only and advisory. It counts and logs; it never edits a submission,
// because a wrong declaration is a bug to fix at its source, not to paper over
// at the boundary.
void AssertDeclaredMatchesRendered(const XrFovf& declared)
{
    float tanLeft = 0.0f, tanRight = 0.0f, tanUp = 0.0f, tanDown = 0.0f;
    if (!dll::RenderedEyeTangents(tanLeft, tanRight, tanUp, tanDown)) {
        return;   // no eye built yet; silence is correct, not a pass
    }
    const float declaredTan[4] = {
        std::tan(declared.angleLeft), std::tan(declared.angleRight),
        std::tan(declared.angleUp), std::tan(declared.angleDown)};
    const float renderedTan[4] = {tanLeft, tanRight, tanUp, tanDown};

    float worst = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(declaredTan[i]) || !std::isfinite(renderedTan[i])) {
            return;
        }
        worst = std::max(worst, std::abs(declaredTan[i] - renderedTan[i]));
    }
    // 0.01 in tangent is well under a degree near the axis and still catches the
    // 50-vs-60 degree half-angle case, which differs by 0.54.
    if (worst <= 0.01f) {
        gFovAgree.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    gFovDiverge.fetch_add(1, std::memory_order_relaxed);
    gWorstDivergenceMilliTan.store(
        static_cast<unsigned int>(worst * 1000.0f + 0.5f), std::memory_order_relaxed);
    // Logged once. A per-frame line would bury the thing it is reporting.
    if (!gDivergenceLogged.exchange(true, std::memory_order_relaxed)) {
        std::ostringstream line;
        line << "result=warning detail=declared_fov_differs_from_rendered"
             << " worstTanDelta=" << worst
             << " declaredTan=[" << declaredTan[0] << "," << declaredTan[1]
             << "," << declaredTan[2] << "," << declaredTan[3] << "]"
             << " renderedTan=[" << renderedTan[0] << "," << renderedTan[1]
             << "," << renderedTan[2] << "," << renderedTan[3] << "]";
        Log(line.str());
    }
}

// **Why the flat mirror looks small, and the one knob for it.**
//
// Without a held eye pair -- at the main menu, or any time Prey is not rendering
// alternating eyes -- the mirror submits Prey's OWN declared frustum, because
// that is the frustum the pixels came from. Declaring the runtime's field over a
// narrower render would claim an angular size the image does not have.
//
// Measured on this build at the main menu: Prey declares 51.8 degrees horizontal
// and symmetric, while the runtime's view is 98 degrees and asymmetric, so the
// compositor lays the image into the central 41% and the menu reads as a small
// window rather than a screen. That is the honest projection working, not a bug.
//
// For a flat 2D menu, though, geometric honesty buys nothing: there is no depth
// to get wrong, only text to read. Scaling the declared field makes the image
// span more of the view. It is DEFAULT OFF at 100, because it is a visual
// trade-off that only a headset can settle, and for 3D content it is a genuine
// distortion rather than a preference.
std::atomic<unsigned int> gMirrorFovPercent{100};

XrFovf ScaleDeclaredFov(const XrFovf& fov)
{
    const unsigned int percent = gMirrorFovPercent.load(std::memory_order_acquire);
    if (percent == 100u) { return fov; }
    // Scaling the TANGENT, not the angle: the projection is linear in tangent
    // space, so doubling the angle would not double the apparent size.
    const float scale = static_cast<float>(percent) * 0.01f;
    const auto grow = [scale](float angle) {
        return std::atan(std::tan(angle) * scale);
    };
    XrFovf out{};
    out.angleLeft = grow(fov.angleLeft);
    out.angleRight = grow(fov.angleRight);
    out.angleUp = grow(fov.angleUp);
    out.angleDown = grow(fov.angleDown);
    return out;
}

std::optional<XrFovf> DeclaredFovFromLiveCamera()
{
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return std::nullopt;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    auto* const systemPtr = *reinterpret_cast<std::uint8_t**>(base + engine::SystemLayout::pointerRva);
    if (systemPtr == nullptr) {
        return std::nullopt;
    }
    const auto* const camera = reinterpret_cast<const std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(systemPtr) + engine::SystemLayout::viewCamera);
    const auto view = stereoframe::TangentsFromCamera(
        std::span<const std::uint8_t>(camera, engine::CameraLayout::size));
    if (!view) {
        return std::nullopt;
    }
    const auto angles = stereoframe::AnglesFromTangents(*view);
    XrFovf fov{};
    fov.angleLeft = angles.angleLeft;
    fov.angleRight = angles.angleRight;
    fov.angleDown = angles.angleDown;
    fov.angleUp = angles.angleUp;
    return fov;
}

// Holds one image per eye and submits a complete pair every frame.
//
// Prey renders one eye at a time, so the backbuffer is only ever half of a
// stereo pair. This keeps the other half from the last time that eye was on
// screen. The frame service consumes one eye label at every renderer frame,
// including menu/flat/skipped XR frames, and passes that label with this buffer.
//
// Returns false until both eyes have been seen at least once. Submitting a pair
// with one empty half would show a black eye, which reads as a broken headset
// rather than as a warming-up mod.
bool SubmitStereoPair(
    ID3D11DeviceContext* context,
    ID3D11Texture2D* backBuffer,
    std::uint32_t imageIndex,
    const XrView* views,
    const std::optional<XrFovf>& declaredFov,
    XrTime displayTime, int renderedEye, bool hudCaptured)
{
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);

    for (int eye = 0; eye < 2; ++eye) {
        if (gHost.eyeImage[eye] != nullptr) {
            continue;
        }
        D3D11_TEXTURE2D_DESC hold = desc;
        hold.Usage = D3D11_USAGE_DEFAULT;
        hold.BindFlags = 0;
        hold.CPUAccessFlags = 0;
        hold.MiscFlags = 0;
        gHost.heldWidth = hold.Width;
        gHost.heldHeight = hold.Height;
        gHost.heldFormat = static_cast<std::uint32_t>(hold.Format);
        if (FAILED(gHost.device->CreateTexture2D(&hold, nullptr, &gHost.eyeImage[eye]))) {
            gHost.eyeImage[eye] = nullptr;
            LogOnce("result=failed step=create_eye_image");
            return false;
        }
    }

    // **The eye arrives with the frame.** No dwell, no lock, no waiting: the
    // camera hook published which eye it built and this pops the one belonging
    // to the frame just finished. Every rendered frame therefore updates an eye,
    // so a pair refreshes every 2 frames rather than every 2*dwell.
    //
    // -1 means the game thread has not published yet -- starting up, or the
    // render thread ran ahead. Hold the existing pair for a frame rather than
    // copying a backbuffer whose eye is unknown, which would be a coin flip.
    const int eye = renderedEye;
    if (eye == 0 || eye == 1) {
        const int target = gSwapEyes.load(std::memory_order_acquire) ? (1 - eye) : eye;
        context->CopyResource(gHost.eyeImage[target], backBuffer);
        gHost.hudCapturePair.RecordEye(target,hudCaptured);
        // Publish the pixels and the contract that produced them together. The
        // copy and these three writes are the one publication unit; nothing
        // downstream may pair this image with any other frame's pose or FOV.
        gHost.eyePose[target] = views[target].pose;
        gHost.eyeFov[target] = declaredFov ? *declaredFov : views[target].fov;
        // Stored in the SAME publication unit as the declaration it is compared
        // against, so a coverage figure can never pair this frame's request with
        // another frame's render.
        gHost.requestedFov[target] = views[target].fov;
        gHost.requestedFovValid[target] = true;
        // Checked here, at the one point the declaration is bound to the pixels
        // it describes -- the same publication unit FAIL-STR-044 established.
        AssertDeclaredMatchesRendered(gHost.eyeFov[target]);
        gHost.eyeDisplayTime[target] = displayTime;
        gHost.eyeImageValid[target] = true;
    }

    if (!gHost.eyeImageValid[0] || !gHost.eyeImageValid[1]) {
        return false;
    }
    // Both halves are present, so the pair about to be submitted is complete and
    // its skew is meaningful. Sampled here rather than at the copy, because a
    // single eye's timestamp is not a skew until the other one exists.
    if (gHost.eyeDisplayTime[0] != 0 && gHost.eyeDisplayTime[1] != 0) {
        const XrTime a = gHost.eyeDisplayTime[0], b = gHost.eyeDisplayTime[1];
        const auto skew = static_cast<std::uint64_t>(a > b ? a - b : b - a);
        gEyeSkew.AddNanoseconds(skew);
        gEyeSkewSamples.fetch_add(1, std::memory_order_relaxed);
        // Which eye is behind, not merely how far. A skew that stays on one eye
        // is a scheduling asymmetry; one that alternates is the ordinary
        // one-frame-per-eye cadence.
        gStaleEye.store(a < b ? 0 : 1, std::memory_order_relaxed);
    }
    for (std::uint32_t slice = 0; slice < 2; ++slice) {
        context->CopySubresourceRegion(
            gHost.images[imageIndex], D3D11CalcSubresource(0, slice, 1),
            0, 0, 0, gHost.eyeImage[slice], 0, nullptr);
    }
    return true;
}

void PumpEvents()
{
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(gHost.instance, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            if (changed->state == XR_SESSION_STATE_READY && !gHost.sessionBegun) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_SUCCEEDED(xrBeginSession(gHost.session, &begin))) {
                    gHost.sessionBegun = true;
                    gHost.contract.SetSessionRunning(true);
                    Log("session begun");
                }
            } else if (changed->state == XR_SESSION_STATE_STOPPING) {
                xrEndSession(gHost.session);
                gHost.sessionBegun = false;
                gHost.contract.SetSessionRunning(false);
                Log("session stopping");
                gStopRequested.store(true);
            } else if(changed->state==XR_SESSION_STATE_EXITING || changed->state==XR_SESSION_STATE_LOSS_PENDING) {
                gStopRequested.store(true);
            }
        } else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            gStopRequested.store(true);
        }
        event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
    }
}

} // namespace

DWORD SetXrRuntimeManifest(const char* manifestPath)
{
    std::unique_lock lock(gMutex, kControlLockTimeout);
    if (!lock.owns_lock()) {
        Log("result=failed detail=busy step=set_runtime_manifest");
        return static_cast<DWORD>(XrSessionStatus::failed);
    }
    if (gHost.started) {
        // The loader has already resolved a runtime; changing the variable now
        // would only mislead whoever reads the log later.
        Log("result=refused detail=runtime_manifest_after_session_start");
        return static_cast<DWORD>(XrSessionStatus::failed);
    }

    if (manifestPath == nullptr || *manifestPath == 0) {
        SetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr);
        Log("runtime_manifest cleared detail=using_machine_runtime");
        return static_cast<DWORD>(XrSessionStatus::idle);
    }

    const int needed = MultiByteToWideChar(CP_UTF8, 0, manifestPath, -1, nullptr, 0);
    if (needed <= 1 || needed > MAX_PATH * 4) {
        Log("result=refused detail=runtime_manifest_path_invalid");
        return static_cast<DWORD>(XrSessionStatus::failed);
    }
    std::wstring wide(static_cast<std::size_t>(needed), 0);
    MultiByteToWideChar(CP_UTF8, 0, manifestPath, -1, wide.data(), needed);
    wide.resize(static_cast<std::size_t>(needed-1));

    // Checked, because a wrong path does not fail -- the loader falls back to the
    // registry runtime and the session runs against something else entirely while
    // the log claims otherwise.
    if (GetFileAttributesW(wide.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(std::string("result=refused detail=runtime_manifest_not_found path=") + manifestPath);
        return static_cast<DWORD>(XrSessionStatus::unavailable);
    }

    if (!SetEnvironmentVariableW(L"XR_RUNTIME_JSON", wide.c_str())) {
        Log("result=failed detail=set_runtime_manifest_env");
        return static_cast<DWORD>(XrSessionStatus::failed);
    }
    Log(std::string("runtime_manifest set path=") + manifestPath);
    return static_cast<DWORD>(XrSessionStatus::idle);
}

DWORD StartXrSession()
{
    std::unique_lock lock(gMutex, kControlLockTimeout);
    if (!lock.owns_lock()) {
        Log("result=failed detail=busy step=start_session");
        return static_cast<DWORD>(XrSessionStatus::failed);
    }
    if (gHost.started) {
        return gStatus.load(std::memory_order_acquire);
    }

    // Prey's renderer and device, not ours.
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        Log("result=unavailable detail=preydll_absent");
        gStatus.store(static_cast<DWORD>(XrSessionStatus::unavailable), std::memory_order_release);
        return gStatus.load(std::memory_order_acquire);
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    void* renderer = *reinterpret_cast<void**>(
        base + engine::GlobalEnvironmentLayout::baseRva + engine::GlobalEnvironmentLayout::renderer);
    gHost.device = PreyDevice(renderer);
    if (gHost.device == nullptr) {
        Log("result=unavailable detail=prey_device_null");
        gStatus.store(static_cast<DWORD>(XrSessionStatus::unavailable), std::memory_order_release);
        return gStatus.load(std::memory_order_acquire);
    }

    // The swapchain is built at the backbuffer's size, so read it from the
    // swapchain description rather than from a cvar that may have been changed
    // since the device was created.
    auto* swapChain = *reinterpret_cast<IDXGISwapChain**>(
        reinterpret_cast<std::uintptr_t>(renderer) + engine::RendererLayout::swapchain);
    DXGI_SWAP_CHAIN_DESC desc{};
    if (swapChain == nullptr || FAILED(swapChain->GetDesc(&desc))) {
        Log("result=unavailable detail=prey_swapchain_desc");
        gStatus.store(static_cast<DWORD>(XrSessionStatus::unavailable), std::memory_order_release);
        return gStatus.load(std::memory_order_acquire);
    }
    gHost.width = desc.BufferDesc.Width;
    gHost.height = desc.BufferDesc.Height;

    // Checked before any XR call, because the loader is delay-loaded.
    if (!EnsureLoaderPresent()) {
        Log("result=unavailable detail=openxr_loader_not_found "
            "note=expected_beside_PreyVR.dll");
        gStatus.store(static_cast<DWORD>(XrSessionStatus::unavailable), std::memory_order_release);
        return gStatus.load(std::memory_order_acquire);
    }

    if (!CreateInstanceAndSystem()) {
        Teardown();
        gStatus.store(static_cast<DWORD>(XrSessionStatus::unavailable), std::memory_order_release);
        return gStatus.load(std::memory_order_acquire);
    }
    if (!AdapterAgrees()) {
        Teardown();
        gStatus.store(static_cast<DWORD>(XrSessionStatus::adapterMismatch), std::memory_order_release);
        return gStatus.load(std::memory_order_acquire);
    }
    if (!CreateSessionAndSwapchain()) {
        Teardown();
        gStatus.store(static_cast<DWORD>(XrSessionStatus::failed), std::memory_order_release);
        return gStatus.load(std::memory_order_acquire);
    }

    // Bring-up is single-threaded on the render thread; see the header.
    gHost.contract.AllowSingleThreaded(true);
    gHost.started = true;
    gStopAcknowledged.store(false,std::memory_order_release);
    gStopRequested.store(false, std::memory_order_release);
    gStatus.store(static_cast<DWORD>(XrSessionStatus::running), std::memory_order_release);
    Log("result=0 detail=started");
    return gStatus.load(std::memory_order_acquire);
}

DWORD StopXrSession()
{
    gStopAcknowledged.store(true,std::memory_order_release);
    gStopRequested.store(true, std::memory_order_release);
    return gStatus.load(std::memory_order_acquire);
}

bool XrSessionLossPending() {
    return gStopRequested.load(std::memory_order_acquire) &&
        !gStopAcknowledged.load(std::memory_order_acquire);
}

DWORD XrSessionStatusValue()
{
    return gStatus.load(std::memory_order_acquire);
}

unsigned long long XrSubmittedFrameCount()
{
    return gSubmitted.load(std::memory_order_acquire);
}

void ServiceXrFrame(void* renderer)
{
    // Renderer-frame obligation, independent of whether XR submits this frame.
    // Menu/flat paths still build cameras. Leaving their labels queued changed
    // alternating-eye parity when the backlog was eventually discarded.
    const int renderedEye = dll::ConsumeRenderedEye();
    // Take each capture once, even on skipped submissions; never reuse a PDA
    // image after a dialog or another menu has replaced it.
    ID3D11Texture2D* inventoryRightTexture=nullptr;
    ID3D11Texture2D* inventoryTexture = InventoryLayerTexture(&inventoryRightTexture);
    ID3D11Texture2D* hudTexture = HudLayerTexture();
    SetInventoryConsumerReady(nullptr);
    if (gStatus.load(std::memory_order_acquire) != static_cast<DWORD>(XrSessionStatus::running)) {
        return; // the hot path
    }
    // The render thread never waits. Skipping a frame is free; blocking Prey's
    // renderer on a control call is not.
    std::unique_lock lock(gMutex, std::try_to_lock);
    if (!lock.owns_lock() || !gHost.started) {
        return;
    }

    if (gStopRequested.load(std::memory_order_acquire)) {
        if(!gStopAcknowledged.load(std::memory_order_acquire))return;
        // Torn down here rather than in StopXrSession, because the D3D resources
        // belong to this thread.
        Teardown();
        gStatus.store(static_cast<DWORD>(XrSessionStatus::stopped), std::memory_order_release);
        return;
    }

    PumpEvents();
    if(gStopRequested.load()) {
        // Preserve the session until the worker has disarmed native camera,
        // pose and input consumers, then acknowledged StopXrSession.
        return;
    }
    if (!gHost.sessionBegun) {
        return;
    }

    const std::uint32_t thread = GetCurrentThreadId();

    // **`xrWaitFrame` blocks on purpose**, and how long it blocks is the single
    // most diagnostic number here: time spent waiting is the runtime pacing us,
    // not the scene costing us, and reclaiming pixels would not touch it.
    const bool timing = gTimingEnabled.load(std::memory_order_acquire);
    // Applied here, on the thread that records, before any sample this frame.
    // A reset run from the command thread could race an in-flight writer and let
    // a fresh epoch inherit a stale sample.
    gServiceInterval.ApplyPendingReset();
    gWaitFrame.ApplyPendingReset();
    gAcquireWait.ApplyPendingReset();
    gEndFrame.ApplyPendingReset();
    gServiceTotal.ApplyPendingReset();
    gEyeSkew.ApplyPendingReset();
    const std::uint64_t serviceStart = timing ? preyvr::timing::MonotonicNanoseconds() : 0;
    if (timing) { gServiceInterval.Mark(serviceStart); }

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    const std::uint64_t waitStart = timing ? preyvr::timing::MonotonicNanoseconds() : 0;
    const XrResult waitResult = xrWaitFrame(gHost.session, &waitInfo, &frameState);
    if (timing) {
        gWaitFrame.AddNanoseconds(preyvr::timing::MonotonicNanoseconds() - waitStart);
    }
    if (XR_FAILED(waitResult)) {
        return;
    }
    if (frameState.predictedDisplayPeriod > 0) {
        gRuntimePeriodUs.store(
            static_cast<std::uint32_t>(frameState.predictedDisplayPeriod / 1000),
            std::memory_order_relaxed);
    }
    if (gHost.contract.OnWaited(frameState.predictedDisplayTime,
                                frameState.shouldRender != XR_FALSE, thread) !=
        xrframe::Result::ok) {
        return;
    }

    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = frameState.predictedDisplayTime;
    locate.space = gHost.space;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    std::uint32_t located = 0;
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    const bool haveViews =
        XR_SUCCEEDED(xrLocateViews(gHost.session, &locate, &viewState, 2, &located, views)) &&
        located == 2 &&
        (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    gHost.contract.OnViewsLocated(frameState.predictedDisplayTime, haveViews, haveViews);

    // Hand the head pose to the M1 camera hook. The midpoint of the two eyes is
    // the head, and CyclopsPose is already the project's answer for that -- it
    // takes orientation from the left eye rather than slerping two identical
    // quaternions, which would introduce error rather than remove it.
    //
    // Published here because this is where the pose already exists. The scope
    // records that this is *after* rasterisation and therefore not where the
    // playbook says to sample; the age counter exists to measure exactly how much
    // that costs before deciding whether to move it.
    // Located against the same predicted display time as the views, so the hands
    // and the eyes describe the same instant.
    Pose inputHead{};
    PoseValidity inputHeadValidity{};

    if (haveViews) {
        const Pose left{
            Quaternion{views[0].pose.orientation.x, views[0].pose.orientation.y,
                       views[0].pose.orientation.z, views[0].pose.orientation.w},
            Vec3{views[0].pose.position.x, views[0].pose.position.y, views[0].pose.position.z}};
        const Pose right{
            Quaternion{views[1].pose.orientation.x, views[1].pose.orientation.y,
                       views[1].pose.orientation.z, views[1].pose.orientation.w},
            Vec3{views[1].pose.position.x, views[1].pose.position.y, views[1].pose.position.z}};
        inputHead = stereo::CyclopsPose(left, right);
        const Vec3 separation{right.position.x-left.position.x,
                              right.position.y-left.position.y,right.position.z-left.position.z};
        gRuntimeIpd.store(std::sqrt(separation.x*separation.x + separation.y*separation.y +
                                   separation.z*separation.z));
        for (int eye=0;eye<2;++eye) {
            gHost.requestedFov[eye]=views[eye].fov;
            gHost.requestedFovValid[eye]=true;
        }
        gOptics.Publish({views[0].fov,views[1].fov});
        inputHeadValidity.positionValid = inputHeadValidity.orientationValid = true;
        inputHeadValidity.positionTracked = (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_TRACKED_BIT) != 0;
        inputHeadValidity.orientationTracked = (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT) != 0;
        dll::PublishHeadPose(inputHead);
    }

    UpdateXrInput(gHost.session, gHost.space,
                  static_cast<long long>(frameState.predictedDisplayTime), inputHead, inputHeadValidity);

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(xrBeginFrame(gHost.session, &beginInfo))) {
        return;
    }
    gHost.contract.OnBegun();

    XrCompositionLayerProjectionView projViews[2]{};
    const auto panelMode=gUiPanelMode.load();
    // A delayed main-thread poll is unknown, not proof that a menu closed.
    // Preserve its presentation through a stall; pointer/gameplay input still
    // requires a fresh modal sample. Otherwise a busy frame flashes projection
    // and reanchors the screen when the next sample arrives.
    const bool modalForDisplay=HudMenuStateKnown()?HudMenuIsOpen():gHost.panelWasActive;
    const bool wantPanel=haveViews && (panelMode==2 ||
        (panelMode==1 && modalForDisplay));
    if (wantPanel != gHost.panelWasActive) {
        gHost.menuPanel.reset();
        gHost.guidePanel.reset();
        gHost.eyeImageValid[0]=gHost.eyeImageValid[1]=false;
        gHost.panelWasActive=wantPanel;
        Log(std::string("result=0 detail=ui_panel active=")+(wantPanel?"1":"0"));
    }
    const auto reference=HeadTrackingReferenceGeneration();
    // Stereo inventory uses a planar disparity contract. Include this in the
    // fit/serial decision so switching modes also cancels an old curved drag.
    const bool planarInventory=(InventoryStereoEnabled() && HudInventoryIsOpen()) || inventoryRightTexture;
    const float curve=gHost.cylinderSupported && !planarInventory?
        static_cast<float>(gUiCurveDegrees.load())*.01745329252f:0;
    if (wantPanel && (!gHost.menuPanel || curve!=gHost.panelAngle || (reference%2==0 && reference!=gHost.panelReference))) {
        std::array<ui::Eye,2> optical{};
        for(int eye=0;eye<2;++eye) {
            const auto& p=views[eye].pose;
            optical[eye]={Pose{{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},
                                   {p.position.x,p.position.y,p.position.z}},
                          views[eye].fov.angleLeft,views[eye].fov.angleRight,
                          views[eye].fov.angleUp,views[eye].fov.angleDown};
        }
        const float aspect=static_cast<float>(gHost.width)/static_cast<float>(gHost.height);
        constexpr float guideRatio=static_cast<float>(kGuideHeight)/kGuideWidth;
        constexpr float gapRatio=.015f;
        const float margin=UiFitMargin();
        const bool wantGuide=gUiGuide.load(std::memory_order_relaxed);
        // Without the card the menu is fitted on its own aspect, so the height
        // the card and its gap used to reserve goes back to the menu instead of
        // being left empty -- which is the whole point of switching it off.
        const float stackAspect=wantGuide?1/(1/aspect+guideRatio+gapRatio):aspect;
        auto layout=ui::FitPanel(inputHead,optical,stackAspect,2.0f,
            static_cast<float>(gUiScalePercent.load(std::memory_order_relaxed))*.01f,margin);
        if(layout) {
            gHost.menuPanel.reset();gHost.guidePanel.reset();
            for(int attempt=0;attempt<80;++attempt) {
                auto menu=*layout,guide=*layout;
                menu.height=menu.width/aspect;
                guide.height=guide.width*guideRatio;
                const float gap=menu.width*gapRatio;
                if(wantGuide) {
                    menu.pose=Compose(layout->pose,Pose{{},{0,(guide.height+gap)*.5f,0}});
                    guide.pose=Compose(layout->pose,Pose{{},{0,-(menu.height+gap)*.5f,0}});
                } else {
                    menu.pose=layout->pose;
                }
                if(ui::SurfaceVisible({menu,curve},optical,margin)&&
                   (!wantGuide||ui::SurfaceVisible({guide,0},optical,margin))) {
                    gHost.menuPanel=menu;
                    if(wantGuide)gHost.guidePanel=guide;
                    break;
                }
                layout->width*=.95f;layout->height*=.95f;
            }
        }
        gHost.panelReference=reference;
        gHost.panelAngle=curve;
        ++gHost.surfaceSerial;
        Log("ui_surface angle_degrees="+std::to_string(curve*57.2957795f));
    }
    const bool panelActive=wantPanel && gHost.menuPanel.has_value();
    if(panelActive && !gHost.guideAttempted) {
        gHost.guideAttempted=true;
        std::uint32_t count=0;
        xrEnumerateSwapchainFormats(gHost.session,0,&count,nullptr);
        std::vector<std::int64_t> formats(count);
        xrEnumerateSwapchainFormats(gHost.session,count,&count,formats.data());
        const std::int64_t format=std::find(formats.begin(),formats.end(),DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)!=formats.end()
            ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if(std::find(formats.begin(),formats.end(),format)!=formats.end()) {
            auto pixels=MakeMenuGuide();
            if(format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
                for(std::size_t i=0;i<pixels.size();i+=4)std::swap(pixels[i],pixels[i+2]);
            XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            create.createFlags=XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
            create.usageFlags=XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
            create.format=format;create.sampleCount=1;create.width=kGuideWidth;create.height=kGuideHeight;
            create.faceCount=1;create.arraySize=1;create.mipCount=1;
            if(!pixels.empty() && XR_SUCCEEDED(xrCreateSwapchain(gHost.session,&create,&gHost.guideSwapchain))) {
                std::uint32_t imageCount=0,index=0;
                xrEnumerateSwapchainImages(gHost.guideSwapchain,0,&imageCount,nullptr);
                std::vector<XrSwapchainImageD3D11KHR> images(imageCount,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
                const auto enumerated=xrEnumerateSwapchainImages(gHost.guideSwapchain,imageCount,&imageCount,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
                XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                if(XR_SUCCEEDED(enumerated) && XR_SUCCEEDED(xrAcquireSwapchainImage(gHost.guideSwapchain,&acquire,&index))) {
                    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout=XR_INFINITE_DURATION;
                    const bool waited=XR_SUCCEEDED(xrWaitSwapchainImage(gHost.guideSwapchain,&wait));
                    if(!waited)gStopRequested.store(true);
                    if(waited && index<images.size()) {
                        ID3D11DeviceContext* context=nullptr;gHost.device->GetImmediateContext(&context);
                        if(context) {
                            context->UpdateSubresource(images[index].texture,0,nullptr,pixels.data(),kGuideWidth*4,0);
                            context->Release();gHost.guideReady=true;
                        }
                        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                        if(XR_FAILED(xrReleaseSwapchainImage(gHost.guideSwapchain,&release)))gHost.guideReady=false;
                    } else if(waited) {
                        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                        xrReleaseSwapchainImage(gHost.guideSwapchain,&release);
                    }
                }
            }
        }
    }
    bool rendered = false;
    bool stereoPair = false;
    bool inventorySubmitted = false, inventoryPrepared = false;
    bool inventoryStereoSubmitted=false,inventoryStereoPrepared=false;
    D3D11_TEXTURE2D_DESC inventoryDescription{};
    if (!InventoryCaptureEnabled() && !inventoryTexture) gHost.inventorySwapchain.reset();
    if (!InventoryStereoEnabled() && !inventoryRightTexture) gHost.inventoryRightSwapchain.reset();

    if (gHost.contract.ShouldRenderThisFrame()) {
        IDXGISwapChain* swapChain = *reinterpret_cast<IDXGISwapChain**>(
            reinterpret_cast<std::uintptr_t>(renderer) + engine::RendererLayout::swapchain);
        ID3D11Texture2D* backBuffer = nullptr;
        if (swapChain != nullptr &&
            SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                           reinterpret_cast<void**>(&backBuffer))) &&
            backBuffer != nullptr) {
            std::uint32_t imageIndex = 0;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            XrSwapchainImageWaitInfo waitImage{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitImage.timeout = XR_INFINITE_DURATION;

            // Check before acquiring an XR image. A resize skips the copy, but
            // must still reach xrEndFrame/OnSubmitted below with zero layers.
            // Returning here used to strand both an acquired image and a begun
            // frame; our FrameContract then refused every subsequent wait.
            D3D11_TEXTURE2D_DESC backDesc{};
            backBuffer->GetDesc(&backDesc);
            const bool matchingSize = backDesc.Width == gHost.width &&
                                      backDesc.Height == gHost.height;
            gHost.sizeMismatch = !matchingSize;
            if (!matchingSize && !gHost.loggedSizeMismatch) {
                gHost.loggedSizeMismatch = true;
                std::ostringstream line;
                line << "result=refused detail=backbuffer_resized"
                     << " sessionSize=" << gHost.width << "x" << gHost.height
                     << " nowSize=" << backDesc.Width << "x" << backDesc.Height
                     << " note=restart_to_adopt_the_new_size";
                Log(line.str());
            }
            // Timed together: they are one logical "get me an image to draw
            // into", and splitting them would suggest the acquire can be slow
            // independently of the wait it exists to set up.
            const std::uint64_t acquireStart =
                timing ? preyvr::timing::MonotonicNanoseconds() : 0;
            ID3D11Texture2D* panelSource = backBuffer;
            // Finish an already redirected draw even if the command channel
            // disabled capture between the native callback and this boundary.
            if (matchingSize && panelActive && HudInventoryIsOpen() && (InventoryCaptureEnabled() || inventoryTexture)) {
                if (!gHost.inventorySwapchain) {
                    gHost.inventorySwapchain = std::make_unique<InventorySwapchain>(InventorySwapchainApi{
                        xrEnumerateSwapchainFormats, xrCreateSwapchain, xrEnumerateSwapchainImages,
                        xrAcquireSwapchainImage, xrWaitSwapchainImage, xrReleaseSwapchainImage, xrDestroySwapchain});
                }
                inventoryPrepared = gHost.inventorySwapchain->Prepare(gHost.session, backDesc, gHost.colorFormat);
                if(inventoryPrepared && (InventoryStereoEnabled() || inventoryRightTexture)) {
                    if(!gHost.inventoryRightSwapchain)gHost.inventoryRightSwapchain=std::make_unique<InventorySwapchain>(InventorySwapchainApi{
                        xrEnumerateSwapchainFormats,xrCreateSwapchain,xrEnumerateSwapchainImages,
                        xrAcquireSwapchainImage,xrWaitSwapchainImage,xrReleaseSwapchainImage,xrDestroySwapchain});
                    inventoryStereoPrepared=gHost.inventoryRightSwapchain->Prepare(gHost.session,backDesc,gHost.colorFormat);
                }
                inventoryDescription = backDesc;
                if (inventoryTexture) {
                    // This draw was redirected already. If XR upload fails,
                    // retain a complete opaque inventory in the legacy panel.
                    D3D11_TEXTURE2D_DESC capturedDesc{}; inventoryTexture->GetDesc(&capturedDesc);
                    if (InventoryTextureCompatible(backDesc, capturedDesc)) panelSource = inventoryTexture;
                    ID3D11DeviceContext* context = nullptr; gHost.device->GetImmediateContext(&context);
                    const auto upload = inventoryPrepared ? gHost.inventorySwapchain->Copy(context, inventoryTexture)
                                                          : InventorySwapchain::Upload::failed;
                    inventorySubmitted = upload == InventorySwapchain::Upload::ready;
                    if(inventorySubmitted && inventoryRightTexture && inventoryStereoPrepared) {
                        const auto rightUpload=gHost.inventoryRightSwapchain->Copy(context,inventoryRightTexture);
                        inventoryStereoSubmitted=rightUpload==InventorySwapchain::Upload::ready;
                        if(!inventoryStereoSubmitted) {
                            SetInventoryStereoEnabled(0);
                            inventoryStereoPrepared=false;
                            if(rightUpload==InventorySwapchain::Upload::sessionFault)gStopRequested.store(true);
                            Log("result=refused detail=inventory_right_upload mono_fallback=1");
                        }
                    }
                    // Keep our context reference through both eye uploads.
                    if (context) context->Release();
                    rendered = inventorySubmitted;
                    if (!inventorySubmitted) {
                        RefuseInventoryCapture("swapchain_upload_failed");
                        inventoryPrepared = false;
                        Log("result=refused detail=inventory_swapchain_upload legacy_panel_fallback=1");
                        if (upload == InventorySwapchain::Upload::sessionFault) gStopRequested.store(true);
                    }
                }
            }
            const bool gotImage =
                matchingSize && !inventorySubmitted &&
                XR_SUCCEEDED(xrAcquireSwapchainImage(gHost.swapchain, &acquire, &imageIndex));
            const bool imageWaited=gotImage && XR_SUCCEEDED(xrWaitSwapchainImage(gHost.swapchain,&waitImage));
            if(gotImage && !imageWaited)gStopRequested.store(true);
            if (timing && matchingSize) {
                gAcquireWait.AddNanoseconds(
                    preyvr::timing::MonotonicNanoseconds() - acquireStart);
            }
            if (imageWaited && imageIndex < gHost.images.size()) {
                ID3D11DeviceContext* context = nullptr;
                gHost.device->GetImmediateContext(&context);
                if (context != nullptr) {
                    if (gStereoSubmission.load(std::memory_order_acquire) && !panelActive) {
                        stereoPair = SubmitStereoPair(
                            context, backBuffer, imageIndex,
                            views, DeclaredFovFromLiveCamera(),
                            frameState.predictedDisplayTime, renderedEye, hudTexture!=nullptr);
                    } else {
                        // First light is a **flat mirror**: the same backbuffer
                        // into both eyes. It proves the whole path -- device,
                        // session, swapchain, submission -- without depending on
                        // the per-eye camera work, which is a separate experiment
                        // with its own failure modes.
                        for (std::uint32_t slice = 0; slice < 2; ++slice) {
                            context->CopySubresourceRegion(
                                gHost.images[imageIndex],
                                D3D11CalcSubresource(0, slice, 1),
                                0, 0, 0, panelSource, 0, nullptr);
                        }
                    }
                    rendered=panelActive || !gStereoSubmission.load() || stereoPair;
                    context->Release();
                }
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                if(XR_FAILED(xrReleaseSwapchainImage(gHost.swapchain,&release))) {rendered=false;gStopRequested.store(true);}

                // **What FOV to declare, and why it is not the runtime's.**
                //
                // These pixels were rendered by Prey, through Prey's frustum.
                // Declaring `views[eye].fov` -- what the headset would like -- is
                // a statement about the image that is not true, and the runtime
                // cannot detect it: it reprojects to whatever is claimed, so a
                // wrong claim shows up as wrong depth and wrong scale rather than
                // as an error. See docs/SUBMISSION_CONTRACT.md.
                //
                // So when stereo submission is armed we declare the frustum the
                // game actually drew with, read live. Both eyes get the same one,
                // because Prey renders symmetric and rung 3 separates the eyes by
                // translation alone.
                //
                // Fails closed: if the camera cannot be read we submit no layer
                // at all rather than fall back to the runtime's FOV. A black
                // headset is honest and diagnosable; a plausible-looking wrong
                // image is neither.
                // **Each eye is submitted with the contract that rendered it.**
                // FAIL-STR-044: the two held images come from different frames, so
                // reading a single live pose and FOV here would relabel the older
                // eye's pixels with the newer eye's contract. In stereo the held
                // values are used; the flat mirror has no held pair and uses the
                // live ones, which is correct because its pixels are this frame's.
                bool haveDeclared = false;
                XrFovf liveDeclared{};
                if (!stereoPair) {
                    const auto preyFov = DeclaredFovFromLiveCamera();
                    if (preyFov) {
                        liveDeclared = ScaleDeclaredFov(*preyFov);
                        haveDeclared = true;
                    }
                }
                for (int eye = 0; eye < 2; ++eye) {
                    projViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                    projViews[eye].pose = stereoPair ? gHost.eyePose[eye] : views[eye].pose;
                    projViews[eye].fov = stereoPair
                        ? gHost.eyeFov[eye]
                        : (haveDeclared ? liveDeclared : views[eye].fov);
                    projViews[eye].subImage.swapchain = gHost.swapchain;
                    projViews[eye].subImage.imageArrayIndex = static_cast<std::uint32_t>(eye);
                    projViews[eye].subImage.imageRect.offset = {0, 0};
                    gHost.submittedWidth = gHost.width;
                    gHost.submittedHeight = gHost.height;
                    projViews[eye].subImage.imageRect.extent = {
                        static_cast<std::int32_t>(gHost.width),
                        static_cast<std::int32_t>(gHost.height)};
                }
            } else if(imageWaited) {
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(gHost.swapchain,&release);gStopRequested.store(true);
            }
            backBuffer->Release();
        }
    }

    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = gHost.space;
    layer.viewCount = 2;
    layer.views = projViews;
    XrCompositionLayerQuad panelLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad rightPanelLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    if (panelActive) {
        const auto& p=*gHost.menuPanel;
        panelLayer.space=gHost.space;
        panelLayer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        panelLayer.pose={{p.pose.orientation.x,p.pose.orientation.y,p.pose.orientation.z,p.pose.orientation.w},
                         {p.pose.position.x,p.pose.position.y,p.pose.position.z}};
        panelLayer.size={p.width,p.height};
        panelLayer.subImage.swapchain=inventorySubmitted?gHost.inventorySwapchain->Handle():gHost.swapchain;
        if(inventorySubmitted)panelLayer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        panelLayer.subImage.imageArrayIndex=0;
        panelLayer.subImage.imageRect={{0,0},{static_cast<int>(gHost.width),static_cast<int>(gHost.height)}};
        if(inventoryStereoSubmitted) {
            rightPanelLayer=panelLayer;
            panelLayer.eyeVisibility=XR_EYE_VISIBILITY_LEFT;
            rightPanelLayer.eyeVisibility=XR_EYE_VISIBILITY_RIGHT;
            rightPanelLayer.subImage.swapchain=gHost.inventoryRightSwapchain->Handle();
        }
    }
    XrCompositionLayerQuad guideLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerCylinderKHR cylinderLayer{XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
    ui::Surface surface{};
    if(panelActive) {
        // The prototype's disparity and pointer mapping are defined on a plane.
        // Do not wrap stereo imagery onto a cylinder with a different geometry.
        surface={*gHost.menuPanel,gHost.panelAngle};
        if(surface.angle>0) {
            const auto axis=ui::CylinderAxis(surface);
            cylinderLayer.space=panelLayer.space;cylinderLayer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
            cylinderLayer.subImage=panelLayer.subImage;
            cylinderLayer.layerFlags=panelLayer.layerFlags;
            cylinderLayer.pose={{axis.orientation.x,axis.orientation.y,axis.orientation.z,axis.orientation.w},
                {axis.position.x,axis.position.y,axis.position.z}};
            cylinderLayer.radius=surface.panel.width/surface.angle;
            cylinderLayer.centralAngle=surface.angle;
            cylinderLayer.aspectRatio=surface.panel.width/surface.panel.height;
        }
        if(rendered)EnsurePointerTexture();
    }
    const auto pointer=PublishUiPointer(panelActive&&rendered&&gHost.pointerReady?&surface:nullptr,
        gHost.width,gHost.height,gHost.surfaceSerial);
    XrCompositionLayerQuad cursorLayer{XR_TYPE_COMPOSITION_LAYER_QUAD},beamLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const bool cursorActive=pointer.active&&pointer.hit&&pointer.hit->inside;
    bool beamActive=false;
    auto pointerLayer=[&](XrCompositionLayerQuad& target,const Pose& pose,float width,float height) {
        target.space=gHost.space;target.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        target.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        target.pose={{pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w},
            {pose.position.x,pose.position.y,pose.position.z}};
        target.size={width,height};target.subImage.swapchain=gHost.pointerSwapchain;
        target.subImage.imageRect={{0,0},{32,32}};
    };
    if(cursorActive) {
        const auto& hit=*pointer.hit;
        const auto point=Compose(ui::SurfacePoint(surface,hit.u,hit.v),Pose{{},{0,0,.002f}});
        const float size=pointer.pressed?.020f:.013f;
        pointerLayer(cursorLayer,point,size,size);
    }
    if(pointer.active) {
        const float length=cursorActive?pointer.hit->distance:.65f;
        const auto y=Rotate(pointer.aim.orientation,{0,0,-1});
        const Vec3 centre{pointer.aim.position.x+y.x*length*.5f,pointer.aim.position.y+y.y*length*.5f,
            pointer.aim.position.z+y.z*length*.5f};
        const Vec3 toHead{inputHead.position.x-centre.x,inputHead.position.y-centre.y,inputHead.position.z-centre.z};
        Vec3 x{y.y*toHead.z-y.z*toHead.y,y.z*toHead.x-y.x*toHead.z,y.x*toHead.y-y.y*toHead.x};
        const float norm=std::sqrt(x.x*x.x+x.y*x.y+x.z*x.z);
        if(norm>.0001f) {
            x={x.x/norm,x.y/norm,x.z/norm};
            const Vec3 z{x.y*y.z-x.z*y.y,x.z*y.x-x.x*y.z,x.x*y.y-x.y*y.x};
            pointerLayer(beamLayer,Pose{stereo::QuaternionFromBasis(x,y,z),centre},.002f,length);
            beamLayer.subImage.imageRect={{15,15},{1,1}};
            beamActive=true;
        }
    }
    const bool guideActive=panelActive && gHost.guideReady && gHost.guidePanel.has_value();
    if(guideActive) {
        const auto& p=*gHost.guidePanel;
        guideLayer.space=gHost.space;guideLayer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
        guideLayer.layerFlags=0; // Opaque instruction card.
        guideLayer.pose={{p.pose.orientation.x,p.pose.orientation.y,p.pose.orientation.z,p.pose.orientation.w},
                         {p.pose.position.x,p.pose.position.y,p.pose.position.z}};
        guideLayer.size={p.width,p.height};
        guideLayer.subImage.swapchain=gHost.guideSwapchain;
        guideLayer.subImage.imageRect={{0,0},{kGuideWidth,kGuideHeight}};
    }
    XrCompositionLayerQuad hudLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    bool hudActive=false;
    // A held eye may still contain the native reticle from before capture was
    // enabled/refused. Never add another HUD over that eye's baked-in copy.
    const bool hudRemovedFromWorld=gHost.hudCapturePair.CanOverlay(gStereoSubmission.load(),hudTexture!=nullptr);
    if(rendered && haveViews && !panelActive && HudLayerEnabled() && hudRemovedFromWorld) {
        auto captured=hudTexture;
        if(captured) {
            D3D11_TEXTURE2D_DESC desc{};captured->GetDesc(&desc);
            if(gHost.hudSwapchain && (desc.Width!=gHost.hudDesc.Width || desc.Height!=gHost.hudDesc.Height || desc.Format!=gHost.hudDesc.Format)) {
                xrDestroySwapchain(gHost.hudSwapchain);gHost.hudSwapchain=XR_NULL_HANDLE;gHost.hudImages.clear();
            }
            if(!gHost.hudSwapchain) {
                XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
                create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
                create.format=desc.Format;create.sampleCount=1;create.width=desc.Width;create.height=desc.Height;
                create.faceCount=1;create.arraySize=1;create.mipCount=1;
                if(XR_SUCCEEDED(xrCreateSwapchain(gHost.session,&create,&gHost.hudSwapchain))) {
                    gHost.hudDesc=desc;
                    std::uint32_t count=0;
                    xrEnumerateSwapchainImages(gHost.hudSwapchain,0,&count,nullptr);
                    gHost.hudImages.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
                    if(XR_FAILED(xrEnumerateSwapchainImages(gHost.hudSwapchain,count,&count,
                        reinterpret_cast<XrSwapchainImageBaseHeader*>(gHost.hudImages.data())))) {
                            gHost.hudImages.clear();RefuseHudLayer("swapchain_images");
                        }
                } else {
                    RefuseHudLayer("swapchain_format_or_allocation");
                }
            }
            if(!gHost.hudImages.empty()) {
                XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
                std::uint32_t index=0;
                const bool acquired=XR_SUCCEEDED(xrAcquireSwapchainImage(gHost.hudSwapchain,&acquire,&index));
                const bool waited=acquired && XR_SUCCEEDED(xrWaitSwapchainImage(gHost.hudSwapchain,&wait));
                if(acquired && !waited) {gStopRequested.store(true);RefuseHudLayer("image_wait");}
                if(waited) {
                    if(index<gHost.hudImages.size()) {
                        ID3D11DeviceContext* context=nullptr;gHost.device->GetImmediateContext(&context);
                        if(context) {context->CopyResource(gHost.hudImages[index].texture,captured);context->Release();hudActive=true;}
                    }
                    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    if(XR_FAILED(xrReleaseSwapchainImage(gHost.hudSwapchain,&release))) {hudActive=false;RefuseHudLayer("image_release");}
                }
            }
            std::array<ui::Eye,2> optical{};
            for(int eye=0;eye<2;++eye) {
                const auto& p=views[eye].pose;
                optical[eye]={Pose{{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},
                    {p.position.x,p.position.y,p.position.z}},views[eye].fov.angleLeft,views[eye].fov.angleRight,
                    views[eye].fov.angleUp,views[eye].fov.angleDown};
            }
            auto panel=ui::FitPanel(inputHead,optical,
                static_cast<float>(desc.Width)/desc.Height,2.0f,
                static_cast<float>(gUiScalePercent.load(std::memory_order_relaxed))*.01f,
                UiFitMargin());
            hudActive=hudActive && panel.has_value();
            if(hudActive) {
                const auto& p=*panel;
                hudLayer.space=gHost.space;hudLayer.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
                hudLayer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                hudLayer.pose={{p.pose.orientation.x,p.pose.orientation.y,p.pose.orientation.z,p.pose.orientation.w},
                               {p.pose.position.x,p.pose.position.y,p.pose.position.z}};
                hudLayer.size={p.width,p.height};hudLayer.subImage.swapchain=gHost.hudSwapchain;
                hudLayer.subImage.imageRect={{0,0},{static_cast<int>(desc.Width),static_cast<int>(desc.Height)}};
                SetHudLayerPresentation(true,p.width,p.height,2);
            }
        }
    }
    if(!hudActive)SetHudLayerPresentation(false);
    std::vector<const XrCompositionLayerBaseHeader*> layers;
    if(panelActive)layers.push_back(surface.angle>0?reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cylinderLayer):
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&panelLayer));
    else layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer));
    if(inventoryStereoSubmitted)layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&rightPanelLayer));
    if(guideActive)layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&guideLayer));
    else if(hudActive)layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&hudLayer));
    if(beamActive)layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&beamLayer));
    if(cursorActive)layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cursorLayer));

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = rendered ? static_cast<std::uint32_t>(layers.size()) : 0;
    endInfo.layers = rendered ? layers.data() : nullptr;
    const std::uint64_t endStart = timing ? preyvr::timing::MonotonicNanoseconds() : 0;
    const auto endResult=xrEndFrame(gHost.session, &endInfo);
    // Arm only after a complete successful frame. Until then the native movie
    // keeps rendering normally and the existing full-menu panel remains usable.
    if (rendered && endResult == XR_SUCCESS && inventoryPrepared && !gStopRequested.load() && InventoryCaptureEnabled()) {
        const auto dx=views[1].pose.position.x-views[0].pose.position.x;
        const auto dy=views[1].pose.position.y-views[0].pose.position.y;
        const auto dz=views[1].pose.position.z-views[0].pose.position.z;
        SetInventoryConsumerReady(&inventoryDescription,gHost.menuPanel?gHost.menuPanel->width:0,
            std::sqrt(dx*dx+dy*dy+dz*dz),inventoryStereoPrepared);
    }
    if (timing) {
        const std::uint64_t now = preyvr::timing::MonotonicNanoseconds();
        gEndFrame.AddNanoseconds(now - endStart);
        gServiceTotal.AddNanoseconds(now - serviceStart);
    }
    gHost.contract.OnSubmitted(thread);
    PublishResolution();

    if (rendered && XR_SUCCEEDED(endResult)) {
        if (inventorySubmitted) {
            if (gInventoryLayerFrames.fetch_add(1)==0)
                Log("result=0 detail=first_inventory_layer_submitted separate_swapchain=1 array_size=1 world_copy=0");
            gHost.submittedWidth=gHost.width;gHost.submittedHeight=gHost.height;
        }
        if (panelActive) { gUiPanelFrames.fetch_add(1); }
        const unsigned long long count = gSubmitted.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count == 1) {
            Log("result=0 detail=first_frame_submitted");
        }
    }
}

void ObserveXrBackbuffer(void* renderer)
{
    if(!renderer)return;
    const auto swap=*reinterpret_cast<IDXGISwapChain**>(
        reinterpret_cast<std::uintptr_t>(renderer)+engine::RendererLayout::swapchain);
    ID3D11Texture2D* texture=nullptr;
    if(!swap || FAILED(swap->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture))))return;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);texture->Release();
    static BackbufferSample sample{}; // frame observer / render thread owns it
    const auto now=MonotonicNanoseconds();
    if(sample.width!=desc.Width || sample.height!=desc.Height) {
        sample={desc.Width,desc.Height,now,now,0};
    }
    sample.stamp=now;++sample.frames;gBackbufferSample.Publish(sample);
}
bool XrBackbufferReady(unsigned int width,unsigned int height)
{
    BackbufferSample s{};
    const auto now=MonotonicNanoseconds();
    return gBackbufferSample.TryRead(s) && FreshSample(now,s.stamp) && s.width>0 && s.height>0 &&
        s.frames>=20 && now>=s.changed && now-s.changed>=1000000000ull &&
        (!width || width==s.width) && (!height || height==s.height);
}

DWORD SetUiPanelMode(unsigned int mode) {
    if(mode>2) return 1;
    gUiPanelMode.store(mode);if(mode==0)ClearUiPointer();return 0;
}
DWORD SetUiCurveDegrees(unsigned int degrees) {
    if(degrees>60)return ERROR_INVALID_PARAMETER;
    gUiCurveDegrees.store(degrees);return 0;
}
unsigned int UiPanelMode() { return gUiPanelMode.load(); }
unsigned long long UiPanelFrameCount() { return gUiPanelFrames.load(); }
unsigned long long InventoryLayerFrameCount() { return gInventoryLayerFrames.load(); }
float XrRuntimeIpdMetres() { return gRuntimeIpd.load(); }

DWORD SetMirrorFovPercent(unsigned int percent)
{
    // Clamped, not rejected. Below 100 shrinks the image further, which nobody
    // wants but is harmless; far above it declares a field wider than the
    // headset has and the compositor simply crops.
    const unsigned int clamped = percent < 50u ? 50u : (percent > 400u ? 400u : percent);
    gMirrorFovPercent.store(clamped, std::memory_order_release);
    Log("mirror_fov percent=" + std::to_string(clamped));
    return 0;
}

DWORD MirrorFovPercent() { return gMirrorFovPercent.load(std::memory_order_relaxed); }

DWORD SetXrStereoSubmission(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on) {
        // A backlog from a previous arm would decide the first frames' eyes.
        dll::ResetEyeHandoff();
        // The handoff only ever sees both eyes if the camera hook is free-running.
        // A lock left set from an offline capture would publish one eye forever,
        // and the pair would never complete.
        dll::SetStereoEyeLockFromRenderThread(-1);
    }
    gStereoSubmission.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "result=0 detail=stereo_submission enabled=" << (on ? "1" : "0");
    Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
}

bool XrRequestedEyeFov(int eye, float* left, float* right, float* up, float* down)
{
    if (eye < 0 || eye > 1 || left == nullptr || right == nullptr ||
        up == nullptr || down == nullptr) {
        return false;
    }
    std::array<XrFovf,2> optical{};
    if (!gOptics.TryRead(optical)) { return false; }
    const XrFovf& fov = optical[eye];
    if (!(fov.angleRight > fov.angleLeft) || !(fov.angleUp > fov.angleDown)) {
        return false;
    }
    *left = fov.angleLeft;
    *right = fov.angleRight;
    *up = fov.angleUp;
    *down = fov.angleDown;
    return true;
}

DWORD SetUiScalePercent(unsigned int percent)
{
    if (percent < 20 || percent > 200) {
        Log("result=refused detail=ui_scale_out_of_range");
        return 1;
    }
    gUiScalePercent.store(percent, std::memory_order_release);
    // The panels are rebuilt from the fit on the next frame that needs one, so
    // drop the cached ones rather than waiting for a reference change.
    gHost.menuPanel.reset();
    gHost.guidePanel.reset();
    std::ostringstream line;
    line << "result=0 detail=ui_scale percent=" << percent;
    Log(line.str());
    return 0;
}

DWORD UiScalePercent() { return gUiScalePercent.load(std::memory_order_acquire); }

// **Why this exists.** `ui.scale` stopped having any effect above about 130 at
// 2688x2880, which reads as a hard maximum but is not one: the fit loops were
// shrinking the panel back until it sat inside 72 percent of the frustum the
// runtime reports. This exposes that 72.
//
// Raising it is a real trade, not a free win. The reported frustum is already
// wider than a Quest 3's lenses actually show, so at 100 percent the corners
// are beyond what the wearer can see -- which is exactly why the inset was
// there. It is reversible, and a wearer is the only instrument that can settle
// where their own edge is.
DWORD SetUiFitMarginPercent(unsigned int percent)
{
    if (percent < 30 || percent > 100) {
        Log("result=refused detail=ui_margin_out_of_range");
        return 1;
    }
    gUiFitMarginPercent.store(percent, std::memory_order_release);
    gHost.menuPanel.reset();
    gHost.guidePanel.reset();
    Log("result=0 detail=ui_margin percent=" + std::to_string(percent));
    return 0;
}
DWORD UiFitMarginPercent() { return gUiFitMarginPercent.load(std::memory_order_acquire); }

// The onboarding card under the menu. Ours, not Prey's, and it is charged to
// the same vertical budget as the menu -- so switching it off is also the
// cheapest way to make the menu bigger.
DWORD SetUiGuideEnabled(unsigned int enabled)
{
    const bool on = enabled != 0;
    gUiGuide.store(on, std::memory_order_release);
    gHost.menuPanel.reset();
    gHost.guidePanel.reset();
    Log(std::string("result=0 detail=ui_guide enabled=") + (on ? "1" : "0"));
    return 0;
}
DWORD UiGuideEnabled() { return gUiGuide.load(std::memory_order_acquire) ? 1u : 0u; }

DWORD SetXrTimingEnabled(unsigned int enabled, unsigned int displayHz)
{
    if (enabled == 0) {
        gTimingEnabled.store(false, std::memory_order_release);
        return 0;
    }
    // Requested, not performed: the clear happens on the render thread at the
    // top of the next service. Arming therefore takes effect one frame later,
    // which is the price of an epoch boundary that cannot straddle a writer.
    gServiceInterval.RequestReset();
    gWaitFrame.RequestReset();
    gAcquireWait.RequestReset();
    gEndFrame.RequestReset();
    gServiceTotal.RequestReset();
    gEyeSkew.RequestReset();
    if (displayHz > 0 && displayHz <= 1000) {
        gServiceInterval.SetBudgetMicroseconds(1000000u / displayHz);
    }
    gTimingEnabled.store(true, std::memory_order_release);
    return 0;
}

std::string XrTimingReport()
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(2);
    out << " timing=" << (gTimingEnabled.load(std::memory_order_acquire) ? 1 : 0);

    const auto emit = [&out](const char* name, const preyvr::timing::DurationStats& d) {
        if (!d.valid) { out << ' ' << name << "=none"; return; }
        // **Window and lifetime, always both.** The percentiles describe at most
        // the last 512 samples -- a few seconds, not a run -- while `Total` is
        // every sample this generation. Emitting only the first is what let a
        // 512-sample window be described as a 30-second one.
        out << ' ' << name << "P50=" << d.p50 << ' ' << name << "P95=" << d.p95
            << ' ' << name << "P99=" << d.p99 << ' ' << name << "Max=" << d.max
            << ' ' << name << "N=" << d.count << ' ' << name << "Total=" << d.total;
    };

    const auto interval = gServiceInterval.Compute();
    emit("frame", interval.duration);
    // **"Over budget", never "missed".** No compositor is consulted: this counts
    // service-to-service intervals longer than a configured threshold. One long
    // interval counts once however many display periods it spans, and a sample a
    // microsecond over counts the same as a stall.
    out << " frameBudgetUs=" << interval.budgetMicroseconds
        << " frameRuntimePeriodUs=" << gRuntimePeriodUs.load(std::memory_order_relaxed)
        << " frameOverBudgetWindow=" << interval.windowOverBudget
        << " frameOverBudgetTotal=" << interval.lifetimeOverBudget
        << " frameWindowSec=" << interval.windowSeconds
        << " frameSessionSec=" << interval.sessionSeconds
        << " timingGeneration=" << interval.generation;

    emit("wait", gWaitFrame.Compute());
    emit("acquire", gAcquireWait.Compute());
    emit("end", gEndFrame.Compute());
    emit("service", gServiceTotal.Compute());

    // **Stages are separate rings and are NOT paired by frame.** Subtracting one
    // stage's median from another's does not give a per-frame remainder, and an
    // early return can produce an interval sample with no matching service
    // sample. Comparing these populations needs frame-linked records, which this
    // instrument does not produce.
    emit("eyeSkew", gEyeSkew.Compute());
    // **This is the temporal disparity between the two submitted eyes.** With
    // one eye rendered per frame it should sit near one frame interval; a p50
    // far above `frameP50` means an eye is starving rather than alternating,
    // and a `staleEye` that never changes means the asymmetry is fixed rather
    // than the ordinary cadence.
    out << " eyeSkewSamples=" << gEyeSkewSamples.load(std::memory_order_relaxed)
        << " staleEye=" << gStaleEye.load(std::memory_order_relaxed);
    out << " timingNote=unpaired_stage_rings";
    return out.str();
}

std::string XrCoverageReport()
{
    std::unique_lock lock(gMutex,std::try_to_lock);
    if(!lock.owns_lock())return "coverage=busy";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(5);
    for (int eye = 0; eye < 2; ++eye) {
        const char* const name = eye == 0 ? " left" : " right";
        if (!gHost.requestedFovValid[eye]) {
            out << name << "=unavailable";
            continue;
        }
        const preyvr::xr::FovAngles rendered{gHost.eyeFov[eye].angleLeft,
                                             gHost.eyeFov[eye].angleRight,
                                             gHost.eyeFov[eye].angleUp,
                                             gHost.eyeFov[eye].angleDown};
        const preyvr::xr::FovAngles requested{gHost.requestedFov[eye].angleLeft,
                                              gHost.requestedFov[eye].angleRight,
                                              gHost.requestedFov[eye].angleUp,
                                              gHost.requestedFov[eye].angleDown};
        const auto coverage = preyvr::xr::TangentCoverage(rendered, requested);
        if (!coverage.valid) {
            out << name << "=invalid";
            continue;
        }
        out << name << "Used=" << coverage.utilisationArea
            << name << "Covered=" << coverage.satisfactionArea
            << name << "Short=" << (coverage.requestExceedsRender ? 1 : 0);
        // The equal-density target size, so the figure lands as pixels rather
        // than as a ratio to be multiplied out by hand later. Margin for
        // reprojection is NOT included; this is the floor, not a launch preset.
        const DWORD width = XrResolutionChain(4);
        const DWORD height = XrResolutionChain(5);
        if (width > 0 && height > 0) {
            out << name << "EqualDensity="
                << static_cast<int>(static_cast<float>(width) *
                                    preyvr::xr::EqualDensityScale(coverage.utilisationWidth))
                << "x"
                << static_cast<int>(static_cast<float>(height) *
                                    preyvr::xr::EqualDensityScale(coverage.utilisationHeight));
        }
    }
    return out.str();
}

DWORD XrResolutionChain(unsigned int field)
{
    std::array<DWORD,15> state{};
    return field<state.size() && gResolution.TryRead(state)?state[field]:0;
}

unsigned long long DeclaredFovAgreeCount() { return gFovAgree.load(std::memory_order_relaxed); }
unsigned long long DeclaredFovDivergeCount() { return gFovDiverge.load(std::memory_order_relaxed); }
DWORD DeclaredFovWorstMilliTan() { return gWorstDivergenceMilliTan.load(std::memory_order_relaxed); }

DWORD SetXrSwapEyes(unsigned int enabled)
{
    const bool on = enabled != 0u;
    gSwapEyes.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "result=0 detail=swap_eyes enabled=" << (on ? "1" : "0");
    Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
}

DWORD SetXrPreferSrgbFormat(unsigned int enabled)
{
    // Only read when the swapchain is built, so setting it on a live session
    // does nothing and would be a confusing no-op. Refuse instead.
    if (gStatus.load(std::memory_order_acquire) ==
        static_cast<DWORD>(XrSessionStatus::running)) {
        Log("result=refused detail=srgb_preference_needs_restart");
        return static_cast<DWORD>(XrSessionStatus::failed);
    }
    const bool on = enabled != 0u;
    gPreferSrgb.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "result=0 detail=prefer_srgb enabled=" << (on ? "1" : "0");
    Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
}

} // namespace preyvr::dll
