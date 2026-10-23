#include "preyvr/ShotRay.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::shot;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

void TestRoutes()
{
    Require(RouteFromReturnRva(0x169F492) == Route::Gloo, "GLOO query return");
    Require(RouteFromReturnRva(0x16AB0C3) == Route::Ballistic, "pistol/shotgun/toy-gun query return");
    Require(RouteFromReturnRva(0x13BB791) == Route::Route13BB, "0x13BB530 query return");
    Require(RouteFromReturnRva(0x16A2CC8) == Route::Helper16A2, "0x16A29E0 query return");
    Require(RouteFromReturnRva(0x169F48D) == Route::Unknown, "a call site is not a return address");
    Require(FiringPositionFlags(Route::Gloo) == 0x8000 && FiringPositionFlags(Route::Ballistic) == 0,
            "each route's own firing-position flags");
}

void TestGeometry()
{
    Require(Near(AngleDegrees({0, 1, 0}, {0, 1, 0}), 0), "same direction");
    Require(Near(AngleDegrees({0, 1, 0}, {1, 0, 0}), 90, 1e-3f), "perpendicular");
    // Small angles stay exact: 1 mm at 10 m is 0.0057 degrees.
    Require(Near(AngleDegrees({0, 10, 0}, {.001f, 10, 0}), .0057296f, 2e-5f), "small angle precision");
    Require(Near(DistanceToLine({.3f, 5, .4f}, {0, 0, 0}, {0, 1, 0}), .5f), "perpendicular distance");
    Require(Near(DistanceToLine({0, -5, 0}, {0, 0, 0}, {0, 1, 0}), 0), "a line, not a ray");
}

// The handoff's measurement, reproduced: CryEngine is Z-up, forward +Y here.
// Eye at 1.6 m; hand at the hip 20 cm right, 20 cm down, 30 cm forward;
// aiming straight ahead at a wall 3.94 m away.
void TestOldConvergenceAndTheFix()
{
    const Vec3 eye{0, 0, 1.6f};
    const Vec3 muzzle{.2f, .3f, 1.4f};
    const Vec3 dir{0, 1, 0};
    const float wall = 3.94f;
    // Old: the query's ray starts at the eye; the shot flies from the muzzle to
    // where the eye ray lands.
    const Vec3 eyeHit = Add(eye, Scale(dir, wall));
    const auto old = Measure(muzzle, eyeHit, muzzle, dir);
    Require(old.valid && old.targetOffLineMetres > .28f && old.targetOffLineMetres < .29f,
            "old: the target is the eye's point, 28 cm off the barrel line");
    // atan(0.283 / 3.64) = 4.44 degrees.
    Require(old.angleDegrees > 4.4f && old.angleDegrees < 4.5f, "old: the projectile turns ~4.4 degrees off the barrel");
    Require(ConvergenceErrorMetres(muzzle, eye, dir, 1.0f) > .28f,
            "old: at 1 m the shot is as far off as the hand is from the eye");
    // New: the query's ray starts at the muzzle.
    const Vec3 muzzleHit = Add(muzzle, Scale(dir, wall - .3f));
    const auto fixed = Measure(muzzle, muzzleHit, muzzle, dir);
    Require(fixed.valid && Near(fixed.angleDegrees, 0) && Near(fixed.targetOffLineMetres, 0) &&
                Near(fixed.spawnOffLineMetres, 0),
            "new: the projectile flies exactly along the drawn ray");
    for (float d : {.5f, 1.f, 4.f, 40.f}) {
        Require(Near(ConvergenceErrorMetres(muzzle, muzzle, dir, d), 0), "new: zero error at every distance");
    }
}

void TestMeasureRefusesNonsense()
{
    Require(!Measure({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 1, 0}).valid, "zero-length flight");
    Require(!Measure({0, 0, 0}, {0, 1, 0}, {0, 0, 0}, {0, 0, 0}).valid, "no direction");
    Require(!Measure({NAN, 0, 0}, {0, 1, 0}, {0, 0, 0}, {0, 1, 0}).valid, "non-finite input");
}

void TestSpawnDecision()
{
    SpawnInput in{};
    Require(DecideSpawn(in) == Spawn::Native, "the engine used the muzzle: nothing to decide");
    in.nativeFallback = true;
    Require(DecideSpawn(in) == Spawn::Blocked, "no muzzle or hand: keep the engine's fallback");
    in.haveMuzzle = in.haveHand = true;
    in.eyeToHandClear = true;
    Require(DecideSpawn(in) == Spawn::Blocked, "barrel through a wall: keep the fallback");
    in.handToMuzzleClear = true;
    Require(DecideSpawn(in) == Spawn::Restored, "weapon in open air: the shot leaves the muzzle");
    in.eyeToHandClear = false;
    Require(DecideSpawn(in) == Spawn::Blocked, "hand through a wall: keep the fallback");
    Require(SpawnName(Spawn::Restored)[0] == 'r', "names");
}

void TestSegmentStart()
{
    const Vec3 a{0, 0, 0}, b{0, 1, 0};
    const Vec3 s = SegmentStart(a, b, .4f);
    Require(Near(s.y, .6f) && Near(s.x, 0), "a long reach checks the part nearest the muzzle");
    const Vec3 t = SegmentStart(a, {0, .3f, 0}, .4f);
    Require(Near(t.y, 0), "a short one is checked whole");
}

}  // namespace

int main()
{
    TestRoutes();
    TestGeometry();
    TestOldConvergenceAndTheFix();
    TestMeasureRefusesNonsense();
    TestSpawnDecision();
    TestSegmentStart();
    std::cout << "shot_ray: all tests passed\n";
    return 0;
}
