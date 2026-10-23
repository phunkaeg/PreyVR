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

std::optional<UvMapping> NativeFovUv(const Tangents& r,const Tangents& n) {
    if(!Valid(r)||!Valid(n)||n.left>=r.right||n.right<=r.left||n.down>=r.up||n.up<=r.down)return {};
    const double w=static_cast<double>(r.right)-r.left,h=static_cast<double>(r.up)-r.down;
    UvMapping m{static_cast<float>((static_cast<double>(n.right)-n.left)/w),
        static_cast<float>((static_cast<double>(n.up)-n.down)/h),
        static_cast<float>((static_cast<double>(n.left)-r.left)/w),
        static_cast<float>((static_cast<double>(r.up)-n.up)/h)};
    if(!std::isfinite(m.scaleX)||!std::isfinite(m.scaleY)||!std::isfinite(m.offsetX)||!std::isfinite(m.offsetY))return {};
    return m;
}

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
    const double width=(static_cast<double>(native.right)-native.left)*px;
    const double height=(static_cast<double>(native.up)-native.down)*py;
    if(!std::isfinite(width)||!std::isfinite(height)||width<.5||height<.5||width>swapW||height>swapH)return {};
    p.rectW = static_cast<int>(std::lround(width));
    p.rectH = static_cast<int>(std::lround(height));
    if (p.rectW <= 0 || p.rectH <= 0 || p.rectW > swapW || p.rectH > swapH) {
        return std::nullopt;
    }
    // the render's top-left corner, in the native rect's pixels (negative = the render starts outside it)
    const double x=(static_cast<double>(rendered.left)-native.left)*px;
    const double y=(static_cast<double>(native.up)-rendered.up)*py;
    // Limit conversions before rounding. Disjoint fields have no copy region.
    if(!std::isfinite(x)||!std::isfinite(y)||x>=p.rectW||y>=p.rectH||x<=-imageW||y<=-imageH)return {};
    const int x0 = static_cast<int>(std::lround(x));
    const int y0 = static_cast<int>(std::lround(y));
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
