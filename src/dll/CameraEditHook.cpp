#include "CameraEditHook.h"

#include "FrameCaptureWin32.h"
#include "Logger.h"
#include "preyvr/CameraEdit.h"
#include "preyvr/EngineMap.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/StereoFrame.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>

namespace preyvr::dll {
namespace {

// CSystem::Render (R-058). Prologue stops before the jne's rel32 so the
// signature is the stable part: push r15 / sub rsp,0xD0 / cmp byte [rcx+0x9D7],0
// / mov r15,rcx. The +0x9D7 test is what makes it distinctive rather than a
// generic MSVC prologue.
constexpr std::uintptr_t kSystemRenderRva = 0xE0BA30;
constexpr std::array<std::uint8_t, 19> kSystemRenderPrologue{
    0x41, 0x57, 0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00, 0x80,
    0xB9, 0xD7, 0x09, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xF9,
};

// CCamera::UpdateFrustum. Rebuilds the corners, the six planes at +0x10C, the
// plane sign tables and the cached position at +0x230.
constexpr std::uintptr_t kUpdateFrustumRva = 0x121D70;
constexpr std::array<std::uint8_t, 20> kUpdateFrustumPrologue{
    0x48, 0x8B, 0xC4, 0x55, 0x53, 0x48, 0x8D, 0x68, 0xA1, 0x48,
    0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x59,
};

using SystemRenderFn = void(__fastcall*)(void* system);
using UpdateFrustumFn = void(__fastcall*)(void* camera);

// Timed, for the reason XrSessionHost is: a control export called from an
// external tool can have its call aborted mid-flight, and a plain mutex held at
// that moment is never released -- poisoning every later call with a hang rather
// than an error. Observed live 2026-08-31, where it silently stopped the yaw
// from arming and left a capture that looked fine and proved nothing.
std::timed_mutex gMutex;
constexpr auto kControlLockTimeout = std::chrono::milliseconds(250);
std::atomic<DWORD> gStatus{static_cast<DWORD>(CameraEditStatus::unavailable)};
std::atomic<SystemRenderFn> gOriginal{nullptr};
std::atomic<UpdateFrustumFn> gUpdateFrustum{nullptr};
std::atomic<float> gYawDegrees{0.0f};
std::atomic<bool> gArmed{false};
std::atomic<unsigned long long> gApplied{0};
std::atomic<unsigned long long> gRestoreFailures{0};
std::atomic<bool> gLoggedFirstApplication{false};
std::atomic<float> gStereoIpd{0.0f};
std::atomic<float> gStereoHalfFov{50.0f};
std::atomic<unsigned long long> gEyeCounter{0};
std::atomic<int> gLastEye{-1};
std::atomic<bool> gDoubleRender{false};
std::atomic<unsigned int> gDoubleRenderBudget{0};
std::atomic<unsigned long long> gDoubleRendered{0};
void* gTarget = nullptr;
bool gHookCreated = false;

bool PrologueMatches(std::uintptr_t address, const std::uint8_t* expected, std::size_t length)
{
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) !=
            sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY)) == 0) {
        return false;
    }
    return std::memcmp(reinterpret_cast<const void*>(address), expected, length) == 0;
}

