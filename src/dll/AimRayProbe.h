#pragma once

#include <windows.h>

// Measures whether per-eye stereo is contaminating Prey's cached aim ray.
//
// **This is an acceptance check the project already required and never ran.**
// H-008 records it directly: `ArkPlayer::UpdateCachedReticleViewPosAndDir`
// (R-011) rebuilds the cached aim ray by unprojecting through
// `ISystem::GetViewCamera()` -- the *global* view camera, not ArkPlayer's own --
// and its one caller is `CArkUIHUD::OnPreRender`, which runs during render
// preparation. H-008's conclusion was that setting the system view camera per eye
// would feed R-011 an eye-specific camera and corrupt ArkPlayer `+0x17D4` /
// `+0x17E0`, and that those two fields were therefore "a required before/after
// acceptance check".
//
// **The current build does exactly what that warning describes.** The camera edit
// writes the eye camera into `CSystem::m_ViewCamera`, runs the whole of
// `CSystem::Render`, and restores afterwards -- so the eye camera is live for the
// entire render, including whenever `OnPreRender` happens to run.
//
// It could not be settled by reading the binary: `OnPreRender` is dispatched
// through a vtable and has no static callers, so whether it lands inside that
// window is a live-timing question. H-008 said as much. This turns it into a
// number.
//
// **What the number means.** Under alternate-eye the two cameras differ by one
// interpupillary distance, so if the aim ray is being rebuilt from whichever eye
// is current, the ray origin will jump by roughly the full IPD between
// consecutive frames -- about 64000 micrometres at the configuration in use.
// A player turning their view also moves the ray, so the test is run with the
// player still: then a gap near the IPD is contamination, and a gap near zero is
// not.
//
// Consumers of that ray are not cosmetic. Weapon firing (R-014), target
// selection (R-022) and wrench hits (R-020) all read it, so contamination shows
// up as flickering interaction prompts and inconsistent targeting rather than as
// anything that looks like a rendering bug.
namespace preyvr::dll {

DWORD SetAimRayProbeEnabled(unsigned int enabled);

// Called once per rendered frame from the frame observer. Returns immediately on
// a single atomic load when the probe is disarmed.
void SampleAimRay();

unsigned long long AimRaySampleCount();

// Frames where the player or the ray could not be read. A high count means the
// probe is measuring nothing, which must not be mistaken for a clean result.
unsigned long long AimRayUnreadableCount();

// The largest jump in ray origin between consecutive frames, in micrometres.
// **This is the headline number**: near the IPD means contaminated.
unsigned long long AimRayMaxOriginGapMicrometres();

// The largest change in ray direction between consecutive frames, in
// thousandths of a degree.
unsigned long long AimRayMaxAngleGapMillidegrees();

} // namespace preyvr::dll
