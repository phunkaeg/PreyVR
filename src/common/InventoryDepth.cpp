#include "preyvr/InventoryDepth.h"

#include <cmath>

namespace preyvr::inventory {

std::optional<DepthMapping> MapDepth(float cameraUnits, float panelMetres,
                                     float ipdMetres, float depthScale)
{
    if (!std::isfinite(cameraUnits) || !std::isfinite(panelMetres) ||
        !std::isfinite(ipdMetres) || !std::isfinite(depthScale)) {
        return {};
    }
    const float camera = std::abs(cameraUnits);
    // Below 1 unit the division below stops meaning anything: a camera that
    // close to its own scene is a misread, not a very small camera.
    if (camera <= 1.f) return {};
    if (panelMetres < .5f || panelMetres > 10.f) return {};
    if (ipdMetres < .04f || ipdMetres > .09f) return {};
    if (depthScale < 0 || depthScale > 1) return {};

    DepthMapping mapping{};
    mapping.panelMetres = panelMetres;
    mapping.metresPerUnit = panelMetres / camera;
    // The eye offset expressed in the movie's own units, so a caller can hand
    // it straight to SetView3D without knowing anything about metres.
    mapping.halfSeparationUnits = (ipdMetres * .5f / mapping.metresPerUnit) * depthScale;
    return mapping;
}

float PlaneMetres(const DepthMapping& mapping, float offsetUnits)
{
    if (!std::isfinite(offsetUnits)) return mapping.panelMetres;
    // Offsets recede negatively, so subtracting moves the plane away.
    return mapping.panelMetres - offsetUnits * mapping.metresPerUnit;
}

} // namespace preyvr::inventory
