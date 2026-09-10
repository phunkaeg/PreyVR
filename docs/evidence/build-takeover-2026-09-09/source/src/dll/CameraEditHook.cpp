#include "CameraEditHook.h"

#include "XrSessionHost.h"
#include "MinHookInit.h"

#include "InputPost.h"
#include "HudBridge.h"
#include "AimTakeover.h"
#include "MoveLane.h"

#include "HeadTrackingHook.h"

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

// A small ring of recently built eyes, matched by camera position. Sized well
// past the game/render thread separation so a view in flight can still find the
// record that produced it.
constexpr unsigned int kBuiltEyeSlots = 16;
std::array<BuiltEye, kBuiltEyeSlots> gBuiltEyes{};
std::atomic<unsigned long long> gBuiltEyeSerial{0};
std::atomic<unsigned long long> gBuiltEyeCount{0};
std::atomic<unsigned long long> gBuiltEyeMisses{0};
std::atomic<int> gEyeLock{-1};   // -1 = alternate
std::atomic<float> gAsymmetry{1.1f};

// Rung 3: inherit Prey's own projection instead of overwriting it.
//
// **Why this exists.** Every eye camera below this point was built by replacing
// the engine's fov, projection ratio and asymmetry with synthetic values. That
// was the right thing to do while the real frustum was unmeasured -- a synthetic
// half-FOV exercises the asymmetry path, which is exactly what A2b needed.
//
// It is the wrong thing to do once the frustum is known, and as of 2026-09-02 it
// is known: Prey renders at tangents -1.732051/+1.732051 horizontally and
// -0.974279/+0.974279 vertically with all four asymmetry fields zero, confirmed
// against the player's own 120-degree FOV slider.
//
// PreyVR is injected and cannot change what Prey renders. Its pixels come from
// the game's frustum, so that frustum is what must be declared to OpenXR. If the
// eye camera's projection has been overwritten with a synthetic one, **the image
// no longer matches the declaration** -- and a mismatch between what is drawn and
// what is claimed is not visible on a monitor, only inside a headset, where it
// reads as warped depth rather than as a bug.
//
// So when this is set the eye camera differs from the engine's by translation
// alone. Parallax comes from the eye offset, which is the only thing A2b actually
// measured, and the projection is left exactly as the engine built it.
//
// Off by default: leaving it off keeps every prior measurement meaning what it
// meant when it was taken.
std::atomic<bool> gNativeProjection{false};
// **Render the frustum the headset actually asked for.** Measured on this
// machine: only 41.25% of the submitted pixel rectangle falls inside the
// runtime's requested frustum, with nothing missing (R-121). The other 59% is
// rasterised and then discarded by the compositor.
//
// Off by default and deliberately so. This changes the scene projection, and
// the near pass carries its own FOV (`r_DrawNearFoV`) that was tuned against the
// old one -- so enabling this without matching the near pass leaves the weapon
// model at the wrong scale. It is a prototype behind an opt-in, not a default.
std::atomic<bool> gRuntimeFrustum{false};
std::atomic<unsigned long long> gRuntimeFrustumUsed{0}, gRuntimeFrustumMissing{0};

// Head rotation on the upstream camera -- the one culling reads from.
std::atomic<bool> gHeadRotationArmed{false};
std::atomic<unsigned long long> gHeadRotationApplied{0};
std::atomic<unsigned long long> gHeadRotationRefused{0};

// The forward axis the edit last wrote, for the pass-camera probe to compare
// against. Three floats rather than a matrix: the question is only which way the
// camera faces, and a direction is cheap to publish without a lock.
std::atomic<float> gWrittenForwardX{0.0f};
std::atomic<float> gWrittenForwardY{0.0f};
std::atomic<float> gWrittenForwardZ{0.0f};
std::atomic<bool> gHaveWrittenForward{false};

// Leave the head rotation on the camera instead of restoring it, so the next
// frame's occlusion job sees it. Off by default: it changes what every reader of
// the global camera sees, which is a bigger commitment than a bounded edit.
std::atomic<bool> gKeepHeadRotation{false};
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
std::atomic<bool> gSecondPassMarkSecondary{true};

std::atomic<unsigned long long> gSecondPassFrameIdMoved{0};
std::atomic<bool> gLoggedFrameIdMove{false};

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
// A6: the interpose shape. Hooks RenderWorld itself rather than appending after
// CSystem::Render, so both eyes are drawn before the HUD and present.
constexpr std::uintptr_t kRenderWorldRva = 0x21F520;
std::atomic<RenderWorldFn> gRenderWorldOriginal{nullptr};
void* gRenderWorldTarget = nullptr;
bool gRenderWorldHookCreated = false;
std::atomic<bool> gInterpose{false};
std::atomic<unsigned int> gInterposeBudget{0};
std::atomic<unsigned long long> gInterposeFrames{0};
std::atomic<bool> gInterposeActive{false};
std::atomic<bool> gLoggedFirstInterpose{false};

// A7: the Crysis VR shape. CSystem::RenderBegin between the two full renders.
constexpr std::uintptr_t kRenderBeginRva = 0xE0BD80;
using RenderBeginFn = void(__fastcall*)(void* system);
std::atomic<RenderBeginFn> gRenderBegin{nullptr};
std::atomic<bool> gFrameShape{false};
std::atomic<unsigned int> gFrameShapeBudget{0};
std::atomic<unsigned long long> gFrameShapeFrames{0};
std::atomic<bool> gLoggedFirstFrameShape{false};
// 0 shares everything (A6 as first run); 1 gives the second render its own
// recursive render view; 2 also marks it a secondary pass (R-073).
std::atomic<unsigned int> gInterposeMode{0};
std::atomic<std::uintptr_t> gPreyBase{0};

