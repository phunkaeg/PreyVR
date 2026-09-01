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
std::atomic<int> gEyeLock{-1};   // -1 = alternate
std::atomic<float> gAsymmetry{1.1f};
std::atomic<bool> gDoubleRender{false};
std::atomic<unsigned int> gDoubleRenderBudget{0};

// **A deadline, because the frame budget did not contain F-013.**
//
// The budget counts frames and disarms when it reaches zero, which assumes frames
// keep completing. On 2026-09-01 the engine wedged at roughly frame 4 of 300, so
// no further frames completed, the budget never drained, and the mode stayed
// armed until the process was killed from outside.
//
// A budget expressed in frames cannot bound a failure that stops frames. This one
// is wall-clock and is enforced by a watchdog thread, so it still fires when the
// render thread is stuck.
//
// **What it does and does not buy.** It bounds *exposure* -- how long the mode
// can keep attempting double renders -- and it is honest that it cannot bound
// *damage*: a render thread already stuck inside the original function is not
// freed by clearing a flag. It would not have saved the A3 session. It does mean
// the next experiment cannot run away while somebody reaches for the keyboard.
std::atomic<bool> gDoubleRenderWatchdogRunning{false};
std::atomic<unsigned long long> gDoubleRenderDeadlineMs{0};

std::atomic<bool> gProbeRenderViews{false};
std::atomic<DWORD> gRenderViewProbeStatus{
    static_cast<DWORD>(RenderViewProbeStatus::notRun)};

// A4: the recursive second pass. See the header for why this replaces A3.
std::atomic<bool> gSecondPass{false};
std::atomic<unsigned int> gSecondPassBudget{0};
std::atomic<unsigned long long> gSecondPassFrames{0};
std::atomic<DWORD> gSecondPassStatus{static_cast<DWORD>(SecondPassStatus::idle)};
std::atomic<bool> gLoggedFirstSecondPass{false};
std::atomic<bool> gSecondPassZeroDelta{false};

// **A breadcrumb naming the sub-step in flight.**
//
// F-013 is the argument for it: when A3 wedged we knew four frames had completed
// and nothing about which step of the fifth killed it. Prior art (FEAR VR, via
// ss2vr-work/docs/UEVR_STEREO_LESSONS.md) sets a string before every sub-step so
// a crash or hang names the step in the log. One pointer store per step, and the
// watchdog prints it on expiry -- which is exactly the moment it is worth having.
//
// A string literal only, so the pointer is always valid to read from the
// watchdog thread without any lifetime question.
std::atomic<const char*> gStereoStep{"idle"};

void Step(const char* name)
{
    gStereoStep.store(name, std::memory_order_release);
}

// SRenderingPassInfo::CreateGeneralPassRenderingInfo (R-071), byte-gated as the
// landmark `pass.create_general`. Signature from its decompilation: it fills a
// caller-supplied buffer and returns it.
constexpr std::uintptr_t kCreatePassInfoRva = 0x1E5B30;
using CreatePassInfoFn = void*(__fastcall*)(void* out, const void* camera, std::uint32_t flags,
                                            int auxWindow);
std::atomic<CreatePassInfoFn> gCreatePassInfo{nullptr};

// RenderWorld, reached through CSystem::m_pProcess's vtable (R-054/R-059).
using RenderWorldFn = void(__fastcall*)(void* process, int flags, void* passInfo,
                                        const char* debugName);

// CRenderView vtable slots that CreateGeneralPassRenderingInfo drives on the view
// it selected. They have to be re-driven on the recursive view after the swap,
// or the pass would carry state derived from the default view instead.
constexpr std::uintptr_t kRenderViewSetFlagsSlot = 0x20;
constexpr std::uintptr_t kRenderViewDerivedSlot = 0x58;
using RenderViewSetFlagsFn = void(__fastcall*)(void* view, std::uint32_t flags);
using RenderViewDerivedFn = void*(__fastcall*)(void* view);
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
    //
    // Built by the pure layer rather than here so that the property A2b depends
    // on -- scale 1.0 giving both eyes the same frustum -- is covered by a test
    // instead of by inspection.
    const stereoframe::EyeView view = stereoframe::SyntheticEyeView(
        eye, halfFovDegrees, gAsymmetry.load(std::memory_order_acquire));

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