// Produces the edited camera for a synthetic stereo eye, composed onto whatever
// camera the engine was about to render with.
bool BuildSyntheticEye(
    std::array<std::uint8_t, cameraedit::kCameraSize>& edited,
    int eye,
    float ipdMetres,
    float halfFovDegrees)
{
    const stereo::Matrix34 baseMatrix = stereo::ReadMatrix(edited);
    const Pose basePose = stereo::PoseFromMatrix(baseMatrix);

    // Half the IPD along the camera's own right axis.
    const float half = ipdMetres * 0.5f;
    const Vec3 offset{eye == 0 ? -half : half, 0.0f, 0.0f};
    const Pose eyePose = stereo::OffsetInLocalFrame(basePose, offset);
    if (!stereo::WriteMatrix(edited, stereo::MatrixFromPose(eyePose))) {
        return false;
    }

    // Mirrored asymmetry, the way a headset actually reports it: the outer edge
    // extends further than the inner one. Symmetric values here would leave the
    // shift path untested until a headset was attached, which is precisely when
    // an untested path is most expensive.
    const float radians = halfFovDegrees * 3.14159265358979323846f / 180.0f;
    const float outer = std::tan(radians * 1.1f);
    const float inner = std::tan(radians * 0.9f);
    const float vertical = std::tan(radians);

    stereoframe::EyeView view{};
    view.tanUp = vertical;
    view.tanDown = -vertical;
    view.tanLeft = eye == 0 ? -outer : -inner;
    view.tanRight = eye == 0 ? inner : outer;

    const float nearPlane = stereoframe::NearPlaneOf(edited);
    const auto projection = stereoframe::ProjectionFromTangents(view, nearPlane);
    if (!projection) {
        return false;
    }

    const auto write = [&edited](std::size_t offsetBytes, float value) {
        std::memcpy(edited.data() + offsetBytes, &value, sizeof(float));
    };
    write(engine::CameraLayout::fov, projection->fov);
    write(engine::CameraLayout::projectionRatio, projection->projectionRatio);
    write(engine::CameraLayout::asymLeft, projection->asymmetry.left);
    write(engine::CameraLayout::asymRight, projection->asymmetry.right);
    write(engine::CameraLayout::asymBottom, projection->asymmetry.bottom);
    write(engine::CameraLayout::asymTop, projection->asymmetry.top);
    return true;
}

