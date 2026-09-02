#include "XrSessionHost.h"

#include "Logger.h"
#include "preyvr/EngineMap.h"
#include "preyvr/XrFrameContract.h"
#include "CameraEditHook.h"
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

#include <atomic>
#include <optional>
#include <span>
#include <chrono>
#include <mutex>
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
std::atomic<unsigned long long> gSubmitted{0};

struct Host {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;

    ID3D11Device* device = nullptr;   // Prey's, borrowed -- never released here
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool sessionBegun = false;
    bool started = false;

    // Stereo submission: one held image per eye.
    //
    // Prey renders one eye per frame, so the backbuffer is never both. These two
    // textures hold the most recent image of each eye so that every submitted
    // frame can carry a complete pair -- one of them a few frames older than the
    // other. That staleness is the price of alternate-eye and it is invisible in
    // a frozen scene, which is how the first depth test is run.
    ID3D11Texture2D* eyeImage[2] = {nullptr, nullptr};
    bool eyeImageValid[2] = {false, false};
    xrframe::FrameContract contract;
};

Host gHost;

// Stereo submission, off by default.
//
// With this clear the path is the original flat mirror: the same backbuffer into
// both eyes, declaring the runtime's own FOV. That is a mono image and a false
// declaration, but it is a *known* one that proved the plumbing, and it stays
// reachable so a regression here can be bisected against it.
std::atomic<bool> gStereoSubmission{false};

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

bool CreateInstanceAndSystem()
{
    const char* enabled[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    // Newest-first with a fallback: xr-sim accepts 1.1, VirtualDesktopXR does not
    // (F-010). One build has to work against both.
    const XrVersion candidates[] = {XR_CURRENT_API_VERSION, XR_API_VERSION_1_0};

    XrResult result = XR_ERROR_RUNTIME_FAILURE;
    for (const XrVersion candidate : candidates) {
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        info.enabledExtensionCount = 1;
        info.enabledExtensionNames = enabled;
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

    // Built at **Prey's backbuffer size**, not the runtime's recommendation, so
    // the first light can CopyResource straight across with no scaling blit. The
    // runtime scales for display. Matching the recommendation is a later
    // optimisation and a different problem.
    XrSwapchainCreateInfo swapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainInfo.usageFlags =
        XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    swapchainInfo.format = choice->format;
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
    return true;
}

void Teardown()
{
    for (auto*& image : gHost.eyeImage) {
        if (image != nullptr) {
            image->Release();
            image = nullptr;
        }
    }
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
// screen. Eye identity comes from *asking* -- the eye lock is held for several
// frames and only then is the image taken -- rather than from reading back which
// eye the engine thinks it drew, which cannot be trusted across the engine's
// game and render threads.
//
// Returns false until both eyes have been seen at least once. Submitting a pair
// with one empty half would show a black eye, which reads as a broken headset
// rather than as a warming-up mod.
bool SubmitStereoPair(
    ID3D11DeviceContext* context,
    ID3D11Texture2D* backBuffer,
    std::uint32_t imageIndex)
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
    const int eye = dll::ConsumeRenderedEye();
    if (eye == 0 || eye == 1) {
        context->CopyResource(gHost.eyeImage[eye], backBuffer);
        gHost.eyeImageValid[eye] = true;
    }

    if (!gHost.eyeImageValid[0] || !gHost.eyeImageValid[1]) {
        return false;
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
            }
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
    std::wstring wide(static_cast<std::size_t>(needed - 1), 0);
    MultiByteToWideChar(CP_UTF8, 0, manifestPath, -1, wide.data(), needed);

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
    gStopRequested.store(false, std::memory_order_release);
    gStatus.store(static_cast<DWORD>(XrSessionStatus::running), std::memory_order_release);
    Log("result=0 detail=started");
    return gStatus.load(std::memory_order_acquire);
}

DWORD StopXrSession()
{
    gStopRequested.store(true, std::memory_order_release);
    return gStatus.load(std::memory_order_acquire);
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
        // Torn down here rather than in StopXrSession, because the D3D resources
        // belong to this thread.
        Teardown();
        gStatus.store(static_cast<DWORD>(XrSessionStatus::stopped), std::memory_order_release);
        return;
    }

    PumpEvents();
    if (!gHost.sessionBegun) {
        return;
    }

    const std::uint32_t thread = GetCurrentThreadId();

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    if (XR_FAILED(xrWaitFrame(gHost.session, &waitInfo, &frameState))) {
        return;
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
        (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    gHost.contract.OnViewsLocated(frameState.predictedDisplayTime, haveViews, haveViews);

    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(xrBeginFrame(gHost.session, &beginInfo))) {
        return;
    }
    gHost.contract.OnBegun();

    XrCompositionLayerProjectionView projViews[2]{};
    bool rendered = false;
    bool stereoPair = false;

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

            if (XR_SUCCEEDED(xrAcquireSwapchainImage(gHost.swapchain, &acquire, &imageIndex)) &&
                XR_SUCCEEDED(xrWaitSwapchainImage(gHost.swapchain, &waitImage)) &&
                imageIndex < gHost.images.size()) {
                ID3D11DeviceContext* context = nullptr;
                gHost.device->GetImmediateContext(&context);
                if (context != nullptr) {
                    if (gStereoSubmission.load(std::memory_order_acquire)) {
                        stereoPair = SubmitStereoPair(context, backBuffer, imageIndex);
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
                                0, 0, 0, backBuffer, 0, nullptr);
                        }
                    }
                    context->Release();
                }
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(gHost.swapchain, &release);

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
                XrFovf declared{};
                bool haveDeclared = false;
                if (stereoPair) {
                    const auto preyFov = DeclaredFovFromLiveCamera();
                    if (preyFov) {
                        declared = *preyFov;
                        haveDeclared = true;
                    }
                }
                if (stereoPair && !haveDeclared) {
                    rendered = false;
                    LogOnce("result=refused detail=camera_unreadable_no_layer");
                } else {
                for (int eye = 0; eye < 2; ++eye) {
                    projViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                    projViews[eye].pose = views[eye].pose;
                    projViews[eye].fov = haveDeclared ? declared : views[eye].fov;
                    projViews[eye].subImage.swapchain = gHost.swapchain;
                    projViews[eye].subImage.imageArrayIndex = static_cast<std::uint32_t>(eye);
                    projViews[eye].subImage.imageRect.offset = {0, 0};
                    projViews[eye].subImage.imageRect.extent = {
                        static_cast<std::int32_t>(gHost.width),
                        static_cast<std::int32_t>(gHost.height)};
                }
                rendered = true;
                }
            }
            backBuffer->Release();
        }
    }

    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = gHost.space;
    layer.viewCount = 2;
    layer.views = projViews;
    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = rendered ? 1u : 0u;
    endInfo.layers = rendered ? layers : nullptr;
    xrEndFrame(gHost.session, &endInfo);
    gHost.contract.OnSubmitted(thread);

    if (rendered) {
        const unsigned long long count = gSubmitted.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count == 1) {
            Log("result=0 detail=first_frame_submitted");
        }
    }
}

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
