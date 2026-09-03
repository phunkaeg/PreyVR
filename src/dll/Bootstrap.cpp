#include "Bootstrap.h"

#include "CameraEditHook.h"
#include "XrSessionHost.h"
#include "ConsoleBridgeWin32.h"
#include "FrameCaptureWin32.h"

#include "FrameObserverHook.h"
#include "Logger.h"
#include "AimRayProbe.h"
#include "HeadTrackingHook.h"
#include "PassCameraProbe.h"
#include "HotkeyBridge.h"
#include "OpenXRPreflightWin32.h"
#include "RuntimeSnapshotWin32.h"
#include "Version.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoFrame.h"

#include <bcrypt.h>
#include <Psapi.h>

#include <array>
#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace preyvr::dll {
namespace {

std::atomic<DWORD> gSmokeStatus{0};
std::atomic<DWORD> gOpenXRPreflightStatus{
    static_cast<DWORD>(OpenXRBootstrapStatus::disabled)};
std::atomic<DWORD> gModulePinned{0};

std::filesystem::path ModulePath(HMODULE module)
{
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

std::string Narrow(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), needed, nullptr, nullptr);
    return result;
}

std::string Sha256File(const std::filesystem::path& path)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0;
    DWORD hashBytes = 0;
    DWORD copied = 0;
    std::vector<UCHAR> object;
    std::vector<UCHAR> digest;

    const auto cleanup = [&] {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    };

    if (BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectBytes),
            sizeof(objectBytes),
            &copied,
            0) < 0 ||
        BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashBytes),
            sizeof(hashBytes),
            &copied,
            0) < 0 ||
        hashBytes == 0) {
        cleanup();
        return "unavailable";
    }

    object.resize(objectBytes);
    digest.resize(hashBytes);
    if (BCryptCreateHash(
            algorithm,
            &hash,
            object.data(),
            static_cast<ULONG>(object.size()),
            nullptr,
            0,
            0) < 0) {
        cleanup();
        return "unavailable";
    }

    std::ifstream input(path, std::ios::binary);
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && BCryptHashData(
                hash,
                reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(count),
                0) < 0) {
            cleanup();
            return "unavailable";
        }
    }
    if (!input.eof() || BCryptFinishHash(
            hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        cleanup();
        return "unavailable";
    }

    std::ostringstream output;
    output << std::hex << std::uppercase << std::setfill('0');
    for (const UCHAR value : digest) {
        output << std::setw(2) << static_cast<unsigned int>(value);
    }
    cleanup();
    return output.str();
}

bool PinCurrentModule()
{
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&BootstrapMain),
            &pinned)) {
        return false;
    }
    gModulePinned.store(1, std::memory_order_release);
    return true;
}