void __fastcall RenderWithCameraEdit(void* system)
{
    const SystemRenderFn original = gOriginal.load(std::memory_order_acquire);

    const bool stereoArmed = gStereoIpd.load(std::memory_order_acquire) > 0.0f;
    if ((!gArmed.load(std::memory_order_acquire) && !stereoArmed) || system == nullptr) {
        if (original != nullptr) {
            original(system);
        }
        return;
    }

    auto* camera = reinterpret_cast<std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(system) + engine::SystemLayout::viewCamera);
    const std::span<std::uint8_t> live(camera, cameraedit::kCameraSize);

    cameraedit::RestorePoint restore{};
    if (!cameraedit::Capture(live, restore)) {
        if (original != nullptr) {
            original(system);
        }
        return;
    }

    // Build the edited camera entirely in our own memory first. UpdateFrustum is
    // called on this copy, so the engine function never touches game state and
    // an aborted edit costs nothing.
    std::array<std::uint8_t, cameraedit::kCameraSize> edited{};
    std::memcpy(edited.data(), restore.bytes.data(), cameraedit::kCameraSize);

    const UpdateFrustumFn updateFrustum = gUpdateFrustum.load(std::memory_order_acquire);

    if (gDoubleRender.load(std::memory_order_acquire) && updateFrustum != nullptr) {
        // Render both eyes inside one frame. The budget is decremented first so
        // that a call which never returns still costs exactly one frame of the
        // allowance rather than leaving the mode armed forever.
        const unsigned int remaining = gDoubleRenderBudget.load(std::memory_order_acquire);
        if (remaining == 0) {
            gDoubleRender.store(false, std::memory_order_release);
            gStereoIpd.store(0.0f, std::memory_order_release);
            SetFrameCaptureTagOverride(-1);
            lifecycle::Log("preyvr_camera_edit result=0 detail=double_render_budget_exhausted");
            if (original != nullptr) {
                original(system);
            }
            return;
        }
        gDoubleRenderBudget.store(remaining - 1, std::memory_order_release);

        const float ipd = gStereoIpd.load(std::memory_order_acquire);
        const float halfFov = gStereoHalfFov.load(std::memory_order_acquire);
        bool bothEyesOk = true;

        for (int eye = 0; eye < 2 && bothEyesOk; ++eye) {
            std::array<std::uint8_t, cameraedit::kCameraSize> eyeCamera{};
            std::memcpy(eyeCamera.data(), restore.bytes.data(), cameraedit::kCameraSize);
            if (!BuildSyntheticEye(eyeCamera, eye, ipd, halfFov)) {
                bothEyesOk = false;
                break;
            }
            updateFrustum(eyeCamera.data());
            if (!cameraedit::RotationIsSafeToWrite(eyeCamera)) {
                bothEyesOk = false;
                break;
            }
            // Each eye is built from the *original* camera, not from the previous
            // eye's, so an error cannot accumulate across the two passes.
            std::memcpy(camera, eyeCamera.data(), cameraedit::kCameraSize);
            gLastEye.store(eye, std::memory_order_release);
            SetFrameCaptureTagOverride(eye);
            if (original != nullptr) {
                original(system);
            }
        }

        std::memcpy(camera, restore.bytes.data(), cameraedit::kCameraSize);
        if (!cameraedit::MatchesRestorePoint(live, restore)) {
            gRestoreFailures.fetch_add(1, std::memory_order_relaxed);
            gDoubleRender.store(false, std::memory_order_release);
            gStereoIpd.store(0.0f, std::memory_order_release);
            SetFrameCaptureTagOverride(-1);
            lifecycle::Log("preyvr_camera_edit result=restore_failed detail=double_render_disarmed");
            return;
        }
        if (!bothEyesOk) {
            gDoubleRender.store(false, std::memory_order_release);
            gStereoIpd.store(0.0f, std::memory_order_release);
            SetFrameCaptureTagOverride(-1);
            lifecycle::Log("preyvr_camera_edit result=refused detail=double_render_eye_build_failed");
            return;
        }

        const unsigned long long done = gDoubleRendered.fetch_add(1, std::memory_order_relaxed) + 1;
        bool expectedFirst = false;
        if (gLoggedFirstApplication.compare_exchange_strong(expectedFirst, true)) {
            std::ostringstream line;
            line << "preyvr_camera_edit result=0 detail=double_render_first_frame"
                 << " ipd=" << ipd << " budget=" << remaining << " done=" << done;
            lifecycle::Log(line.str());
        }
        return;
    }

    bool built = false;
    if (stereoArmed) {
        // Alternate every frame. With the simulation frozen, consecutive frames
        // differ only by the eye, which is exactly a stereo pair.
        const int eye = static_cast<int>(gEyeCounter.fetch_add(1, std::memory_order_relaxed) & 1ull);
        built = BuildSyntheticEye(edited, eye,
                                  gStereoIpd.load(std::memory_order_acquire),
                                  gStereoHalfFov.load(std::memory_order_acquire));
        if (built) {
            gLastEye.store(eye, std::memory_order_release);
            // Stamp the capture so the two dumps of a pair cannot be confused.
            SetFrameCaptureTagOverride(eye);
        }
    } else {
        const cameraedit::YawEdit edit{gYawDegrees.load(std::memory_order_acquire)};
        built = cameraedit::ApplyYaw(edited, edit);
    }

    if (!built || updateFrustum == nullptr) {
        if (original != nullptr) {
            original(system);
        }
        return;
    }
    updateFrustum(edited.data());

    // Gated on both predicates: orthonormal keeps the engine from negating its
    // plane normals, and non-degenerate closes the hole where an all-zero matrix
    // satisfies the engine's own check.
    if (!cameraedit::RotationIsSafeToWrite(edited)) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=unsafe_rotation");
        // Disarm *both* producers. Leaving stereo armed after rejecting its
        // camera would retry the same bad build every frame.
        gArmed.store(false, std::memory_order_release);
        gStereoIpd.store(0.0f, std::memory_order_release);
        SetFrameCaptureTagOverride(-1);
        if (original != nullptr) {
            original(system);
        }
        return;
    }

    std::memcpy(camera, edited.data(), cameraedit::kCameraSize);

    if (original != nullptr) {
        original(system);
    }

    std::memcpy(camera, restore.bytes.data(), cameraedit::kCameraSize);

    // Verified, not assumed. A restore that is merely performed is a hope.
    if (!cameraedit::MatchesRestorePoint(live, restore)) {
        gRestoreFailures.fetch_add(1, std::memory_order_relaxed);
        gArmed.store(false, std::memory_order_release);
        gStereoIpd.store(0.0f, std::memory_order_release);
        SetFrameCaptureTagOverride(-1);
        lifecycle::Log("preyvr_camera_edit result=restore_failed detail=disarmed");
        return;
    }

    const unsigned long long applied = gApplied.fetch_add(1, std::memory_order_relaxed) + 1;
    bool expected = false;
    if (gLoggedFirstApplication.compare_exchange_strong(expected, true)) {
        // Logged once rather than per frame: at 144 Hz a per-frame line would
        // bury everything else in the smoke log within seconds.
        const auto position = cameraedit::PositionOf(restore.bytes);
        std::ostringstream line;
        line << "preyvr_camera_edit result=0 detail=first_application"
             << " mode=" << (stereoArmed ? "stereo" : "yaw");
        if (stereoArmed) {
            line << " ipd=" << gStereoIpd.load(std::memory_order_acquire)
                 << " eye=" << gLastEye.load(std::memory_order_acquire);
        } else {
            line << " yawDegrees=" << gYawDegrees.load(std::memory_order_acquire);
        }
        line << " originalPos=" << position[0] << ',' << position[1] << ',' << position[2]
             << " applied=" << applied;
        lifecycle::Log(line.str());
    }
}

