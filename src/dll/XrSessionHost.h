#pragma once

#include <string>

#include <Windows.h>

// Hurdle 2: an OpenXR session hosted inside Prey, bound to **Prey's own D3D11
// device**, submitting Prey's own backbuffer.
//
// The session probe already proved this code path works, but it created its own
// device on whatever adapter the runtime asked for. That is the easy case. Here
// the device belongs to Prey, was created long before we loaded, and sits on
// whatever adapter Prey chose -- so the adapter question stops being theoretical.
//
// **This is what makes running under xr-sim worth doing.** xr-sim requires
// adapter index 3; Prey creates its device on index 0. That mismatch is exactly
// what `r_overrideDXGIAdapter` (R-052) exists to fix, and the real headset can
// never test it, because there the required adapter *is* index 0 and a broken
// override looks perfectly correct. The simulator hands us the adversarial case
// inside the real game.
//
// We cannot fix a mismatch at runtime: the cvar is read once during device
// creation, long before this DLL loads. So the honest thing is to **detect it
// precisely and say what to set before the next launch**, rather than attempt a
// session that will fail with a message pointing somewhere else.
//
// Bring-up runs the whole XR frame on Prey's render thread at `RT_EndFrame`,
// because that is the only place the backbuffer is valid. That deviates from
// XR-005's game/render split, so the frame contract's rule is opted out of
// explicitly via `AllowSingleThreaded` rather than left to fire every frame --
// and it has to go back before per-eye submission ships.
//
// Never starts on load. Like every other capability here, it is reached only
// through an explicit export.
namespace preyvr::dll {
DWORD SetUiCurveDegrees(unsigned int degrees);

enum class XrSessionStatus : DWORD {
    idle = 0,             // never started
    running = 1,          // session live and submitting
    adapterMismatch = 2,  // Prey's device is on the wrong GPU for this runtime
    unavailable = 3,      // no runtime, no system, or the loader is absent
    failed = 4,
    stopped = 5,
};

// Points the OpenXR loader at a specific runtime manifest, **for this process
// only**, by setting XR_RUNTIME_JSON before the loader is first used.
//
// This is not a test convenience, it is a requirement. Prey is launched by Steam,
// which hands the game Steam's environment rather than ours -- launching
// Prey.exe directly just exits under DRM. So a mod injected into that process
// cannot inherit a runtime selection from whoever started it, and has to make
// the choice itself.
//
// It works because `openxr_loader.dll` is delay-loaded: the loader is not even
// in the process until StartXrSession brings it in, so the variable is read
// fresh. Call this before StartXrSession; afterwards is too late.
//
// Passing null or an empty string clears the override and returns to the
// machine's configured runtime.
//
// **The path is checked for existence**, because a wrong one does not fail --
// the loader silently falls back to the registry runtime and the session runs
// against VirtualDesktopXR while the transcript says xr-sim. That is precisely
// the false pass this project keeps designing against.
DWORD SetXrRuntimeManifest(const char* manifestPath);

// Starts the session. Returns an XrSessionStatus.
//
// On adapterMismatch the log carries the index to set, and nothing is created --
// a session bound to the wrong device either fails at creation or, worse,
// succeeds and silently never displays.
DWORD StartXrSession();

// Requests a stop. The session is torn down from the render thread at the next
// frame boundary rather than here, because the D3D resources it holds belong to
// that thread.
DWORD StopXrSession();
// Runtime loss waits for native feature shutdown before render-thread destruction.
bool XrSessionLossPending();

DWORD XrSessionStatusValue();
// 0=scene only, 1=automatic modal screen, 2=force screen (diagnostic).
DWORD SetUiPanelMode(unsigned int mode);
unsigned int UiPanelMode();
unsigned long long UiPanelFrameCount();
unsigned long long InventoryLayerFrameCount();
float XrRuntimeIpdMetres();
void ObserveXrBackbuffer(void* renderer);
bool XrBackbufferReady(unsigned int width=0,unsigned int height=0);
unsigned long long XrSubmittedFrameCount();

// Arms stereo submission: a real image per eye, and Prey's own frustum declared
// over them instead of the runtime's.
//
// Off by default. Clear it and the path reverts to the flat mirror that proved
// the plumbing -- mono, and declaring the runtime's FOV -- which stays reachable
// so a regression can be bisected against a known state.
// Scales the field the FLAT MIRROR declares for Prey's own pixels, as a
// percentage of what Prey declares. 100 is the honest projection and the
// default; higher makes the image span more of the headset's view.
//
// It exists because the main menu measures at 51.8 degrees against a 98 degree
// runtime view, so it lands in the central 41% and reads as a small window. For
// flat 2D content there is no depth to distort and only text to read; for 3D
// content this is a real distortion. Which is why it is off by default and a
// headset decides. No effect once a held eye pair exists -- the stereo path uses
// the runtime's own per-eye field.
DWORD SetMirrorFovPercent(unsigned int percent);
DWORD MirrorFovPercent();

DWORD SetXrStereoSubmission(unsigned int enabled);

// Sends each eye's image to the other eye's socket. Safe to toggle live.
//
// Insurance against inverted stereo, which does not look broken so much as
// subtly wrong -- depth that will not settle, easily misattributed to judder or
// to the frustum. One call settles it rather than a rebuild.
DWORD SetXrSwapEyes(unsigned int enabled);

// Asks for the sRGB swapchain format rather than the exact match -- the
// FAIL-STR-033 double-encoded-gamma fix.
//
// Must be set BEFORE StartXrSession; the format is chosen once when the
// swapchain is built. Refused on a running session rather than silently doing
// nothing.
DWORD SetXrPreferSrgbFormat(unsigned int enabled);


// Called from the frame observer, on the render thread. Returns immediately when
// no session is running.
void ServiceXrFrame(void* renderer);

// Declared-vs-rendered frustum agreement, counted per submitted eye.
//
// This is the check xr-tape's own CHECKS.md says no API layer can make: it can
// see the FOV the runtime located and the FOV we declared, but never the
// projection the engine actually rendered with. A non-zero diverge count means
// the submitted image is a lie about its own geometry.
// The resolution chain, end to end, so "the image looks soft" becomes numbers.
//
// Fields: 0/1 runtime recommended per-eye WxH, 2/3 runtime maximum, 4/5 Prey's
// backbuffer, 6/7 the held eye texture, 8/9 what is actually submitted, 10 the
// recommended sample count, 11 whether the two views ask for different sizes,
// 12 the held texture's DXGI format, 13 the view count.
//
// The mod builds its swapchain at Prey's backbuffer size and lets the runtime
// scale, so 4/5 and 8/9 agree by construction while 0/1 was never consulted.
// That policy is deliberate and documented; measuring it is the first step
// RE-HEADSET-RESOLUTION asks for, because a scale factor argued from desktop
// settings is not a measurement of either end of this chain.
//
// **This does not measure the scene's own render target.** Prey may render
// internally at another size and resolve before the backbuffer, and a
// supersampling path exists in the binary. A recommendation-to-backbuffer ratio
// bounds what the compositor receives, not what was actually drawn.
// How much of each rendered eye the runtime can actually show, and how much of
// what it asked for we supplied. Two numbers rather than one, because a low
// figure means "reclaimable pixels" only when the request is fully covered;
// with a shortfall the same figure means content is missing. See
// FrustumCoverage.h.
// Frame-stage timing, off by default. `displayHz` sets the missed-deadline
// threshold; arming resets every series so a run's numbers cannot mix frames
// from before a change with frames from after it.
//
// Answers the question nothing else here can: time inside `xrWaitFrame` is the
// runtime pacing us, time in the scene is work we chose. Reclaiming pixels helps
// only the second kind.
// Panel size as a percentage of FitPanel's default caps, 20..200. Those caps
// are a comfort choice and the corner test measures the frustum the runtime
// REPORTS, which is wider than a Quest 3's lenses actually show -- so where the
// usable edge is cannot be derived, only worn. Applies to the menu panel and
// the HUD together.
DWORD SetUiScalePercent(unsigned int percent);
DWORD UiScalePercent();
// The fraction of the RUNTIME-REPORTED frustum a panel must stay inside, 30..100.
// This, not ui.scale, is what caps panel size: the fit loops shrink the panel
// until it satisfies this, so past the point where it binds, raising the scale
// does nothing. Default 72. Raising it trades edge visibility for size.
DWORD SetUiFitMarginPercent(unsigned int percent);
DWORD UiFitMarginPercent();
// The onboarding card stacked under the menu -- ours, not Prey's. It shares the
// menu's vertical budget, so turning it off also makes the menu bigger.
DWORD SetUiGuideEnabled(unsigned int enabled);
DWORD UiGuideEnabled();

DWORD SetXrTimingEnabled(unsigned int enabled, unsigned int displayHz);
std::string XrTimingReport();

// The frustum the RUNTIME asked for, in radians, for one eye. False before
// `xrLocateViews` has ever answered, or if what it answered is degenerate --
// never a default, because a fabricated frustum would be rendered and submitted
// as if it were the headset's own.
bool XrRequestedEyeFov(int eye, float* left, float* right, float* up, float* down);

std::string XrCoverageReport();

DWORD XrResolutionChain(unsigned int field);

unsigned long long DeclaredFovAgreeCount();
unsigned long long DeclaredFovDivergeCount();
DWORD DeclaredFovWorstMilliTan();

} // namespace preyvr::dll
