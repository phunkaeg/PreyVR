#pragma once

#include <windows.h>

// Answers one question: is the camera the engine culls with the camera we wrote?
//
// **Why it is worth an instrument rather than an argument.** With head rotation
// applied to `CSystem::m_ViewCamera`, the rendered view tracks the headset and
// the cull frustum follows the mouse. Everything on paper says that should be
// impossible: R-071 is called from `CSystem::Render` as
// `CreateGeneralPassRenderingInfo(local_58, &m_ViewCamera, 0x2e5df, 0)` -- handed
// `m_ViewCamera` **directly** -- and CryEngine's own source sets the pass camera
// to whatever it is handed unless `e_CameraFreeze` is set, which it is not.
//
// So either the paper is wrong about this binary, or something rewrites
// `m_ViewCamera` between our edit and that call. Those have different fixes and
// look identical from outside, which is exactly when to measure instead of
// reason.
//
// This hooks R-071 read-only, reads the forward axis of the camera actually
// handed to it, and compares it against the forward axis the camera edit last
// wrote. It changes nothing.
//
// Interpreting the result:
//
// - **agrees** -- our write does reach the pass camera, so culling is not driven
//   from it and there is a separate cull camera, the SS2VR shape (CAM-002).
// - **disagrees** -- our write is being lost or bypassed before the call, and the
//   angle says by how much.
//
// The playbook's rule either way is that widening a render projection cannot
// resurrect geometry the CPU never submitted; the fix is to drive whatever camera
// is authoritative for visibility.
namespace preyvr::dll {

DWORD SetPassCameraProbeEnabled(unsigned int enabled);

unsigned long long PassCameraSamples();

// Samples where the pass camera's forward axis matched what the camera edit
// wrote, within a degree.
unsigned long long PassCameraAgreements();

unsigned long long PassCameraDisagreements();

// The most recent angular difference, in thousandths of a degree. Large and
// varying with head movement means the pass camera is not ours.
unsigned long long PassCameraLastDifferenceMillidegrees();

unsigned long long PassCameraMaxDifferenceMillidegrees();

} // namespace preyvr::dll
