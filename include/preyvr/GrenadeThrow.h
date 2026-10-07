#pragma once

#include "preyvr/VrMath.h"

// Grenades thrown with the right hand (EMP charge, Recycler charge, Nullwave
// transmitter, Lure): the trigger holds the grenade, letting go throws it with
// the hand's own motion -- speed, direction and spin -- the way VR games throw
// (Half-Life: Alyx, Boneworks, Saints & Sinners) instead of along the view at
// a charged speed.
//
// The native throw (CArkWeaponGrenade, Steam PreyDll.dll): pressing fire starts
// a "charge" timer, releasing plays the throw animation, and its "Throw" event
// calls ThrowGrenade, which spawns the projectile at the weapon's firing
// position, aims it at the reticle and sets its speed to
// lerp(fMinSpeed, fMaxSpeed, charge). The mod keeps the native spawn, ammo,
// sounds and projectile, and replaces three things (GrenadeLane.cpp):
//   * WHEN it leaves: at the trigger's release, not after the wind-up animation.
//   * FROM WHERE: the grenade in the hand (pulled back out of a wall the hand is in).
//   * HOW FAST: the decision below.
//
// Pure decisions, testable offline.
namespace preyvr::grenade {

struct ThrowSettings {
    // **Where a real one would land.** VR throws are weaker than real ones --
    // nothing in the hand weighs anything, the play space is small, the arm
    // stops short of a real follow-through -- so VR games scale the release a
    // little (SteamVR's Throwable: 1.1-1.5): vrFactor is that, the speed a real
    // ball would have left the same hand with.
    float vrFactor = 1.3f;
    // And a Prey grenade is not a real ball: measured 2026-10-07 (EMP charge,
    // frame by frame), it falls at ~12.7 m/s^2 and its air damping takes
    // ~0.85 of its velocity per second. With parity on, the speed is solved so
    // the grenade comes down on the floor where a real ball thrown with that
    // speed would have (gain 1.7-3 depending on speed and angle), keeping the
    // hand's direction. Off: a fixed gain.
    bool parity = true;
    float gravity = 12.7f;     // the grenade's own, m/s^2
    float damping = 0.85f;     // 1/s
    float fixedGain = 1.5f;    // parity off
    // Below dropSpeed the hand is setting the grenade down: it leaves with the
    // hand's own velocity, unscaled (opened still, it falls at the feet). From
    // throwSpeed up the full throw applies; in between it eases in, so the
    // speed the grenade leaves at always grows with the hand's (no step).
    float dropSpeed = 0.8f;
    float throwSpeed = 2.0f;
    // The fastest it may leave relative to the player: the weapon's own
    // fMaxSpeed times this (the range the game designed for the grenade), or
    // fallbackMaxSpeed when the weapon's is unknown. Approached smoothly from
    // kneeFraction of it: a harder throw always goes a little further.
    float maxOverNative = 1.15f;
    float fallbackMaxSpeed = 40.0f;
    float kneeFraction = 0.8f;
    float maxSpin = 20.0f;   // rad/s
    // The grenade sits in the fist, off the grip pose's origin: a wrist flick
    // moves it faster than the grip origin (v + w x r). Off = the grip origin's.
    bool lever = true;
};

struct ReleaseInput {
    Vec3 handVelocity{};   // world, m/s: the grip pose's (peak of the last ~0.1 s)
    Vec3 handAngular{};    // world, rad/s
    Vec3 lever{};          // world, m: grenade centre - grip pose origin
    Vec3 player{};         // world, m/s: the player's own velocity
    float nativeMax = 0;   // the weapon's fMaxSpeed, m/s (0 = unknown)
    float height = 1.3f;   // m, the grenade above the floor it would land on
};

struct Release {
    bool thrown = false;    // false: a drop (no gain)
    float handSpeed = 0;    // |grip velocity|
    float objectSpeed = 0;  // |grip velocity + w x r|, before the gain
    float gain = 1;         // the gain applied
    float speed = 0;        // after gain and limit, relative to the player
    float limit = 0;        // the limit used
    bool limited = false;   // the knee reduced it
    Vec3 velocity{};        // world: player + the grenade's own
    Vec3 angular{};         // world, rad/s
};

// How much of the throw applies at a hand speed: 0 below dropSpeed (a drop),
// 1 from throwSpeed, a smooth ease in between.
float ThrowWeight(float speed, const ThrowSettings& settings = {});
// Horizontal distance to where a body launched `height` above a floor at
// `speed` and `elevation` (radians above horizontal) comes down on it, under
// gravity g and linear damping k (k = 0: the exact ballistic range).
float RangeOnFloor(float speed, float elevation, float height, float g, float k);
// How far along `heading` (horizontal unit) a body launched with world
// velocity v, `height` above the floor, comes down on it (negative = behind).
float SignedRange(Vec3 v, Vec3 heading, float height, float g, float k);
// The speed along `direction` (unit; the grenade's own velocity relative to
// the player) that, with the player's velocity added, lands the grenade
// (settings' gravity and damping) on the floor where a real ball (9.81, no
// drag) launched with world velocity `real` would land. Bounded by `limit`.
float ParitySpeed(Vec3 direction, Vec3 player, Vec3 real, float height, const ThrowSettings& settings, float limit);
// A speed past knee*limit compressed smoothly towards limit (never above it).
float SoftLimit(float speed, float limit, float kneeFraction);
Release ComputeRelease(const ReleaseInput& in, const ThrowSettings& settings = {});

// Where to spawn the grenade: `want` (the grenade in the hand) unless the
// segment from the head to it is blocked at `hitFraction` (0..1; >= 1 = free),
// then that far along it, `margin` metres short of the hit -- never behind a
// wall the hand has gone through.
Vec3 PullBack(Vec3 head, Vec3 want, float hitFraction, float margin);

// Ballistic flight (engine Z up, gravity g > 0 down): where a body launched at
// p with velocity v crosses the height floorZ on the way down, and when.
// False if it never does (launched below it and never rising to it).
bool Landing(Vec3 p, Vec3 v, float g, float floorZ, Vec3& at, float& seconds);

}  // namespace preyvr::grenade