// Renderer vtable slots, read straight out of CreateGeneralPassRenderingInfo
// (R-071). Both are called there in exactly this shape, so this replicates the
// engine's own usage rather than inventing a call.
constexpr std::uintptr_t kRendererQuerySlot = 0x888;          // EF_Query
constexpr std::uintptr_t kRendererGetRenderViewSlot = 0x198;  // GetRenderViewForThread
constexpr int kRenderThreadListQuery = 6;

using EfQueryFn = void(__fastcall*)(void*, int, void*, int, int, int);
using GetRenderViewFn = void*(__fastcall*)(void*, int, int);

// Asks the renderer for the Default and Recursive render views and compares them.
// See the header for why this decides whether native stereo is reachable.
void RunRenderViewProbe(void* system)
{
    const auto unresolved = [](const char* detail) {
        gRenderViewProbeStatus.store(static_cast<DWORD>(RenderViewProbeStatus::unresolved),
                                     std::memory_order_release);
        std::ostringstream line;
        line << "preyvr_render_view_probe result=unresolved detail=" << detail;
        lifecycle::Log(line.str());
    };

    if (system == nullptr) {
        unresolved("null_system");
        return;
    }
    auto* const gEnv = *reinterpret_cast<std::uint8_t**>(
        reinterpret_cast<std::uintptr_t>(system) + engine::SystemLayout::gEnvPointer);
    if (gEnv == nullptr) {
        unresolved("null_genv");
        return;
    }
    auto* const renderer = *reinterpret_cast<std::uint8_t**>(
        gEnv + engine::GlobalEnvironmentLayout::renderer);
    if (renderer == nullptr) {
        unresolved("null_renderer");
        return;
    }
    auto* const vtable = *reinterpret_cast<std::uint8_t**>(renderer);
    if (vtable == nullptr) {
        unresolved("null_renderer_vtable");
        return;
    }

    const auto query = *reinterpret_cast<EfQueryFn*>(vtable + kRendererQuerySlot);
    const auto getView = *reinterpret_cast<GetRenderViewFn*>(vtable + kRendererGetRenderViewSlot);
    if (query == nullptr || getView == nullptr) {
        unresolved("null_vtable_slot");
        return;
    }

    // The engine writes four bytes here and then keeps only the low one, so the
    // slot is masked to a byte to match its own usage exactly.
    int queried = 0;
    query(renderer, kRenderThreadListQuery, &queried, static_cast<int>(sizeof(queried)), 0, 0);
    const int slot = queried & 0xFF;

    void* const defaultView = getView(renderer, slot, 0);
    void* const recursiveView = getView(renderer, slot, 1);

    RenderViewProbeStatus status = RenderViewProbeStatus::viewsIdentical;
    if (recursiveView == nullptr) {
        status = RenderViewProbeStatus::recursiveUnavailable;
    } else if (recursiveView != defaultView) {
        status = RenderViewProbeStatus::viewsDiffer;
    }
    gRenderViewProbeStatus.store(static_cast<DWORD>(status), std::memory_order_release);

    std::ostringstream line;
    line << "preyvr_render_view_probe result=0 slot=" << slot
         << " defaultView=0x" << std::hex << reinterpret_cast<std::uintptr_t>(defaultView)
         << " recursiveView=0x" << reinterpret_cast<std::uintptr_t>(recursiveView) << std::dec
         << " verdict=" << (status == RenderViewProbeStatus::viewsDiffer ? "distinct"
                            : status == RenderViewProbeStatus::recursiveUnavailable ? "recursive_null"
                                                                                    : "same_view");
    lifecycle::Log(line.str());
}