DWORD RunBootstrap(HMODULE self)
{
    lifecycle::ResetLog();

    const auto selfPath = ModulePath(self);
    std::ostringstream identity;
    identity << "preyvr_smoke_start version=" << PREYVR_VERSION
             << " dll=\"" << Narrow(selfPath.wstring()) << "\""
             << " dllSha256=" << Sha256File(selfPath)
             << " host=\"" << Narrow(ModulePath(nullptr).wstring()) << "\""
             << " expectedLandmarks=" << engine::Landmarks().size()
             << " hooks=compiled_default_off openxr=preflight_only";
    lifecycle::Log(identity.str());

    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        lifecycle::Log("preyvr_smoke_result status=unsupported reason=PreyDll.dll_not_loaded");
        gSmokeStatus.store(1, std::memory_order_release);
        return 0;
    }

    MODULEINFO moduleInfo{};
    if (!GetModuleInformation(
            GetCurrentProcess(), preyDll, &moduleInfo, static_cast<DWORD>(sizeof(moduleInfo))) ||
        moduleInfo.lpBaseOfDll == nullptr || moduleInfo.SizeOfImage == 0) {
        lifecycle::Log("preyvr_smoke_result status=unsupported reason=module_info_unavailable");
        gSmokeStatus.store(1, std::memory_order_release);
        return 0;
    }

    const auto image = std::span<const std::uint8_t>(
        static_cast<const std::uint8_t*>(moduleInfo.lpBaseOfDll),
        static_cast<std::size_t>(moduleInfo.SizeOfImage));
    const auto results = engine::ValidateLandmarks(image);
    for (const auto& result : results) {
        std::ostringstream line;
        line << "preyvr_landmark id=" << result.landmark->id
             << " rva=0x" << std::hex << std::uppercase << result.landmark->rva
             << " status=" << engine::ToString(result.status);
        if (result.status == engine::LandmarkStatus::mismatch) {
            line << " mismatchOffset=0x" << result.mismatchOffset;
        }
        lifecycle::Log(line.str());
    }

    if (!engine::AllLandmarksMatch(results)) {
        lifecycle::Log("preyvr_smoke_result status=unsupported reason=engine_landmark_mismatch hooks=disabled");
        gSmokeStatus.store(1, std::memory_order_release);
        return 0;
    }

    if (!ConfigureFrameObserver(preyDll, image)) {
        lifecycle::Log("preyvr_smoke_result status=unsupported reason=frame_observer_plan_failed hooks=disabled");
        gSmokeStatus.store(1, std::memory_order_release);
        return 0;
    }
    // Read-only capture of the engine facts that only a running Prey can supply.
    // It runs here, after the landmark gate has proved the image is the supported
    // build and before any hook is armed, so the values recorded are the engine's
    // own undisturbed state. Failure is never fatal: a partial snapshot still
    // localises the first bad pointer, and the mod proceeds exactly as before.
    // See docs/LIVE_CAPTURE_PLAN.md for what each field is for.
    {
        const auto captured = CaptureRuntimeSnapshot(reinterpret_cast<std::uintptr_t>(preyDll));
        snapshot::Report(
            captured,
            [](std::string_view line, void*) { lifecycle::Log(line); },
            nullptr);
    }

    const auto openxr = CollectOpenXRPreflight(selfPath);
    gOpenXRPreflightStatus.store(
        static_cast<DWORD>(openxr.status),
        std::memory_order_release);
    std::ostringstream openxrLine;
    openxrLine << "preyvr_openxr_preflight status=" << ToString(openxr.status)
               << " runtimeSource=" << openxr.runtimeSource
               << " runtimeManifest=\"" << Narrow(openxr.runtimeManifest.wstring()) << "\""
               << " loader=\"" << Narrow(openxr.loader.wstring()) << "\""
               << " action=none";
    lifecycle::Log(openxrLine.str());

    if (!PinCurrentModule()) {
        lifecycle::Log("preyvr_smoke_result status=unsupported reason=module_pin_failed hooks=disabled");
        gSmokeStatus.store(1, std::memory_order_release);
        return 0;
    }
    lifecycle::Log("preyvr_lifecycle module=pinned unload=process_exit_only");

    std::ostringstream success;
    success << "preyvr_smoke_result status=verified landmarks=" << results.size()
            << " hooks=compiled_default_off observer=ready"
            << " module=pinned"
            << " openxr=preflight_only openxrStatus=" << ToString(openxr.status)
            << " next=supported_host_smoke";
    lifecycle::Log(success.str());
    gSmokeStatus.store(2, std::memory_order_release);
    return 0;
}

} // namespace

DWORD WINAPI BootstrapMain(void* moduleParameter)
{
    const auto self = static_cast<HMODULE>(moduleParameter);
    HMODULE workerReference = nullptr;
    const bool workerPinned = GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&BootstrapMain),
        &workerReference) != FALSE;

    DWORD result = 1;
    try {
        result = RunBootstrap(self);
    }
    catch (const std::exception& error) {
        lifecycle::Log(std::string("preyvr_smoke_result status=unsupported reason=exception detail=\"") +
            error.what() + "\"");
        gSmokeStatus.store(1, std::memory_order_release);
    }
    catch (...) {
        lifecycle::Log("preyvr_smoke_result status=unsupported reason=unknown_exception");
        gSmokeStatus.store(1, std::memory_order_release);
    }

    if (workerPinned) {
        FreeLibraryAndExitThread(workerReference, result);
    }
    return result;
}

DWORD SmokeStatus()
{
    return gSmokeStatus.load(std::memory_order_acquire);
}

DWORD OpenXRPreflightStatus()
{
    return gOpenXRPreflightStatus.load(std::memory_order_acquire);
}

DWORD ModulePinStatus()
{
    return gModulePinned.load(std::memory_order_acquire);
}

