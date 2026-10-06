#pragma once

#include <windows.h>

#include <string>

// `near.fx`: "draw near" particles in the weapon's camera space, so the near
// pass gives them the weapon's parallax instead of twice it. The defect and the
// native contract are in preyvr/NearParticles.h; measurements in
// docs/NEAR-PARTICLES-2026-10-07.md.
//
// Two hooks, both prologue-gated and only active for DrawNear particles whose
// pass camera is a stereo eye CameraEditHook built:
//   - `SParticleVertexContext::Init` (0x1AFF70), geometry builder: the context's
//     camera becomes that eye's cyclops before Init and RenderGeometry use it,
//     and the Q-Beam's inner beam (a flat-screen bridge authored behind the
//     muzzle) is mirrored ahead of it (near.fxbridge);
//   - `CParticle::GetRenderMatrix` (0x1AF210), sprite callers: the world-space
//     vertex moves by the eye offset.
// On by default with the near-pass stereo; `near.fx 0` restores the native
// behaviour for A/B in the same session, PREYVR_NEAR_FX=0 leaves it unhooked.
namespace preyvr::dll {

// 1 corrects (default), 0 forwards everything. Installs the hook on first use.
DWORD SetNearParticleStereo(unsigned enabled);
unsigned NearParticleStereoEnabled();
// 1 (default): the Q-Beam inner beam leaves the muzzle forward. 0: native (behind it).
DWORD SetNearParticleBridge(unsigned enabled);
// Research: 1 moves every DrawNear particle far away (which parts are they?).
DWORD SetNearParticleHide(unsigned hide);
// Installs the hook if the requested state is on. Called when the near-pass
// stereo arms; a failure leaves particles native and is reported, never fatal.
void EnsureNearParticleStereo();
std::string NearParticleStereoReport();

} // namespace preyvr::dll