// Issues one extra RenderWorld for the second eye, with its own pass info and
// the recursive render view. See the header for why this is not A3.
//
// Everything it needs is resolved fresh each call and every step is null-checked,
// because this runs on the render thread and a wrong pointer here is a crash
// rather than a wrong number.
bool RunSecondPass(void* system)
{
    Step("second_pass:resolve");
    const auto createPass = gCreatePassInfo.load(std::memory_order_acquire);
    const UpdateFrustumFn updateFrustum = gUpdateFrustum.load(std::memory_order_acquire);
    if (system == nullptr || createPass == nullptr || updateFrustum == nullptr) {
        return false;
    }

    auto* const gEnv = *reinterpret_cast<std::uint8_t**>(
        reinterpret_cast<std::uintptr_t>(system) + engine::SystemLayout::gEnvPointer);
    if (gEnv == nullptr) {
        return false;
    }
    auto* const renderer = *reinterpret_cast<std::uint8_t**>(
        gEnv + engine::GlobalEnvironmentLayout::renderer);
    if (renderer == nullptr) {
        return false;
    }
    auto* const rendererVtable = *reinterpret_cast<std::uint8_t**>(renderer);
    if (rendererVtable == nullptr) {
        return false;
    }

    // RenderWorld is dispatched through CSystem::m_pProcess, not through
    // gEnv->p3DEngine, even though R-059 measured them as the same object.
    auto* const process = *reinterpret_cast<std::uint8_t**>(
        reinterpret_cast<std::uintptr_t>(system) + engine::SystemLayout::processPointer);
    if (process == nullptr) {
        return false;
    }
    auto* const processVtable = *reinterpret_cast<std::uint8_t**>(process);
    if (processVtable == nullptr) {
        return false;
    }
    const auto renderWorld = *reinterpret_cast<RenderWorldFn*>(
        processVtable + engine::SystemLayout::vtableRenderWorld);
    if (renderWorld == nullptr) {
        return false;
    }

    const auto query = *reinterpret_cast<EfQueryFn*>(
        rendererVtable + engine::RendererLayout::vtableQuery);
    const auto getView = *reinterpret_cast<GetRenderViewFn*>(
        rendererVtable + engine::RendererLayout::vtableGetRenderViewForThread);
    if (query == nullptr || getView == nullptr) {
        return false;
    }

    Step("second_pass:query_slot");
    int queried = 0;
    query(renderer, engine::RendererLayout::queryRenderThreadList, &queried,
          static_cast<int>(sizeof(queried)), 0, 0);
    const int slot = queried & 0xFF;
    void* const recursiveView =
        getView(renderer, slot, engine::RendererLayout::viewTypeRecursive);
    if (recursiveView == nullptr) {
        return false;
    }

    // The eye camera is built in our own memory from the game's live camera and
    // is never written back -- the game's camera is not touched by this path at
    // all, which is why there is no restore point here.
    Step("second_pass:build_eye_camera");
    auto* const liveCamera = reinterpret_cast<const std::uint8_t*>(
        reinterpret_cast<std::uintptr_t>(system) + engine::SystemLayout::viewCamera);
    std::array<std::uint8_t, cameraedit::kCameraSize> eyeCamera{};
    std::memcpy(eyeCamera.data(), liveCamera, cameraedit::kCameraSize);
    // **The zero-delta control (BN-SFX-001).** Everything on this path runs
    // identically -- same pass info, same recursive view, same RenderWorld call --
    // and only the eye offset is zero, so the second image should be the first
    // image. Anything that changes is therefore a *side effect* of repeating the
    // pass, not stereo. That is the discriminator the playbook's fast_test names,
    // and without it "the second pass changed the picture" cannot be told from
    // "the second pass advanced something that runs once per frame".
    const bool zeroDelta = gSecondPassZeroDelta.load(std::memory_order_acquire);
    const float eyeIpd = zeroDelta ? 0.0f : gStereoIpd.load(std::memory_order_acquire);
    if (!BuildSyntheticEye(eyeCamera, 1, eyeIpd,
                           gStereoHalfFov.load(std::memory_order_acquire))) {
        return false;
    }
    updateFrustum(eyeCamera.data());
    if (!cameraedit::RotationIsSafeToWrite(eyeCamera)) {
        return false;
    }

    // Built by the engine's own constructor into our buffer, so every field it
    // derives -- frame ids, zoom, the camera registration at +0x18 -- is derived
    // the way the engine derives it rather than guessed at here.
    Step("second_pass:create_pass_info");
    std::array<std::uint8_t, engine::PassInfoLayout::size> passInfo{};
    createPass(passInfo.data(), eyeCamera.data(), engine::PassInfoLayout::generalPassFlags, 0);

    // The constructor selected the *default* view and drove two calls on it.
    // Swapping the pointer alone would leave the pass carrying state derived
    // from a view it no longer references, so both are re-driven on the
    // recursive view.
    Step("second_pass:swap_render_view");
    std::memcpy(passInfo.data() + engine::PassInfoLayout::renderView, &recursiveView,
                sizeof(recursiveView));
    auto* const viewVtable = *reinterpret_cast<std::uint8_t**>(recursiveView);
    if (viewVtable == nullptr) {
        return false;
    }
    std::uint32_t passFlags = 0;
    std::memcpy(&passFlags, passInfo.data() + engine::PassInfoLayout::flags, sizeof(passFlags));
    const auto setFlags =
        *reinterpret_cast<RenderViewSetFlagsFn*>(viewVtable + kRenderViewSetFlagsSlot);
    if (setFlags == nullptr) {
        return false;
    }
    setFlags(recursiveView, passFlags);
    const auto derived =
        *reinterpret_cast<RenderViewDerivedFn*>(viewVtable + kRenderViewDerivedSlot);
    if (derived == nullptr) {
        return false;
    }
    void* const derivedValue = derived(recursiveView);
    std::memcpy(passInfo.data() + engine::PassInfoLayout::renderViewDerived, &derivedValue,
                sizeof(derivedValue));

    Step("second_pass:render_world");
    renderWorld(process, engine::PassInfoLayout::renderWorldFlags, passInfo.data(),
                "PreyVR::SecondPass");
    Step("second_pass:done");

    const unsigned long long done = gSecondPassFrames.fetch_add(1, std::memory_order_relaxed) + 1;
    bool expected = false;
    if (gLoggedFirstSecondPass.compare_exchange_strong(expected, true)) {
        std::ostringstream line;
        line << "preyvr_second_pass result=0 detail=first_frame"
             << " zeroDelta=" << (zeroDelta ? 1 : 0) << " slot=" << slot
             << " recursiveView=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(recursiveView) << std::dec
             << " done=" << done;
        lifecycle::Log(line.str());
    }
    return true;
}

