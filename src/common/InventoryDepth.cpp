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
    // Eye displacement magnitude, not an inverse-view matrix write.
    mapping.halfSeparationUnits = (ipdMetres * .5f / mapping.metresPerUnit) * depthScale;
    return mapping;
}

float PlaneMetres(const DepthMapping& mapping, float offsetUnits)
{
    if (!std::isfinite(offsetUnits)) return mapping.panelMetres;
    return mapping.panelMetres + offsetUnits * mapping.metresPerUnit;
}

bool ApplyStereoParallax(std::array<float,16>& clip, float cameraUnits,
                         float panelWidth, float eyeOnPanel, float depthScale)
{
    if (!std::isfinite(cameraUnits) || cameraUnits <= 1.f ||
        !std::isfinite(panelWidth) || panelWidth < .1f || panelWidth > 20.f ||
        !std::isfinite(eyeOnPanel) || std::abs(eyeOnPanel) > .1f ||
        !std::isfinite(depthScale) || depthScale < 0.f || depthScale > 1.f) return false;
    for (float v:clip) if (!std::isfinite(v)) return false;
    if (depthScale==0.f || eyeOnPanel==0.f) return true;
    auto result=clip;
    const float factor=2.f*eyeOnPanel/panelWidth*depthScale;
    for (unsigned i=0;i<4;++i)
        result[i]+=factor*(clip[12+i]-(i==3?cameraUnits:0.f));
    for (float v:result) if (!std::isfinite(v)) return false;
    clip=result;
    return true;
}

} // namespace preyvr::inventory
