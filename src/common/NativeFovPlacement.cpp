#include "preyvr/NativeFovPlacement.h"

#include <algorithm>
#include <cmath>

namespace preyvr::xr {
namespace {
bool Valid(const Tangents& t)
{
    return std::isfinite(t.left) && std::isfinite(t.right) && std::isfinite(t.up) && std::isfinite(t.down) &&
           t.right > t.left && t.up > t.down;
}
}  // namespace

std::optional<Placement> PlaceInNativeFov(const Tangents& rendered, int imageW, int imageH, const Tangents& native,
                                          int swapW, int swapH)
{
    if (!Valid(rendered) || !Valid(native) || imageW <= 0 || imageH <= 0 || swapW <= 0 || swapH <= 0) {
        return std::nullopt;
    }
    // pixels per unit of tangent, in the render
    const double px = imageW / static_cast<double>(rendered.right - rendered.left);
    const double py = imageH / static_cast<double>(rendered.up - rendered.down);
    Placement p{};
    p.rectW = static_cast<int>(std::lround((native.right - native.left) * px));
    p.rectH = static_cast<int>(std::lround((native.up - native.down) * py));
    if (p.rectW <= 0 || p.rectH <= 0 || p.rectW > swapW || p.rectH > swapH) {
        return std::nullopt;
    }
    // the render's top-left corner, in the native rect's pixels (negative = the render starts outside it)
    const int x0 = static_cast<int>(std::lround((rendered.left - native.left) * px));
    const int y0 = static_cast<int>(std::lround((native.up - rendered.up) * py));
    p.srcX = std::max(0, -x0);
    p.srcY = std::max(0, -y0);
    p.dstX = std::max(0, x0);
    p.dstY = std::max(0, y0);
    p.copyW = std::min(imageW - p.srcX, p.rectW - p.dstX);
    p.copyH = std::min(imageH - p.srcY, p.rectH - p.dstY);
    if (p.copyW <= 0 || p.copyH <= 0) {
        return std::nullopt;
    }
    p.complete = p.dstX == 0 && p.dstY == 0 && p.copyW == p.rectW && p.copyH == p.rectH;
    return p;
}

}  // namespace preyvr::xr