// Wall-clock stop for the double render, run off the render thread so it still
// fires when that thread is the thing that is stuck. See the deadline note above
// for what this does and does not bound.
DWORD WINAPI DoubleRenderWatchdog(LPVOID)
{
    // Covers both risky modes: A3's double render and A4's second pass. Either
    // one stopping the frame loop is exactly the case the in-band budgets cannot
    // see, which is the whole reason this thread exists.
    while (gDoubleRender.load(std::memory_order_acquire) ||
           gSecondPass.load(std::memory_order_acquire)) {
        const unsigned long long deadline = gDoubleRenderDeadlineMs.load(std::memory_order_acquire);
        if (deadline != 0 && GetTickCount64() >= deadline) {
            gDoubleRender.store(false, std::memory_order_release);
            gDoubleRenderBudget.store(0, std::memory_order_release);
            gSecondPass.store(false, std::memory_order_release);
            gSecondPassBudget.store(0, std::memory_order_release);
            gStereoIpd.store(0.0f, std::memory_order_release);
            SetFrameCaptureTagOverride(-1);
            std::ostringstream expiry;
            expiry << "preyvr_camera_edit result=0 detail=deadline_expired step="
                   << gStereoStep.load(std::memory_order_acquire);
            lifecycle::Log(expiry.str());
            break;
        }
        Sleep(50);
    }
    gDoubleRenderWatchdogRunning.store(false, std::memory_order_release);
    return 0;
}

