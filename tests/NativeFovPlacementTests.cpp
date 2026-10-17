#include "preyvr/NativeFovPlacement.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr::xr;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}
float T(float degrees) { return std::tan(degrees * 3.14159265f / 180.0f); }

// The direction a native-rect pixel shows, through the placement and back into the render: it must be the direction
// the render drew at that pixel. Checked at the copy's corners and centre.
void RequireSameDirections(const Placement& p, const Tangents& r, int w, int h, const Tangents& n, const char* what)
{
    const double rx = (r.right - r.left) / w, ry = (r.up - r.down) / h;
    const double nx = (n.right - n.left) / p.rectW, ny = (n.up - n.down) / p.rectH;
    for (int i = 0; i < 3; ++i) {
        const double fx = i * 0.5, fy = i * 0.5;
        const double sx = p.srcX + fx * (p.copyW - 1) + 0.5, sy = p.srcY + fy * (p.copyH - 1) + 0.5;
        const double dx = p.dstX + (sx - p.srcX), dy = p.dstY + (sy - p.srcY);
        const double renderTx = r.left + sx * rx, renderTy = r.up - sy * ry;
        const double nativeTx = n.left + dx * nx, nativeTy = n.up - dy * ny;
        // within one render pixel
        Require(std::fabs(renderTx - nativeTx) <= rx && std::fabs(renderTy - nativeTy) <= ry, what);
    }
}

}  // namespace

int main()
{
    // The Quest 3 session: Prey renders 2016x2160 at +-49 x +-50.95 degrees; the eyes are mirrored-asymmetric.
    const Tangents prey{-T(49), T(49), T(50.95f), -T(50.95f)};
    const Tangents left{-T(52), T(42), T(48), -T(50)}, right{-T(42), T(52), T(48), -T(50)};
    for (const Tangents* eye : {&left, &right}) {
        const auto p = PlaceInNativeFov(prey, 2016, 2160, *eye, 2016, 2160);
        Require(p.has_value(), "the Quest 3 native field fits Prey's swapchain");
        Require(p->rectW < 2016 && p->rectH < 2160, "the native rect is smaller than the render");
        Require(!p->complete, "Prey's 49 degrees do not reach the outer 52: a band stays uncovered");
        RequireSameDirections(*p, prey, 2016, 2160, *eye, "every copied pixel keeps its direction");
    }
    const auto l = *PlaceInNativeFov(prey, 2016, 2160, left, 2016, 2160);
    const auto r = *PlaceInNativeFov(prey, 2016, 2160, right, 2016, 2160);
    Require(l.dstX > 0 && l.srcX == 0, "left eye: the uncovered band is on the left, the outer side");
    Require(r.dstX == 0 && r.srcX > 0, "right eye: the render's surplus is cut on the left, the inner side");
    Require(l.rectW == r.rectW && l.rectH == r.rectH, "mirrored eyes get rects of the same size");
    Require(l.srcY == r.srcY && l.srcY > 0, "both eyes drop the same rows above the native top (48 < 50.95)");

    // A render that covers the native field entirely: the copy fills the rect.
    const Tangents wide{-T(55), T(55), T(52), -T(52)};
    const auto w = PlaceInNativeFov(wide, 2200, 2000, left, 2200, 2000);
    Require(w && w->complete && w->dstX == 0 && w->dstY == 0, "a wider render covers the whole native field");
    RequireSameDirections(*w, wide, 2200, 2000, left, "wide render keeps directions");

    // Identity: native field = rendered field.
    const auto same = PlaceInNativeFov(prey, 2016, 2160, prey, 2016, 2160);
    Require(same && same->complete && same->rectW == 2016 && same->rectH == 2160 && same->srcX == 0 && same->dstX == 0,
            "the same field is a plain full copy");

    // A native field too large for the swapchain at the render's density is refused, never squeezed.
    const Tangents huge{-T(70), T(70), T(60), -T(60)};
    Require(!PlaceInNativeFov(prey, 2016, 2160, huge, 2016, 2160), "a field that does not fit is refused");
    // Degenerate input.
    Require(!PlaceInNativeFov({1, -1, 1, -1}, 2016, 2160, left, 2016, 2160), "an inverted frustum is refused");
    Require(!PlaceInNativeFov(prey, 0, 2160, left, 2016, 2160), "an empty image is refused");
    // No overlap at all.
    const Tangents far{T(60), T(70), T(10), -T(10)};
    Require(!PlaceInNativeFov({-T(10), T(10), T(10), -T(10)}, 200, 200, far, 2016, 2160), "disjoint fields are refused");

    std::cout << "native_fov_placement: all checks passed\n";
    return 0;
}
