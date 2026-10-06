#pragma once

#include <windows.h>

#include "preyvr/ShotRay.h"
#include "preyvr/WeaponAim.h"

#include <cstdint>
#include <string>

// `aim.shot`: the weapon's ray starts at the muzzle the shot leaves from, so
// the projectile flies along the direction the player points and lands where
// the drawn ray, the scene reticle and the HUD reticle say it will. The why and
// the native routes are in preyvr/ShotRay.h; measurements in
// docs/SHOT-RAY-2026-10-06.md.
//
// Four parts, each a no-op while the lane is off:
//   - shot time: `GetReticleInfoForFiring` runs with the cached ray's origin at
//     the weapon's firing position (restored on return), so `target - spawn` is
//     the aim direction on every native firearm route (GLOO, pistol/shotgun/
//     crossbow, Disruptor);
//   - shot scope: the pistol/shotgun/crossbow pellet function reads the cached
//     ray again (GetReticlePosition) to centre its pattern; until it returns,
//     on that thread, those reads also start at the firing position;
//   - spawn: when `GetFiringPosition` falls back to the camera, a VR test
//     (eye -> hand -> muzzle clear) puts the shot back at the muzzle;
//   - per frame: the published aim sample starts at the muzzle, so everything
//     drawn from it (scene query, reticles, debug overlay) is the shot's ray.
// The engine's cached ray is never left edited: other consumers (interaction,
// target selection, the wrench) keep the eye-origin ray they were written for.
namespace preyvr::dll {
struct GameplayPoseFrame;

DWORD SetShotRay(unsigned enabled);
unsigned ShotRayEnabled();

// Aim lane, gameplay thread, once the direction is final and before the scene
// query: moves the sample's origin to the equipped weapon's muzzle. Returns
// whether it did (no weapon, an empty ammo helper or the lane off leave it).
bool ShotRayFrame(const GameplayPoseFrame& frame, aim::Sample& sample);

// From the firing-position hook, after the native function returned `result`
// (16 bytes: byte fallback, Vec3 at +4). May rewrite it; returns true if so.
bool ShotRayAdjustFiringPosition(void* weapon, void* result, std::uint32_t flags, void* overrideEntity);

// The last shot, engine world space, for the debug overlay.
struct ShotTrace {
    bool valid = false;
    shot::Route route = shot::Route::Unknown;
    shot::Spawn spawnKind = shot::Spawn::Native;
    Vec3 spawn{};           // where the projectile left (observed when a spawn hook saw it)
    Vec3 target{};          // the query's target
    Vec3 direction{};       // the aim direction the query used
    bool entityHit = false;
    bool projectileSeen = false;
    float angleDegrees = 0; // projectile direction against the aim direction
    float spawnOffLineMm = 0;
    std::uint64_t ns = 0;
};
bool TryGetLastShot(ShotTrace& out);

std::string ShotRayReport();

// `aim.beam`: the Q-Beam's last beam, observed only (installs the observers).
// Pairs the damage raycast with the drawn beam and compares both with the drawn
// muzzle, the aim direction and the aim lane's scene hit (the amber marker).
std::string BeamReport();

// Research: records the return addresses of every read of the cached reticle
// ray (IArkPlayer::GetReticleViewPositionAndDir) while on. 1 starts afresh.
DWORD SetShotProbe(unsigned enabled);
std::string ShotProbeReport();
}  // namespace preyvr::dll