void __fastcall RenderWithCameraEdit(void* system)
{
    const SystemRenderFn original = gOriginal.load(std::memory_order_acquire);

    // One-shot, and ahead of the armed check because the probe is independent of
    // any edit -- it needs this thread, not an armed camera.
    if (gProbeRenderViews.exchange(false, std::memory_order_acq_rel)) {
        RunRenderViewProbe(system);
    }

    // A4. Its own branch and its own early return, so it cannot interact with
    // the camera-edit or alternating-stereo modes below. The game's frame is
    // rendered first and untouched -- that is the first eye, and it is why this
    // path never writes the game's camera and needs no restore point.
    if (gSecondPass.load(std::memory_order_acquire)) {
        const unsigned int remaining = gSecondPassBudget.load(std::memory_order_acquire);
        if (remaining == 0) {
            gSecondPass.store(false, std::memory_order_release);
            gStereoIpd.store(0.0f, std::memory_order_release);
            lifecycle::Log("preyvr_second_pass result=0 detail=budget_exhausted");
            if (original != nullptr) {
                original(system);
            }
            return;
        }
        // Decremented before the work, so a call that never returns still costs
        // exactly one frame of the allowance.
        gSecondPassBudget.store(remaining - 1, std::memory_order_release);

        if (original != nullptr) {
            original(system);
        }
        if (RunSecondPass(system)) {
            gSecondPassStatus.store(static_cast<DWORD>(SecondPassStatus::ranAtLeastOnce),
                                    std::memory_order_release);
        } else {
            // Disarm on the first refusal rather than retrying every frame: if a
            // pointer could not be resolved once it will not resolve next frame,
            // and a log line per frame would bury the reason.
            gSecondPass.store(false, std::memory_order_release);
            gStereoIpd.store(0.0f, std::memory_order_release);
            gSecondPassStatus.store(static_cast<DWORD>(SecondPassStatus::refusedUnresolved),
                                    std::memory_order_release);
            lifecycle::Log("preyvr_second_pass result=refused detail=unresolved");
        }
        return;
    }

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
        // A locked eye is held for as long as it is set, so a capture taken a
        // few frames later is unambiguously that eye no matter how the game and
        // render threads are phased. Alternating remains available, but nothing
        // should depend on identifying which frame it produced.
        const int locked = gEyeLock.load(std::memory_order_acquire);
        const int eye = (locked == 0 || locked == 1)
            ? locked
            : static_cast<int>(gEyeCounter.fetch_add(1, std::memory_order_relaxed) & 1ull);
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

    // Wall-clock stop alongside the frame budget, whichever comes first. Sized
    // generously against the budget so it only fires when frames have stopped
    // arriving, which is precisely the case the frame budget cannot see.
    const unsigned long long allowanceMs =
        2000ull + static_cast<unsigned long long>(frameBudget) * 40ull;
    gDoubleRenderDeadlineMs.store(GetTickCount64() + allowanceMs, std::memory_order_release);
    gDoubleRender.store(true, std::memory_order_release);

    bool expected = false;
    if (gDoubleRenderWatchdogRunning.compare_exchange_strong(expected, true)) {
        const HANDLE watchdog = CreateThread(nullptr, 0, DoubleRenderWatchdog, nullptr, 0, nullptr);
        if (watchdog == nullptr) {
            // Refuse to arm rather than run the risky mode with no stop. The
            // whole point of F-013 is that the in-band budget is not enough.
            gDoubleRenderWatchdogRunning.store(false, std::memory_order_release);
            gDoubleRender.store(false, std::memory_order_release);
            gDoubleRenderBudget.store(0, std::memory_order_release);
            gStereoIpd.store(0.0f, std::memory_order_release);
            lifecycle::Log("preyvr_camera_edit result=refused detail=watchdog_thread_failed");
            return static_cast<DWORD>(CameraEditStatus::failed);
        }
        CloseHandle(watchdog);
    }

    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=double_render_armed frameBudget=" << frameBudget
         << " deadlineMs=" << allowanceMs;
    lifecycle::Log(line.str());
    return static_cast<DWORD>(CameraEditStatus::armed);
}