bool EnsureHook()
{
    if (gHookCreated) {
        return true;
    }

    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);

    const auto renderAddress = base + kSystemRenderRva;
    const auto frustumAddress = base + kUpdateFrustumRva;
    if (!PrologueMatches(renderAddress, kSystemRenderPrologue.data(), kSystemRenderPrologue.size())) {
        lifecycle::Log("preyvr_camera_edit result=unavailable detail=system_render_prologue");
        return false;
    }
    if (!PrologueMatches(frustumAddress, kUpdateFrustumPrologue.data(),
                         kUpdateFrustumPrologue.size())) {
        lifecycle::Log("preyvr_camera_edit result=unavailable detail=update_frustum_prologue");
        return false;
    }
    gUpdateFrustum.store(reinterpret_cast<UpdateFrustumFn>(frustumAddress),
                         std::memory_order_release);

    gTarget = reinterpret_cast<void*>(renderAddress);
    SystemRenderFn original = nullptr;
    MH_STATUS status = MH_CreateHook(
        gTarget, reinterpret_cast<void*>(&RenderWithCameraEdit),
        reinterpret_cast<void**>(&original));
    if (status != MH_OK) {
        std::ostringstream line;
        line << "preyvr_camera_edit result=failed detail=create_hook minhook="
             << MH_StatusToString(status);
        lifecycle::Log(line.str());
        return false;
    }
    gOriginal.store(original, std::memory_order_release);

    status = MH_EnableHook(gTarget);
    if (status != MH_OK) {
        std::ostringstream line;
        line << "preyvr_camera_edit result=failed detail=enable_hook minhook="
             << MH_StatusToString(status);
        lifecycle::Log(line.str());
        return false;
    }

    gHookCreated = true;
    gStatus.store(static_cast<DWORD>(CameraEditStatus::ready), std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera_edit result=ready targetRva=0x" << std::hex << std::uppercase
         << kSystemRenderRva << " target=0x" << renderAddress;
    lifecycle::Log(line.str());
    return true;
}

} // namespace

DWORD SetCameraYawEdit(float degrees)
{
    std::unique_lock lock(gMutex, kControlLockTimeout);
    if (!lock.owns_lock()) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=busy step=SetCameraYawEdit");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }

    if (degrees == 0.0f) {
        gArmed.store(false, std::memory_order_release);
        if (gHookCreated) {
            gStatus.store(static_cast<DWORD>(CameraEditStatus::ready), std::memory_order_release);
        }
        lifecycle::Log("preyvr_camera_edit result=0 detail=disarmed");
        return gStatus.load(std::memory_order_acquire);
    }

    if (!cameraedit::IsWithinBounds({degrees})) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=out_of_bounds");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }

    if (!EnsureHook()) {
        gStatus.store(static_cast<DWORD>(CameraEditStatus::unavailable), std::memory_order_release);
        return static_cast<DWORD>(CameraEditStatus::unavailable);
    }

    gYawDegrees.store(degrees, std::memory_order_release);
    gLoggedFirstApplication.store(false, std::memory_order_release);
    gArmed.store(true, std::memory_order_release);
    gStatus.store(static_cast<DWORD>(CameraEditStatus::armed), std::memory_order_release);

    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=armed yawDegrees=" << degrees;
    lifecycle::Log(line.str());
    return static_cast<DWORD>(CameraEditStatus::armed);
}

