#include "preyvr/InventoryDepth.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr::inventory;

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
bool Near(float a, float b, float tolerance = .0001f) { return std::abs(a - b) < tolerance; }
} // namespace

int main()
{
    // The numbers RE-INVENTORY-DEPTH-2026-09-11 recorded, at the panel distance
    // the session actually uses.
    const auto mapping = MapDepth(kObservedCameraUnits, 2.f, .064f, 1.f);
    Require(mapping.has_value(), "the observed camera maps at a 2 m panel");

    // 2 m over 36882.86 units. Stated as a division rather than a literal so
    // the test says where the number comes from.
    Require(Near(mapping->metresPerUnit, 2.f / 36882.86f, 1e-9f),
        "one native unit is the panel distance over the camera distance");

    // Half an IPD converted back into the movie's own units, which is what
    // SetView3D would be handed.
    Require(Near(mapping->halfSeparationUnits, .032f * 36882.86f / 2.f, .01f),
        "the eye offset is half an IPD expressed in native units");

    // **The zero plane must land exactly where the panel is**, or the whole
    // mapping is off by a constant and every plane behind it inherits that.
    Require(Near(PlaneMetres(*mapping, 0.f), 2.f), "the zero plane sits at the panel");

    // Recorded view m22=-1, m32=-C and RH projection W=-viewZ imply W=C+worldZ.
    // Negative authored offsets approach the viewer, not recede from it.
    float previous = 100.f;
    for (const float offset : kObservedPlaneOffsets) {
        const float metres = PlaneMetres(*mapping, offset);
        Require(metres < previous, "negative authored offsets approach the viewer");
        previous = metres;
    }
    const float nearest = PlaneMetres(*mapping, kObservedPlaneOffsets.back());
    Require(nearest > 1.1f && nearest < 1.3f,
        "the most negative plane maps about 0.8 m in front of the panel");

    // depthScale is the comfort dial. Zero must produce two identical images --
    // the honest way to switch stereo off, rather than a very small separation
    // that still costs a second render.
    const auto flat = MapDepth(kObservedCameraUnits, 2.f, .064f, 0.f);
    Require(flat && flat->halfSeparationUnits == 0.f, "zero depth scale is exactly mono");
    Require(Near(flat->metresPerUnit, mapping->metresPerUnit),
        "and does not disturb the depth mapping itself");
    const auto half = MapDepth(kObservedCameraUnits, 2.f, .064f, .5f);
    Require(half && Near(half->halfSeparationUnits, mapping->halfSeparationUnits * .5f, .01f),
        "the dial is linear in between");

    // Placing the panel further away must scale every plane with it, or the
    // layer spacing would look right at one distance and wrong at another.
    const auto far_ = MapDepth(kObservedCameraUnits, 4.f, .064f, 1.f);
    Require(far_.has_value(), "a 4 m panel maps");
    for (const float offset : kObservedPlaneOffsets) {
        Require(Near(PlaneMetres(*far_, offset), PlaneMetres(*mapping, offset) * 2.f, .001f),
            "doubling the panel distance doubles every plane's distance");
    }
    // The separation in NATIVE units must then halve: the same metric offset is
    // fewer of a now-larger unit. Getting this backwards would double parallax
    // exactly when the panel got further away and needed less.
    Require(Near(far_->halfSeparationUnits, mapping->halfSeparationUnits * .5f, .01f),
        "a further panel needs a smaller native-unit eye offset");

    // A wider IPD separates the eyes further, proportionally.
    const auto wide = MapDepth(kObservedCameraUnits, 2.f, .072f, 1.f);
    Require(wide && Near(wide->halfSeparationUnits,
                         mapping->halfSeparationUnits * (.072f / .064f), .01f),
        "the eye offset tracks the measured IPD");

    // Sign of the camera is ignored: the probe recorded it negative, but a
    // consumer reading it from a matrix should not have to know which.
    const auto positive = MapDepth(-kObservedCameraUnits, 2.f, .064f, 1.f);
    Require(positive && Near(positive->metresPerUnit, mapping->metresPerUnit),
        "the camera's sign does not change the scale");

    // Refused, not clamped. Each of these is a measurement, so out of range
    // means the measurement is wrong -- and substituting a plausible number
    // would present a fabricated depth as a real one.
    Require(!MapDepth(std::nanf(""), 2.f, .064f, 1.f), "a non-finite camera is refused");
    Require(!MapDepth(0.f, 2.f, .064f, 1.f), "a zero camera is refused");
    Require(!MapDepth(.5f, 2.f, .064f, 1.f), "a degenerate camera distance is refused");
    Require(!MapDepth(kObservedCameraUnits, .2f, .064f, 1.f), "a panel inside 0.5 m is refused");
    Require(!MapDepth(kObservedCameraUnits, 20.f, .064f, 1.f), "a panel beyond 10 m is refused");
    Require(!MapDepth(kObservedCameraUnits, 2.f, .01f, 1.f), "an implausible IPD is refused");
    Require(!MapDepth(kObservedCameraUnits, 2.f, .2f, 1.f), "so is one that is too wide");
    Require(!MapDepth(kObservedCameraUnits, 2.f, .064f, 1.5f), "depth scale above 1 is refused");
    Require(!MapDepth(kObservedCameraUnits, 2.f, .064f, -.1f), "and below 0");
    Require(!MapDepth(kObservedCameraUnits, 2.f, .064f, std::nanf("")), "and non-finite");

    // A non-finite offset must not propagate into a layer position; it degrades
    // to the panel plane, which is visible and wrong rather than invisible.
    Require(Near(PlaneMetres(*mapping, std::nanf("")), 2.f),
        "a bad offset falls back to the panel plane");

    // Independent projection fixture: at a 2m-wide panel, eyes +/-32mm,
    // C=40000. A vertex at W=C has no texture disparity; W=C/2 receives
    // crossed disparity and W=2C uncrossed disparity. Check after divide.
    for(float w:{20000.f,40000.f,80000.f}) {
        std::array<float,16> native{1,0,0,12000,0,1,0,9000,0,0,0,0,0,0,0,w};
        auto left=native,right=native,control=native;
        Require(ApplyStereoParallax(left,40000,2,-.032f,1) &&
                ApplyStereoParallax(right,40000,2,.032f,1),"stereo projection accepted");
        const float disparity=left[3]/w-right[3]/w;
        Require(Near(disparity,.064f*(40000.f/w-1.f)),"disparity sign and magnitude after homogeneous divide");
        for(unsigned i=4;i<16;++i)Require(left[i]==native[i],"Y/Z/W and native clipping are unchanged");
        Require(ApplyStereoParallax(control,40000,2,-.032f,0) && control==native,"zero depth is bit-exact native output");
        Require(!ApplyStereoParallax(control,40000,0,-.032f,1) && control==native,"invalid geometry refuses without mutation");
    }
    // Tilted geometry has varying W across a triangle. Correct every column,
    // not only the translation; otherwise the triangle distorts incorrectly.
    std::array<float,16> tilted{1,0,0,0,0,1,0,0,0,0,0,0,.2f,-.3f,0,40000};
    auto eye=tilted;
    Require(ApplyStereoParallax(eye,40000,2,-.032f,.5f),"tilted transform accepted");
    Require(Near(eye[0],1.f-.016f*.2f) && Near(eye[1],.016f*.3f) && eye[3]==0,
        "tilted homogeneous depth contributes to the full X row");
    std::cout << "Inventory depth mapping and stereo projection passed\n";
    return 0;
}
