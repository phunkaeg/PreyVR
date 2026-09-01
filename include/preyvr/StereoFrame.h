#pragma once

#include "preyvr/RuntimeSnapshot.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/VrMath.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

// A complete description of one stereo frame: given the camera the engine was
// about to render with, and what OpenXR says about the two eyes, produce the two
// cameras that should be rendered instead.
//
// This is the payload of the double-render experiment. Keeping it pure means the
// whole per-eye construction can be checked without a headset, a GPU, or a
// running Prey -- and it means the hook that eventually executes it is small
// enough to read in one sitting, which is what you want in code that runs inside
// someone else's render loop.
//
// It deliberately does **not** call CCamera::UpdateFrustum. That is an engine
// function and this is pure; the caller runs it on each produced camera before
// the bytes reach the game. Skipping it leaves the cached planes describing the
// previous camera -- see CameraEditHook for how that is sequenced.
namespace preyvr::stereoframe {

inline constexpr std::size_t kCameraSize = 0x240;

// One eye as OpenXR reports it: a pose in the reference space, and the four
// signed tangent half-angles of its frustum.
//
// Tangents rather than XrFovf angles so this stays free of the OpenXR headers
// and the caller owns that dependency -- the same choice AsymmetryFromFovTangents
// already makes.
struct EyeView {
    Pose openXrPose{};
    float tanLeft = 0.0f;  // negative for a normal frustum
    float tanRight = 0.0f;
    float tanDown = 0.0f;  // negative
    float tanUp = 0.0f;
};

// What Prey's CCamera needs to express that frustum: a symmetric vertical FOV, a
// projection ratio, and the four asymmetry shifts that carry the difference.
//
// Prey can express a genuinely asymmetric frustum -- SetCamera folds m_asymL/R/B/T
// into the render frustum, which Crysis' CCamera could not do, and fholger's mod
// had to fall back to a symmetric FOV plus cropping at submission. We use the
// asymmetric path because the engine supports it.
//
// **The known caveat**, and it is not hypothetical: the engine header marks those
// shift fields "not used for culling". So the *render* frustum will be correct
// per eye while the *cull* frustum stays symmetric. Expect the visible symptom to
// be geometry appearing or vanishing near the outer edge of each eye rather than
// a wrong image. Recorded here so that when it shows up it is recognised instead
// of investigated from scratch.
struct EyeProjection {
    float fov = 0.0f; // vertical, radians
    float projectionRatio = 0.0f;
    snapshot::EyeAsymmetry asymmetry{};
};

// Chooses the symmetric envelope that makes the shifts small, then solves for
// them. Any envelope works -- the shifts absorb whatever is left -- but keeping
// them near zero means a mistake shows up as a small artefact rather than a
// wildly skewed image, and it keeps the numbers legible in a log.
std::optional<EyeProjection> ProjectionFromTangents(const EyeView& view, float nearPlane);

// ---------------------------------------------------------------------------
// The other direction: what we must DECLARE to OpenXR
// ---------------------------------------------------------------------------

// Reads a live Prey CCamera and returns the frustum it actually renders with, as
// tangent half-extents.
//
// **This is the function the submission path needs, and its absence is why
// docs/SUBMISSION_CONTRACT.md records that we have been reading
// `submitted_fov_matches_located` backwards.** PreyVR is an injected mod: it
// cannot change Prey's projection, so its pixels come from the game's frustum.
// Declaring the runtime's FOV over them is a lie about the image. What must be
// declared is *this*.
//
// **The asymmetry semantics come from CryEngine's own source**, not from our
// recomputation -- `DriverD3D.cpp` builds the projection from `wL/wR/wB/wT`, the
// same four fields this project reverse-engineered as `fWL/fWR/fWB/fWT`, and they
// are **frustum-edge offsets in near-plane units added to the computed edges**,
// not angles. So converting an edge offset to a tangent is a division by the near
// plane. That independence matters: our own asymmetry test was self-referential,
// recomputing with the same formula on the same inputs, and this is the first
// outside confirmation of the shape. See docs/CRYENGINE_SOURCE_FINDINGS.md.
//
// Returns nothing if the camera is not usable -- a zero or negative near plane, a
// non-finite field, or a degenerate FOV. **Fail closed**: submitting a frustum
// derived from a camera we could not read is worse than dropping the frame,
// because it is wrong in a way the runtime cannot detect.
std::optional<EyeView> TangentsFromCamera(std::span<const std::uint8_t> camera);

// Tangents to the angles an XrFovf carries. Kept separate from the read above so
// the trigonometry is testable without a camera block, and so callers work in
// tangents for as long as possible -- every correct operation on a frustum is
// linear in tangents and not in angles, which this project has already paid for
// once in F-011.
struct EyeFovAngles {
    float angleLeft = 0.0f;   // radians, negative
    float angleRight = 0.0f;
    float angleDown = 0.0f;   // radians, negative
    float angleUp = 0.0f;
};

EyeFovAngles AnglesFromTangents(const EyeView& view);

// The synthetic per-eye frustum used when there is no headset to report one.
//
// `asymmetryScale` scales the outer half-angle and shrinks the inner one by the
// same amount, mirrored per eye the way a headset actually reports: 1.1 gives 55
// degrees out and 45 in for a 50-degree half-FOV, and **1.0 gives both eyes an
// identical frustum**.
//
// **This lives here, rather than inline in the hook, because that last property
// is load-bearing and was previously only true by inspection.** A2b isolates the
// eye offset by dialling the asymmetry to 1.0 so that translation is the only
// difference between the two images; if this function quietly kept some per-eye
// difference at 1.0, the sweep would measure that instead and the failure would
// look like a bad IPD. It is cheap to pin with a test and expensive to get wrong,
// so it is pinned -- see StereoFrameTests.
//
// The pose is left at identity: callers compose the eye offset onto the engine's
// live camera, which this function knows nothing about.
EyeView SyntheticEyeView(int eye, float halfFovDegrees, float asymmetryScale);

// A finished camera, ready for UpdateFrustum and then a blit.
struct EyeCamera {
    std::array<std::uint8_t, kCameraSize> bytes{};
};

struct StereoPlan {
    EyeCamera left{};
    EyeCamera right{};

    // The single eye point for gameplay systems, in engine world space.
    Pose cyclops{};

    EyeProjection leftProjection{};
    EyeProjection rightProjection{};
};

// Builds both eye cameras from the camera the engine was about to use.
//
// Everything not related to the view is inherited from `baseCamera` unchanged --
// dimensions, near and far planes, and every cached field. A per-eye camera
// should differ from the engine's own in exactly the ways it has to and in no
// others, both because that is the smallest change and because it makes a diff
// of the two blobs readable.
//
// Returns nothing if the base camera is too small, if either eye's tangents are
// not a valid frustum, or if a produced matrix is not safe to write. A partial
// stereo plan is never returned: rendering one good eye and one bad one is worse
// than rendering neither, because it looks like it nearly works.
std::optional<StereoPlan> BuildStereoPlan(
    std::span<const std::uint8_t> baseCamera,
    const stereo::ReferenceFrame& reference,
    const EyeView& leftEye,
    const EyeView& rightEye);

// Reads the near plane the engine is currently using, since the asymmetry solve
// needs it and it must come from the live camera rather than a constant.
float NearPlaneOf(std::span<const std::uint8_t> camera);

} // namespace preyvr::stereoframe