DWORD SetSyntheticStereo(float ipdMetres, float halfFovDegrees)
{
    std::unique_lock lock(gMutex, kControlLockTimeout);
    if (!lock.owns_lock()) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=busy step=SetSyntheticStereo");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }

    if (ipdMetres == 0.0f) {
        gStereoIpd.store(0.0f, std::memory_order_release);
        gLastEye.store(-1, std::memory_order_release);
        SetFrameCaptureTagOverride(-1);
        if (gHookCreated) {
            gStatus.store(static_cast<DWORD>(CameraEditStatus::ready), std::memory_order_release);
        }
        lifecycle::Log("preyvr_camera_edit result=0 detail=stereo_disarmed");
        return gStatus.load(std::memory_order_acquire);
    }

    // A human IPD is 55-75mm. Anything outside that is a caller error rather
    // than an unusual head, and a wildly wrong separation is exactly the input
    // that produces a nauseating image instead of an obviously broken one.
    if (!std::isfinite(ipdMetres) || ipdMetres < 0.045f || ipdMetres > 0.085f ||
        !std::isfinite(halfFovDegrees) || halfFovDegrees < 20.0f || halfFovDegrees > 70.0f) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=stereo_out_of_bounds");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }

    if (!EnsureHook()) {
        gStatus.store(static_cast<DWORD>(CameraEditStatus::unavailable), std::memory_order_release);
        return static_cast<DWORD>(CameraEditStatus::unavailable);
    }

    gStereoIpd.store(ipdMetres, std::memory_order_release);
    gStereoHalfFov.store(halfFovDegrees, std::memory_order_release);
    gEyeCounter.store(0, std::memory_order_release);
    gLoggedFirstApplication.store(false, std::memory_order_release);
    gStatus.store(static_cast<DWORD>(CameraEditStatus::armed), std::memory_order_release);

    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=stereo_armed ipd=" << ipdMetres
         << " halfFovDegrees=" << halfFovDegrees;
    lifecycle::Log(line.str());
    return static_cast<DWORD>(CameraEditStatus::armed);
}

DWORD SetDoubleRenderStereo(float ipdMetres, float halfFovDegrees, unsigned int frameBudget)
{
    if (ipdMetres == 0.0f || frameBudget == 0) {
        std::unique_lock lock(gMutex, kControlLockTimeout);
        gDoubleRender.store(false, std::memory_order_release);
        gDoubleRenderBudget.store(0, std::memory_order_release);
        gStereoIpd.store(0.0f, std::memory_order_release);
        SetFrameCaptureTagOverride(-1);
        lifecycle::Log("preyvr_camera_edit result=0 detail=double_render_disarmed");
        return gStatus.load(std::memory_order_acquire);
    }

    // A ceiling on the ceiling: this is the riskiest thing the DLL can do, and
    // an experiment that runs for ten thousand frames is not an experiment.
    if (frameBudget > 600) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=double_render_budget_too_large");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }

    // Reuses the synthetic-stereo bounds check, then swaps the mode over.
    const DWORD armed = SetSyntheticStereo(ipdMetres, halfFovDegrees);
    if (armed != static_cast<DWORD>(CameraEditStatus::armed)) {
        return armed;
    }

    std::unique_lock lock(gMutex, kControlLockTimeout);
    if (!lock.owns_lock()) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=busy step=double_render");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }
    gDoubleRenderBudget.store(frameBudget, std::memory_order_release);
    gDoubleRender.store(true, std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=double_render_armed frameBudget=" << frameBudget;
    lifecycle::Log(line.str());
    return static_cast<DWORD>(CameraEditStatus::armed);
}

unsigned long long DoubleRenderedFrameCount()
{
    return gDoubleRendered.load(std::memory_order_acquire);
}

int LastRenderedEye()
{
    return gLastEye.load(std::memory_order_acquire);
}

DWORD CameraEditStatusValue()
{
    return gStatus.load(std::memory_order_acquire);
}

unsigned long long CameraEditAppliedCount()
{
    return gApplied.load(std::memory_order_acquire);
}

unsigned long long CameraEditRestoreFailureCount()
{
    return gRestoreFailures.load(std::memory_order_acquire);
}

} // namespace preyvr::dll
