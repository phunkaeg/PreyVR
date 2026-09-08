#include "preyvr/WeaponAim.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::aim;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }

Quaternion AxisAngle(Vec3 axis, float radians)
{
    const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    const float s = std::sin(radians * 0.5f) / len;
    return Quaternion{axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f)};
}

Sample FreshSample(std::uint64_t equip, std::uint64_t reference, std::uint64_t published)
{
    Sample s;
    s.origin = Vec3{1.0f, 2.0f, 3.0f};
    s.direction = Vec3{0.0f, 1.0f, 0.0f};
    s.confidence = Confidence::barrel;
    s.equipGeneration = equip;
    s.referenceGeneration = reference;
    s.publishedNs = published;
    return s;
}

// Every reason a sample must be refused, each on its own, so a future change
// that drops one of the checks fails here rather than in a headset.
void TestUsableRefusesStaleSamples()
{
    const std::uint64_t now = 1'000'000'000ull;
    Require(Usable(FreshSample(7, 3, now - 1000), 7, 3, now), "a fresh matching sample is usable");

    Require(!Usable(FreshSample(6, 3, now - 1000), 7, 3, now),
            "a weapon change invalidates the whole sample, not just its rotation");
    Require(!Usable(FreshSample(7, 2, now - 1000), 7, 3, now),
            "a recentre invalidates a world direction computed in the old frame");
    Require(!Usable(FreshSample(7, 3, now - 500'000'000ull), 7, 3, now),
            "an old sample is refused");
    Require(!Usable(FreshSample(7, 3, now + 1000), 7, 3, now),
            "a sample from the future is refused rather than treated as fresh");
    Require(!Usable(FreshSample(7, 3, 0), 7, 3, now), "an unpublished sample is refused");

    Sample noPose = FreshSample(7, 3, now - 1000);
    noPose.confidence = Confidence::none;
    Require(!Usable(noPose, 7, 3, now), "a sample with no pose is refused");

    Sample nan = FreshSample(7, 3, now - 1000);
    nan.direction.x = std::nanf("");
    Require(!Usable(nan, 7, 3, now), "a non-finite direction is refused");
}

// Without a calibration the sample is honest about being the pointing axis
// rather than the barrel, so a consumer cannot silently treat them as the same.
void TestComposeReportsWhatItKnows()
{
    Pose grip;
    grip.position = Vec3{1.0f, 0.0f, 2.0f};
    grip.orientation = Quaternion{};

    const Sample raw = Compose(grip, Quaternion{}, false);
    Require(raw.confidence == Confidence::origin,
            "without a calibration this is an origin and a pointing axis, not a barrel");
    Require(Near(raw.direction.y, 1.0f), "identity grip points along engine forward");
    Require(Near(raw.origin.x, 1.0f) && Near(raw.origin.z, 2.0f), "the origin is the grip");

    // A 90 degree yaw in the grip-to-barrel offset must rotate the direction.
    const Sample turned = Compose(grip, AxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 3.14159265f * 0.5f), true);
    Require(turned.confidence == Confidence::barrel, "a calibrated sample says so");
    Require(Near(turned.direction.x, -1.0f, 1e-3f) && Near(turned.direction.y, 0.0f, 1e-3f),
            "the grip-to-barrel rotation composes on the right");
}

// The projection has to be per eye, and the two eyes must disagree for a point
// at finite depth -- that disagreement IS the stereo.
void TestProjectionIsPerEye()
{
    const Vec3 point{0.0f, 5.0f, 0.0f};          // 5 m straight ahead
    const Quaternion level{};
    const float t = 1.0f;                         // 45 degree symmetric frustum

    const ScreenPoint left = ProjectToScreen(point, Vec3{-0.032f, 0.0f, 0.0f}, level, -t, t, t, -t);
    const ScreenPoint right = ProjectToScreen(point, Vec3{0.032f, 0.0f, 0.0f}, level, -t, t, t, -t);
    Require(left.onScreen && right.onScreen, "a point ahead is on screen for both eyes");
    Require(left.x > right.x,
            "a nearer-than-infinity point must land further right in the LEFT eye");
    Require(Near(left.y, 0.5f, 1e-3f) && Near(right.y, 0.5f, 1e-3f),
            "and at the same height, or the reticle splits vertically");

    // A cyclops projection reused for both eyes has zero disparity, which is the
    // defect this signature exists to prevent.
    const ScreenPoint mono = ProjectToScreen(point, Vec3{}, level, -t, t, t, -t);
    Require(Near(mono.x, 0.5f, 1e-3f), "dead ahead from the midpoint is centred");
    Require(!Near(mono.x, left.x, 1e-4f),
            "reusing one projection for both eyes would lose the disparity");

    // Behind the eye is refused rather than wrapped to a plausible position.
    const ScreenPoint behind = ProjectToScreen(Vec3{0.0f, -5.0f, 0.0f}, Vec3{}, level, -t, t, t, -t);
    Require(behind.behind && !behind.onScreen, "a point behind the eye has no screen position");
}

// An off-screen point must be reported as such, not clamped: a stale crosshair
// left over an unrelated object is worse than no crosshair.
void TestOffScreenIsReported()
{
    const float t = 1.0f;
    const ScreenPoint far = ProjectToScreen(Vec3{50.0f, 1.0f, 0.0f}, Vec3{}, Quaternion{},
                                            -t, t, t, -t);
    Require(!far.onScreen && !far.behind, "far off to the side is off screen but not behind");
    Require(far.x > 1.0f, "and its coordinate says which way, rather than being clamped");
}

} // namespace

int main()
{
    TestUsableRefusesStaleSamples();
    TestComposeReportsWhatItKnows();
    TestProjectionIsPerEye();
    TestOffScreenIsReported();
    std::cout << "PreyVR weapon aim tests passed\n";
    return 0;
}