// CRenderView vtable slots that CreateGeneralPassRenderingInfo drives on the view
// it selected. They have to be re-driven on the recursive view after the swap,
// or the pass would carry state derived from the default view instead.
constexpr std::uintptr_t kRenderViewSetFlagsSlot = 0x20;
constexpr std::uintptr_t kRenderViewDerivedSlot = 0x58;
using RenderViewSetFlagsFn = void(__fastcall*)(void* view, std::uint32_t flags);
using RenderViewDerivedFn = void*(__fastcall*)(void* view);
using RegisterPassCameraFn = void*(__fastcall*)(void* engine, const void* camera);
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
// ---------------------------------------------------------------------------
// Eye handoff: which eye is in the frame the render thread is finishing
// ---------------------------------------------------------------------------
//
// **The problem this replaces.** The submission path used to identify the eye by
// asking and waiting: set the lock, hold it for N frames so the game thread's
// camera edit could reach the render thread, then take the image. That works,
// and it is why the first stereo test was unambiguous -- but the wait is the
// whole cost. A full left-right cycle took 2*N rendered frames, so at a dwell of
// 4 each eye refreshed at about 11 Hz. That is the slideshow.
//
// **Why a queue is correct here.** The engine's MT/RT double buffer *delays*
// work but does not *reorder* it: the camera built for frame N is rendered
// before the camera built for frame N+1. So the eye does not need to be
// searched for or waited on -- it only needs to be carried alongside the frame.
// The game thread pushes the eye it just built, the render thread pops one per
// finished frame, and identity is exact with no dwell at all. Each eye then
// refreshes every 2 frames instead of every 2*N.
//
// **Order is an assumption, so it is measured rather than trusted.** `Lag()` is
// pushes minus pops, which is the pipeline depth in frames. It should sit at a
// small constant. If it drifts, the 1:1 correspondence this relies on is not
// holding, and the caller can say so instead of quietly rendering the wrong eye
// into the wrong socket -- which looks like broken stereo rather than like a bug
// in a queue, and would be miserable to diagnose from inside a headset.
namespace eyehandoff {

constexpr std::size_t kSlots = 64;   // power of two, so the wrap is a mask
std::array<std::atomic<int>, kSlots> gSlots{};
std::atomic<unsigned long long> gPushed{0};
std::atomic<unsigned long long> gPopped{0};
std::atomic<unsigned long long> gStarved{0};
std::atomic<unsigned long long> gDropped{0};

void Publish(int eye)
{
    const unsigned long long seq = gPushed.load(std::memory_order_relaxed);
    gSlots[seq & (kSlots - 1)].store(eye, std::memory_order_relaxed);
    gPushed.store(seq + 1, std::memory_order_release);
}

// Returns the eye for the frame just finished, or -1 if nothing is queued.
//
// If the queue has run long -- the render thread fell behind and the game thread
// kept going -- the stale entries are discarded rather than shown. An old eye is
// worse than a repeated one: it is a frame from a different camera position, so
// it reads as a jolt rather than as a dropped update.
int Consume()
{
    const unsigned long long pushed = gPushed.load(std::memory_order_acquire);
    unsigned long long popped = gPopped.load(std::memory_order_relaxed);
    if (popped >= pushed) {
        gStarved.fetch_add(1, std::memory_order_relaxed);
        return -1;
    }
    // Keep at most a couple of frames of slack; skip the rest.
    constexpr unsigned long long kMaxLag = 4;
    if (pushed - popped > kMaxLag) {
        const unsigned long long skip = (pushed - popped) - kMaxLag;
        gDropped.fetch_add(skip, std::memory_order_relaxed);
        popped += skip;
    }
    const int eye = gSlots[popped & (kSlots - 1)].load(std::memory_order_relaxed);
    gPopped.store(popped + 1, std::memory_order_release);
    return eye;
}

void Reset()
{
    gPushed.store(0, std::memory_order_relaxed);
    gPopped.store(0, std::memory_order_relaxed);
    gStarved.store(0, std::memory_order_relaxed);
    gDropped.store(0, std::memory_order_relaxed);
}

} // namespace eyehandoff

// What the eye camera we just handed the renderer actually projects with.
//
// **This is the third quantity, and nothing outside the process can see it.**
// xr-tape's own CHECKS.md says so: it can observe the FOV the runtime located and
// the FOV we declared, but "the engine's own projection matrix never crosses the
// OpenXR boundary, so no API layer can see it". The mod is the only place all
// three coexist, so the comparison has to live here.
//
// Published from the one function every path uses to build an eye, so a route
// that skips it cannot silently skip the check with it.
std::atomic<float> gRenderedTanLeft{0.0f};
std::atomic<float> gRenderedTanRight{0.0f};
std::atomic<float> gRenderedTanUp{0.0f};
std::atomic<float> gRenderedTanDown{0.0f};
std::atomic<bool> gRenderedTangentsValid{false};

void PublishRenderedTangents(
    const std::array<std::uint8_t, cameraedit::kCameraSize>& edited)
{
    const auto view = stereoframe::TangentsFromCamera(
        std::span<const std::uint8_t>(edited.data(), engine::CameraLayout::size));
    if (!view) {
        gRenderedTangentsValid.store(false, std::memory_order_release);
        return;
    }
    gRenderedTanLeft.store(view->tanLeft, std::memory_order_relaxed);
    gRenderedTanRight.store(view->tanRight, std::memory_order_relaxed);
    gRenderedTanUp.store(view->tanUp, std::memory_order_relaxed);
    gRenderedTanDown.store(view->tanDown, std::memory_order_relaxed);
    gRenderedTangentsValid.store(true, std::memory_order_release);
}

