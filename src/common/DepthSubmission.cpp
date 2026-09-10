#include "preyvr/DepthSubmission.h"

#include <cmath>

namespace preyvr::depth {

DepthRange BuildRange(float nearPlane, float farPlane, float unitsPerMetre,
                      bool reverseZ)
{
    DepthRange out{};
    if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        !std::isfinite(unitsPerMetre) || unitsPerMetre <= 0.0f ||
        nearPlane <= 0.0f || farPlane <= nearPlane) {
        return out;   // invalid, and deliberately not defaulted to anything
    }

    const float nearMetres = nearPlane / unitsPerMetre;
    const float farMetres = farPlane / unitsPerMetre;
    if (!std::isfinite(nearMetres) || !std::isfinite(farMetres) ||
        nearMetres <= 0.0f || farMetres <= nearMetres) {
        return out;
    }

    out.minDepth = 0.0f;
    out.maxDepth = 1.0f;
    // **The whole difference is which distance each end of the range means.**
    // Reversed: stored 0 is the far plane, stored 1 is the near plane, so the
    // distance at minDepth is the FAR one. That inequality (nearZ > farZ) is
    // itself the declaration the runtime reads.
    out.nearZ = reverseZ ? farMetres : nearMetres;
    out.farZ = reverseZ ? nearMetres : farMetres;
    out.reversed = reverseZ;
    out.valid = true;
    return out;
}

} // namespace preyvr::depth
