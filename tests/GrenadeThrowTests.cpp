#include "preyvr/GrenadeThrow.h"
#include "preyvr/HandCarry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::grenade;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
bool NearV(Vec3 a, Vec3 b, float e = 1e-3f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e); }
float Len(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// Opened still, the grenade falls at the feet: no gain, no minimum speed.
void TestStillHandDrops()
{
    ReleaseInput in{};
    in.nativeMax = 12.0f;
    const Release r = ComputeRelease(in);
    Require(!r.thrown, "a still hand drops");
    Require(NearV(r.velocity, {}), "and the grenade leaves with no velocity of its own");
    in.handVelocity = {0.3f, 0, -0.2f};
    const Release slow = ComputeRelease(in);
    Require(!slow.thrown && Near(slow.gain, 1.0f), "a hand setting it down is not scaled");
    Require(NearV(slow.velocity, in.handVelocity), "it leaves with the hand's own velocity");
}

// Brute-force flight on the floor, independent of the closed form under test.
float Simulate(float speed, float elevation, float height, float g, float k)
{
    float x = 0, z = height, vx = speed * std::cos(elevation), vz = speed * std::sin(elevation);
    const float dt = 1e-4f;
    for (int i = 0; i < 400000 && z > 0; ++i) {
        vx -= k * vx * dt;
        vz -= (g + k * vz) * dt;
        x += vx * dt;
        z += vz * dt;
    }
    return x;
}

// Brute-force landing point of a world velocity (x, y on the floor).
Vec3 Land(Vec3 v, float height, float g, float k)
{
    Vec3 p{0, 0, height};
    const float dt = 1e-4f;
    for (int i = 0; i < 400000 && p.z > 0; ++i) {
        v.x -= k * v.x * dt;
        v.y -= k * v.y * dt;
        v.z -= (g + k * v.z) * dt;
        p.x += v.x * dt;
        p.y += v.y * dt;
        p.z += v.z * dt;
    }
    return {p.x, p.y, 0};
}

void TestRangeOnFloor()
{
    for (float e : {-0.4f, 0.0f, 0.3f, 0.7f, 1.1f}) {
        for (float v : {1.0f, 5.0f, 15.0f}) {
            for (float k : {0.0f, 0.85f}) {
                const float a = RangeOnFloor(v, e, 1.3f, 12.7f, k), b = Simulate(v, e, 1.3f, 12.7f, k);
                Require(std::fabs(a - b) <= 0.01f + 0.002f * b, "range on the floor = brute-force flight");
            }
        }
    }
    Require(RangeOnFloor(10.0f, 0.5f, 1.3f, 9.81f, 0.85f) < RangeOnFloor(10.0f, 0.5f, 1.3f, 9.81f, 0.0f),
            "damping shortens it");
}

// A real throw keeps the hand's direction; with parity it comes down where a
// real ball (x vrFactor) would have.
void TestThrowKeepsDirection()
{
    ReleaseInput in{};
    in.handVelocity = {4.0f, 1.0f, 2.0f};
    in.player = {0.5f, -0.5f, 0};
    in.nativeMax = 35.0f;
    in.height = 1.3f;
    ThrowSettings fixed{};
    fixed.parity = false;
    const Release f = ComputeRelease(in, fixed);
    Require(f.thrown && Near(f.gain, 1.5f), "fixed gain from the throw speed");
    Require(Near(f.speed, Len(in.handVelocity) * 1.5f, 1e-3f), "speed = hand x gain (under the limit)");
    const Release r = ComputeRelease(in);
    const Vec3 own = carry::Sub(r.velocity, in.player);
    const float c = (own.x * 4 + own.y * 1 + own.z * 2) / (Len(own) * Len(in.handVelocity));
    Require(c > 0.99999f, "direction = the hand's");
    // Along the throw's heading, with the player's velocity in both.
    const float hx = 4.0f / std::sqrt(17.0f), hy = 1.0f / std::sqrt(17.0f);
    const Vec3 game = Land(r.velocity, 1.3f, 12.7f, 0.85f);
    const Vec3 real = Land(carry::Add(in.player, carry::Scale(in.handVelocity, 1.3f)), 1.3f, 9.81f, 0.0f);
    Require(std::fabs((game.x * hx + game.y * hy) - (real.x * hx + real.y * hy)) < 0.05f,
            "parity: it lands where a real ball would");
    Require(r.gain > 1.6f && r.gain < 2.6f, "the parity gain is in the expected band");
}

// The speed it leaves at always grows with the hand's, through the ease and the limit.
void TestMonotonic()
{
    ReleaseInput in{};
    in.nativeMax = 10.0f;
    for (float up : {-0.3f, 0.0f, 0.5f, 1.5f}) {
        float last = -1;
        for (int i = 0; i <= 400; ++i) {
            in.handVelocity = {i * 0.05f, 0, i * 0.05f * up};
            const Release r = ComputeRelease(in);
            Require(r.speed >= last - 1e-4f, "speed never drops as the hand speeds up");
            Require(r.speed <= 10.0f * 1.15f + 1e-4f, "never past the limit");
            last = r.speed;
        }
    }
    Require(Near(ThrowWeight(0.5f), 0.0f) && Near(ThrowWeight(5.0f), 1.0f), "throw weight ends");
    Require(ThrowWeight(1.4f) > 0.0f && ThrowWeight(1.4f) < 1.0f, "the throw eases in");
}

// The limit is soft: under the knee untouched, above it compressed, never past.
void TestSoftLimit()
{
    Require(Near(SoftLimit(5.0f, 10.0f, 0.8f), 5.0f), "under the knee untouched");
    const float a = SoftLimit(9.0f, 10.0f, 0.8f), b = SoftLimit(12.0f, 10.0f, 0.8f), c = SoftLimit(40.0f, 10.0f, 0.8f);
    Require(a < 9.0f && a > 8.0f && b > a && c > b && c <= 10.0f, "compressed towards the limit");
    ReleaseInput in{};
    in.nativeMax = 0;   // unknown: the fallback
    in.handVelocity = {40.0f, 0, 0};
    const Release big = ComputeRelease(in);
    Require(big.limited && big.speed <= 40.0f && Near(big.limit, 40.0f), "the fallback limit when the weapon's is unknown");
}

// On the move: the grenade still comes down where a real ball would -- the
// body's velocity plus the arm's -- along the throw's heading.
void TestParityOnTheMove()
{
    for (Vec3 player : {Vec3{4.0f, 0, 0}, Vec3{-4.0f, 0, 0}, Vec3{0, 3.0f, 0}}) {
        ReleaseInput in{};
        in.handVelocity = {5.0f, 0, 2.0f};
        in.player = player;
        in.nativeMax = 35.0f;
        in.height = 1.3f;
        const Release r = ComputeRelease(in);
        const Vec3 game = Land(r.velocity, 1.3f, 12.7f, 0.85f);
        const Vec3 real = Land(carry::Add(player, carry::Scale(in.handVelocity, 1.3f)), 1.3f, 9.81f, 0.0f);
        Require(std::fabs(game.x - real.x) < 0.05f, "on the move: same distance along the throw");
        const Vec3 own = carry::Sub(r.velocity, player);
        Require(std::fabs(own.y) < 1e-4f && own.x > 0, "the arm's direction kept");
    }
}

// A wrist flick: the grenade in the fist moves faster than the grip origin.
void TestLever()
{
    ReleaseInput in{};
    in.handVelocity = {2.0f, 0, 0};
    in.handAngular = {0, 20.0f, 0};   // about +Y: w x r with r = +Z 5 cm -> +X 1 m/s
    in.lever = {0, 0, 0.05f};
    in.nativeMax = 30.0f;
    const Release r = ComputeRelease(in);
    Require(Near(r.objectSpeed, 3.0f, 1e-3f), "v + w x r");
    ThrowSettings s{};
    s.lever = false;
    Require(Near(ComputeRelease(in, s).objectSpeed, 2.0f), "lever off = the grip origin's");
    Require(Near(Len(r.angular), 20.0f), "spin kept under the cap");
    in.handAngular = {0, 60.0f, 0};
    Require(Near(Len(ComputeRelease(in).angular), 20.0f), "spin capped");
}

void TestNonFinite()
{
    ReleaseInput in{};
    in.handVelocity = {NAN, 1, 1};
    in.handAngular = {INFINITY, 0, 0};
    in.player = {NAN, 0, 0};
    const Release r = ComputeRelease(in);
    Require(carry::Finite(r.velocity) && carry::Finite(r.angular), "garbage in, finite out");
}

void TestPullBack()
{
    const Vec3 head{0, 0, 1.6f}, hand{0.6f, 0, 1.6f};
    Require(NearV(PullBack(head, hand, 1.0f, 0.05f), hand), "free: the hand");
    const Vec3 p = PullBack(head, hand, 0.5f, 0.05f);
    Require(NearV(p, {0.25f, 0, 1.6f}), "blocked half way: 5 cm short of the wall");
    Require(NearV(PullBack(head, hand, 0.01f, 0.05f), head), "never behind the head");
}

void TestLanding()
{
    Vec3 at{};
    float t = 0;
    Require(Landing({0, 0, 1.0f}, {5.0f, 0, 0}, 9.81f, 0.0f, at, t), "lands");
    Require(Near(t, std::sqrt(2.0f / 9.81f), 1e-4f) && Near(at.x, 5.0f * t, 1e-4f), "horizontal throw");
    Require(Landing({0, 0, 0}, {0, 3.0f, 4.0f}, 9.81f, 0.0f, at, t) && Near(t, 8.0f / 9.81f, 1e-4f),
            "lob from the floor comes back down");
    Require(!Landing({0, 0, -1.0f}, {1.0f, 0, 0}, 9.81f, 0.0f, at, t), "below the floor and falling: never");
}

}  // namespace

int main()
{
    TestStillHandDrops();
    TestRangeOnFloor();
    TestParityOnTheMove();
    TestThrowKeepsDirection();
    TestMonotonic();
    TestSoftLimit();
    TestLever();
    TestNonFinite();
    TestPullBack();
    TestLanding();
    std::cout << "grenade_throw: all tests passed\n";
    return 0;
}
