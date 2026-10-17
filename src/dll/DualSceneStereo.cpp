#include "DualSceneStereo.h"

#include "CameraEditHook.h"
#include "FrameCaptureWin32.h"
#include "XrSessionHost.h"
#include "FrameObserverHook.h"
#include "Logger.h"
#include "MinHookInit.h"
#include "preyvr/EngineMap.h"

#include <MinHook.h>
#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

#pragma intrinsic(_ReturnAddress)

namespace preyvr::dll {
namespace {

void Log(const std::string& line)
{
    lifecycle::Log("preyvr_dual " + line);
}

// RT_RenderScene(this, CRenderView*, int nFlags, SThreadInfo&, void (*)()).
// RC_RenderScene (0xFEE280) passes m_TI[fillSlot] and the render callback; the
// command processor (0xFE9A80) passes its own copy of the queued SThreadInfo.
using RenderSceneFn = void(__fastcall*)(void* renderer, void* view, int flags, void* threadInfo,
                                        void* renderFunc);
using SwitchUsageModeFn = void(__fastcall*)(void* view, int mode);
using ClearFn = void(__fastcall*)(void* view);
// Called, never hooked: the engine's own camera setters, used to build each eye.
using RendererSetCameraFn = void(__fastcall*)(void* renderer, const void* camera);
using ViewSetCameraFn = void(__fastcall*)(void* view, const void* camera);
using UpdateFrustumFn = void(__fastcall*)(void* camera);

constexpr std::uintptr_t kRenderSceneRva = 0xF41CA0;
constexpr std::uintptr_t kSwitchUsageModeRva = 0xEE82F0;
constexpr std::uintptr_t kClearRva = 0xEE4030;
constexpr std::uintptr_t kRendererSetCameraRva = 0xF7FE70;
constexpr std::uintptr_t kViewSetCameraRva = 0xEE7E80;
constexpr std::uintptr_t kUpdateFrustumRva = 0x121D70;

// SThreadInfo is 0x328 bytes, one per frame slot at renderer + 0x49A0. The
// camera block CD3D9Renderer::SetCamera writes runs from the zero-translation
// camera matrix at +0x28, through m_cam (+0x68, a 0x240-byte CCamera), to the
// end of m_rcam (+0x2A8, 0x48 bytes). The view and projection matrices live in
// matrix stacks the SThreadInfo points at (+0x18, +0x20), so they are shared
// with any copy of it and are loaded by SetCamera itself.
constexpr std::size_t kThreadInfoSize = 0x328;
constexpr std::size_t kThreadInfoBlocks = 0x49A0;
constexpr std::size_t kCameraBlockBegin = 0x28;
constexpr std::size_t kCameraBlockEnd = 0x2F0;
constexpr std::size_t kCameraSize = 0x240;
// RT_RenderScene indexes the slot by renderer + 0x499C; SetCamera on the render
// thread by renderThread(+0xD90) + 0x48. Both are logged by observe, and the
// per-eye path copies from the slot SetCamera actually wrote.
constexpr std::size_t kSceneSlot = 0x499C;
constexpr std::size_t kRenderThread = 0xD90;
constexpr std::size_t kRenderThreadProcessSlot = 0x48;
constexpr std::size_t kRenderThreadFillSlot = 0x4C;

// Return addresses of RT_RenderScene's three teardown calls, the only calls the
// deferral may skip. From the disassembly of its tail:
//   0xF42A46 call [rax+0x50]   view->SwitchUsageMode(ReadingDone)
//   0xF42A8C call [rax+0x50]   shadowView->SwitchUsageMode(ReadingDone), per view
//   0xF42A9B call 0xEE4030     view->Clear()
constexpr std::uintptr_t kReturnMainReadingDone = 0xF42A49;
constexpr std::uintptr_t kReturnShadowReadingDone = 0xF42A8F;
constexpr std::uintptr_t kReturnClear = 0xF42AA0;
constexpr int kUsageModeReadingDone = 2;

// CRenderView fields. +0x14 is the view type, 1 for the recursive (Looking
// Glass) view, which RT_RenderScene itself tests. +0x11A0 is the view's own
// CCamera (R-049), whose Matrix34 translation sits at +0x0C/+0x1C/+0x2C.
constexpr std::size_t kViewType = 0x14;
constexpr int kViewTypeRecursive = 1;
constexpr std::size_t kViewCamera = 0x11A0;
// SThreadInfo::m_cam, the CCamera copied in by CD3D9Renderer::SetCamera
// (0xF7FE70 writes it through CCamera::operator= at slot + 0x68).
constexpr std::size_t kThreadInfoCamera = 0x68;

enum class Step : int { idle = 0, single = 1, first = 2, between = 3, second = 4 };
const char* StepName(int step)
{
    switch (static_cast<Step>(step)) {
    case Step::idle: return "idle";
    case Step::single: return "single";
    case Step::first: return "first_submission";
    case Step::between: return "between";
    case Step::second: return "second_submission";
    }
    return "unknown";
}

std::mutex gInstallMutex;
bool gInstalled = false;
std::uintptr_t gBase = 0;
std::atomic<RenderSceneFn> gRenderScene{nullptr};
std::atomic<SwitchUsageModeFn> gSwitchUsageMode{nullptr};
std::atomic<ClearFn> gClear{nullptr};
std::atomic<RendererSetCameraFn> gRendererSetCamera{nullptr};
std::atomic<ViewSetCameraFn> gViewSetCamera{nullptr};
std::atomic<UpdateFrustumFn> gUpdateFrustum{nullptr};
// Half the interpupillary distance, in micrometres so it crosses as an integer.
std::atomic<unsigned int> gHalfIpdMicrometres{32000};

std::atomic<bool> gObserve{false};
std::atomic<unsigned int> gMode{0};
std::atomic<unsigned int> gBudget{600};
std::atomic<bool> gCaptureArmed{false};
std::atomic<unsigned int> gCaptureTag{0};

std::atomic<unsigned long long> gCalls{0}, gRecursiveCalls{0}, gNestedCalls{0};
std::atomic<unsigned long long> gDoubled{0}, gRefused{0}, gCaptures{0};
std::atomic<unsigned long long> gDeferredMain{0}, gDeferredShadow{0}, gDeferredClear{0};
std::atomic<unsigned long long> gFirstMicros{0}, gSecondMicros{0}, gSingleMicros{0}, gSingles{0};
std::atomic<unsigned long long> gArmedAtFrame{0}, gObservedAtFrame{0}, gCallsAtObserve{0};
std::atomic<int> gStep{0};
std::atomic<int> gMaxDepth{0};
std::atomic<int> gLastFlags{0};
std::atomic<const char*> gLastRefusal{"none"};
std::atomic<unsigned int> gLogged{0};
std::atomic<unsigned int> gSlotsLogged{0};
std::atomic<unsigned long long> gHeadsetBuildMisses{0};

// Per-thread, so the deferral can never apply to another thread's calls.
thread_local int tDepth = 0;
thread_local bool tDeferTeardown = false;
thread_local unsigned int tDeferredMain = 0;
thread_local unsigned int tDeferredShadow = 0;
thread_local unsigned int tDeferredClear = 0;

unsigned long long QpcMicros()
{
    static const long long frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<unsigned long long>(now.QuadPart * 1000000LL / frequency);
}

template <typename T>
bool SafeRead(const void* address, T& out)
{
    __try {
        std::memcpy(&out, address, sizeof(T));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int ViewType(const void* view)
{
    int type = -1;
    if (view != nullptr) {
        SafeRead(static_cast<const std::uint8_t*>(view) + kViewType, type);
    }
    return type;
}

bool CameraPosition(const void* camera, float out[3])
{
    const auto* bytes = static_cast<const std::uint8_t*>(camera);
    return camera != nullptr && SafeRead(bytes + 0x0C, out[0]) && SafeRead(bytes + 0x1C, out[1]) &&
           SafeRead(bytes + 0x2C, out[2]);
}

// The first submissions are logged in full: which thread, which view, what the
// two candidate camera sources hold. That is what decides whether a per-eye
// camera has to go through the SThreadInfo, the view, or both.
void Observe(const void* view, int type, int flags, const void* threadInfo)
{
    if (gLogged.fetch_add(1, std::memory_order_relaxed) >= 48) {
        return;
    }
    float viewPos[3]{}, infoPos[3]{};
    const bool haveView =
        view != nullptr && CameraPosition(static_cast<const std::uint8_t*>(view) + kViewCamera, viewPos);
    const bool haveInfo = threadInfo != nullptr &&
        CameraPosition(static_cast<const std::uint8_t*>(threadInfo) + kThreadInfoCamera, infoPos);
    std::ostringstream line;
    line << "observe frame=" << ObservedFrameCount() << " thread=" << GetCurrentThreadId()
         << " depth=" << tDepth << " view=0x" << std::hex << reinterpret_cast<std::uintptr_t>(view)
         << std::dec << " type=" << type << " flags=0x" << std::hex << flags << std::dec;
    if (haveView) {
        line << " viewCam=" << viewPos[0] << ',' << viewPos[1] << ',' << viewPos[2];
    }
    if (haveInfo) {
        line << " infoCam=" << infoPos[0] << ',' << infoPos[1] << ',' << infoPos[2];
    }
    Log(line.str());
}

// Which SThreadInfo slot each side uses. The per-eye path is only sound if the
// slot CD3D9Renderer::SetCamera writes on this thread is the slot RT_RenderScene
// installs into, and if the passed SThreadInfo shares that slot's matrix stacks.
void ObserveSlots(const void* renderer, const void* threadInfo)
{
    const auto* r = static_cast<const std::uint8_t*>(renderer);
    int sceneSlot = -1, processSlot = -1, fillSlot = -1;
    std::uintptr_t renderThread = 0, passedView = 0, passedProj = 0, slotView = 0, slotProj = 0;
    SafeRead(r + kSceneSlot, sceneSlot);
    if (SafeRead(r + kRenderThread, renderThread) && renderThread != 0) {
        SafeRead(reinterpret_cast<const void*>(renderThread + kRenderThreadProcessSlot), processSlot);
        SafeRead(reinterpret_cast<const void*>(renderThread + kRenderThreadFillSlot), fillSlot);
    }
    const auto* info = static_cast<const std::uint8_t*>(threadInfo);
    SafeRead(info + 0x18, passedView);
    SafeRead(info + 0x20, passedProj);
    if (sceneSlot == 0 || sceneSlot == 1) {
        const auto* slot = r + kThreadInfoBlocks + sceneSlot * kThreadInfoSize;
        SafeRead(slot + 0x18, slotView);
        SafeRead(slot + 0x20, slotProj);
    }
    std::ostringstream line;
    line << "slots sceneSlot=" << sceneSlot << " processSlot=" << processSlot
         << " fillSlot=" << fillSlot << " passedIsSlot="
         << (threadInfo == r + kThreadInfoBlocks + (sceneSlot < 0 ? 0 : sceneSlot) * kThreadInfoSize)
         << " stacksShared=" << (passedView == slotView && passedProj == slotProj && passedView != 0);
    Log(line.str());
}

// Each eye is the frame's own camera moved half the IPD along its right axis
// (column 0 of the row-major Matrix34; translation is column 3). Rotation,
// projection and asymmetry are untouched, as in the native-projection path, so
// both eyes share Prey's frustum and differ by translation alone.
bool BuildEyes(const void* threadInfo, std::uint8_t (&centre)[kCameraSize],
               std::uint8_t (&eyes)[2][kCameraSize])
{
    const auto* info = static_cast<const std::uint8_t*>(threadInfo);
    __try {
        std::memcpy(centre, info + kThreadInfoCamera, kCameraSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    float m[12];
    std::memcpy(m, centre, sizeof(m));
    const float length = std::sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    if (!(length > 0.5f && length < 2.0f)) {
        return false;
    }
    const float half = static_cast<float>(gHalfIpdMicrometres.load(std::memory_order_relaxed)) * 1e-6f;
    const auto updateFrustum = gUpdateFrustum.load(std::memory_order_acquire);
    for (int eye = 0; eye < 2; ++eye) {
        const float d = (eye == 0 ? -half : half) / length;
        float e[12];
        std::memcpy(e, m, sizeof(e));
        e[3] += m[0] * d;
        e[7] += m[4] * d;
        e[11] += m[8] * d;
        std::memcpy(eyes[eye], centre, kCameraSize);
        std::memcpy(eyes[eye], e, sizeof(e));
        updateFrustum(eyes[eye]);
    }
    return true;
}

// Mode 3: the eyes the headset will be told about. Built by the camera hook
// (BuildDualEyeCamera) -- the same construction as the alternate-eye path, at the
// stereo path's own IPD -- which also publishes each eye record, so the near pass
// can identify the eye from the camera the render view is given.
bool BuildHeadsetEyes(const void* threadInfo, std::uint8_t (&centre)[kCameraSize],
                      std::uint8_t (&eyes)[2][kCameraSize])
{
    const auto* info = static_cast<const std::uint8_t*>(threadInfo);
    __try {
        std::memcpy(centre, info + kThreadInfoCamera, kCameraSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    for (int eye = 0; eye < 2; ++eye) {
        std::memcpy(eyes[eye], centre, kCameraSize);
        if (!BuildDualEyeCamera(eyes[eye], eye)) {
            return false;
        }
    }
    return true;
}

// Installs one eye through the engine's own setters, then builds the
// SThreadInfo for that submission: the frame's own, with the camera block
// SetCamera just wrote. Returns false if the render thread's slot is not one of
// the two frame slots, in which case nothing has been changed except the
// renderer camera, which the caller restores.
bool PrepareEye(void* renderer, void* view, const void* threadInfo,
                const std::uint8_t* eyeCamera, std::uint8_t* outInfo)
{
    gRendererSetCamera.load(std::memory_order_acquire)(renderer, eyeCamera);
    const auto* r = static_cast<const std::uint8_t*>(renderer);
    std::uintptr_t renderThread = 0;
    int slot = -1;
    if (!SafeRead(r + kRenderThread, renderThread) || renderThread == 0 ||
        !SafeRead(reinterpret_cast<const void*>(renderThread + kRenderThreadProcessSlot), slot) ||
        (slot != 0 && slot != 1)) {
        return false;
    }
    const auto* written = r + kThreadInfoBlocks + slot * kThreadInfoSize;
    std::memcpy(outInfo, threadInfo, kThreadInfoSize);
    std::memcpy(outInfo + kCameraBlockBegin, written + kCameraBlockBegin,
                kCameraBlockEnd - kCameraBlockBegin);
    gViewSetCamera.load(std::memory_order_acquire)(view, eyeCamera);
    return true;
}

void Capture(void* renderer, const char* stem)
{
    if (CaptureBackBufferNow(renderer, gCaptureTag.load(std::memory_order_acquire),
                             ObservedFrameCount(), stem) == 0) {
        gCaptures.fetch_add(1, std::memory_order_relaxed);
    }
}

// Replays whatever teardown was held back, in RT_RenderScene's own order: the
// view to ReadingDone, each shadow-frustum view to ReadingDone (the vector at
// +0x1888/+0x1890, 0x20-byte entries holding the view at +0x18), then Clear.
void CompleteDeferredTeardown(void* view, unsigned int main, unsigned int shadow, unsigned int clear)
{
    const auto switchUsageMode = gSwitchUsageMode.load(std::memory_order_acquire);
    if (main != 0) {
        switchUsageMode(view, kUsageModeReadingDone);
    }
    if (shadow != 0) {
        const auto* bytes = static_cast<const std::uint8_t*>(view);
        std::uintptr_t begin = 0, end = 0;
        if (SafeRead(bytes + 0x1888, begin) && SafeRead(bytes + 0x1890, end) && begin <= end &&
            end - begin < 0x20 * 256) {
            for (auto entry = begin; entry != end; entry += 0x20) {
                void* shadowView = nullptr;
                if (SafeRead(reinterpret_cast<const void*>(entry + 0x18), shadowView) &&
                    shadowView != nullptr) {
                    switchUsageMode(shadowView, kUsageModeReadingDone);
                }
            }
        }
    }
    if (clear != 0) {
        gClear.load(std::memory_order_acquire)(view);
    }
}

void Refuse(const char* reason)
{
    gRefused.fetch_add(1, std::memory_order_relaxed);
    gLastRefusal.store(reason, std::memory_order_release);
    // Disarm rather than retry: a teardown that did not go through the gates means
    // the model of this function is wrong, and the next frame would be the same.
    const unsigned int was = gMode.exchange(0, std::memory_order_acq_rel);
    if (was == 3) {
        // The headset path must not go dark: return the game thread to
        // alternate-eye contracts, which need no second submission.
        SetDualStereoContract(0);
    }
    Log(std::string("result=refused detail=") + reason + " mode=0 was=" + std::to_string(was) +
        (was == 3 ? " fallback=alternate_eye" : ""));
}

void __fastcall SwitchUsageModeDetour(void* view, int mode)
{
    if (tDeferTeardown && mode == kUsageModeReadingDone) {
        const auto from = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        if (from == gBase + kReturnMainReadingDone) {
            ++tDeferredMain;
            return;
        }
        if (from == gBase + kReturnShadowReadingDone) {
            ++tDeferredShadow;
            return;
        }
    }
    gSwitchUsageMode.load(std::memory_order_acquire)(view, mode);
}

void __fastcall ClearDetour(void* view)
{
    if (tDeferTeardown &&
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == gBase + kReturnClear) {
        ++tDeferredClear;
        return;
    }
    gClear.load(std::memory_order_acquire)(view);
}

void __fastcall RenderSceneDetour(void* renderer, void* view, int flags, void* threadInfo,
                                  void* renderFunc)
{
    const auto original = gRenderScene.load(std::memory_order_acquire);
    const int type = ViewType(view);
    gCalls.fetch_add(1, std::memory_order_relaxed);
    if (type == kViewTypeRecursive) {
        gRecursiveCalls.fetch_add(1, std::memory_order_relaxed);
    }
    if (tDepth > 0) {
        gNestedCalls.fetch_add(1, std::memory_order_relaxed);
    }
    if (tDepth + 1 > gMaxDepth.load(std::memory_order_relaxed)) {
        gMaxDepth.store(tDepth + 1, std::memory_order_relaxed);
    }
    if (gObserve.load(std::memory_order_relaxed)) {
        Observe(view, type, flags, threadInfo);
        if (gSlotsLogged.fetch_add(1, std::memory_order_relaxed) < 3) {
            ObserveSlots(renderer, threadInfo);
        }
    }

    // Only the top-level main view is ever doubled. Recursive views and anything
    // nested inside a submission pass straight through.
    const bool topLevel = tDepth == 0 && type != kViewTypeRecursive && view != nullptr &&
                          threadInfo != nullptr;
    const unsigned int mode = gMode.load(std::memory_order_acquire);
    // Mode 3 doubles exactly while the game thread is publishing both-eyes
    // contracts with stereo armed, and is not budgeted: it is the headset path.
    const bool doubleIt = topLevel &&
        (((mode == 1 || mode == 2) && gBudget.load(std::memory_order_acquire) > 0) ||
         (mode == 3 && DualStereoContractEnabled() && SyntheticStereoIpdMetres() > 0.0f));
    ++tDepth;
    if (!doubleIt) {
        const auto start = topLevel ? QpcMicros() : 0;
        if (topLevel) {
            gStep.store(static_cast<int>(Step::single), std::memory_order_release);
        }
        original(renderer, view, flags, threadInfo, renderFunc);
        if (topLevel) {
            gSingleMicros.fetch_add(QpcMicros() - start, std::memory_order_relaxed);
            gSingles.fetch_add(1, std::memory_order_relaxed);
            gStep.store(static_cast<int>(Step::idle), std::memory_order_release);
            if (gCaptureArmed.exchange(false, std::memory_order_acq_rel)) {
                Capture(renderer, "dual-single");
            }
        }
        --tDepth;
        return;
    }

    gLastFlags.store(flags, std::memory_order_relaxed);
    const bool capture = gCaptureArmed.exchange(false, std::memory_order_acq_rel);

    // Mode 2: both eye cameras are built from the frame's camera before either
    // submission, so the second eye cannot see an edit made for the first.
    alignas(16) std::uint8_t centre[kCameraSize];
    alignas(16) std::uint8_t eyes[2][kCameraSize];
    alignas(16) std::uint8_t infos[2][kThreadInfoSize];
    const bool perEye = mode >= 2;
    const auto restoreCentre = [&] {
        gRendererSetCamera.load(std::memory_order_acquire)(renderer, centre);
        gViewSetCamera.load(std::memory_order_acquire)(view, centre);
    };
    void* firstInfo = threadInfo;
    void* secondInfo = threadInfo;
    if (perEye) {
        const bool built = mode == 3 ? BuildHeadsetEyes(threadInfo, centre, eyes)
                                     : BuildEyes(threadInfo, centre, eyes);
        if (!built && mode == 3) {
            // Transient (stereo disarmed between the game thread's contract and
            // this submission, or an unsafe eye): draw this frame once. Its
            // both-eyes contract finds no held first eye and is refused, which
            // costs one frame rather than the mode.
            gHeadsetBuildMisses.fetch_add(1, std::memory_order_relaxed);
            original(renderer, view, flags, threadInfo, renderFunc);
            gStep.store(static_cast<int>(Step::idle), std::memory_order_release);
            --tDepth;
            return;
        }
        if (!built || !PrepareEye(renderer, view, threadInfo, eyes[0], infos[0])) {
            if (built) {
                restoreCentre();
            }
            Refuse(built ? "render_thread_slot_unknown" : "eye_camera_unavailable");
            original(renderer, view, flags, threadInfo, renderFunc);
            gStep.store(static_cast<int>(Step::idle), std::memory_order_release);
            --tDepth;
            return;
        }
        firstInfo = infos[0];
        secondInfo = infos[1];
    }

    // First submission, teardown deferred.
    gStep.store(static_cast<int>(Step::first), std::memory_order_release);
    tDeferredMain = tDeferredShadow = tDeferredClear = 0;
    tDeferTeardown = true;
    const auto t0 = QpcMicros();
    original(renderer, view, flags, firstInfo, renderFunc);
    const auto t1 = QpcMicros();
    tDeferTeardown = false;
    gDeferredMain.fetch_add(tDeferredMain, std::memory_order_relaxed);
    gDeferredShadow.fetch_add(tDeferredShadow, std::memory_order_relaxed);
    gDeferredClear.fetch_add(tDeferredClear, std::memory_order_relaxed);
    gStep.store(static_cast<int>(Step::between), std::memory_order_release);
    // The backbuffer is the first eye, post-processed and before the HUD.
    const bool held = mode == 3 && HoldDualFirstEye(renderer);
    if (capture) {
        Capture(renderer, "dual-eye0");
    }

    // Exactly one main ReadingDone and one Clear must have been held back. Zero of
    // both is RT_RenderScene's early exit (device lost), which tore nothing down;
    // anything else means a teardown escaped the gates and the view may already
    // be cleared, so a second submission would draw nothing or worse.
    if (tDeferredMain == 0 && tDeferredClear == 0 && tDeferredShadow == 0) {
        if (perEye) {
            restoreCentre();
        }
        gStep.store(static_cast<int>(Step::idle), std::memory_order_release);
        --tDepth;
        return;
    }
    if (tDeferredMain != 1 || tDeferredClear != 1) {
        Refuse("teardown_escaped_gate");
        // Finish whatever was held back so the view is not left in Reading.
        CompleteDeferredTeardown(view, tDeferredMain, tDeferredShadow, tDeferredClear);
        if (perEye) {
            restoreCentre();
        }
        gStep.store(static_cast<int>(Step::idle), std::memory_order_release);
        --tDepth;
        return;
    }
    if (perEye && !PrepareEye(renderer, view, threadInfo, eyes[1], infos[1])) {
        Refuse("render_thread_slot_unknown");
        CompleteDeferredTeardown(view, tDeferredMain, tDeferredShadow, tDeferredClear);
        restoreCentre();
        gStep.store(static_cast<int>(Step::idle), std::memory_order_release);
        --tDepth;
        return;
    }

    // Second submission performs the one real teardown.
    gStep.store(static_cast<int>(Step::second), std::memory_order_release);
    original(renderer, view, flags, secondInfo, renderFunc);
    const auto t2 = QpcMicros();
    if (perEye) {
        // Anything the renderer draws after the scene this frame -- the HUD,
        // auxiliary geometry -- gets the frame's own camera back.
        restoreCentre();
    }
    if (held) {
        // From here to RT_EndFrame the game draws its overlays; each draw into
        // the backbuffer can now be repeated into the held first eye (DualOverlay.h).
        OpenDualOverlayWindow();
    }
    gStep.store(static_cast<int>(Step::idle), std::memory_order_release);
    if (capture) {
        Capture(renderer, "dual-eye1");
    }

    gFirstMicros.fetch_add(t1 - t0, std::memory_order_relaxed);
    gSecondMicros.fetch_add(t2 - t1, std::memory_order_relaxed);
    const auto doubled = gDoubled.fetch_add(1, std::memory_order_relaxed) + 1;
    if (doubled == 1) {
        std::ostringstream line;
        line << "result=0 detail=first_doubled_frame frame=" << ObservedFrameCount()
             << " flags=0x" << std::hex << flags << std::dec << " shadowsDeferred=" << tDeferredShadow
             << " firstUs=" << (t1 - t0) << " secondUs=" << (t2 - t1);
        Log(line.str());
    }
    if (mode != 3 && gBudget.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        gMode.store(0, std::memory_order_release);
        Log("result=0 detail=budget_exhausted mode=0 doubled=" + std::to_string(doubled));
    }
    --tDepth;
}

const engine::Landmark* FindLandmark(std::string_view id)
{
    for (const auto& landmark : engine::Landmarks()) {
        if (landmark.id == id) {
            return &landmark;
        }
    }
    return nullptr;
}

bool PrologueMatches(std::uintptr_t address, const engine::Landmark& landmark)
{
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) !=
            sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY)) == 0) {
        return false;
    }
    return std::memcmp(reinterpret_cast<const void*>(address), landmark.expected.data(),
                       landmark.expected.size()) == 0;
}

// All three or none: deferring the teardown without the RT_RenderScene hook is
// meaningless, and doubling without the deferral draws a cleared view.
DWORD EnsureHooks()
{
    std::lock_guard lock(gInstallMutex);
    if (gInstalled) {
        return 0;
    }
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) {
        Log("result=unavailable detail=preydll_not_loaded");
        return 1;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(preyDll);
    struct Target {
        const char* id;
        std::uintptr_t rva;
        void* detour;
        void** original;
        std::uintptr_t address = 0;
    };
    void* renderScene = nullptr;
    void* switchUsageMode = nullptr;
    void* clear = nullptr;
    Target targets[] = {
        {"renderer.render_scene", kRenderSceneRva, reinterpret_cast<void*>(&RenderSceneDetour),
         &renderScene},
        {"view.switch_usage_mode", kSwitchUsageModeRva,
         reinterpret_cast<void*>(&SwitchUsageModeDetour), &switchUsageMode},
        {"view.clear", kClearRva, reinterpret_cast<void*>(&ClearDetour), &clear},
    };
    for (auto& target : targets) {
        const auto* landmark = FindLandmark(target.id);
        if (landmark == nullptr || landmark->rva != target.rva) {
            Log(std::string("result=unavailable detail=landmark_missing id=") + target.id);
            return 1;
        }
        target.address = base + landmark->rva;
        if (!PrologueMatches(target.address, *landmark)) {
            Log(std::string("result=unavailable detail=prologue_mismatch id=") + target.id);
            return 2;
        }
    }
    // The engine functions mode 2 calls but never hooks. Gated the same way, so a
    // build where any of them moved refuses the whole lane rather than calling into
    // the wrong code mid-frame.
    struct Callee {
        const char* id;
        std::uintptr_t rva;
        std::uintptr_t address = 0;
    };
    Callee callees[] = {
        {"renderer.set_camera", kRendererSetCameraRva},
        {"view.set_camera", kViewSetCameraRva},
        {"camera.update_frustum", kUpdateFrustumRva},
    };
    for (auto& callee : callees) {
        const auto* landmark = FindLandmark(callee.id);
        if (landmark == nullptr || landmark->rva != callee.rva) {
            Log(std::string("result=unavailable detail=landmark_missing id=") + callee.id);
            return 1;
        }
        callee.address = base + landmark->rva;
        if (!PrologueMatches(callee.address, *landmark)) {
            Log(std::string("result=unavailable detail=prologue_mismatch id=") + callee.id);
            return 2;
        }
    }
    gRendererSetCamera.store(reinterpret_cast<RendererSetCameraFn>(callees[0].address),
                             std::memory_order_release);
    gViewSetCamera.store(reinterpret_cast<ViewSetCameraFn>(callees[1].address),
                         std::memory_order_release);
    gUpdateFrustum.store(reinterpret_cast<UpdateFrustumFn>(callees[2].address),
                         std::memory_order_release);
    if (!EnsureMinHook()) {
        return 3;
    }
    // Create all three before enabling any, so a failure leaves nothing live.
    for (auto& target : targets) {
        const MH_STATUS status = MH_CreateHook(reinterpret_cast<void*>(target.address),
                                               target.detour, target.original);
        if (status != MH_OK) {
            Log(std::string("result=failed detail=create_hook id=") + target.id +
                " minhook=" + MH_StatusToString(status));
            for (auto& created : targets) {
                MH_RemoveHook(reinterpret_cast<void*>(created.address));
            }
            return 3;
        }
    }
    gBase = base;
    gRenderScene.store(reinterpret_cast<RenderSceneFn>(renderScene), std::memory_order_release);
    gSwitchUsageMode.store(reinterpret_cast<SwitchUsageModeFn>(switchUsageMode),
                           std::memory_order_release);
    gClear.store(reinterpret_cast<ClearFn>(clear), std::memory_order_release);
    for (auto& target : targets) {
        const MH_STATUS status = MH_EnableHook(reinterpret_cast<void*>(target.address));
        if (status != MH_OK) {
            Log(std::string("result=failed detail=enable_hook id=") + target.id +
                " minhook=" + MH_StatusToString(status));
            return 3;
        }
    }
    gInstalled = true;
    // Frames are counted at RT_EndFrame; without the observer every report would
    // read frame 0 and calls-per-frame could not be computed.
    SetFrameObserverEnabled(true);
    Log("result=0 detail=hooks_installed renderScene=0xF41CA0 switchUsageMode=0xEE82F0 clear=0xEE4030");
    return 0;
}

} // namespace

DWORD SetDualSceneObserve(unsigned int enabled)
{
    if (enabled != 0) {
        const DWORD installed = EnsureHooks();
        if (installed != 0) {
            return installed;
        }
        gLogged.store(0, std::memory_order_relaxed);
        gObservedAtFrame.store(ObservedFrameCount(), std::memory_order_relaxed);
        gCallsAtObserve.store(gCalls.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    gObserve.store(enabled != 0, std::memory_order_release);
    return 0;
}

DWORD SetDualSceneIpdMillimetres(unsigned int millimetres)
{
    if (millimetres < 40 || millimetres > 90) {
        return 1;
    }
    gHalfIpdMicrometres.store(millimetres * 500u, std::memory_order_release);
    return 0;
}

DWORD SetDualSceneMode(unsigned int mode, unsigned int frameBudget)
{
    if (mode > 3) {
        return 4;
    }
    if (mode != 0) {
        const DWORD installed = EnsureHooks();
        if (installed != 0) {
            return installed;
        }
    }
    if (frameBudget != 0) {
        gBudget.store(frameBudget, std::memory_order_release);
    }
    gArmedAtFrame.store(ObservedFrameCount(), std::memory_order_relaxed);
    gLastRefusal.store("none", std::memory_order_release);
    gMode.store(mode, std::memory_order_release);
    std::ostringstream line;
    line << "result=0 detail=mode mode=" << mode << " budget=" << gBudget.load();
    Log(line.str());
    return 0;
}

DWORD RequestDualSceneCapture(unsigned int tag)
{
    const DWORD installed = EnsureHooks();
    if (installed != 0) {
        return installed;
    }
    gCaptureTag.store(tag, std::memory_order_release);
    gCaptureArmed.store(true, std::memory_order_release);
    return 0;
}

std::string DualSceneReport()
{
    const auto doubled = gDoubled.load(std::memory_order_relaxed);
    const auto singles = gSingles.load(std::memory_order_relaxed);
    const auto frames = ObservedFrameCount();
    const auto framesSinceObserve = frames - gObservedAtFrame.load(std::memory_order_relaxed);
    const auto callsSinceObserve = gCalls.load(std::memory_order_relaxed) -
                                   gCallsAtObserve.load(std::memory_order_relaxed);
    std::ostringstream out;
    out << " installed=" << (gInstalled ? 1 : 0) << " observe=" << (gObserve.load() ? 1 : 0)
        << " mode=" << gMode.load() << " budget=" << gBudget.load()
        << " ipdMm=" << gHalfIpdMicrometres.load() / 500.0
        << " step=" << StepName(gStep.load(std::memory_order_acquire)) << " frames=" << frames
        << " calls=" << gCalls.load() << " recursive=" << gRecursiveCalls.load()
        << " nested=" << gNestedCalls.load() << " maxDepth=" << gMaxDepth.load()
        << " callsPerFrameSinceObserve=";
    if (framesSinceObserve > 0) {
        out << static_cast<double>(callsSinceObserve) / static_cast<double>(framesSinceObserve);
    } else {
        out << "n/a";
    }
    out << " doubled=" << doubled << " refused=" << gRefused.load()
        << " lastRefusal=" << gLastRefusal.load(std::memory_order_acquire)
        << " deferredMain=" << gDeferredMain.load() << " deferredShadow=" << gDeferredShadow.load()
        << " deferredClear=" << gDeferredClear.load() << " lastFlags=0x" << std::hex
        << gLastFlags.load() << std::dec << " captures=" << gCaptures.load();
    out << " singleUs=" << (singles ? gSingleMicros.load() / singles : 0)
        << " firstUs=" << (doubled ? gFirstMicros.load() / doubled : 0)
        << " secondUs=" << (doubled ? gSecondMicros.load() / doubled : 0)
        << " dualContract=" << (DualStereoContractEnabled() ? 1 : 0)
        << " stereoIpd=" << SyntheticStereoIpdMetres()
        << " headsetBuildMisses=" << gHeadsetBuildMisses.load() << DualPairReport();
    return out.str();
}

} // namespace preyvr::dll
