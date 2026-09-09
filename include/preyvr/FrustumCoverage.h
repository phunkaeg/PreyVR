#pragma once

// How much of the image we render is the headset actually able to show, and how
// much of what it asked for did we supply?
//
// **These are two different questions and conflating them hides a real fault.**
// Prey renders a wide symmetric frustum; the runtime asks for a narrower,
// asymmetric one. Dividing one area by the other gives a single number that
// reads as "wasted pixels" -- but the same number moves when the rendered
// frustum is too SMALL on an edge, which is not waste at all, it is missing
// content the wearer sees as a black or stretched border. So this reports the
// overlap against each denominator separately:
//
//   utilisation  = overlap / rendered   -- how much of our pixel budget is used
//   satisfaction = overlap / requested  -- how much of the ask we actually cover
//
// Low utilisation with full satisfaction is the good case: pixels are being
// spent outside the lens and can be reclaimed. Satisfaction below 1 means we are
// already short somewhere, and reclaiming pixels would make it worse.
//
// Everything is computed in TANGENT space, not angles. A frustum is linear in
// tangents and not in degrees, so averaging or differencing angles gives the
// wrong area -- the same mistake the withdrawn "70% head-yaw leakage" claim was
// built on.
namespace preyvr::xr {

// Half-angles in radians, in OpenXR's sign convention: left and down negative,
// right and up positive.
struct FovAngles {
    float left = 0.0f;
    float right = 0.0f;
    float up = 0.0f;
    float down = 0.0f;
};

struct FrustumCoverage {
    float utilisationWidth = 0.0f;
    float utilisationHeight = 0.0f;
    // The product, i.e. the fraction of the rendered pixel rectangle that lands
    // inside the requested frustum.
    float utilisationArea = 0.0f;

    float satisfactionWidth = 0.0f;
    float satisfactionHeight = 0.0f;
    float satisfactionArea = 0.0f;

    // True when the requested frustum reaches past the rendered one on any edge:
    // content the runtime wants and we never drew.
    bool requestExceedsRender = false;

    // False when either frustum is degenerate or non-finite. Nothing else in the
    // struct should be read when this is false.
    bool valid = false;
};

// Neither argument is modified. Angles are clamped to a sane half-open range
// before tangents are taken, so a 90-degree or wider half-angle -- where the
// tangent diverges -- refuses rather than producing an infinity.
FrustumCoverage TangentCoverage(const FovAngles& rendered, const FovAngles& requested);

// The linear per-axis scale that would let a smaller render target carry the
// same angular sampling density over the REQUESTED frustum only. Multiply the
// current per-axis pixel count by this. Returns 0 when coverage is invalid.
//
// This is an equal-density figure and not a recommendation: it excludes the
// overscan margin reprojection needs, and it assumes the eye orientation the
// coverage was computed for.
float EqualDensityScale(float utilisationAxisFraction);

} // namespace preyvr::xr