// Resolves the pass constructor behind its own gate, separately from EnsureHook.
//
// Deliberately not folded into EnsureHook: a mismatch here must block only A4,
// not the camera edit and alternating stereo that already work. The bytes come
// from the landmark table rather than a second copy in this file, so there is one
// definition of what this function is expected to look like.
bool EnsureSecondPassTargets()
{
    if (gCreatePassInfo.load(std::memory_order_acquire) != nullptr) {
        return true;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);

    for (const auto& landmark : engine::Landmarks()) {
        if (landmark.id != "pass.create_general") {
            continue;
        }
        if (landmark.rva != kCreatePassInfoRva) {
            lifecycle::Log("preyvr_second_pass result=unavailable detail=landmark_rva_disagrees");
            return false;
        }
        const auto address = base + landmark.rva;
        if (!PrologueMatches(address, landmark.expected.data(), landmark.expected.size())) {
            lifecycle::Log("preyvr_second_pass result=unavailable detail=create_pass_prologue");
            return false;
        }
        gCreatePassInfo.store(reinterpret_cast<CreatePassInfoFn>(address),
                              std::memory_order_release);
        return true;
    }
    lifecycle::Log("preyvr_second_pass result=unavailable detail=landmark_missing");
    return false;
}

DWORD SetSecondPassStereo(float ipdMetres, float halfFovDegrees, unsigned int frameBudget,
                          bool zeroCameraDelta)
{
    if (ipdMetres == 0.0f || frameBudget == 0) {
        std::unique_lock lock(gMutex, kControlLockTimeout);
        gSecondPass.store(false, std::memory_order_release);
        gSecondPassBudget.store(0, std::memory_order_release);
        gStereoIpd.store(0.0f, std::memory_order_release);
        gSecondPassStatus.store(static_cast<DWORD>(SecondPassStatus::idle),
                                std::memory_order_release);
        lifecycle::Log("preyvr_second_pass result=0 detail=disarmed");
        return gStatus.load(std::memory_order_acquire);
    }

    if (frameBudget > 600) {
        lifecycle::Log("preyvr_second_pass result=refused detail=budget_too_large");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }

    // Reuses the synthetic-stereo bounds check, which also installs the hook.
    const DWORD armed = SetSyntheticStereo(ipdMetres, halfFovDegrees);
    if (armed != static_cast<DWORD>(CameraEditStatus::armed)) {
        return armed;
    }
    if (!EnsureSecondPassTargets()) {
        gStereoIpd.store(0.0f, std::memory_order_release);
        gSecondPassStatus.store(static_cast<DWORD>(SecondPassStatus::refusedUnresolved),
                                std::memory_order_release);
        return static_cast<DWORD>(CameraEditStatus::unavailable);
    }

    std::unique_lock lock(gMutex, kControlLockTimeout);
    if (!lock.owns_lock()) {
        lifecycle::Log("preyvr_second_pass result=refused detail=busy");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }
    gSecondPassBudget.store(frameBudget, std::memory_order_release);
    gSecondPassZeroDelta.store(zeroCameraDelta, std::memory_order_release);

    // Same wall-clock stop as the double render, for the same F-013 reason.
    const unsigned long long allowanceMs =
        2000ull + static_cast<unsigned long long>(frameBudget) * 40ull;
    gDoubleRenderDeadlineMs.store(GetTickCount64() + allowanceMs, std::memory_order_release);
    gSecondPass.store(true, std::memory_order_release);
    gSecondPassStatus.store(static_cast<DWORD>(SecondPassStatus::armed), std::memory_order_release);

    bool expected = false;
    if (gDoubleRenderWatchdogRunning.compare_exchange_strong(expected, true)) {
        const HANDLE watchdog = CreateThread(nullptr, 0, DoubleRenderWatchdog, nullptr, 0, nullptr);
        if (watchdog == nullptr) {
            gDoubleRenderWatchdogRunning.store(false, std::memory_order_release);
            gSecondPass.store(false, std::memory_order_release);
            gSecondPassBudget.store(0, std::memory_order_release);
            gStereoIpd.store(0.0f, std::memory_order_release);
            lifecycle::Log("preyvr_second_pass result=refused detail=watchdog_thread_failed");
            return static_cast<DWORD>(CameraEditStatus::failed);
        }
        CloseHandle(watchdog);
    }

    std::ostringstream line;
    line << "preyvr_second_pass result=0 detail=armed frameBudget=" << frameBudget
         << " zeroCameraDelta=" << (zeroCameraDelta ? 1 : 0)
         << " deadlineMs=" << allowanceMs << " ipd=" << ipdMetres;
    lifecycle::Log(line.str());
    return static_cast<DWORD>(CameraEditStatus::armed);
}

