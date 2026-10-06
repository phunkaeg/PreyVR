#pragma once

#include "preyvr/VrMath.h"

#include <cstddef>
#include <cstdint>

// "Draw near" particles (weapon charge glows, the Q-Beam's inner beam, muzzle
// flashes, psi screen effects) in the same camera space as the weapon.
//
// **The defect (measured 2026-10-06, docs/NEAR-PARTICLES-2026-10-07.md).** The
// near pass draws camera-space content: the arms and the held weapon are posed
// relative to the camera, and the pass's view-projection carries no camera
// translation (view-info +0xA0). NearViewStereo gives that content its parallax
// by adding the eye's offset to that view-projection. A particle effect authored
// `DrawNear` is simulated in WORLD space, so the engine converts it to camera
// space itself: `CParticle::RenderGeometry` (0x1B13A0, at 0x1B1A7F) does
// `if (params.bDrawNear) position -= context.m_vCamPos` -- the CryEngine 5.1
// "DrawNear is now rendered in camera space" step. `m_vCamPos` is the camera of
// the pass being rendered, which in alternating stereo is the EYE camera, half an
// IPD from the camera the game built the frame (and the weapon's helpers) around.
// The particle therefore arrives in camera space already shifted by the eye
// offset, and NearViewStereo shifts it once more: twice the weapon's parallax.
// The Disruptor's coil glow was drawn ~105 px more crossed than the coil, i.e.
// floating a hand's width in front of the face.
//
// The fix is the convention the weapon already follows: a DrawNear particle is
// made camera-relative to the CYCLOPS camera the eye was offset from, and the
// near pass then gives it exactly the weapon's parallax. Nothing else about the
// particle changes; non-DrawNear particles, and every pass whose camera is not a
// stereo eye (shadows, cubemaps, a mono frame), are left exactly as they were.
//
// Two native paths, two seams (both observed static, then live):
//   - GEOMETRY particles (the Disruptor's inner mesh glow) are converted on the
//     CPU with the vertex context's `m_vCamPos`: the context gets the cyclops.
//   - SPRITE particles (sparks, bolts, flares, the Q-Beam's inner beam) are
//     written in WORLD space (`CParticle::SetVertices` 0x1B2B90 ->
//     `GetRenderMatrix` 0x1AF210, vT = the vertex position) and made
//     camera-relative later by the renderer with the eye camera. Their vertex
//     position is moved by the eye's offset (eye - cyclops), so the renderer's
//     subtraction leaves `world - cyclops`.
namespace preyvr::nearfx {

// `SParticleVertexContext::Init(float fMaxContainerPixels, CParticleContainer*)`.
// The three vertex-context builders -- sprites (0x1AE9D0, return 0x1AEA76),
// geometry (`CParticleContainer::RenderGeometry` 0x1B1D10, return 0x1B1EFC) and
// 0x1B0BF0 (return 0x1B0C88) -- copy the pass camera's translation into
// `m_vCamPos` and then call it, and Init derives the camera-distance offset
// from it. Only the geometry builder's context is corrected: its consumer
// (`CParticle::RenderGeometry`) is the one proven to subtract it for DrawNear.
inline constexpr std::uint32_t kVertexContextInitRva = 0x1AFF70;
inline constexpr std::uint32_t kSpriteContextReturn = 0x1AEA76;
inline constexpr std::uint32_t kGeometryContextReturn = 0x1B1EFC;
inline constexpr std::uint32_t kOtherContextReturn = 0x1B0C88;
// SParticleVertexContext: +0x00 `ResourceParticleParams const& m_Params`,
// +0x18 `Vec3 m_vCamPos` (Init reads both; RenderGeometry subtracts +0x18).
inline constexpr std::size_t kContextParams = 0x00;
inline constexpr std::size_t kContextCameraPosition = 0x18;
// ParticleParams `bDrawNear` (the type-info table registers "bDrawNear" at
// +0x4C8, "bDrawOnTop" at +0x4C9; RenderGeometry tests +0x4C8).
inline constexpr std::size_t kParamsDrawNear = 0x4C8;

// `CParticle::GetRenderMatrix(this, vX, vY, vZ, vT, loc, renderData, context,
// bool, bool)`: vT is the particle's position. Sprite callers, by return address.
inline constexpr std::uint32_t kGetRenderMatrixRva = 0x1AF210;
inline constexpr std::uint32_t kSetVerticesReturn = 0x1B2D69;      // CParticle::SetVertices
inline constexpr std::uint32_t kSetTailVerticesReturn = 0x1B279E;  // CParticle::SetTailVertices
bool IsSpriteVertexCaller(std::uint32_t rva);

enum class Builder : std::uint8_t { Unknown, Sprites, Geometry, Other };
Builder BuilderFromReturnRva(std::uint32_t rva);
const char* BuilderName(Builder builder);

// One stereo eye as CameraEditHook built it: where its camera ended up and the
// cyclops camera it was offset from.
struct EyeCamera {
    Vec3 position;
    Vec3 centre;
};

// The camera a DrawNear particle must be made camera-relative to. When `camera`
// is that eye's camera (same bits, within `tolerance`), the cyclops; otherwise
// false and the engine's camera stands.
bool CameraSpaceOrigin(Vec3 camera, const EyeCamera& eye, Vec3& out, float tolerance = 1e-4f);

// The amount a world-space DrawNear vertex must move so that the renderer's
// `vertex - eye camera` equals `world - cyclops`: eye minus cyclops. False (and
// no move) under the same conditions as CameraSpaceOrigin.
bool EyeOffset(Vec3 camera, const EyeCamera& eye, Vec3& out, float tolerance = 1e-4f);

// --- the Q-Beam's inner beam: a flat-screen bridge --------------------------
//
// Measured 2026-10-07 against the flat game (VR off, same save). The near pass
// draws the weapon at r_DrawNearFoV, so in flat its nozzle is at the bottom of
// the screen while the world beam starts higher up, at the muzzle helper. The
// Q-Beam's `InnerBeam_00` / `InnerBeamStart_00` (PlayerWeapons.xml, in
// InstaLaser_Start and its siblings) -- DrawNear GEOMETRY, a plane with the
// inner-laser material -- sit `PositionOffset y=-0.27..-0.6` BEHIND the
// emitter along its +Y (the barrel): the stretch of camera space that in flat
// projects between the drawn nozzle and the world beam, so the core of the
// beam appears to leave the nozzle. In VR the near pass projects like the
// world, nozzle and beam coincide, and that stretch lies back along the gun
// toward the player: "the beam comes out of the bottom of the weapon"
// (captures 2026-10-06/07; hiding DrawNear particles leaves the beam clean).
//
// The rule keeps the look and puts it where VR needs it: a DrawNear geometry
// particle authored at least kBridgeMinBack behind its emitter is mirrored
// through the emitter along the barrel -- the same core, leaving the muzzle and
// running FORWARD along the beam. Nothing else in the game's DrawNear set
// matches (every other negative-Y DrawNear is a sprite on the weapon itself).
//
// ResourceParticleParams `vPositionOffset` (type-info registration 0x18A400:
// name "vPositionOffset", offset 0x6C, the next field at 0x78).
inline constexpr std::size_t kParamsPositionOffset = 0x6C;
inline constexpr float kBridgeMinBack = 0.15f;
// `positionOffset` in emitter space (+Y = the barrel), `forward` the emitter's
// +Y in world space (unit). True with the world translation that mirrors the
// particle through its emitter along the barrel: forward * (-2 * offset.y).
bool BridgeShift(Vec3 positionOffset, Vec3 forward, Vec3& shift);

} // namespace preyvr::nearfx
