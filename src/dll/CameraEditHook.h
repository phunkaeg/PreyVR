#pragma once

#include <Windows.h>

// The first write to Prey's render path, as a bounded and reversible experiment.
//
// **Why CSystem::Render and not CSystem::SetViewCamera.** The game rewrites
// m_ViewCamera every frame from ArkPlayerCamera, so a write issued at
// RT_EndFrame is simply overwritten before the next render reads it. The edit
// has to happen inside the window between the camera being set and the render
// consuming it. CSystem::Render (R-058) *is* that window: it reads m_ViewCamera
// and hands it to both consumers, so wrapping it gives
//
//     save -> modify -> render -> restore
//
// which is also the shape that generalises to stereo (call the original twice
// with two cameras). SetViewCamera itself is a 12-byte function ending in a tail
// jump -- a poor hook target for no benefit.
//
// **Why the frustum is rebuilt on our own copy.** CCamera caches derived state:
// eight frustum corners, six planes at +0x10C, plane sign tables, and the
// position at +0x230. Writing the matrix alone leaves all of it stale. So the
// edit is applied to a local copy, CCamera::UpdateFrustum (RVA 0x121D70) is
// called *on that copy*, and only then are the 0x240 bytes blitted into the
// game. The engine function therefore only ever touches our stack, and the sole
// write to game memory is one bounded memcpy with the original bytes held.
//
// **The hook stays installed and becomes a pass-through when disarmed**, rather
// than being removed. F-009 recorded that our observer's disable path did not
// restore the target prologue on a live host, so a design that depends on
// unhooking would be depending on something we have never observed working.
namespace preyvr::dll {

enum class CameraEditStatus : DWORD {
    unavailable = 0,  // signatures did not match, or MinHook refused
    ready = 1,        // hook installed, currently a pass-through
    armed = 2,        // an edit is being applied every frame
    failed = 3,
};

// Installs the hook if needed and arms a bounded yaw, in degrees. Passing 0
// disarms without removing the hook. Rejects anything outside the bound in
// preyvr::cameraedit.
DWORD SetCameraYawEdit(float degrees);

// Alternating-eye stereo, without any double-render.
//
// **This is the point of the design.** Validating per-eye camera construction
// and validating that Prey can render twice in one frame are separate problems,
// and only the second one is risky. With the scene frozen (`t_Scale 0`), a
// left-eye frame followed by a right-eye frame *is* a stereo pair -- so the whole
// eye-construction path can be proven correct, and looked at through
// New-StereoView, before anyone calls the render function twice.
//
// The eye offset is composed onto the engine's *current* camera rather than
// replacing it, so this exercises the real path including the game's own pitch,
// roll and position. `ipdMetres` of 0 disarms.
//
// The FOV is synthetic and deliberately asymmetric per eye, mirrored the way a
// real headset reports, so the asymmetry-shift path is exercised too rather than
// being left untested until a headset is attached.
DWORD SetSyntheticStereo(float ipdMetres, float halfFovDegrees);

// Scales how asymmetric the synthetic per-eye frustum is. 1.0 makes both eyes
// symmetric, so the *only* difference between them is the eye offset.
//
// Needed because the first A2 run could not be judged. With the default 1.1 the
// two eyes' frusta differ by ten degrees, which at 2560 px across a 100-degree
// field is about 256 px of uniform sideways shear -- and that completely masked
// the 64 mm eye separation it was supposed to be measuring. Two effects in one
// image is one effect too many.
//
// Set 1.0 to measure the eye offset alone; restore 1.1 to exercise the asymmetry
// path separately.
DWORD SetStereoAsymmetry(float outerScale);

// Locks synthetic stereo to one eye instead of alternating.
//
// **This replaces the per-frame tagging, which did not work.** The camera hook
// runs on the game thread and the capture runs on the render thread, and the two
// are offset by the engine's MT/RT double buffer -- so a tag written by one is
// not reliably read by the other for the same frame. Observed live 2026-08-31:
// two captures reported as eye 1 then eye 0 both landed in files tagged 1, which
// is exactly the silently-swapped pair the tagging existed to prevent.
//
// Compensating for the offset would mean guessing at it. Locking the eye removes
// it: hold one eye, let a few frames pass, capture, then hold the other. The
// signal is now far longer than the uncertainty, so no ordering assumption is
// needed at all.
//
// 0 locks left, 1 locks right, any other value returns to alternating.
DWORD SetStereoEyeLock(unsigned int eye);

// Which eye the most recent render used, or -1.
//
// **Reported for observation only -- do not identify a capture by it.** It is
// written on the game thread and any capture reads it on the render thread, and
// the two are a frame or so apart. Use SetStereoEyeLock instead; that is the
// whole reason it exists.
int LastRenderedEye();

// **The double-render experiment: native stereo, or not.**
//
// Calls the original CSystem::Render twice inside one frame, once per eye. This
// is the one remaining architectural unknown for native stereo, and it is the
// risky one -- everything else can be validated by the alternating-eye mode
// above, which is why that exists and why this is separate.
//
// What it answers: whether Prey survives rendering the world twice in a frame at
// all, and what it costs. What it does **not** answer: whether the per-eye
// cameras are correct. A2 answers that, and it should be run first, because a
// crash here would otherwise be ambiguous between "cannot render twice" and
// "the second camera was malformed".
//
// `frameBudget` is a hard ceiling: the mode disarms itself after that many
// frames, so a crash-prone experiment cannot run away while a person reaches for
// the keyboard. There is no unbounded option on purpose.
//
// Note that the second render overwrites the backbuffer, so the presented image
// is the right eye. A capture taken here is therefore *one* eye, not a pair --
// use the alternating mode for pairs.
DWORD SetDoubleRenderStereo(float ipdMetres, float halfFovDegrees, unsigned int frameBudget);

unsigned long long DoubleRenderedFrameCount();

// **Settles R-072, which decides whether native stereo is possible at all.**
//
// `SRenderingPassInfo`'s render view comes from `pRenderer->vtable[0x198](slot,
// type)` (R-071), and every pass Prey builds asks for type 0. CryEngine's
// `EViewType` puts Recursive at 1. If type 1 hands back a *different* view, a
// second per-eye pass can own its own view and `RenderWorld` can be called twice
// without the re-entry that wedged the engine in F-013. If it hands back the same
// pointer, that whole approach is dead and we need another one.
//
// Runs **once, inside the render hook**, because that is the thread the engine
// itself calls this from; calling a renderer method from an arbitrary thread is
// how a probe becomes the bug it was looking for. Nothing is written and no pass
// is built -- it asks for two pointers and compares them.
DWORD ProbeRenderViews();

enum class RenderViewProbeStatus : DWORD {
    notRun = 0,
    viewsDiffer = 1,     // recursive view is distinct -- R-072 holds
    viewsIdentical = 2,  // one view for both types -- R-072 refuted
    unresolved = 3,      // renderer or vtable could not be reached; nothing was called
    recursiveUnavailable = 4, // type 1 returned null; distinct from "same view"
};

DWORD RenderViewProbeStatusValue();

// The sub-step the stereo path was last in, as a string literal. See F-014.
const char* StereoStepName();

// **A4 -- the recursive second pass. The successor to A3, at the right layer.**
//
// A3 re-entered `CSystem::Render`, which forced both eyes through one
// `SRenderingPassInfo` and one `CRenderView`. The second pass then culled against
// state the first had already consumed, the world vanished leaving only skybox,
// and the engine wedged (F-013).
//
// This does not re-enter anything. The original `CSystem::Render` runs untouched
// with the game's own camera, and *afterwards* one extra `RenderWorld` (R-054) is
// issued with:
//
//   * our own 64-byte `SRenderingPassInfo`, built by the engine's own
//     `CreateGeneralPassRenderingInfo` (R-071, byte-gated as `pass.create_general`)
//   * an eye-offset camera in our own memory, never the game's
//   * **the recursive render view** rather than the default one (R-072), so the
//     two passes share no per-frame view state
//
// That last point is the whole hypothesis. R-072 measured the recursive view as a
// distinct object on both thread slots, and `CRenderer::GetRenderViewForThread`
// disassembles to a plain indexed load with no allocation, so obtaining it is
// free of side effects.
//
// **What this answers and what it does not.** It answers whether the engine
// survives a second world render given its own view -- which is the last
// architectural unknown. It does *not* yet composite or submit anything: the
// second pass renders over the first in the same backbuffer, so the presented
// image is the second eye. Making a submittable pair is the next problem, and
// bundling it into this one would make a failure uninterpretable.
//
// **KNOWN WRONG IN ITS FRAME PLACEMENT -- see docs/STEREO_RENDER_ARCHITECTURE.md.**
// Prior art from two injected native-stereo mods gives the frame flow as: both
// eye world-renders, then HUD exactly once, then present exactly once. This runs
// the original in full -- world, HUD and present -- and appends a second world
// render afterwards, so the second eye lands after the HUD and after the present,
// and composites over a finished frame instead of a cleared target.
//
// It is still worth running as the architectural probe it was built to be: it
// answers whether the engine survives a second RenderWorld given its own view,
// which is the A3 question at the right layer. It is **not** a stereo path, and
// must not be treated as one or submitted from.
//
// Bounded by both a frame budget and the wall-clock watchdog, because F-013's
// lesson was that a budget in frames cannot bound a failure that stops frames.
// Pass ipd 0 or budget 0 to disarm.
// `zeroCameraDelta` runs the control from BN-SFX-001's fast_test: the second
// pass is built and dispatched identically, but with a zero eye offset, so the
// second image should equal the first. Anything that changes is a side effect of
// repeating the pass rather than stereo -- which is the only way to tell those
// two apart. Run it before the eye-delta test, not after.
DWORD SetSecondPassStereo(float ipdMetres, float halfFovDegrees, unsigned int frameBudget,
                          bool zeroCameraDelta);

unsigned long long SecondPassFrameCount();

// How many second passes advanced the renderer's own frame ids. **Non-zero is a
// positive detection of a per-frame side effect** and fails BN-SFX-001's gate;
// zero is necessary but not sufficient, since it sees renderer bookkeeping only.
unsigned long long SecondPassFrameIdMovedCount();

enum class SecondPassStatus : DWORD {
    idle = 0,
    armed = 1,
    ranAtLeastOnce = 2,
    refusedUnresolved = 3,  // renderer, process or pass creator could not be reached
};

DWORD SecondPassStatusValue();

DWORD CameraEditStatusValue();

// Counts frames on which the edit was actually applied and the restore verified
// byte for byte. A frame that applied but failed to restore increments the
// failure counter instead, and disarms -- one unverified restore is enough to
// stop, because every later measurement in the session would be suspect.
unsigned long long CameraEditAppliedCount();
unsigned long long CameraEditRestoreFailureCount();

} // namespace preyvr::dll
