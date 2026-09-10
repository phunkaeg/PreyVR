#pragma once

namespace preyvr::depth {

// The depth range handed to `XrCompositionLayerDepthInfoKHR`.
//
// **Getting this backwards is worse than submitting no depth at all.** The
// runtime uses these numbers to turn stored depth values into world distances
// for reprojection. Reversed, every pixel is reprojected as though near geometry
// were far and far geometry near, which is a worse image than the runtime's own
// flat assumption -- and it would look like a subtle warp under head motion
// rather than an obvious failure.
//
// **Prey renders reverse-Z.** `r_ReverseDepth` is registered with a default of 1
// (R-131), so a stored 0 is the FAR plane and a stored 1 is the NEAR plane. The
// OpenXR spec accommodates this directly: `nearZ` is the distance corresponding
// to `minDepth`, `farZ` the distance at `maxDepth`, and **`nearZ` greater than
// `farZ` is how an application declares reversed depth.** So the reversed case
// is not a different formula, it is the same one with the two distances swapped.
struct DepthRange {
    // The range of values present in the depth image itself. D3D depth is
    // normalised to [0,1] regardless of which end is near.
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
    // Metres. `nearZ` is the distance at `minDepth`, `farZ` at `maxDepth`.
    float nearZ = 0.0f;
    float farZ = 0.0f;
    // Whether the two came out swapped, i.e. reversed depth was declared. Kept
    // so a report can state what was submitted rather than what was intended.
    bool reversed = false;
    // False means do not submit. A refused range must never be silently
    // replaced by a plausible default: a wrong depth layer degrades the image
    // everywhere, while no depth layer simply leaves the runtime as it was.
    bool valid = false;
};

// `nearPlane`/`farPlane` are the engine's own values, in engine units.
// `unitsPerMetre` converts them; it is a parameter rather than a constant
// because the project measured it as 1 for this game and that measurement
// should be visible at the call site instead of buried here.
DepthRange BuildRange(float nearPlane, float farPlane, float unitsPerMetre,
                      bool reverseZ);

} // namespace preyvr::depth