// On-demand Capture B. Refuses unless the gate has already verified this process
// (smoke status 2), so it can never read a process we have not identified.
// Read-only: no COM call, no write, and a failure is reported rather than thrown.
DWORD CaptureRenderViewsToLog()
{
    if (gSmokeStatus.load(std::memory_order_acquire) != 2) {
        lifecycle::Log("preyvr_renderview refused reason=host_not_verified");
        return 1;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        lifecycle::Log("preyvr_renderview refused reason=PreyDll.dll_not_loaded");
        return 1;
    }
    const auto capture = CaptureRenderViewsNow(reinterpret_cast<std::uintptr_t>(preyDll));
    snapshot::Report(
        capture,
        [](std::string_view line, void*) { lifecycle::Log(line); },
        nullptr);
    return capture.complete ? 0 : 2;
}

} // namespace preyvr::dll

extern "C" __declspec(dllexport) DWORD PreyVR_GetSmokeStatus()
{
    return preyvr::dll::SmokeStatus();
}

extern "C" __declspec(dllexport) DWORD PreyVR_SetFrameObserverEnabled(DWORD enabled)
{
    return preyvr::dll::SetFrameObserverEnabled(enabled != 0);
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetFrameObserverStatus()
{
    return preyvr::dll::FrameObserverStatus();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetObservedFrameCount()
{
    return preyvr::dll::ObservedFrameCount();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetOpenXRPreflightStatus()
{
    return preyvr::dll::OpenXRPreflightStatus();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetModulePinStatus()
{
    return preyvr::dll::ModulePinStatus();
}

// Capture B, on demand. Returns 0 complete, 2 partial, 1 refused. Results go to
// the smoke log as `preyvr_renderview` lines. Never runs on load.
extern "C" __declspec(dllexport) DWORD PreyVR_CaptureRenderViews()
{
    return preyvr::dll::CaptureRenderViewsToLog();
}

// --- Test harness -----------------------------------------------------------
//
// These make a rendered result and a deterministic scene reachable from an
// automation script, which is what turns "does the view move?" from something a
// human squints at into something with a number attached. Both are inert until
// the frame observer is enabled, because both are serviced from inside it.

// Arms a one-shot backbuffer dump of the next observed frame. `tag` is written
// into the dump header so an A/B pair is distinguishable without trusting
// filenames. Returns 0 ok, 1 refused (one already pending), 2 unavailable,
// 3 failed.
extern "C" __declspec(dllexport) DWORD PreyVR_RequestFrameCapture(DWORD tag)
{
    return preyvr::dll::RequestFrameCapture(static_cast<std::uint32_t>(tag));
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetLastFrameCaptureResult()
{
    return preyvr::dll::LastFrameCaptureResult();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetCompletedFrameCaptureCount()
{
    return preyvr::dll::CompletedFrameCaptureCount();
}

// Queues one allowlisted console command. Anything not on the list is refused
// here rather than passed to the engine. Returns 0 ok, 1 denied, 2 unavailable,
// 3 signature mismatch, 4 busy, 5 too long.
extern "C" __declspec(dllexport) DWORD PreyVR_QueueConsoleCommand(const char* command)
{
    return preyvr::dll::QueueConsoleCommand(command);
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetLastConsoleResult()
{
    return preyvr::dll::LastConsoleBridgeResult();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetSubmittedConsoleCommandCount()
{
    return preyvr::dll::SubmittedConsoleCommandCount();
}

// The first write to the render path. Wraps CSystem::Render so a bounded yaw is
// applied to m_ViewCamera for the duration of one render and then restored, with
// the restore verified byte for byte. Pass 0 to disarm; the hook stays installed
// as a pass-through because F-009 recorded that our unhook path has never been
// observed restoring a prologue on a live host.
//
// Returns 0 unavailable, 1 ready, 2 armed, 3 failed.
extern "C" __declspec(dllexport) DWORD PreyVR_SetCameraYawEdit(float degrees)
{
    return preyvr::dll::SetCameraYawEdit(degrees);
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetCameraEditStatus()
{
    return preyvr::dll::CameraEditStatusValue();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetCameraEditAppliedCount()
{
    return preyvr::dll::CameraEditAppliedCount();
}

// Non-zero here means an edit was applied but the camera did not come back
// byte-identical. The hook disarms itself on the first such frame.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetCameraEditRestoreFailureCount()
{
    return preyvr::dll::CameraEditRestoreFailureCount();
}

// Alternating-eye stereo with a synthetic IPD and FOV, no double-render and no
// headset. With the scene frozen via t_Scale 0, consecutive frames differ only
// by the eye, so they form a genuine stereo pair that New-StereoView can judge.
// Captures are stamped with the eye index automatically. Pass ipd 0 to disarm.
extern "C" __declspec(dllexport) DWORD PreyVR_SetSyntheticStereo(float ipdMetres, float halfFovDegrees)
{
    return preyvr::dll::SetSyntheticStereo(ipdMetres, halfFovDegrees);
}

// Scales the synthetic per-eye frustum asymmetry. 1.0 makes both eyes symmetric
// so the only difference is the eye offset; 1.1 is the default and exercises the
// asymmetry path. Measuring both at once is what made the first stereo pair
// unjudgeable.
extern "C" __declspec(dllexport) DWORD PreyVR_SetStereoAsymmetry(float outerScale)
{
    return preyvr::dll::SetStereoAsymmetry(outerScale);
}

// Locks synthetic stereo to one eye: 0 left, 1 right, anything else alternates.
// Replaces per-frame eye tagging, which cannot work across the engine's game and
// render threads - they are offset by its MT/RT double buffer, so a tag written
// by one is not reliably read by the other for the same frame. Hold an eye, let a
// few frames pass, capture; then hold the other.
extern "C" __declspec(dllexport) DWORD PreyVR_SetStereoEyeLock(unsigned int eye)
{
    return preyvr::dll::SetStereoEyeLock(eye);
}

// Rung 3: build each eye by translation alone and keep Prey's own projection,
// so the frustum we declare to OpenXR is the frustum the game actually drew.
extern "C" __declspec(dllexport) DWORD PreyVR_SetNativeProjection(unsigned int enabled)
{
    return preyvr::dll::SetNativeProjection(enabled);
}

// Pointer-taking variant, per F-009 - Frida's NativeFunction aborts calls into
// this DLL, so anything that must be driven from a probe needs a thread-callable
// shape.
extern "C" __declspec(dllexport) DWORD PreyVR_SetNativeProjectionPtr(void* enabled)
{
    return preyvr::dll::SetNativeProjection(
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(enabled)));
}

// Which eye the most recent render used, or -1.
extern "C" __declspec(dllexport) int PreyVR_GetLastRenderedEye()
{
    return preyvr::dll::LastRenderedEye();
}

// The double-render experiment: calls CSystem::Render twice in one frame, once
// per eye. This is the one remaining architectural unknown for native stereo and
// the only genuinely risky mode here - run the alternating-eye stereo first, so
// that a crash cannot be ambiguous between "cannot render twice" and "the second
// camera was malformed". frameBudget is a hard ceiling, capped at 600; there is
// no unbounded option. Pass ipd 0 or budget 0 to disarm.
extern "C" __declspec(dllexport) DWORD PreyVR_SetDoubleRenderStereo(
    float ipdMetres, float halfFovDegrees, unsigned int frameBudget)
{
    return preyvr::dll::SetDoubleRenderStereo(ipdMetres, halfFovDegrees, frameBudget);
}

// A4: one extra RenderWorld per frame for the second eye, with its own pass info
// and the recursive render view. The successor to the double render (A3), which
// wedged the engine by re-entering CSystem::Render and forcing both eyes through
// one pass info and one view (F-013).
//
// The game's own frame renders first and untouched, so this path never writes the
// game's camera. Bounded by a frame budget and by the wall-clock watchdog.
// args[0] = IPD metres, args[1] = half-FOV degrees, args[2] = frame budget.
struct PreyVRSecondPassArgs {
    float ipdMetres;
    float halfFovDegrees;
    unsigned int frameBudget;
    // Non-zero runs the zero-camera-delta control: identical pass, identical
    // view, zero eye offset. Whatever changes is a side effect, not stereo.
    unsigned int zeroCameraDelta;
    // Non-zero sets the R-073 secondary-pass flag. Off is the F-014 A/B: it
    // separates "a second RenderWorld is fatal" from "marking it secondary is".
    unsigned int markSecondary;
};

extern "C" __declspec(dllexport) DWORD PreyVR_SetSecondPassStereoPtr(
    const PreyVRSecondPassArgs* args)
{
    if (args == nullptr) {
        return static_cast<DWORD>(preyvr::dll::CameraEditStatus::failed);
    }
    return preyvr::dll::SetSecondPassStereo(
        args->ipdMetres, args->halfFovDegrees, args->frameBudget,
        args->zeroCameraDelta != 0, args->markSecondary != 0);
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetSecondPassFrameCount()
{
    return preyvr::dll::SecondPassFrameCount();
}

// 0 idle, 1 armed, 2 ran at least once, 3 refused because something could not be
// resolved.
// Non-zero means a second pass advanced renderer frame bookkeeping: a positive
// side-effect detection, and a failure of BN-SFX-001's gate.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetSecondPassFrameIdMovedCount()
{
    return preyvr::dll::SecondPassFrameIdMovedCount();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetSecondPassStatus()
{
    return preyvr::dll::SecondPassStatusValue();
}

// A6: the interpose shape. Hooks RenderWorld and calls the original twice from
// inside it, so the frame still contains one CSystem::Render, one HUD draw and
// one present -- which is what A4 got wrong and F-014 charged us for.
//
// The first test is zero-delta: both calls take the game's own unmodified pass
// info, so the only variable is whether the call can be repeated at this point.
// args[0] = frame budget, args[1] = mode (0 share all, 1 own recursive view,
// 2 own view plus secondary flag). Pointer-taking so it runs on a real thread.
struct PreyVRInterposeArgs {
    unsigned int frameBudget;
    unsigned int mode;
};

extern "C" __declspec(dllexport) DWORD PreyVR_SetInterposeStereoPtr(
    const PreyVRInterposeArgs* args)
{
    if (args == nullptr) {
        return static_cast<DWORD>(preyvr::dll::CameraEditStatus::failed);
    }
    return preyvr::dll::SetInterposeStereo(args->frameBudget, args->mode);
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetInterposeFrameCount()
{
    return preyvr::dll::InterposeFrameCount();
}

// A7: the Crysis VR shape -- two full CSystem::Render calls with
// CSystem::RenderBegin between them. That middle call is the one thing A3 and A6
// were both missing, and the Crysis VR source names its absence as "messed up
// object culling", which is exactly the symptom A4 crashed inside.
extern "C" __declspec(dllexport) DWORD PreyVR_SetFrameShapeStereo(unsigned int frameBudget)
{
    return preyvr::dll::SetFrameShapeStereo(frameBudget);
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetFrameShapeFrameCount()
{
    return preyvr::dll::FrameShapeFrameCount();
}

// Reads Prey's live view camera and returns the frustum PreyVR must DECLARE to
// OpenXR, through the shipped code path rather than a re-derivation.
//
// **Why this exists as an export at all.** The live values were first checked by
// reading the camera from a Frida script and doing the arithmetic there. That
// confirms the fields are readable, but it tests a re-implementation rather than
// `TangentsFromCamera` itself -- and a re-derivation that agrees with itself is
// exactly the self-referential check this project has already been caught by
// once, in the asymmetry test. This calls the real function so the numbers that
// come back are the ones the mod would actually submit.
//
// Pure read: nothing is armed, nothing is written, no hook is required.
//
// Pointer-taking so it matches LPTHREAD_START_ROUTINE and can be driven on a real
// thread, per F-009 -- Frida's NativeFunction aborts calls into this DLL.
struct PreyVRDeclaredFov {
    unsigned int valid;          // 0 if the camera could not be read or was refused
    float tanLeft, tanRight, tanDown, tanUp;
    float angleLeft, angleRight, angleDown, angleUp;   // radians
    // The source fields, echoed so the derivation can be audited rather than
    // trusted -- and so a convention error shows up as a wrong input, not just a
    // wrong answer.
    float fov, projectionRatio, nearPlane;
};

extern "C" __declspec(dllexport) DWORD PreyVR_ReadDeclaredFovPtr(PreyVRDeclaredFov* out)
{
    if (out == nullptr) {
        return 0;
    }
    *out = PreyVRDeclaredFov{};

    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return 0;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    auto* const systemPtr = *reinterpret_cast<std::uint8_t**>(
        base + preyvr::engine::SystemLayout::pointerRva);
    if (systemPtr == nullptr) {
        return 0;
    }
    const auto* const camera = reinterpret_cast<const std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(systemPtr) + preyvr::engine::SystemLayout::viewCamera);
    const auto span = std::span<const std::uint8_t>(camera, preyvr::engine::CameraLayout::size);

    const auto readFloat = [camera](std::size_t offset) {
        float value = 0.0f;
        std::memcpy(&value, camera + offset, sizeof(value));
        return value;
    };
    out->fov = readFloat(preyvr::engine::CameraLayout::fov);
    out->projectionRatio = readFloat(preyvr::engine::CameraLayout::projectionRatio);
    out->nearPlane = readFloat(preyvr::engine::CameraLayout::edgeNearLeftTop + sizeof(float));

    const auto view = preyvr::stereoframe::TangentsFromCamera(span);
    if (!view) {
        return 0;
    }
    const auto angles = preyvr::stereoframe::AnglesFromTangents(*view);
    out->tanLeft = view->tanLeft;
    out->tanRight = view->tanRight;
    out->tanDown = view->tanDown;
    out->tanUp = view->tanUp;
    out->angleLeft = angles.angleLeft;
    out->angleRight = angles.angleRight;
    out->angleDown = angles.angleDown;
    out->angleUp = angles.angleUp;
    out->valid = 1;
    return 1;
}

// The sub-step the stereo path was last in.
//
// **Exported because F-014 proved a breadcrumb readable on only one failure path
// is not a breadcrumb.** It was previously printed by the watchdog on deadline
// expiry alone, so a mode that disarmed cleanly and then crashed the render
// thread a second later left no trace of where it had been. Readable at any time,
// including after the fact from a surviving process.
extern "C" __declspec(dllexport) const char* PreyVR_GetStereoStep()
{
    return preyvr::dll::StereoStepName();
}

// Arms the one-shot render-view probe. It runs on the next rendered frame and
// nothing needs to be armed for it to work.
//
// Settles R-072: whether `pRenderer->GetRenderViewForThread(slot, 1)` hands back
// a render view distinct from the type-0 one every Prey pass uses. That decides
// whether a second per-eye pass can own its own view -- which is the whole
// question left open by F-013.
extern "C" __declspec(dllexport) DWORD PreyVR_ProbeRenderViews()
{
    return preyvr::dll::ProbeRenderViews();
}

// 0 not run, 1 views differ (R-072 holds), 2 same view (refuted), 3 unresolved,
// 4 recursive view was null.
extern "C" __declspec(dllexport) DWORD PreyVR_GetRenderViewProbeStatus()
{
    return preyvr::dll::RenderViewProbeStatusValue();
}

// ---------------------------------------------------------------------------
// Pointer-argument variants, for automation
// ---------------------------------------------------------------------------
//
// **These exist because a float argument cannot be driven reliably from Frida.**
//
// F-009 records that Frida's NativeFunction aborts a call into this DLL with a
// bare "system error". The harness works around it by invoking anything that
// does work on a real Windows thread, where Frida marshals nothing -- but
// CreateThread can only pass a *pointer*, and the x64 calling convention passes
// floats in XMM registers, so a float-taking export cannot go through that route
// at all.
//
// That left the float exports on the unreliable path, and on 2026-09-01 it cost
// a live A2b run: SetStereoAsymmetry raised the abort and **never executed**, as
// the log proved by not containing its line. Had the sweep continued, every step
// would have run at the default 1.1 asymmetry and measured the same 462 px shear
// that F-011 is about -- while looking like a clean run.
//
// Each of these takes a pointer to its arguments, so it matches
// LPTHREAD_START_ROUTINE exactly and can be called on a real thread. The thread's
// exit code is the return value. Null is rejected rather than dereferenced;
// beyond that the caller is trusted with its own memory, which is inherent to
// the pattern and is why these are automation-only entry points.
extern "C" __declspec(dllexport) DWORD PreyVR_SetStereoAsymmetryPtr(const float* outerScale)
{
    if (outerScale == nullptr) {
        return static_cast<DWORD>(preyvr::dll::CameraEditStatus::failed);
    }
    return preyvr::dll::SetStereoAsymmetry(*outerScale);
}

// args[0] = IPD in metres, args[1] = half-FOV in degrees.
extern "C" __declspec(dllexport) DWORD PreyVR_SetSyntheticStereoPtr(const float* args)
{
    if (args == nullptr) {
        return static_cast<DWORD>(preyvr::dll::CameraEditStatus::failed);
    }
    return preyvr::dll::SetSyntheticStereo(args[0], args[1]);
}

extern "C" __declspec(dllexport) DWORD PreyVR_SetCameraYawEditPtr(const float* degrees)
{
    if (degrees == nullptr) {
        return static_cast<DWORD>(preyvr::dll::CameraEditStatus::failed);
    }
    return preyvr::dll::SetCameraYawEdit(*degrees);
}

// Laid out as two floats followed by a frame budget, so one allocation carries
// the whole call.
struct PreyVRDoubleRenderArgs {
    float ipdMetres;
    float halfFovDegrees;
    unsigned int frameBudget;
};

extern "C" __declspec(dllexport) DWORD PreyVR_SetDoubleRenderStereoPtr(
    const PreyVRDoubleRenderArgs* args)
{
    if (args == nullptr) {
        return static_cast<DWORD>(preyvr::dll::CameraEditStatus::failed);
    }
    return preyvr::dll::SetDoubleRenderStereo(
        args->ipdMetres, args->halfFovDegrees, args->frameBudget);
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetDoubleRenderedFrameCount()
{
    return preyvr::dll::DoubleRenderedFrameCount();
}

// --- Hurdle 2: an OpenXR session hosted inside Prey ------------------------
//
// Binds a session to *Prey's own* D3D11 device and mirrors its backbuffer into
// both eyes. Never starts on load.
//
// Returns 0 idle, 1 running, 2 adapter mismatch, 3 unavailable, 4 failed,
// 5 stopped. On 2 the log names the r_overrideDXGIAdapter index to set **before
// the next launch** - the cvar is read once during device creation, so it cannot
// be fixed from here.
// Selects the OpenXR runtime for this process, before the loader is first used.
// Required rather than convenient: Prey is launched by Steam and inherits Steam's
// environment, so an injected mod cannot be handed a runtime choice and must make
// one itself. Pass null or "" to return to the machine's runtime. Must be called
// before PreyVR_StartXrSession.
extern "C" __declspec(dllexport) DWORD PreyVR_SetXrRuntimeManifest(const char* manifestPath)
{
    return preyvr::dll::SetXrRuntimeManifest(manifestPath);
}

extern "C" __declspec(dllexport) DWORD PreyVR_StartXrSession()
{
    return preyvr::dll::StartXrSession();
}

extern "C" __declspec(dllexport) DWORD PreyVR_StopXrSession()
{
    return preyvr::dll::StopXrSession();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetXrSessionStatus()
{
    return preyvr::dll::XrSessionStatusValue();
}

// Arms stereo submission: a real image per eye, and Prey's own frustum declared
// over them rather than the runtime's. Pointer-taking per F-009.
extern "C" __declspec(dllexport) DWORD PreyVR_SetXrStereoSubmissionPtr(void* enabled)
{
    return preyvr::dll::SetXrStereoSubmission(
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(enabled)));
}

// Asks for the sRGB swapchain format -- the double-encoded-gamma fix. Must be
// set before PreyVR_StartXrSession. Pointer-taking per F-009.
extern "C" __declspec(dllexport) DWORD PreyVR_SetXrPreferSrgbFormatPtr(void* enabled)
{
    return preyvr::dll::SetXrPreferSrgbFormat(
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(enabled)));
}

// Sends each eye's image to the other eye's socket. Safe to toggle live, and the
// only way to settle inverted stereo without a rebuild. Pointer-taking per F-009.
extern "C" __declspec(dllexport) DWORD PreyVR_SetXrSwapEyesPtr(void* enabled)
{
    return preyvr::dll::SetXrSwapEyes(
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(enabled)));
}

// In-headset A/B. Cycles the eye offset and antialiasing mode from Ctrl+Alt
// chords so two values can be compared seconds apart instead of minutes apart.
extern "C" __declspec(dllexport) DWORD PreyVR_SetHotkeysEnabledPtr(void* enabled)
{
    return preyvr::dll::SetHotkeysEnabled(
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(enabled)));
}

// Eye offset in tenths of a millimetre: 640 is 0.064 m.
extern "C" __declspec(dllexport) DWORD PreyVR_GetHotkeyIpdTenthsMm()
{
    return preyvr::dll::HotkeyIpdTenthsMm();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetHotkeyAaMode()
{
    return preyvr::dll::HotkeyAaMode();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetHotkeyEyeSwap()
{
    return preyvr::dll::HotkeyEyeSwap();
}

// Zero while the wearer is pressing keys means the poll is not seeing them,
// which is a different problem from a setting that does nothing.
extern "C" __declspec(dllexport) DWORD PreyVR_GetHotkeyPressCount()
{
    return preyvr::dll::HotkeyPressCount();
}

// Is the camera the engine culls with the camera we wrote? Read-only.
extern "C" __declspec(dllexport) DWORD PreyVR_SetPassCameraProbeEnabledPtr(void* enabled)
{
    return preyvr::dll::SetPassCameraProbeEnabled(
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(enabled)));
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetPassCameraSamples()
{
    return preyvr::dll::PassCameraSamples();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetPassCameraAgreements()
{
    return preyvr::dll::PassCameraAgreements();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetPassCameraDisagreements()
{
    return preyvr::dll::PassCameraDisagreements();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetPassCameraLastDifferenceMillidegrees()
{
    return preyvr::dll::PassCameraLastDifferenceMillidegrees();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetPassCameraMaxDifferenceMillidegrees()
{
    return preyvr::dll::PassCameraMaxDifferenceMillidegrees();
}

// Leaves head rotation on the camera so the next frame's occlusion job sees it.
extern "C" __declspec(dllexport) DWORD PreyVR_SetKeepHeadRotationPtr(void* enabled)
{
    return preyvr::dll::SetKeepHeadRotation(
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(enabled)));
}

// Head rotation on the upstream camera, which is the one the engine culls from.
// Recenter first.
extern "C" __declspec(dllexport) DWORD PreyVR_SetUpstreamHeadRotationPtr(void* enabled)
{
    return preyvr::dll::SetUpstreamHeadRotation(
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(enabled)));
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetUpstreamHeadRotationApplied()
{
    return preyvr::dll::UpstreamHeadRotationApplied();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetUpstreamHeadRotationRefused()
{
    return preyvr::dll::UpstreamHeadRotationRefused();
}

// M1 head tracking, on the CRenderView::SetCamera seam. Recenter first: arming
// without a reference is refused rather than snapping the world to engine north.
extern "C" __declspec(dllexport) DWORD PreyVR_RecenterHeadTracking()
{
    return preyvr::dll::RecenterHeadTracking();
}

extern "C" __declspec(dllexport) DWORD PreyVR_SetHeadTrackingEnabledPtr(void* enabled)
{
    return preyvr::dll::SetHeadTrackingEnabled(
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(enabled)));
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetHeadTrackingAppliedCount()
{
    return preyvr::dll::HeadTrackingAppliedCount();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetHeadTrackingRefusedCount()
{
    return preyvr::dll::HeadTrackingRefusedCount();
}

// The handoff segment of pose latency only -- it excludes the runtime's own
// prediction and the display pipeline, so it is a floor rather than the whole.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetHeadTrackingLastPoseAgeMicroseconds()
{
    return preyvr::dll::HeadTrackingLastPoseAgeMicroseconds();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetHeadTrackingMaxPoseAgeMicroseconds()
{
    return preyvr::dll::HeadTrackingMaxPoseAgeMicroseconds();
}

extern "C" __declspec(dllexport) DWORD PreyVR_GetHeadTrackingHasReference()
{
    return preyvr::dll::HeadTrackingHasReference();
}

// Measures whether per-eye stereo is contaminating Prey's cached aim ray -- the
// acceptance check H-008 called required and that has never been run.
extern "C" __declspec(dllexport) DWORD PreyVR_SetAimRayProbeEnabledPtr(void* enabled)
{
    return preyvr::dll::SetAimRayProbeEnabled(
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(enabled)));
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetAimRaySampleCount()
{
    return preyvr::dll::AimRaySampleCount();
}

// A high count means the probe measured nothing, which must not be read as a
// clean result.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetAimRayUnreadableCount()
{
    return preyvr::dll::AimRayUnreadableCount();
}

// The headline number: the largest jump in ray origin between consecutive
// frames, in micrometres. Near the IPD (about 64000) means contaminated.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetAimRayMaxOriginGapMicrometres()
{
    return preyvr::dll::AimRayMaxOriginGapMicrometres();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetAimRayMaxAngleGapMillidegrees()
{
    return preyvr::dll::AimRayMaxAngleGapMillidegrees();
}

// Eye-handoff diagnostics. The lag is pushes minus pops -- the pipeline depth in
// frames -- and is how the ordering assumption is checked rather than trusted: it
// should sit at a small constant, and drift means eye identity is no longer safe.
extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetEyeHandoffLag()
{
    return preyvr::dll::EyeHandoffLag();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetEyeHandoffStarvedCount()
{
    return preyvr::dll::EyeHandoffStarvedCount();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetEyeHandoffDroppedCount()
{
    return preyvr::dll::EyeHandoffDroppedCount();
}

extern "C" __declspec(dllexport) ULONGLONG PreyVR_GetXrSubmittedFrameCount()
{
    return preyvr::dll::XrSubmittedFrameCount();
}
