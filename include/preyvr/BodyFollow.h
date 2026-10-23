#pragma once

#include "preyvr/VrMath.h"

#include <optional>

// Pure rules for the character following the player (src/dll/BodyFollow.cpp).
//
// Angles use the engine's yaw convention (StereoCamera.h): a yaw of t sends
// engine forward +Y to (-sin t, cos t, 0). Tracking-space vectors are OpenXR
// (+Y up, -Z forward), where the same yaw t sends forward -Z to
// (-sin t, 0, -cos t).
namespace preyvr::body {

// Into (-pi, pi].
float Wrap(float radians);

// Where the player's head is turned, from the whole head and not only its
// forward axis. Level, it is the forward axis. Pitched, the top of the head
// takes over in proportion -- looking straight down, the crown points where
// the face was pointing, so the heading stays put instead of becoming noise as
// the forward axis approaches vertical. Nullopt only for a degenerate pose.
std::optional<float> HeadHeading(const Quaternion& openXrHead);

// The neck pivot under the eyes, in tracking space. The eyes sit `forward`
// metres in front of it and `up` metres above it in the head's own frame, so
// pitching or turning the head about the neck leaves this point still -- which
// is what the body has to stay under. A body that followed the eyes would
// slide forward every time the player looked down.
Vec3 NeckPoint(const Pose& openXrHead, float up, float forward);

// The horizontal forward of a yaw in tracking space.
Vec3 TrackingForward(float yaw);

// Where the neck should be for an eye anchor `origin` and a body heading
// `yaw`: `forward` metres behind the anchor, at its height.
Vec3 NeckAnchor(const Vec3& origin, float yaw, float forward);

struct YawPolicy {
    float deadzone = 0.0f;   // radians the head may turn before the body follows
    float rate = 0.0f;       // radians per second while catching up; 0 = at once
};

// The turn to give the body this frame for a heading error (head - body).
// Inside the deadzone nothing moves until the error leaves it; once it has,
// the body catches up all the way (to within a tenth of the deadzone), so a
// head held just past the edge does not leave the body hovering at the edge.
float YawStep(float error, float dt, const YawPolicy& policy, bool& catching);

struct RoomPolicy {
    float start = 0.02f;     // metres of neck offset before the body moves
    float stop = 0.004f;     // ... and the offset at which it stops
    float maxStep = 0.05f;   // metres per frame, a bound on a single SetPos
};

// The horizontal move (x, z of tracking space) for a neck offset, with
// hysteresis so millimetre head noise does not move the body every frame.
Vec2 RoomStep(Vec2 offset, const RoomPolicy& policy, bool& moving);

// The lean radius (Quake VR's vr_lean_radius): the head may leave the body's
// neck by `radius` before the body walks after it, at once and as far as it
// takes to bring the head back to the radius. Inside, the body comes back under
// the head at the head's own speed (at least `recentre`), with hysteresis --
// unless the player is leaning, which the visible body shows by bending.
struct LeanPolicy {
    float radius = 0.25f;
    float recentre = 0.5f;   // m/s
    float start = 0.01f, stop = 0.003f;
    float maxStep = 0.05f;   // metres per frame
};
Vec2 LeanStep(Vec2 offset, float headSpeed, float dt, bool leaning, const LeanPolicy& policy, bool& moving);

// Leaning, not stepping (Quake VR: "head lower and tilted toward it"): the
// neck lower than standing (a hip hinge drops it 2-15 cm; looking down about
// the neck does not, a squat drops it more) and the crown tilted the way the
// neck is going -- `direction` is
// its offset from the body plus where it is heading.
bool Leaning(const Quaternion& openXrHead, Vec2 direction, float neckDrop);

// A step that would enter a wall slides along it instead: the part of the
// step against the wall's horizontal normal is removed.
Vec2 Slide(Vec2 step, Vec2 wallNormal);

// Converts between the engine's world frame and the tracking frame for a play
// yaw (the view is Rz(play) * ToEngine(tracking)).
Vec3 WorldToTracking(const Vec3& world, float playYaw);
Vec3 TrackingToWorld(const Vec3& tracking, float playYaw);

} // namespace preyvr::body
