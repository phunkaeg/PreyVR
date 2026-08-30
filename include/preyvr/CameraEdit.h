#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Bounded, reversible edits to a CCamera's 0x240 bytes.
//
// This is the policy half of the first write to Prey's render path. Everything
// before now has been read-only; the question this exists to answer is whether
// modifying CSystem::m_ViewCamera actually changes the rendered image, which is
// the precondition for every stereo design we have considered.
//
// Three deliberate constraints, all mirroring the wrench and interaction
// protocols that came before:
//
//   1. **Bounded.** A yaw edit is clamped to a small angle. An unbounded edit
//      that goes wrong is indistinguishable from a crash; a 10-degree one that
//      goes wrong still renders a recognisable scene.
//   2. **Recorded and restored.** The caller captures all 0x240 bytes first and
//      puts them back afterwards, then verifies the restore byte for byte. A
//      restore that is not verified is a hope.
//   3. **Orthonormal.** See the note on the rotation below -- this one is not
//      style, it changes what the engine renders.
namespace preyvr::cameraedit {

inline constexpr std::size_t kCameraSize = 0x240;

// Matches the 15-degree ceiling the A0b aim protocol already uses. Large enough
// that the resulting image change is unmistakable, small enough that a mistake
// does not throw the camera somewhere unrecognisable.
inline constexpr float kMaxYawDegrees = 15.0f;

struct YawEdit {
    float degrees = 0.0f;
};

// Rejects a non-finite angle, a zero angle (which would make a "did it change?"
// test vacuously pass), and anything past the bound.
bool IsWithinBounds(const YawEdit& edit);

// Applies a Z-up yaw to the Matrix34 at the start of a CCamera.
//
// **This is the engine's own formula, transcribed from
// C3DEngine::UpdateRenderingCamera's e_CameraRotationSpeed path**, not one of
// ours:
//
//   m[i][0]' =  cos * m[i][0] + sin * m[i][1]
//   m[i][1]' = -sin * m[i][0] + cos * m[i][1]
//   m[i][2]  and the translation column are untouched
//
// Using the engine's version matters beyond taste. CCamera::UpdateFrustum calls
// an orthonormality check on the 3x3 and, if it fails, **negates all six
// frustum plane normals** -- so a rotation that quietly denormalises the basis
// does not produce a slightly wrong image, it inverts culling. Reproducing the
// formula the engine already applies to this exact matrix keeps us on the path
// it is known to accept.
//
// Returns false and leaves `camera` untouched if the edit is out of bounds or
// the buffer is not a full camera.
bool ApplyYaw(std::span<std::uint8_t> camera, const YawEdit& edit);

// The same orthonormality predicate the engine uses before deciding whether to
// flip its plane normals, so we can assert the property *before* writing rather
// than discover it from an inverted image. `epsilon` matches nothing in
// particular yet -- it is exposed so the value can be pinned once measured
// against a live camera.
bool RotationIsOrthonormal(std::span<const std::uint8_t> camera, float epsilon = 0.01f);

// **The engine's check has a hole, and this closes it.**
//
// An all-zero 3x3 satisfies every one of the engine's nine comparisons, because
// each one reduces to |0 - 0| <= epsilon. So `RotationIsOrthonormal` returning
// true does not mean the matrix is usable -- it means the engine would not flip
// its plane normals, which is a different and weaker statement. The tests pin
// both behaviours.
//
// Checked separately rather than folded in, because the value of the function
// above is precisely that it agrees with the engine byte for byte; adding our
// own conditions to it would destroy that.
bool RotationIsDegenerate(std::span<const std::uint8_t> camera, float minAxisLength = 0.5f);

// What a caller should actually gate a write on: orthonormal *and* not
// degenerate.
bool RotationIsSafeToWrite(std::span<const std::uint8_t> camera);

// A recorded copy of the bytes as they were, and the check that they came back.
struct RestorePoint {
    std::array<std::uint8_t, kCameraSize> bytes{};
    bool captured = false;
};

bool Capture(std::span<const std::uint8_t> camera, RestorePoint& out);

// Byte-exact. Not "close enough": the whole point of a bounded write protocol is
// that the game is left in precisely the state it was found in, and a partial
// restore is the failure mode that would poison every later measurement in the
// same session.
bool MatchesRestorePoint(std::span<const std::uint8_t> camera, const RestorePoint& point);

// Reads the translation column (m[3], m[7], m[11]) so a caller can log where the
// camera was without decoding the whole structure.
std::array<float, 3> PositionOf(std::span<const std::uint8_t> camera);

} // namespace preyvr::cameraedit
