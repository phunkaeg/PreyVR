#pragma once

#include <windows.h>

#include <string>

// Rung 1b: submit the frame's one render view twice instead of rendering one eye
// per frame.
//
// **Why this seam.** Every rung-1 attempt (A3-A7, F-013..F-016) doubled the
// *traversal* -- `CSystem::Render` or `RenderWorld` -- and each leaked or wedged,
// because the prepare step allocates once per traversal and is freed once per
// frame. CryEngine's own stereo renderer traverses once and calls
// `RT_RenderScene` once per eye (CRYENGINE_SOURCE_FINDINGS.md). That function
// only consumes the view, so doubling it cannot repeat the prepare.
//
// **What RT_RenderScene does to the view, read from its decompile (0xF41CA0).**
// It switches the view to Reading on entry, renders, then on exit switches the
// view and each shadow-frustum view to ReadingDone and **clears** the view
// (`CRenderView::Clear`, 0xEE4030). A naive second call would therefore draw an
// empty view. So the first submission's teardown is *deferred*: its two
// ReadingDone switches and its Clear are skipped, the view stays in Reading, and
// the second submission's own entry switch is a no-op (switching to the current
// mode returns at once) while its exit performs the one real teardown.
//
// **The deferral is gated three ways**, so nothing else in the engine can be
// affected: a thread-local flag set only around the first submission, the
// requested mode (2, ReadingDone), and the exact return address of each of the
// three teardown calls inside RT_RenderScene. If the first submission's teardown
// did not pass through those gates exactly as expected, the second submission is
// refused and the mode disarms itself rather than rendering a cleared view.
//
// **Stages, each a separate switch, in the order they must be run.**
//   observe   pass-through; counts and logs the submissions the engine makes.
//   mode 1    the CONTROL: submit the main view twice with the SAME camera. The
//             two images must match and the session must stay healthy; only then
//             does a camera difference mean anything (BN-SFX-001, A5's design).
//             Passed 2026-10-10: 16,000 doubled frames, no refusal, flat memory,
//             images identical except temporal AA history and animated screens.
//   mode 2    per-eye cameras. Each eye is the frame's camera moved half the IPD
//             along its right axis, installed through the engine's own
//             CD3D9Renderer::SetCamera (which on the render thread loads the
//             SThreadInfo camera block and the shared view/projection stacks and
//             runs RT_SetCameraInfo) and CRenderView::SetCamera. Each submission
//             gets the frame's SThreadInfo with that camera block. The frame's
//             camera is restored after the second submission. Culling is still
//             the frame's single camera.
//
// Off by default. Nothing here changes the frame until a mode is armed. Not yet
// wired into OpenXR submission: the headset still receives alternate-eye frames.
namespace preyvr::dll {

// Returns 0 when the hooks are installed (or already were), otherwise a nonzero
// reason: 1 landmark missing or disagrees, 2 prologue mismatch, 3 MinHook failed.
DWORD SetDualSceneObserve(unsigned int enabled);

// 0 off, 1 same-camera double submission (the control), 2 per-eye cameras.
// `frameBudget` bounds how many frames are doubled before the mode disarms
// itself; 0 keeps the previous budget. Returns 0 armed, 4 unknown mode,
// otherwise as above.
DWORD SetDualSceneMode(unsigned int mode, unsigned int frameBudget);

// Eye separation for mode 2, 40..90 mm (default 64). Returns 1 if out of range.
DWORD SetDualSceneIpdMillimetres(unsigned int millimetres);

// Arms a one-shot capture of the next top-level scene submission(s): one dump
// after each submission of a doubled frame (`dual-eye0`, `dual-eye1`), or one
// after a single submission (`dual-single`) when no mode is armed. Same GPU
// stall as any frame capture, so never during a timing measurement.
DWORD RequestDualSceneCapture(unsigned int tag);

// Counters, timings and the in-flight step, in key=value form. Safe to call from
// any thread, including while the render thread is stuck inside a submission.
std::string DualSceneReport();

} // namespace preyvr::dll