// The six fields Prey's CCamera needs to carry an asymmetric frustum. Shared by
// both replacement paths so they cannot drift: a projection written one way in
// one branch and another way in the other is a bug that only appears in the
// branch nobody is testing.
void WriteEyeProjection(std::array<std::uint8_t, cameraedit::kCameraSize>& edited,
                        const stereoframe::EyeProjection& projection)
{
    const auto write = [&edited](std::size_t offsetBytes, float value) {
        std::memcpy(edited.data() + offsetBytes, &value, sizeof(float));
    };
    write(engine::CameraLayout::fov, projection.fov);
    write(engine::CameraLayout::projectionRatio, projection.projectionRatio);
    write(engine::CameraLayout::asymLeft, projection.asymmetry.left);
    write(engine::CameraLayout::asymRight, projection.asymmetry.right);
    write(engine::CameraLayout::asymBottom, projection.asymmetry.bottom);
    write(engine::CameraLayout::asymTop, projection.asymmetry.top);
}

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

    // Translation only, and nothing else touched. The engine's fov, projection
    // ratio and asymmetry stay exactly as it built them, so the rendered frustum
    // is still the one TangentsFromCamera reads and declares.
    // **Ahead of the native check on purpose.** Native means "keep Prey's
    // frustum"; this means "use the headset's". Both cannot hold, and silently
    // preferring native would make the opt-in look enabled while doing nothing --
    // the exact failure shape as a Scaleform call to a function that is not there.
    if (gRuntimeFrustum.load(std::memory_order_acquire)) {
        float left = 0, right = 0, up = 0, down = 0;
        if (XrRequestedEyeFov(eye, &left, &right, &up, &down)) {
            stereoframe::EyeView view{};
            view.tanLeft = std::tan(left);
            view.tanRight = std::tan(right);
            view.tanDown = std::tan(down);
            view.tanUp = std::tan(up);
            const float nearPlane = stereoframe::NearPlaneOf(edited);
            const auto projection = stereoframe::ProjectionFromTangents(view, nearPlane);
            if (projection) {
                WriteEyeProjection(edited, *projection);
                PublishRenderedTangents(edited);
                gRuntimeFrustumUsed.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
        // **Falls through to the existing behaviour rather than guessing.** No
        // located view yet, or a degenerate one, means we do not know the
        // headset's frustum -- and rendering an invented one would be submitted
        // as though it were the headset's own. Counted, so a mode that never
        // actually engaged cannot read as one that did.
        gRuntimeFrustumMissing.fetch_add(1, std::memory_order_relaxed);
    }

    if (gNativeProjection.load(std::memory_order_acquire)) {
        // Prey's own frustum, unchanged -- so this should match what the
        // submission path declares. Published anyway rather than assumed: an
        // assert that only runs on the path you suspect proves nothing about the
        // path you trust.
        PublishRenderedTangents(edited);
        return true;
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

    WriteEyeProjection(edited, *projection);
    // The synthetic path REPLACED the projection. This is the case no external
    // tool can catch: the declaration still reports Prey's frustum while the
    // pixels came from this one. Measured 2026-09-05 as wall-eyed divergence,
    // horizontal stretch, and a sun that swung with head yaw -- one cause.
    PublishRenderedTangents(edited);
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
using GetFrameIdFn = std::uint32_t(__fastcall*)(void*, int);

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
    // **Mark it secondary before anything else (R-073).** RenderWorld and its
    // dispatch guard about nine once-per-frame sites on this byte being zero:
    // the frame counter, UpdateRenderingCamera, the default-material setup, the
    // CVar snapshot, the occlusion and bbox update. Setting it makes the engine
    // skip its own per-frame work and render only the world -- which is exactly
    // what a second eye needs, and is the engine's own mechanism rather than a
    // gate we invented.
    Step("second_pass:mark_secondary");
    const bool markSecondary = gSecondPassMarkSecondary.load(std::memory_order_acquire);
    if (markSecondary) {
        passInfo[engine::PassInfoLayout::secondaryPassFlag] = 1;
    }

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

    // **The side-effect counter (BN-SFX-001).** Read the renderer's own frame ids
    // either side of the call. If issuing a second RenderWorld advances
    // renderer-side per-frame bookkeeping, these move -- and a pass that advances
    // bookkeeping is a pass that may be advancing simulation, particles and audio
    // with it. Zero deltas do not prove the absence of every side effect, but a
    // non-zero delta is a positive detection, which is what a gate needs.
    const auto getFrameId =
        *reinterpret_cast<GetFrameIdFn*>(rendererVtable + engine::RendererLayout::vtableGetFrameId);
    std::uint32_t beforeA = 0;
    std::uint32_t beforeB = 0;
    if (getFrameId != nullptr) {
        beforeA = getFrameId(renderer, 1);
        beforeB = getFrameId(renderer, 0);
    }

    Step("second_pass:render_world");
    renderWorld(process, engine::PassInfoLayout::renderWorldFlags, passInfo.data(),
                "PreyVR::SecondPass");
    Step("second_pass:done");

    if (getFrameId != nullptr) {
        const std::uint32_t afterA = getFrameId(renderer, 1);
        const std::uint32_t afterB = getFrameId(renderer, 0);
        if (afterA != beforeA || afterB != beforeB) {
            gSecondPassFrameIdMoved.fetch_add(1, std::memory_order_relaxed);
            bool loggedMove = false;
            if (gLoggedFrameIdMove.compare_exchange_strong(loggedMove, true)) {
                std::ostringstream moved;
                moved << "preyvr_second_pass result=0 detail=frame_id_advanced"
                      << " a=" << beforeA << "->" << afterA
                      << " b=" << beforeB << "->" << afterB;
                lifecycle::Log(moved.str());
            }
        }
    }

    const unsigned long long done = gSecondPassFrames.fetch_add(1, std::memory_order_relaxed) + 1;
    bool expected = false;
    if (gLoggedFirstSecondPass.compare_exchange_strong(expected, true)) {
        std::ostringstream line;
        line << "preyvr_second_pass result=0 detail=first_frame"
             << " zeroDelta=" << (zeroDelta ? 1 : 0)
             << " markSecondary=" << (markSecondary ? 1 : 0) << " slot=" << slot
             << " recursiveView=0x" << std::hex
             << reinterpret_cast<std::uintptr_t>(recursiveView) << std::dec
             << " done=" << done;
        lifecycle::Log(line.str());
    }
    return true;
}

// The A6 hook. Calls the original world render twice from inside itself, so the
// frame still contains exactly one CSystem::Render, one HUD draw and one present.
void __fastcall RenderWorldInterpose(void* process, int flags, void* passInfo,
                                     const char* debugName)
{
    const RenderWorldFn original = gRenderWorldOriginal.load(std::memory_order_acquire);
    if (original == nullptr) {
        return;
    }
    if (!gInterpose.load(std::memory_order_acquire)) {
        original(process, flags, passInfo, debugName);
        return;
    }

    // Re-entrancy guard, per the StereoRenderGuard prior art. The trampoline
    // does not route back through this hook, but a nested world render inside
    // the engine would, and doubling that would be unbounded.
    if (gInterposeActive.exchange(true, std::memory_order_acq_rel)) {
        original(process, flags, passInfo, debugName);
        return;
    }

    const unsigned int remaining = gInterposeBudget.load(std::memory_order_acquire);
    if (remaining == 0) {
        gInterpose.store(false, std::memory_order_release);
        gInterposeActive.store(false, std::memory_order_release);
        lifecycle::Log("preyvr_interpose result=0 detail=budget_exhausted");
        original(process, flags, passInfo, debugName);
        return;
    }
    gInterposeBudget.store(remaining - 1, std::memory_order_release);

    Step("interpose:eye0");
    original(process, flags, passInfo, debugName);

    // Mode 0 repeats the call with the game's own pass info -- the first A6 run,
    // which reached 13 frames and then deadlocked inside this second call.
    //
    // Modes 1 and 2 test the exhaustion hypothesis from F-015: if the deadlock is
    // contention over per-frame render resources, giving the second render its
    // **own** view is what separates them. The pass info is copied byte for byte
    // from the game's and only the view pointer is changed, so exactly one
    // variable moves. Mode 2 additionally marks it secondary (R-073).
    const unsigned int mode = gInterposeMode.load(std::memory_order_acquire);
    std::array<std::uint8_t, engine::PassInfoLayout::size> ownPass{};
    void* secondPassInfo = passInfo;
    if (mode > 0 && passInfo != nullptr) {
        Step("interpose:build_own_pass");
        std::memcpy(ownPass.data(), passInfo, engine::PassInfoLayout::size);

        // Mode 3 keeps the primary view deliberately: the workflow's decompilation
        // of the dispatch shows the per-frame prepare FUN_1802114D0 runs only when
        // +0x01 == 0, so marking the SECOND pass secondary makes prepare run once
        // per frame instead of twice -- which is the 13-19 frame leak -- while the
        // shared primary view is the one that prepare just prepared. Mode 1 failed
        // precisely because its own recursive view had nothing prepare it.
        const bool wantRecursiveView = (mode == 1 || mode == 2);
        const bool wantSecondaryFlag = (mode == 2 || mode == 3 || mode == 4);
        // Mode 4 = mode 3 plus a real per-eye camera on the second pass.
        //
        // **This is the test that mode 3 could not be.** Mode 3 ran 600 frames
        // and cost 7% uncapped (301 -> 280 fps), which is far too cheap for a
        // real world render -- but both its passes used an identical camera, so
        // the picture looked the same whether the second pass drew or not. Give
        // the second pass a different camera and the image itself answers it:
        // changed means it renders, identical means it is hollow.
        const bool wantEyeCamera = (mode == 4);
        if (wantSecondaryFlag) {
            ownPass[engine::PassInfoLayout::secondaryPassFlag] = 1;
        }
        secondPassInfo = ownPass.data();

        if (wantEyeCamera) {
            Step("interpose:build_eye_camera");
            const std::uintptr_t preyBase = gPreyBase.load(std::memory_order_acquire);
            const UpdateFrustumFn updateFrustum = gUpdateFrustum.load(std::memory_order_acquire);
            auto* const systemPtr = preyBase != 0
                ? *reinterpret_cast<std::uint8_t**>(preyBase + engine::SystemLayout::pointerRva)
                : nullptr;
            // process IS gEnv->p3DEngine: R-059 measured CSystem::m_pProcess equal
            // to it, so IProcess is C3DEngine's primary base and needs no adjust.
            auto* const engineVtable = *reinterpret_cast<std::uint8_t**>(process);
            if (systemPtr != nullptr && updateFrustum != nullptr && engineVtable != nullptr) {
                const auto* const liveCamera = reinterpret_cast<const std::uint8_t*>(
                    reinterpret_cast<std::uintptr_t>(systemPtr) + engine::SystemLayout::viewCamera);
                static thread_local std::array<std::uint8_t, cameraedit::kCameraSize> eyeCamera{};
                std::memcpy(eyeCamera.data(), liveCamera, cameraedit::kCameraSize);
                if (BuildSyntheticEye(eyeCamera, 1, gStereoIpd.load(std::memory_order_acquire),
                                      gStereoHalfFov.load(std::memory_order_acquire))) {
                    updateFrustum(eyeCamera.data());
                    if (cameraedit::RotationIsSafeToWrite(eyeCamera)) {
                        const auto registerCamera = *reinterpret_cast<RegisterPassCameraFn*>(
                            engineVtable + engine::ThreeDEngineLayout::vtableRegisterPassCamera);
                        if (registerCamera != nullptr) {
                            void* const registered = registerCamera(process, eyeCamera.data());
                            std::memcpy(ownPass.data() + engine::PassInfoLayout::camera,
                                        &registered, sizeof(registered));
                        }
                    }
                }
            }
        }

        const std::uintptr_t base = wantRecursiveView
            ? gPreyBase.load(std::memory_order_acquire) : 0;
        auto* const renderer = base != 0
            ? *reinterpret_cast<std::uint8_t**>(
                  base + engine::GlobalEnvironmentLayout::baseRva +
                  engine::GlobalEnvironmentLayout::renderer)
            : nullptr;
        auto* const rendererVtable = renderer != nullptr
            ? *reinterpret_cast<std::uint8_t**>(renderer) : nullptr;
        if (rendererVtable != nullptr) {
            const auto getView = *reinterpret_cast<GetRenderViewFn*>(
                rendererVtable + engine::RendererLayout::vtableGetRenderViewForThread);
            const int slot = ownPass[engine::PassInfoLayout::threadSlot];
            void* const recursiveView = getView != nullptr
                ? getView(renderer, slot, engine::RendererLayout::viewTypeRecursive) : nullptr;
            if (recursiveView != nullptr) {
                std::memcpy(ownPass.data() + engine::PassInfoLayout::renderView, &recursiveView,
                            sizeof(recursiveView));
                // Re-drive what the constructor drove on the view it picked.
                auto* const viewVtable = *reinterpret_cast<std::uint8_t**>(recursiveView);
                if (viewVtable != nullptr) {
                    std::uint32_t passFlags = 0;
                    std::memcpy(&passFlags, ownPass.data() + engine::PassInfoLayout::flags,
                                sizeof(passFlags));
                    const auto setFlags = *reinterpret_cast<RenderViewSetFlagsFn*>(
                        viewVtable + kRenderViewSetFlagsSlot);
                    if (setFlags != nullptr) {
                        setFlags(recursiveView, passFlags);
                    }
                    const auto derived = *reinterpret_cast<RenderViewDerivedFn*>(
                        viewVtable + kRenderViewDerivedSlot);
                    if (derived != nullptr) {
                        void* const value = derived(recursiveView);
                        std::memcpy(ownPass.data() + engine::PassInfoLayout::renderViewDerived,
                                    &value, sizeof(value));
                    }
                }
            }
        }
    }

    Step("interpose:eye1");
    original(process, flags, secondPassInfo,
             secondPassInfo == passInfo ? debugName : "PreyVR::Eye1");
    Step("interpose:done");

    const unsigned long long done = gInterposeFrames.fetch_add(1, std::memory_order_relaxed) + 1;
    bool expected = false;
    if (gLoggedFirstInterpose.compare_exchange_strong(expected, true)) {
        std::ostringstream line;
        line << "preyvr_interpose result=0 detail=first_frame mode=" << mode
             << " ownView=" << (secondPassInfo == passInfo ? 0 : 1) << " flags=" << flags
             << " debugName=\"" << (debugName != nullptr ? debugName : "?") << "\""
             << " done=" << done;
        lifecycle::Log(line.str());
    }
    gInterposeActive.store(false, std::memory_order_release);
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
           gSecondPass.load(std::memory_order_acquire) ||
           gInterpose.load(std::memory_order_acquire) ||
           gFrameShape.load(std::memory_order_acquire)) {
        const unsigned long long deadline = gDoubleRenderDeadlineMs.load(std::memory_order_acquire);
        if (deadline != 0 && GetTickCount64() >= deadline) {
            gDoubleRender.store(false, std::memory_order_release);
            gDoubleRenderBudget.store(0, std::memory_order_release);
            gSecondPass.store(false, std::memory_order_release);
            gSecondPassBudget.store(0, std::memory_order_release);
            gInterpose.store(false, std::memory_order_release);
            gInterposeBudget.store(0, std::memory_order_release);
            gFrameShape.store(false, std::memory_order_release);
            gFrameShapeBudget.store(0, std::memory_order_release);
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

    // A7: the Crysis VR shape. Two full renders with RenderBegin between them.
    if (gFrameShape.load(std::memory_order_acquire)) {
        const RenderBeginFn renderBegin = gRenderBegin.load(std::memory_order_acquire);
        const unsigned int remaining = gFrameShapeBudget.load(std::memory_order_acquire);
        if (remaining == 0 || renderBegin == nullptr || original == nullptr) {
            gFrameShape.store(false, std::memory_order_release);
            lifecycle::Log("preyvr_frame_shape result=0 detail=budget_exhausted");
            if (original != nullptr) {
                original(system);
            }
            return;
        }
        gFrameShapeBudget.store(remaining - 1, std::memory_order_release);

        Step("frame_shape:eye0");
        original(system);
        // The line the Crysis VR source calls not optional. Without it the second
        // pass inherits state the first left behind, and culling breaks first.
        Step("frame_shape:render_begin");
        renderBegin(system);
        Step("frame_shape:eye1");
        original(system);
        Step("frame_shape:done");

        const unsigned long long done =
            gFrameShapeFrames.fetch_add(1, std::memory_order_relaxed) + 1;
        bool expectedShape = false;
        if (gLoggedFirstFrameShape.compare_exchange_strong(expectedShape, true)) {
            std::ostringstream line;
            line << "preyvr_frame_shape result=0 detail=first_frame done=" << done;
            lifecycle::Log(line.str());
        }
        return;
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
    // Head rotation is an arming condition in its own right. Without it here the
    // edit is armed, reports success, and never runs -- which is exactly what
    // happened on the first upstream run, and cost a test cycle to find because
    // "armed" and "applied" were only distinguishable by a counter.
    // **Queued input is drained here, not on the thread that produced it.**
    // `CSystem::Render` is the engine's main thread and, unlike the gameplay
    // camera callback, it still runs while a menu is up -- which is exactly the
    // case menu navigation needs. Deliberately ahead of the armed check below:
    // driving a menu must not require stereo or head tracking to be enabled
    // first, and the early return would otherwise skip it.
    DrainQueuedInput();
    DrainQueuedHudCalls();
    // Same thread and same frame as the drain: the lane produces at most two
    // axis events per frame and the drain consumes one, so producing anywhere
    // else would race the queue it feeds.
    UpdateMoveLane();
    UpdateTurnAndFireLanes();

    if ((!gArmed.load(std::memory_order_acquire) && !stereoArmed &&
         !gHeadRotationArmed.load(std::memory_order_acquire)) || system == nullptr) {
        if (original != nullptr) {
            UpdateAimReticleForRender();
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
                UpdateAimReticleForRender();
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

    // **Head rotation goes here, upstream, because this is the camera the engine
    // culls from.** Driving it from CRenderView::SetCamera was measured on
    // 2026-09-03 and broke level culling: that seam is downstream of visibility,
    // so the engine culled against an unrotated camera and then rendered through
    // a rotated one. The playbook's rule is to keep the RenderView override for
    // stereo and projection and never for CPU culling.
    //
    // Applied **before** the eye offset, so the offset is taken along the head's
    // own right axis rather than the body's. Reversed, the eyes would separate
    // along a fixed world axis and the stereo would shear as the player looked
    // around.
    //
    // A refusal here is not fatal: the camera is simply left as the engine built
    // it for this frame, which is a frame without head tracking rather than a
    // broken one.
    bool headRotationApplied = false;
    if (gHeadRotationArmed.load(std::memory_order_acquire)) {
        headRotationApplied = ApplyHeadRotation(edited.data(), edited.size());
        if (!headRotationApplied) {
            gHeadRotationRefused.fetch_add(1, std::memory_order_relaxed);
        } else {
            gHeadRotationApplied.fetch_add(1, std::memory_order_relaxed);
        }
    }

    bool built = headRotationApplied;
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
            // Publish the eye WITH the camera it produced, so a downstream
            // consumer can identify it from content rather than from the
            // mutable global below. H-022: the global is a frame ahead of the
            // render thread and every tag it carries is individually valid.
            {
                const stereo::Matrix34 m = stereo::ReadMatrix(edited);
                BuiltEye record{};
                record.position[0] = m[3];  record.position[1] = m[7];  record.position[2] = m[11];
                record.right[0] = m[0];     record.right[1] = m[4];     record.right[2] = m[8];
                record.eye = eye;
                record.serial = gBuiltEyeSerial.fetch_add(1, std::memory_order_relaxed) + 1;
                gBuiltEyes[record.serial % kBuiltEyeSlots] = record;
                gBuiltEyeCount.store(record.serial, std::memory_order_release);
            }
            gLastEye.store(eye, std::memory_order_release);
            // Stamp the capture so the two dumps of a pair cannot be confused.
            SetFrameCaptureTagOverride(eye);
            // Hand the eye to the render thread alongside the frame it belongs
            // to, so submission never has to wait to find out which one it got.
            eyehandoff::Publish(eye);
        }
    } else if (!headRotationApplied) {
        // The research yaw edit is an *alternative* to head tracking, not
        // something to compose with it. ApplyYaw rotates the matrix in place, so
        // running it here would add a fixed offset on top of the live head
        // orientation -- harmless while the yaw is zero, and a slowly-rotating
        // world the moment it is not.
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

    UpdateAimReticleForRender();

    // Publish the forward axis we just wrote, so the pass-camera probe can ask
    // whether the camera the engine culls with is this one.
    {
        const stereo::Matrix34 written = stereo::ReadMatrix(edited);
        gWrittenForwardX.store(written[1], std::memory_order_relaxed);
        gWrittenForwardY.store(written[5], std::memory_order_relaxed);
        gWrittenForwardZ.store(written[9], std::memory_order_relaxed);
        gHaveWrittenForward.store(true, std::memory_order_release);
    }

    if (original != nullptr) {
        original(system);
    }

    // **Head rotation is deliberately left in place; everything else is restored.**
    //
    // The occlusion job is spawned from `GetViewCamera()` in the frame's update,
    // *before* CSystem::Render is called at all, so a rotation that is written
    // during Render and undone before returning is never visible to culling. That
    // is the whole reason the cull frustum followed the mouse while the pass
    // camera agreed with us to zero millidegrees.
    //
    // Leaving it means the next frame's occlusion job reads a camera carrying the
    // previous frame's head rotation. One frame stale, which for a *cull* frustum
    // is cheap: it decides what to submit, and a frame of lag on that shows as
    // slightly-wrong edges during a fast turn rather than as missing rooms.
    //
    // **This is a real behavioural change, not just a timing one.** A rotation
    // left on the global camera is visible to every reader of it, including the
    // cached aim ray, so aim will follow the head. That is a different tradeoff
    // from the transient per-eye offset, which was restored precisely because no
    // engine expects it -- a rotated view camera is ordinary, and is what those
    // readers see whenever the player turns with a mouse.
    //
    // The alternative is a bounded window: write before the occlusion job and
    // restore after Render. That needs the address of Prey's PrepareOcclusion
    // call, which the static hunt has not yet produced.
    const bool keepRotation =
        headRotationApplied && gKeepHeadRotation.load(std::memory_order_acquire);

    // **Restore everything, unconditionally.** The previous form skipped the
    // restore whenever the rotation was being kept, which left the *eye
    // translation and the projection* on the global camera too -- not what the
    // paragraph above describes, and precisely the shape of FAIL-HAND-037: a
    // half-IPD translation escaping into consumers that have no idea it is
    // there, alternating every frame. Latent only because the flag defaults off.
    std::memcpy(camera, restore.bytes.data(), cameraedit::kCameraSize);

    // Verified, not assumed. A restore that is merely performed is a hope. This
    // now runs on every path, including the keep-rotation one, because the
    // comparison happens *before* the basis is deliberately put back.
    if (!cameraedit::MatchesRestorePoint(live, restore)) {
        gRestoreFailures.fetch_add(1, std::memory_order_relaxed);
        gArmed.store(false, std::memory_order_release);
        gStereoIpd.store(0.0f, std::memory_order_release);
        SetFrameCaptureTagOverride(-1);
        lifecycle::Log("preyvr_camera_edit result=restore_failed detail=disarmed");
        return;
    }

    if (keepRotation) {
        // Put back the **basis only**: columns 0-2 of the 3x4 are the axes,
        // column 3 is the translation, which stays exactly as the engine left
        // it. Built in our own memory and validated before it touches game
        // state, the same discipline the edit itself uses -- and the frustum is
        // recomputed so the struct we leave behind is self-consistent rather
        // than carrying planes belonging to a different orientation.
        std::array<std::uint8_t, cameraedit::kCameraSize> kept{};
        std::memcpy(kept.data(), restore.bytes.data(), cameraedit::kCameraSize);
        const stereo::Matrix34 rotated = stereo::ReadMatrix(edited);
        stereo::Matrix34 keep = stereo::ReadMatrix(kept);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                keep[row * 4 + col] = rotated[row * 4 + col];
            }
        }
        if (stereo::WriteMatrix(kept, keep)) {
            updateFrustum(kept.data());
            if (cameraedit::RotationIsSafeToWrite(kept)) {
                std::memcpy(camera, kept.data(), cameraedit::kCameraSize);
            } else {
                lifecycle::Log(
                    "preyvr_camera_edit result=refused detail=keep_rotation_unsafe");
            }
        } else {
            lifecycle::Log("preyvr_camera_edit result=refused detail=keep_rotation_write_failed");
        }
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
    // Shared and idempotent: without it MH_CreateHook returns
    // MH_ERROR_NOT_INITIALIZED and the feature reports "unavailable" for a
    // reason unrelated to itself. See MinHookInit.h.
    EnsureMinHook();
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

DWORD EnsureRenderHookInstalled()
{
    // **Menu input needs this seam without arming a camera edit.** The drain for
    // queued input runs inside the `CSystem::Render` hook, and that hook was only
    // ever installed by the stereo and head-tracking paths -- so posting worked,
    // reported success, queued its events and never delivered one. The live run
    // showed `queued=4 posted=0 drainThread=0`: the counters found it, the return
    // codes did not.
    //
    // Installing without arming is safe by construction: with nothing armed the
    // hook forwards to the original after draining.
    return EnsureHook() ? 0u : 1u;
}

bool FindBuiltEyeByPosition(const float position[3], BuiltEye& out)
{
    const unsigned long long count = gBuiltEyeCount.load(std::memory_order_acquire);
    if (count == 0) { gBuiltEyeMisses.fetch_add(1, std::memory_order_relaxed); return false; }
    // Exact-ish: the render view holds a by-value copy of the camera we built,
    // so the bits should be identical. A small tolerance covers a copy that
    // passed through a float conversion without admitting a different eye --
    // the two eyes are an IPD apart, which is orders of magnitude wider.
    constexpr float kTolerance = 1e-4f;
    const unsigned long long newest = count;
    const unsigned long long oldest = newest > kBuiltEyeSlots ? newest - kBuiltEyeSlots + 1 : 1;
    for (unsigned long long serial = newest; serial >= oldest; --serial) {
        const BuiltEye& candidate = gBuiltEyes[serial % kBuiltEyeSlots];
        if (candidate.serial != serial) { continue; }
        if (std::fabs(candidate.position[0] - position[0]) <= kTolerance &&
            std::fabs(candidate.position[1] - position[1]) <= kTolerance &&
            std::fabs(candidate.position[2] - position[2]) <= kTolerance) {
            out = candidate;
            return true;
        }
        if (serial == 1) { break; }
    }
    gBuiltEyeMisses.fetch_add(1, std::memory_order_relaxed);
    return false;
}

unsigned long long BuiltEyeLookupMissCount()
{
    return gBuiltEyeMisses.load(std::memory_order_relaxed);
}

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

DWORD SetRuntimeFrustum(unsigned int enabled)
{
    const bool on = enabled != 0;
    gRuntimeFrustum.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera result=0 detail=runtime_frustum enabled=" << (on ? "1" : "0")
         << " used=" << gRuntimeFrustumUsed.load(std::memory_order_relaxed)
         << " missing=" << gRuntimeFrustumMissing.load(std::memory_order_relaxed);
    lifecycle::Log(line.str());
    return 0;
}

unsigned long long RuntimeFrustumUsedCount()
{
    return gRuntimeFrustumUsed.load(std::memory_order_relaxed);
}
unsigned long long RuntimeFrustumMissingCount()
{
    return gRuntimeFrustumMissing.load(std::memory_order_relaxed);
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
         << " halfFovDegrees=" << halfFovDegrees
         << " projectionPolicy=" << (gNativeProjection.load(std::memory_order_acquire)
                                         ? "prey_native" : "synthetic");
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

// Installs the RenderWorld hook behind its own byte gate, separately from the
// CSystem::Render hook so a mismatch blocks only A6.
bool EnsureRenderWorldHook()
{
    if (gRenderWorldHookCreated) {
        return true;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);

    const engine::Landmark* landmark = nullptr;
    for (const auto& candidate : engine::Landmarks()) {
        if (candidate.id == "world.render_world") {
            landmark = &candidate;
            break;
        }
    }
    if (landmark == nullptr || landmark->rva != kRenderWorldRva) {
        lifecycle::Log("preyvr_interpose result=unavailable detail=landmark_missing");
        return false;
    }
    const auto address = base + landmark->rva;
    if (!PrologueMatches(address, landmark->expected.data(), landmark->expected.size())) {
        lifecycle::Log("preyvr_interpose result=unavailable detail=render_world_prologue");
        return false;
    }

    gRenderWorldTarget = reinterpret_cast<void*>(address);
    RenderWorldFn original = nullptr;
    // Shared and idempotent: without it MH_CreateHook returns
    // MH_ERROR_NOT_INITIALIZED and the feature reports "unavailable" for a
    // reason unrelated to itself. See MinHookInit.h.
    EnsureMinHook();
    MH_STATUS status = MH_CreateHook(gRenderWorldTarget,
                                     reinterpret_cast<void*>(&RenderWorldInterpose),
                                     reinterpret_cast<void**>(&original));
    if (status != MH_OK) {
        std::ostringstream line;
        line << "preyvr_interpose result=failed detail=create_hook minhook="
             << MH_StatusToString(status);
        lifecycle::Log(line.str());
        return false;
    }
    gRenderWorldOriginal.store(original, std::memory_order_release);
    status = MH_EnableHook(gRenderWorldTarget);
    if (status != MH_OK) {
        std::ostringstream line;
        line << "preyvr_interpose result=failed detail=enable_hook minhook="
             << MH_StatusToString(status);
        lifecycle::Log(line.str());
        return false;
    }
    gRenderWorldHookCreated = true;
    std::ostringstream line;
    line << "preyvr_interpose result=ready targetRva=0x" << std::hex << std::uppercase
         << kRenderWorldRva << " target=0x" << address;
    lifecycle::Log(line.str());
    return true;
}

DWORD SetFrameShapeStereo(unsigned int frameBudget)
{
    if (frameBudget == 0) {
        gFrameShape.store(false, std::memory_order_release);
        gFrameShapeBudget.store(0, std::memory_order_release);
        lifecycle::Log("preyvr_frame_shape result=0 detail=disarmed");
        return static_cast<DWORD>(CameraEditStatus::ready);
    }
    if (frameBudget > 600) {
        lifecycle::Log("preyvr_frame_shape result=refused detail=budget_too_large");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }
    if (!EnsureHook()) {
        return static_cast<DWORD>(CameraEditStatus::unavailable);
    }

    // RenderBegin behind its own byte gate, taken from the landmark table so
    // there is one definition of what it should look like.
    if (gRenderBegin.load(std::memory_order_acquire) == nullptr) {
        const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
        if (preyDll == nullptr) {
            return static_cast<DWORD>(CameraEditStatus::unavailable);
        }
        const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
        const engine::Landmark* landmark = nullptr;
        for (const auto& candidate : engine::Landmarks()) {
            if (candidate.id == "system.render_begin") {
                landmark = &candidate;
                break;
            }
        }
        if (landmark == nullptr || landmark->rva != kRenderBeginRva) {
            lifecycle::Log("preyvr_frame_shape result=unavailable detail=landmark_missing");
            return static_cast<DWORD>(CameraEditStatus::unavailable);
        }
        const auto address = base + landmark->rva;
        if (!PrologueMatches(address, landmark->expected.data(), landmark->expected.size())) {
            lifecycle::Log("preyvr_frame_shape result=unavailable detail=render_begin_prologue");
            return static_cast<DWORD>(CameraEditStatus::unavailable);
        }
        gRenderBegin.store(reinterpret_cast<RenderBeginFn>(address), std::memory_order_release);
    }

    gFrameShapeBudget.store(frameBudget, std::memory_order_release);
    const unsigned long long allowanceMs =
        2000ull + static_cast<unsigned long long>(frameBudget) * 40ull;
    gDoubleRenderDeadlineMs.store(GetTickCount64() + allowanceMs, std::memory_order_release);
    gFrameShape.store(true, std::memory_order_release);

    bool expected = false;
    if (gDoubleRenderWatchdogRunning.compare_exchange_strong(expected, true)) {
        const HANDLE watchdog = CreateThread(nullptr, 0, DoubleRenderWatchdog, nullptr, 0, nullptr);
        if (watchdog == nullptr) {
            gDoubleRenderWatchdogRunning.store(false, std::memory_order_release);
            gFrameShape.store(false, std::memory_order_release);
            gFrameShapeBudget.store(0, std::memory_order_release);
            lifecycle::Log("preyvr_frame_shape result=refused detail=watchdog_thread_failed");
            return static_cast<DWORD>(CameraEditStatus::failed);
        }
        CloseHandle(watchdog);
    }
    std::ostringstream line;
    line << "preyvr_frame_shape result=0 detail=armed frameBudget=" << frameBudget
         << " deadlineMs=" << allowanceMs;
    lifecycle::Log(line.str());
    return static_cast<DWORD>(CameraEditStatus::armed);
}

unsigned long long FrameShapeFrameCount()
{
    return gFrameShapeFrames.load(std::memory_order_acquire);
}

DWORD SetInterposeStereo(unsigned int frameBudget, unsigned int mode)
{
    if (frameBudget == 0) {
        gInterpose.store(false, std::memory_order_release);
        gInterposeBudget.store(0, std::memory_order_release);
        lifecycle::Log("preyvr_interpose result=0 detail=disarmed");
        return static_cast<DWORD>(CameraEditStatus::ready);
    }
    if (frameBudget > 600) {
        lifecycle::Log("preyvr_interpose result=refused detail=budget_too_large");
        return static_cast<DWORD>(CameraEditStatus::failed);
    }
    if (!EnsureRenderWorldHook()) {
        return static_cast<DWORD>(CameraEditStatus::unavailable);
    }

    gInterposeBudget.store(frameBudget, std::memory_order_release);
    gInterposeMode.store(mode, std::memory_order_release);
    gPreyBase.store(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PreyDll.dll")),
                    std::memory_order_release);
    const unsigned long long allowanceMs =
        2000ull + static_cast<unsigned long long>(frameBudget) * 40ull;
    gDoubleRenderDeadlineMs.store(GetTickCount64() + allowanceMs, std::memory_order_release);
    gInterpose.store(true, std::memory_order_release);

    bool expected = false;
    if (gDoubleRenderWatchdogRunning.compare_exchange_strong(expected, true)) {
        const HANDLE watchdog = CreateThread(nullptr, 0, DoubleRenderWatchdog, nullptr, 0, nullptr);
        if (watchdog == nullptr) {
            gDoubleRenderWatchdogRunning.store(false, std::memory_order_release);
            gInterpose.store(false, std::memory_order_release);
            gInterposeBudget.store(0, std::memory_order_release);
            lifecycle::Log("preyvr_interpose result=refused detail=watchdog_thread_failed");
            return static_cast<DWORD>(CameraEditStatus::failed);
        }
        CloseHandle(watchdog);
    }
    std::ostringstream line;
    line << "preyvr_interpose result=0 detail=armed frameBudget=" << frameBudget
         << " mode=" << mode << " deadlineMs=" << allowanceMs;
    lifecycle::Log(line.str());
    return static_cast<DWORD>(CameraEditStatus::armed);
}

unsigned long long InterposeFrameCount()
{
    return gInterposeFrames.load(std::memory_order_acquire);
}

DWORD SetSecondPassStereo(float ipdMetres, float halfFovDegrees, unsigned int frameBudget,
                          bool zeroCameraDelta, bool markSecondary)
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
    gSecondPassMarkSecondary.store(markSecondary, std::memory_order_release);

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
         << " markSecondary=" << (markSecondary ? 1 : 0)
         << " deadlineMs=" << allowanceMs << " ipd=" << ipdMetres;
    lifecycle::Log(line.str());
    return static_cast<DWORD>(CameraEditStatus::armed);
}

unsigned long long SecondPassFrameCount()
{
    return gSecondPassFrames.load(std::memory_order_acquire);
}

unsigned long long SecondPassFrameIdMovedCount()
{
    return gSecondPassFrameIdMoved.load(std::memory_order_acquire);
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

const char* StereoStepName()
{
    return gStereoStep.load(std::memory_order_acquire);
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

int ConsumeRenderedEye()
{
    return eyehandoff::Consume();
}

void ResetEyeHandoff()
{
    eyehandoff::Reset();
}

unsigned long long EyeHandoffLag()
{
    const unsigned long long pushed = eyehandoff::gPushed.load(std::memory_order_acquire);
    const unsigned long long popped = eyehandoff::gPopped.load(std::memory_order_acquire);
    return pushed >= popped ? pushed - popped : 0;
}

unsigned long long EyeHandoffStarvedCount()
{
    return eyehandoff::gStarved.load(std::memory_order_relaxed);
}

unsigned long long EyeHandoffDroppedCount()
{
    return eyehandoff::gDropped.load(std::memory_order_relaxed);
}

void SetStereoEyeLockFromRenderThread(int eye)
{
    // **Deliberately lock-free, and deliberately not SetStereoEyeLock.**
    //
    // The stereo submission path drives eye alternation from inside Prey's
    // render loop. SetStereoEyeLock takes gMutex with a timeout, and a control
    // mutex on the render thread is exactly the shape that turns a slow frame
    // into a hitch and a contended one into a stall. This touches a single
    // atomic and nothing else.
    //
    // It also does not move the frame-capture tag. That tag exists to keep two
    // offline dumps from being confused with each other, which is a debugging
    // concern; submission has its own eye identity and does not read it.
    gEyeLock.store((eye == 0 || eye == 1) ? eye : -1, std::memory_order_release);
}

DWORD SetKeepHeadRotation(unsigned int enabled)
{
    const bool on = enabled != 0u;
    gKeepHeadRotation.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=keep_head_rotation enabled="
         << (on ? "1" : "0");
    lifecycle::Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
}

bool LastWrittenCameraForward(Vec3& out)
{
    if (!gHaveWrittenForward.load(std::memory_order_acquire)) {
        return false;
    }
    out.x = gWrittenForwardX.load(std::memory_order_relaxed);
    out.y = gWrittenForwardY.load(std::memory_order_relaxed);
    out.z = gWrittenForwardZ.load(std::memory_order_relaxed);
    return true;
}

DWORD SetUpstreamHeadRotation(unsigned int enabled)
{
    const bool on = enabled != 0u;
    if (on) {
        // The hook has to exist for the edit to run at all. Arming without it
        // would report success and change nothing.
        if (!EnsureHook()) {
            lifecycle::Log("preyvr_camera_edit result=refused detail=head_rotation_no_hook");
            return static_cast<DWORD>(CameraEditStatus::unavailable);
        }
        gHeadRotationApplied.store(0, std::memory_order_relaxed);
        gHeadRotationRefused.store(0, std::memory_order_relaxed);
    }
    gHeadRotationArmed.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=upstream_head_rotation enabled="
         << (on ? "1" : "0");
    lifecycle::Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
}

unsigned long long UpstreamHeadRotationApplied()
{
    return gHeadRotationApplied.load(std::memory_order_relaxed);
}

unsigned long long UpstreamHeadRotationRefused()
{
    return gHeadRotationRefused.load(std::memory_order_relaxed);
}

DWORD SetNativeProjection(unsigned int enabled)
{
    const bool on = enabled != 0u;
    gNativeProjection.store(on, std::memory_order_release);
    std::ostringstream line;
    line << "preyvr_camera_edit result=0 detail=native_projection enabled="
         << (on ? "1" : "0");
    lifecycle::Log(line.str());
    return static_cast<DWORD>(gStatus.load(std::memory_order_acquire));
}

unsigned int NativeProjectionEnabled()
{
    return gNativeProjection.load(std::memory_order_acquire) ? 1u : 0u;
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

bool RenderedEyeTangents(float& tanLeft, float& tanRight, float& tanUp, float& tanDown)
{
    if (!gRenderedTangentsValid.load(std::memory_order_acquire)) {
        return false;
    }
    tanLeft = gRenderedTanLeft.load(std::memory_order_relaxed);
    tanRight = gRenderedTanRight.load(std::memory_order_relaxed);
    tanUp = gRenderedTanUp.load(std::memory_order_relaxed);
    tanDown = gRenderedTanDown.load(std::memory_order_relaxed);
    return true;
}

unsigned long long CameraEditRestoreFailureCount()
{
    return gRestoreFailures.load(std::memory_order_acquire);
}

} // namespace preyvr::dll