unsigned long long SecondPassFrameCount()
{
    return gSecondPassFrames.load(std::memory_order_acquire);
}

DWORD SecondPassStatusValue()
{
    return gSecondPassStatus.load(std::memory_order_acquire);
}

DWORD ProbeRenderViews()
{
    // The probe runs inside the render hook, so the hook has to exist. Arming it
    // without this left the flag set and the status stuck at notRun on a live
    // host -- indistinguishable from "ran and found nothing" until the log showed
    // the hook had never been installed.
    if (!EnsureHook()) {
        gRenderViewProbeStatus.store(static_cast<DWORD>(RenderViewProbeStatus::unresolved),
                                     std::memory_order_release);
        lifecycle::Log("preyvr_render_view_probe result=unresolved detail=hook_unavailable");
        return static_cast<DWORD>(CameraEditStatus::unavailable);
    }
    gRenderViewProbeStatus.store(static_cast<DWORD>(RenderViewProbeStatus::notRun),
                                 std::memory_order_release);
    gProbeRenderViews.store(true, std::memory_order_release);
    lifecycle::Log("preyvr_render_view_probe result=0 detail=armed");
    return gStatus.load(std::memory_order_acquire);
}

DWORD RenderViewProbeStatusValue()
{
    return gRenderViewProbeStatus.load(std::memory_order_acquire);
}

unsigned long long DoubleRenderedFrameCount()
{
    return gDoubleRendered.load(std::memory_order_acquire);
}

DWORD SetStereoAsymmetry(float outerScale)
{
    if (!std::isfinite(outerScale) || outerScale < 1.0f || outerScale > 1.5f) {
        lifecycle::Log("preyvr_camera_edit result=refused detail=asymmetry_out_of_bounds");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }
    gAsymmetry.store(outerScale, std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=asymmetry outerScale=" << outerScale;
    lifecycle::Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
}

DWORD SetStereoEyeLock(unsigned int eye)
{
    const int value = (eye == 0u || eye == 1u) ? static_cast<int>(eye) : -1;
    gEyeLock.store(value, std::memory_order_release);
    // The capture tag follows the lock, so it is correct again whenever one is
    // held -- and meaningless, by design, when alternating.
    SetFrameCaptureTagOverride(value);
    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=eye_lock eye="
         << (value < 0 ? "alternate" : (value == 0 ? "left" : "right"));
    lifecycle::Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
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
