#include "preyvr/BodyPose.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::bodypose;

namespace {
constexpr float kDeg = 3.14159265f / 180.0f;
void Require(bool ok, const char* why) { if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); } }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Len(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
float Dist(Vec3 a, Vec3 b) { return Len(Sub(a, b)); }

// Morgan's rig at rest (model space, ik.skel dump of 2026-10-05: the pistol idle).
Skeleton Rest()
{
    Skeleton s;
    s.hip = {-0.001f, 0.0001f, 0.9238f};
    s.spine[0] = {0.0f, 0.0056f, 1.0338f};
    s.spine[1] = {0.0f, -0.0181f, 1.1641f};
    s.spine[2] = {0.0f, -0.0519f, 1.3057f};
    s.spine[3] = {0.0f, -0.0737f, 1.4185f};
    s.neck = {0.0f, -0.1079f, 1.5234f};
    s.head = {0.0f, -0.0858f, 1.6485f};
    s.thigh[0] = {0.0859f, -0.0206f, 0.9544f};
    s.knee[0] = {0.0839f, -0.0941f, 0.5376f};
    s.foot[0] = {0.0510f, -0.3244f, 0.1842f};
    s.thigh[1] = {-0.0889f, 0.0160f, 0.9541f};
    s.knee[1] = {-0.0550f, 0.1823f, 0.5664f};
    s.foot[1] = {-0.0481f, 0.1868f, 0.1433f};
    s.legs = true;
    return s;
}

// The legs keep their bones' lengths and end on their feet.
void RequireLegs(const Skeleton& s, const Plan& p, const char* why)
{
    for (int k = 0; k < 2; ++k) {
        const Vec3 thigh = Apply(p.thigh[k], s.thigh[k]), knee = Apply(p.shin[k], s.knee[k]);
        const Vec3 foot = Apply(p.foot[k], s.foot[k]);
        Require(std::fabs(Dist(thigh, knee) - Dist(s.thigh[k], s.knee[k])) < 1e-3f, why);
        Require(std::fabs(Dist(knee, foot) - Dist(s.knee[k], s.foot[k])) < 1e-3f, why);
        Require(Dist(Apply(p.shin[k], s.foot[k]), foot) < 2e-3f, why);
        // The thigh hangs from the hip as it did.
        Require(Dist(thigh, Apply(p.hip, s.thigh[k])) < 1e-4f, why);
    }
}
} // namespace

