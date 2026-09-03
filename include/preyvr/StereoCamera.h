#pragma once

#include "preyvr/VrMath.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// Turning an OpenXR head pose into a CCamera the engine will accept.
//
// This is the arithmetic that 6DoF and stereo both rest on, kept pure so it can
// be tested without a headset, a GPU, or a running game -- which matters,
// because a basis-change error here does not crash, it just renders a world that
// is subtly wrong in a way that is very hard to diagnose while wearing a
// headset.
namespace preyvr::stereo {

// ---------------------------------------------------------------------------
// Coordinate systems
// ---------------------------------------------------------------------------
//
// OpenXR is right-handed, **Y up**, with **-Z forward**.
// CryEngine is right-handed, **Z up**, with **+Y forward**.
//
// Derived rather than guessed. Writing the engine basis vectors in OpenXR terms:
//
//     engine X (right)   =  openxr X
//     engine Y (forward) = -openxr Z
//     engine Z (up)      =  openxr Y
//
// As a matrix whose rows are those expressions:
//
//     [ 1  0  0 ]
//     [ 0  0 -1 ]
//     [ 0  1  0 ]
//
// which is exactly Rx(+90 degrees) -- a proper rotation with determinant +1, not
// a mirror. That matters: a handedness flip would invert every cross product
// downstream, and `CCamera::UpdateFrustum` reacts to a non-proper basis by
// negating all six frustum plane normals.
//
// So the whole conversion is one rotation, and it composes with everything else
// using the quaternion algebra already in VrMath.
inline constexpr Quaternion kOpenXrToEngine{0.70710678118f, 0.0f, 0.0f, 0.70710678118f};

Quaternion Conjugate(Quaternion value);

Vec3 ToEngineSpace(Vec3 openXr);

// A rotation changes basis by conjugation, q_B * q * q_B^-1 -- not by rotating
// the axis vector, which is the easy mistake and produces a result that looks
// plausible until the head tilts.
Quaternion ToEngineSpace(Quaternion openXr);

Pose ToEngineSpace(const Pose& openXr);

// ---------------------------------------------------------------------------
// Matrix34
// ---------------------------------------------------------------------------
//
// Row-major 3x4 with the translation in column 3, matching CameraLayout and the
// layout CCamera::UpdateFrustum walks:
//
//     m[0] m[1] m[2]  m[3]        columns 0,1,2 are the basis vectors
//     m[4] m[5] m[6]  m[7]        column 3 is the position
//     m[8] m[9] m[10] m[11]
//
// Column 0 is right, column 1 is forward, column 2 is up. Confirmed by the
// engine's own e_CameraRotationSpeed yaw, which rotates columns 0 and 1 while
// leaving column 2 alone -- exactly what a Z-up yaw does.
using Matrix34 = std::array<float, 12>;

Matrix34 MatrixFromPose(const Pose& pose);

// The inverse. Needed to ask "where is the engine's camera right now?" so a
// per-eye offset can be composed onto it rather than replacing it -- which is
// what lets the eye construction be exercised against the game's own camera,
// including its pitch and roll, instead of a synthetic one.
//
// Branches on the largest diagonal term rather than using the single-branch
// trace form. That is not premature caution: the naive version loses precision
// as the trace approaches zero, which happens at exactly the 180-degree
// orientations a player reaches by turning around.
Quaternion QuaternionFromBasis(const Vec3& right, const Vec3& forward, const Vec3& up);

Pose PoseFromMatrix(const Matrix34& matrix);

// Composes an offset expressed in the camera's own frame -- +X right, +Y
// forward, +Z up -- onto a pose. Half the IPD along the camera's right axis is
// exactly a synthetic eye.
Pose OffsetInLocalFrame(const Pose& pose, const Vec3& localOffset);

Vec3 RightOf(const Matrix34& matrix);
Vec3 ForwardOf(const Matrix34& matrix);
Vec3 UpOf(const Matrix34& matrix);
Vec3 PositionOf(const Matrix34& matrix);

// ---------------------------------------------------------------------------
// Per-eye construction
// ---------------------------------------------------------------------------

// Where the play space sits in the game world, and how it is oriented. The
// position normally comes from the game's own eye point and the yaw from
// recentering, so that turning in the room and turning with the stick compose
// instead of fighting.
struct ReferenceFrame {
    Vec3 worldPosition{};
    float yawRadians = 0.0f; // about engine Z (up)
};

Quaternion YawQuaternion(float radians);

// The yaw a recenter should store, extracted from a live head pose.
//
// **This is the FAIL-CAM-019 seam, and the method is chosen to make the bug
// unrepresentable rather than merely avoided.** The rule is that the stored
// reference is yaw-only while the live head pose stays full: strip roll from
// both and head tilt stops working; strip it from neither and a headset that was
// crooked at the moment of capture tilts the world for the rest of the session.
//
// Yaw is taken by rotating the engine's forward axis and projecting it onto the
// horizontal plane. That projection is **inherently immune to both roll and
// pitch** -- roll turns about the forward axis and cannot move it, and pitch
// moves it within the vertical plane that already contains it, so neither
// changes its horizontal direction. The immunity is a property of the
// construction, not an extra step that could be forgotten, and the tests pin it
// so that a later "simplification" to Euler decomposition fails loudly.
//
// Returns nothing when the head is near-vertical. Looking straight up or down
// leaves almost no horizontal component to take a direction from, so the answer
// would be noise amplified by `atan2`. The playbook's rule is to **reject rather
// than invent**: a caller that cannot get a yaw should keep the reference it
// already has, which is what a player looking at the ceiling expects anyway.
//
// Takes an OpenXR-space pose, matching EyePoseInWorld, so callers stay in one
// space and the conversion happens in exactly one place.
std::optional<float> RecenterYawFromHeadPose(const Pose& openXrHeadPose);

// The complete recenter: where the play space sits, and which way it faces.
//
// `worldPosition` is the game's own eye point, so walking, collision and
// scripted movement stay the engine's business. Only the yaw comes from the
// headset.
//
// Fails closed with the same rejection as above -- and a caller must treat that
// as "keep the current reference", never as "use a zero yaw", which would snap
// the player's world to face engine north.
// The reference frame that makes head rotation **compose with** the game's own
// camera instead of replacing it.
//
// **This is the correction to the first head-tracking build.** That version set
// the reference yaw to the recenter yaw and left it there, so the rendered camera
// pointed at `recenterYaw + headRotation` and the player's mouse-look was simply
// discarded. In game that reads as the body rotating in front of you: the body
// still turns with the mouse, and the camera no longer does.
//
// The engine's camera already carries where the player is facing -- mouse, stick,
// scripted turns, everything. So the play space must sit at *that* yaw, minus the
// yaw the head was at when recenter was pressed. Then a head back at its recenter
// orientation reproduces the engine's own camera exactly, and head rotation adds
// on top of the player's facing rather than fighting it.
//
// Position is taken unchanged from the engine's camera, so walking, collision and
// scripted movement stay the engine's business.
ReferenceFrame ReferenceFrameForHeadTracking(const Matrix34& gameCamera, float recenterYaw);

// The yaw a camera matrix is facing, about engine Z. Same construction as
// RecenterYawFromHeadPose -- the forward axis projected onto the horizontal plane
// -- so the two cannot disagree about what yaw means.
float CameraYawOf(const Matrix34& matrix);

std::optional<ReferenceFrame> MakeRecenterReference(
    const Pose& openXrHeadPose,
    Vec3 worldPosition);

// The one function the render hook needs: an OpenXR eye pose plus the reference
// frame, in engine space, ready to become a Matrix34.
//
// Composition order is reference-then-eye. The eye pose is expressed in the play
// space, so it must be rotated and translated *by* the reference frame, never
// the other way round -- reversing this yields a camera that orbits the world
// origin when the player walks, which is a spectacular and very confusing bug.
Pose EyePoseInWorld(const ReferenceFrame& reference, const Pose& openXrEyePose);

// The single eye point gameplay systems expect, from the two rendered eyes.
//
// Prey's own code (ArkPlayerCamera::UpdateView, R-009) produces one eye pose,
// and every consumer downstream -- aim rays, interaction queries, target
// selection -- assumes there is exactly one. Handing them the midpoint keeps
// those systems consistent with what is being rendered rather than biased toward
// whichever eye happened to be written last.
//
// The orientation is taken from the left eye rather than being interpolated:
// the two eye orientations are identical in every runtime we care about, and
// slerping two identical quaternions to produce a third is a way to introduce
// error rather than remove it. If they ever differ, that is worth noticing
// rather than averaging away.
Pose CyclopsPose(const Pose& leftEye, const Pose& rightEye);

// ---------------------------------------------------------------------------
// Writing into a live CCamera
// ---------------------------------------------------------------------------

inline constexpr std::size_t kCameraSize = 0x240;

// Writes only the twelve matrix floats. Everything else in the structure --
// fov, dimensions, near/far, the asymmetry shifts, and all the cached derived
// state -- is left exactly as it was, because a per-eye write should change the
// smallest thing that produces the intended effect.
//
// The caller is still responsible for calling CCamera::UpdateFrustum afterwards;
// this function cannot, being pure. Skipping that leaves the cached planes
// describing the previous frame's camera.
bool WriteMatrix(std::span<std::uint8_t> camera, const Matrix34& matrix);

Matrix34 ReadMatrix(std::span<const std::uint8_t> camera);

} // namespace preyvr::stereo
