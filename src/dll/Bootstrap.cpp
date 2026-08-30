#include "Bootstrap.h"

#include "ConsoleBridgeWin32.h"
#include "FrameCaptureWin32.h"

#include "FrameObserverHook.h"
#include "Logger.h"
#include "OpenXRPreflightWin32.h"
#include "RuntimeSnapshotWin32.h"
#include "Version.h"
#include "preyvr/EngineMap.h"

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