int main()
{
    const Skeleton s = Rest();
    // At rest nothing moves.
    {
        const Plan p = Solve(s, Goal{0.0f, s.head, 1.0f});
        Require(p.valid && p.miss < 1e-4f && p.bend < 1e-4f && Len(p.hipShift) < 1e-4f, "at rest nothing moves");
        for (int k = 0; k < 2; ++k) {
            Require(Dist(Apply(p.shin[k], s.knee[k]), s.knee[k]) < 1e-3f, "at rest the knees stay");
        }
    }
    // A turned torso: the head's pivot stays put, the feet turn with the body, and the head
    // itself keeps facing ahead.
    {
        const Plan p = Solve(s, Goal{30.0f * kDeg, s.head, 1.0f});
        Require(p.valid && p.miss < 1e-3f, "a turned torso keeps the head where it is");
        const Vec3 foot = Apply(p.foot[0], s.foot[0]);
        Require(std::fabs(Dist(foot, {s.head.x, s.head.y, foot.z}) -
                          Dist(s.foot[0], {s.head.x, s.head.y, s.foot[0].z})) < 1e-3f, "the feet turn about the head");
        const Vec3 neckForward = Rotate(p.neck.q, {0, 1, 0});
        Require(neckForward.y > 0.999f, "the head does not turn with the torso");
        const Vec3 chest = Rotate(p.spine[3].q, {0, 1, 0});
        Require(std::fabs(std::atan2(-chest.x, chest.y) - 30.0f * kDeg) < 1e-3f, "the chest faces the torso's yaw");
        RequireLegs(s, p, "a turned body's legs");
    }
    // A lean forward over a table: the head's pivot 25 cm ahead and 3 cm lower; the feet stay.
    {
        const Vec3 goal{s.head.x, s.head.y + 0.25f, s.head.z - 0.03f};
        const Plan p = Solve(s, Goal{0.0f, goal, 1.0f});
        Require(p.valid && p.miss < 0.01f, "a 25 cm lean is reached");
        Require(p.bend > 10.0f * kDeg && p.bend < 40.0f * kDeg, "by bending the spine");
        for (int k = 0; k < 2; ++k) { Require(Dist(Apply(p.foot[k], s.foot[k]), s.foot[k]) < 1e-4f, "the feet stay"); }
        RequireLegs(s, p, "a lean's legs");
    }
    // Leaning back is limited: the head ends short of a 30 cm lean back.
    {
        const Vec3 goal{s.head.x, s.head.y - 0.30f, s.head.z - 0.04f};
        const Plan p = Solve(s, Goal{0.0f, goal, 1.0f});
        Require(p.valid && p.bend <= 0.26f + 1e-3f && p.miss > 0.05f, "the spine bends back only so far");
    }
    // A squat 30 cm down: the hips drop and sit back, the knees bend forward, the feet stay.
    {
        const Vec3 goal{s.head.x, s.head.y, s.head.z - 0.30f};
        const Plan p = Solve(s, Goal{0.0f, goal, 1.0f});
        Require(p.valid && p.miss < 0.01f, "a 30 cm squat is reached");
        const Vec3 hip = Apply(p.hip, s.hip);
        Require(hip.z < s.hip.z - 0.2f && hip.y < s.hip.y - 0.05f, "the hips drop and sit back");
        Require(Apply(p.shin[1], s.knee[1]).y > s.knee[1].y + 0.05f, "the knee goes forward");
        RequireLegs(s, p, "a squat's legs");
    }
    // Standing taller than the legs allow: the legs straighten and the rest is a miss.
    {
        const Vec3 goal{s.head.x, s.head.y, s.head.z + 0.20f};
        const Plan p = Solve(s, Goal{0.0f, goal, 1.0f});
        Require(p.valid && p.miss > 0.1f, "the body cannot stand taller than its legs");
        for (int k = 0; k < 2; ++k) {
            const float full = Dist(s.thigh[k], s.knee[k]) + Dist(s.knee[k], s.foot[k]);
            Require(Dist(Apply(p.thigh[k], s.thigh[k]), Apply(p.foot[k], s.foot[k])) <= full * 0.996f,
                    "the legs straighten, no further");
        }
        RequireLegs(s, p, "straight legs");
    }
    // Weight 0 is the animation; a goal 2 m away is not a body and is refused.
    {
        const Plan p = Solve(s, Goal{40.0f * kDeg, {0.3f, 0.3f, 1.2f}, 0.0f});
        Require(p.valid && p.miss < 1e-4f && p.bend < 1e-4f, "weight 0 is the animation");
        Require(!Solve(s, Goal{0.0f, {2.0f, 0.0f, 1.6f}, 1.0f}).valid, "a goal 2 m away is refused");
    }
    // A planted foot: the legs reach it while the body stands elsewhere.
    {
        Goal g{0.0f, s.head, 1.0f};
        g.footSet[1] = true;
        g.foot[1] = {s.foot[1].x, s.foot[1].y - 0.12f, s.foot[1].z};
        const Plan p = Solve(s, g);
        Require(p.valid && Dist(Apply(p.foot[1], s.foot[1]), g.foot[1]) < 1e-4f, "the foot stands where it is planted");
        RequireLegs(s, p, "a planted foot's leg");
    }
    // The planter: a sway does not move the feet; a drift past 20 cm steps them, one
    // at a time, in an arc, onto where the body would stand them; a walk frees them.
    {
        FootPlanter planter;
        const FootSettings fs;
        Vec3 natural[2] = {{0.1f, 0.0f, 0.0f}, {-0.1f, 0.0f, 0.0f}};
        const Vec3 thigh0[2] = {{0.1f, 0.0f, 0.9f}, {-0.1f, 0.0f, 0.9f}};
        const float leg[2] = {1.0f, 1.0f};
        Vec3 out[2]{};
        planter.Update(natural, thigh0, leg, 0.0f, 1.0f / 72.0f, false, fs, out);
        const Vec3 start[2] = {out[0], out[1]};
        for (int k = 0; k < 2; ++k) { natural[k].y += 0.12f; }
        Vec3 thigh[2] = {{0.1f, 0.12f, 0.9f}, {-0.1f, 0.12f, 0.9f}};
        planter.Update(natural, thigh, leg, 0.3f, 1.0f / 72.0f, false, fs, out);
        Require(Dist(out[0], start[0]) < 1e-5f && Dist(out[1], start[1]) < 1e-5f, "a 12 cm sway leaves the feet planted");
        for (int k = 0; k < 2; ++k) { natural[k].y += 0.13f; thigh[k].y += 0.13f; }
        planter.Update(natural, thigh, leg, 0.5f, 1.0f / 72.0f, false, fs, out);
        Require(planter.Stepping(0) != planter.Stepping(1), "25 cm of drift: one foot steps, not both");
        float peak = 0.0f;
        for (int i = 0; i < 60; ++i) {
            planter.Update(natural, thigh, leg, 0.5f, 1.0f / 72.0f, false, fs, out);
            peak = std::max(peak, std::max(out[0].z, out[1].z));
        }
        Require(Dist(out[0], natural[0]) < 1e-4f && Dist(out[1], natural[1]) < 1e-4f, "then the other: both land under the body");
        Require(peak > 0.05f && planter.Steps() == 2, "each step lifts the foot in an arc");
        // A stance that already holds the leg nearly straight is not a reason to step.
        FootPlanter straight;
        const float shortLeg[2] = {0.905f, 0.905f};
        straight.Update(natural, thigh, shortLeg, 0.0f, 1.0f / 72.0f, false, fs, out);
        for (int i = 0; i < 30; ++i) { straight.Update(natural, thigh, shortLeg, 0.0f, 1.0f / 72.0f, false, fs, out); }
        Require(straight.Steps() == 0 && !straight.Stepping(0) && !straight.Stepping(1), "a straight-legged stance stays put");
        natural[0].y += 0.5f;
        planter.Update(natural, thigh, leg, 1.0f, 1.0f / 72.0f, true, fs, out);
        Require(Dist(out[0], natural[0]) < 1e-5f, "a walk the game animates frees the feet");
    }
    std::cout << "body_pose ok\n";
    return 0;
}
