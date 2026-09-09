#include "preyvr/FrustumCoverage.h"

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

bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

constexpr float kPi = 3.14159265358979323846f;
float Deg(float degrees) { return degrees * kPi / 180.0f; }

// The saved Quest 3 / VDXR session, run-20260908-143942, sequence 12380, left
// eye. These are the numbers in PERFORMANCE-PLAN-2026-09-09.md, and pinning the
// test to them means it checks against something that was actually observed on
// this machine rather than against the algebra used to derive it.
FovAngles SubmittedLeftEye()
{
    return FovAngles{Deg(-60.0f), Deg(60.0f), Deg(61.6815f), Deg(-61.6815f)};
}
FovAngles RuntimeRequestedLeftEye()
{
    return FovAngles{Deg(-54.0f), Deg(40.0f), Deg(44.0f), Deg(-55.0f)};
}

void TestReproducesTheSavedHeadsetMeasurement()
{
    const auto coverage = TangentCoverage(SubmittedLeftEye(), RuntimeRequestedLeftEye());
    Require(coverage.valid, "the saved measurement should be a valid pair of frusta");

    // The plan computed 0.63955 x 0.64497 = 0.41249 by hand.
    Require(Near(coverage.utilisationWidth, 0.63955f), "width utilisation should match the plan");
    Require(Near(coverage.utilisationHeight, 0.64497f), "height utilisation should match the plan");
    Require(Near(coverage.utilisationArea, 0.41249f), "area utilisation should match the plan");

    // **And the half of it the single number could not say.** The runtime's
    // frustum sits entirely inside what we render, so nothing the headset wants
    // is missing -- the 41% is pure waste, not a shortfall. If satisfaction were
    // below 1 here the same 41% would mean something else entirely.
    Require(Near(coverage.satisfactionWidth, 1.0f), "the request is fully covered horizontally");
    Require(Near(coverage.satisfactionHeight, 1.0f), "the request is fully covered vertically");
    Require(!coverage.requestExceedsRender, "the request should not exceed the rendered frustum");
}

void TestIdenticalFrustaWasteNothing()
{
    const auto fov = RuntimeRequestedLeftEye();
    const auto coverage = TangentCoverage(fov, fov);
    Require(coverage.valid, "identical frusta are valid");
    Require(Near(coverage.utilisationArea, 1.0f), "identical frusta use every pixel");
    Require(Near(coverage.satisfactionArea, 1.0f), "identical frusta satisfy the whole request");
    Require(!coverage.requestExceedsRender, "identical frusta do not exceed each other");
}

// The case a single ratio hides: rendering too NARROW. Utilisation stays at 1
// because every pixel drawn is used, while satisfaction falls because the
// wearer is missing content at the edges. Reporting one number would show this
// as healthy.
void TestUnderRenderingIsNotWaste()
{
    const FovAngles narrow{Deg(-30.0f), Deg(30.0f), Deg(30.0f), Deg(-30.0f)};
    const FovAngles wanted{Deg(-54.0f), Deg(40.0f), Deg(44.0f), Deg(-55.0f)};
    const auto coverage = TangentCoverage(narrow, wanted);
    Require(coverage.valid, "an under-rendered pair is still valid");
    Require(Near(coverage.utilisationArea, 1.0f), "every rendered pixel is still used");
    Require(coverage.satisfactionArea < 0.6f, "but most of the request is not covered");
    Require(coverage.requestExceedsRender, "the shortfall must be flagged");
}

// Tangent space, not angle space. An asymmetric frustum whose ANGLES average to
// the symmetric one covers a different tangent extent, so an implementation that
// differenced degrees would report these as equal.
void TestAsymmetryIsMeasuredInTangents()
{
    const FovAngles symmetric{Deg(-50.0f), Deg(50.0f), Deg(50.0f), Deg(-50.0f)};
    const FovAngles skewed{Deg(-20.0f), Deg(80.0f), Deg(50.0f), Deg(-50.0f)};
    const auto coverage = TangentCoverage(symmetric, skewed);
    Require(coverage.valid, "a skewed frustum is valid");

    // Same angular width (100 degrees), wildly different tangent extent.
    const float symmetricSpan = std::tan(Deg(50.0f)) - std::tan(Deg(-50.0f));
    const float skewedSpan = std::tan(Deg(80.0f)) - std::tan(Deg(-20.0f));
    Require(skewedSpan > symmetricSpan * 2.0f, "the premise: tangent spans differ hugely");
    Require(coverage.utilisationWidth < 1.0f, "the skewed request cannot use every rendered pixel");
    Require(coverage.requestExceedsRender, "the skew reaches past what was rendered");
}

void TestRefusesDegenerateAndUnboundedFrusta()
{
    const auto good = RuntimeRequestedLeftEye();

    const FovAngles inverted{Deg(40.0f), Deg(-54.0f), Deg(44.0f), Deg(-55.0f)};
    Require(!TangentCoverage(inverted, good).valid, "right must exceed left");

    const FovAngles flat{Deg(-54.0f), Deg(-54.0f), Deg(44.0f), Deg(-55.0f)};
    Require(!TangentCoverage(flat, good).valid, "a zero-width frustum refuses");

    // At and beyond 90 degrees the tangent is not finite. Refuse rather than
    // return an infinity that would propagate into a resolution decision.
    const FovAngles unbounded{Deg(-90.0f), Deg(90.0f), Deg(44.0f), Deg(-55.0f)};
    Require(!TangentCoverage(unbounded, good).valid, "a 90-degree half-angle refuses");

    const FovAngles nonFinite{std::nanf(""), Deg(40.0f), Deg(44.0f), Deg(-55.0f)};
    Require(!TangentCoverage(nonFinite, good).valid, "a NaN refuses");
    Require(!TangentCoverage(good, nonFinite).valid, "a NaN refuses on either side");
}

void TestEqualDensityScaleIsPerAxis()
{
    const auto coverage = TangentCoverage(SubmittedLeftEye(), RuntimeRequestedLeftEye());
    const float width = EqualDensityScale(coverage.utilisationWidth);
    const float height = EqualDensityScale(coverage.utilisationHeight);

    // 2688 x 2880 at the saved coverage. The plan quotes about 1719 x 1858 for
    // the same sampling density before any margin.
    Require(Near(2688.0f * width, 1719.0f, 2.0f), "width at equal density matches the plan");
    Require(Near(2880.0f * height, 1858.0f, 2.0f), "height at equal density matches the plan");

    Require(Near(EqualDensityScale(0.0f), 0.0f), "a zero fraction has no scale");
    Require(Near(EqualDensityScale(std::nanf("")), 0.0f), "a NaN has no scale");
}

} // namespace

int main()
{
    TestReproducesTheSavedHeadsetMeasurement();
    TestIdenticalFrustaWasteNothing();
    TestUnderRenderingIsNotWaste();
    TestAsymmetryIsMeasuredInTangents();
    TestRefusesDegenerateAndUnboundedFrusta();
    TestEqualDensityScaleIsPerAxis();
    std::cout << "frustum coverage tests passed\n";
    return 0;
}
