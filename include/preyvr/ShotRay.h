#pragma once

#include "preyvr/VrMath.h"

#include <cstdint>

// One ray for the weapon in the hand: from the MUZZLE the shot leaves, along the
// direction the player points. The aim marker, the scene reticle and the shot all
// take it from here.
//
// **The defect this removes (measured 2026-10-06, docs/SHOT-RAY-2026-10-06.md).**
// Every native firearm route asks `CArkWeapon::GetReticleInfoForFiring`
// (`0x1694890`) for a target: it raycasts the player's cached reticle ray
// (ArkPlayer +0x17D4 origin, +0x17E0 direction) and returns the hit. The weapon
// then fires from its own firing position (`0x1694BC0`, the ammo helper) TOWARD
// that hit. The aim lane writes the controller's direction into the cache but
// leaves the origin at the eye, so the target lies on a line through the EYE
// parallel to the controller, and the projectile converges from the muzzle onto
// it: about 20 cm off at 4 m with the hand at the hip, worse closer, and the
// error grows with the hand's distance from the eye.
//
// The fix is the identity it implies: if the query's ray starts at the very
// point the projectile spawns from, `target - spawn` IS the aim direction.
// Nothing about the shot is computed by us -- range, collision mask, entity
// hits, spread and point-blank handling all stay the engine's.
namespace preyvr::shot {

// The four native callers of `GetReticleInfoForFiring`, by return address. All
// four aim the projectile at `target - origin`, three of them with origin from
// `GetFiringPosition`.
enum class Route : std::uint8_t {
    Unknown,
    Gloo,       // CArkWeaponGooGun::OnPreRender 0x169F420 (firing position first)
    Ballistic,  // shotgun/pistol/toy-gun StartAttack 0x16AAFF0 (firing position after)
    Route13BB,  // 0x13BB530: query, then firing position
    Helper16A2, // 0x16A29E0: query only under a condition; origin = the ammo helper itself
};
inline constexpr std::uint32_t kGlooQueryReturn = 0x169F492;
inline constexpr std::uint32_t kBallisticQueryReturn = 0x16AB0C3;
inline constexpr std::uint32_t kRoute13BBQueryReturn = 0x13BB791;
inline constexpr std::uint32_t kHelper16A2QueryReturn = 0x16A2CC8;
Route RouteFromReturnRva(std::uint32_t rva);
const char* RouteName(Route route);
// The physics-ray flags that caller passes to `GetFiringPosition`, so a firing
// position we compute ahead of it is the one it will get.
std::uint32_t FiringPositionFlags(Route route);

// Geometry of one shot against the ray the player was shown.
float AngleDegrees(Vec3 a, Vec3 b);
// Perpendicular distance from `point` to the infinite line through `origin`
// along unit `direction`.
float DistanceToLine(Vec3 point, Vec3 origin, Vec3 direction);
struct Metrics {
    // Projectile direction (target - spawn) against the aim direction.
    float angleDegrees = 0;
    // How far the spawn sits off the drawn ray's line.
    float spawnOffLineMetres = 0;
    // How far the target sits off the drawn ray's line.
    float targetOffLineMetres = 0;
    float distanceMetres = 0;   // spawn to target
    bool valid = false;
};
Metrics Measure(Vec3 spawn, Vec3 target, Vec3 rayOrigin, Vec3 rayDirection);

// Where a converging shot misses: the projectile flies from `spawn` toward the
// point at `distance` along the ray from `rayOrigin`; returns how far from the
// aimed line (`spawn` + aim direction) it is at that distance. Zero when the
// ray starts at the spawn -- the property this lane exists for.
float ConvergenceErrorMetres(Vec3 spawn, Vec3 rayOrigin, Vec3 direction, float distance);

// --- the spawn point ------------------------------------------------------
//
// `GetFiringPosition` returns the muzzle helper unless one of two flat-screen
// safety checks fails, and then the CAMERA: an obstacle within a short reach in
// front of the player's body, or anything between the camera and the muzzle.
// Both describe a gun held in front of the face. In VR the hand is elsewhere: a
// wall straight ahead says nothing about a weapon held out to the side or round
// a corner, yet it moves the shot's origin into the player's head.
//
// The VR test asks the physical question instead: can the muzzle be reached from
// the eye through the arm? Eye-to-hand and hand-to-muzzle both clear means the
// weapon is in open air where it is drawn, and the shot leaves its muzzle.
// Either blocked means the barrel really is in a wall, and the engine's own
// fallback is kept -- that is what stops shots through walls.
// Native: the engine used the muzzle itself. Restored: it fell back to the
// camera and the VR test put the shot back at the muzzle. Blocked: fallback kept.
enum class Spawn : std::uint8_t { Native, Restored, Blocked };
struct SpawnInput {
    bool nativeFallback = false;  // GetFiringPosition result byte +0
    bool haveMuzzle = false;      // the ammo helper's world position was read
    bool haveHand = false;        // a tracked grip in world space
    bool eyeToHandClear = false;  // only meaningful with haveHand
    bool handToMuzzleClear = false;
};
Spawn DecideSpawn(const SpawnInput& input);
const char* SpawnName(Spawn spawn);
// Segment queries are bounded; a long reach checks only the part nearest `to`.
Vec3 SegmentStart(Vec3 from, Vec3 to, float maxLength);

} // namespace preyvr::shot
