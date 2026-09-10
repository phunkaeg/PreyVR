#include "preyvr/DepthSubmission.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace preyvr::depth;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void TestStandardDepthPutsNearAtMinDepth()
{
    const auto r = BuildRange(0.25f, 1000.0f, 1.0f, false);
    Require(r.valid, "a sane frustum is valid");
    Require(r.minDepth == 0.0f && r.maxDepth == 1.0f, "the image range is 0..1");
    Require(r.nearZ == 0.25f, "minDepth is the near plane");
    Require(r.farZ == 1000.0f, "maxDepth is the far plane");
    Require(r.nearZ < r.farZ, "and so nearZ is the smaller of the two");
    Require(!r.reversed, "not reversed");
}

// **This is the test that matters.** Prey's r_ReverseDepth defaults to 1, so a
// stored 0 is the FAR plane. The OpenXR spec reads nearZ > farZ as the
// declaration of reversed depth, so the swap is not an implementation detail --
// it IS the signal, and omitting it would tell the runtime the depth buffer runs
// the other way.
void TestReversedDepthSwapsTheDistances()
{
    const auto r = BuildRange(0.25f, 1000.0f, 1.0f, true);
    Require(r.valid, "a sane frustum is valid reversed too");
    Require(r.nearZ == 1000.0f, "minDepth (stored 0) is the FAR plane");
    Require(r.farZ == 0.25f, "maxDepth (stored 1) is the NEAR plane");
    Require(r.nearZ > r.farZ, "nearZ > farZ is how reversed depth is declared");
    Require(r.reversed, "and it is reported as reversed");
}

// The two modes must differ by exactly the swap and nothing else. If some other
// term also changed, one of the two would be wrong and the tests above would
// still pass.
void TestTheModesDifferOnlyByTheSwap()
{
    const auto normal = BuildRange(0.1f, 500.0f, 1.0f, false);
    const auto flipped = BuildRange(0.1f, 500.0f, 1.0f, true);
    Require(normal.valid && flipped.valid, "premise");
    Require(normal.nearZ == flipped.farZ, "near and far are exchanged");
    Require(normal.farZ == flipped.nearZ, "in both directions");
    Require(normal.minDepth == flipped.minDepth, "the image range is unaffected");
    Require(normal.maxDepth == flipped.maxDepth, "in both components");
}

void TestUnitsAreConvertedToMetres()
{
    // unitsPerMetre was measured as 1 for this game, so this exercises the path
    // rather than the game's value -- if the measurement is ever revised, the
    // conversion is already here and tested.
    const auto r = BuildRange(2.0f, 100.0f, 2.0f, false);
    Require(r.nearZ == 1.0f, "2 units at 2 units/m is 1 m");
    Require(r.farZ == 50.0f, "100 units at 2 units/m is 50 m");
}

// **A refused range must stay refused.** Substituting a plausible default would
// submit a wrong depth layer, which degrades the whole image; submitting nothing
// merely leaves the runtime where it already was.
void TestBadFrustaAreRefusedRatherThanDefaulted()
{
    Require(!BuildRange(0.0f, 100.0f, 1.0f, false).valid, "zero near is refused");
    Require(!BuildRange(-1.0f, 100.0f, 1.0f, false).valid, "negative near is refused");
    Require(!BuildRange(10.0f, 10.0f, 1.0f, false).valid, "far == near is refused");
    Require(!BuildRange(100.0f, 1.0f, 1.0f, false).valid, "far < near is refused");
    Require(!BuildRange(0.25f, 100.0f, 0.0f, false).valid, "zero scale is refused");
    Require(!BuildRange(0.25f, 100.0f, -1.0f, false).valid, "negative scale is refused");

    const float nan = std::nanf("");
    const float inf = std::numeric_limits<float>::infinity();
    Require(!BuildRange(nan, 100.0f, 1.0f, false).valid, "NaN near is refused");
    Require(!BuildRange(0.25f, inf, 1.0f, false).valid, "infinite far is refused");
    Require(!BuildRange(0.25f, 100.0f, nan, false).valid, "NaN scale is refused");

    // And a refused range carries zeroes, not a half-built one that a careless
    // caller could submit anyway.
    const auto bad = BuildRange(0.0f, 0.0f, 1.0f, true);
    Require(bad.nearZ == 0.0f && bad.farZ == 0.0f, "refused ranges hold nothing");
}

} // namespace

int main()
{
    TestStandardDepthPutsNearAtMinDepth();
    TestReversedDepthSwapsTheDistances();
    TestTheModesDifferOnlyByTheSwap();
    TestUnitsAreConvertedToMetres();
    TestBadFrustaAreRefusedRatherThanDefaulted();
    std::cout << "depth submission tests passed\n";
    return 0;
}
