#include "preyvr/FrustumCoverage.h"

#include <algorithm>
#include <cmath>

namespace preyvr::xr {
namespace {

// Just under 90 degrees. The tangent of a half-angle at or beyond 90 degrees is
// not finite, and a frustum that wide is not a frustum we can reason about, so
// it is refused rather than clamped into a plausible-looking number.
constexpr float kMaxHalfAngle = 1.5533431f; // 89 degrees

bool Sane(float angle) { return std::isfinite(angle) && std::fabs(angle) <= kMaxHalfAngle; }

bool SaneFov(const FovAngles& fov)
{
    return Sane(fov.left) && Sane(fov.right) && Sane(fov.up) && Sane(fov.down) &&
           fov.right > fov.left && fov.up > fov.down;
}

struct Extent {
    float low = 0.0f;
    float high = 0.0f;
    float Span() const { return high - low; }
};

Extent Horizontal(const FovAngles& fov) { return {std::tan(fov.left), std::tan(fov.right)}; }
Extent Vertical(const FovAngles& fov) { return {std::tan(fov.down), std::tan(fov.up)}; }

// The overlap of two tangent extents, never negative: disjoint frusta give zero
// rather than a negative width that would silently flip the area's sign.
float Overlap(const Extent& a, const Extent& b)
{
    return std::max(0.0f, std::min(a.high, b.high) - std::max(a.low, b.low));
}

float Ratio(float numerator, float denominator)
{
    return denominator > 0.0f ? numerator / denominator : 0.0f;
}

} // namespace

FrustumCoverage TangentCoverage(const FovAngles& rendered, const FovAngles& requested)
{
    FrustumCoverage out{};
    if (!SaneFov(rendered) || !SaneFov(requested)) { return out; }

    const Extent renderedH = Horizontal(rendered);
    const Extent renderedV = Vertical(rendered);
    const Extent requestedH = Horizontal(requested);
    const Extent requestedV = Vertical(requested);
    if (!(renderedH.Span() > 0.0f) || !(renderedV.Span() > 0.0f) ||
        !(requestedH.Span() > 0.0f) || !(requestedV.Span() > 0.0f)) {
        return out;
    }

    const float overlapH = Overlap(renderedH, requestedH);
    const float overlapV = Overlap(renderedV, requestedV);

    out.utilisationWidth = Ratio(overlapH, renderedH.Span());
    out.utilisationHeight = Ratio(overlapV, renderedV.Span());
    out.utilisationArea = out.utilisationWidth * out.utilisationHeight;

    out.satisfactionWidth = Ratio(overlapH, requestedH.Span());
    out.satisfactionHeight = Ratio(overlapV, requestedV.Span());
    out.satisfactionArea = out.satisfactionWidth * out.satisfactionHeight;

    out.requestExceedsRender = requestedH.low < renderedH.low || requestedH.high > renderedH.high ||
                               requestedV.low < renderedV.low || requestedV.high > renderedV.high;
    out.valid = true;
    return out;
}

float EqualDensityScale(float utilisationAxisFraction)
{
    if (!std::isfinite(utilisationAxisFraction) || utilisationAxisFraction <= 0.0f) {
        return 0.0f;
    }
    return utilisationAxisFraction;
}

} // namespace preyvr::xr
