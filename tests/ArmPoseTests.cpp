#include "preyvr/ArmPose.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace preyvr;
using namespace preyvr::armpose;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

constexpr float kDeg = 3.14159265f / 180.0f;
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
float Dist(Vec3 a, Vec3 b) { return Length(Sub(a, b)); }
bool NearVec(Vec3 a, Vec3 b, float e = 1e-3f) { return Dist(a, b) <= e; }
Quaternion AboutX(float a) { return {std::sin(a / 2), 0, 0, std::cos(a / 2)}; }
Quaternion AboutY(float a) { return {0, std::sin(a / 2), 0, std::cos(a / 2)}; }

void TestTorsoFrame()
{
    const TorsoFrame t = TorsoFromYaw(0.0f);
    Require(NearVec(t.forward, {0, 0, -1}) && NearVec(t.right, {1, 0, 0}), "yaw 0 faces -Z, right is +X");
    const TorsoFrame l = TorsoFromYaw(90.0f * kDeg);
    Require(NearVec(l.forward, {-1, 0, 0}) && NearVec(l.right, {0, 0, -1}), "positive yaw turns left");
    Require(std::fabs(YawOf(l.forward) - 90.0f * kDeg) < 1e-4f, "YawOf inverts TorsoFromYaw");
}

void TestHeadYaw()
{
    for (float yaw : {-150.0f, -30.0f, 0.0f, 45.0f, 170.0f}) {
        for (float pitch : {-89.9f, -60.0f, 0.0f, 50.0f, 89.9f}) {
            const Quaternion q = Multiply(AboutY(yaw * kDeg), AboutX(pitch * kDeg));
            const float got = HeadYaw(q);
            float diff = std::fabs(got - yaw * kDeg);
            if (diff > 3.14159265f) { diff = 2 * 3.14159265f - diff; }
            Require(diff < 1e-3f, "head yaw survives any pitch, even straight down or up");
        }
    }
}

float WrapDeg(float r)
{
    float d = std::remainder(r / kDeg, 360.0f);
    return d;
}

// A standing player: eye 1.6 m up, hands where `hands` puts them in the head-yaw frame.
TorsoInput Player(float headYawDeg, float pitchDeg = 0.0f)
{
    TorsoInput in;
    in.head.orientation = Multiply(AboutY(headYawDeg * kDeg), AboutX(pitchDeg * kDeg));
    in.head.position = {0, 1.6f, 0};
    return in;
}
// Hands in the frame of a body facing `bodyYawDeg`: x right, y up from the eye, z forward.
void Hands(TorsoInput& in, float bodyYawDeg, Vec3 right, Vec3 left)
{
    const TorsoFrame t = TorsoFromYaw(bodyYawDeg * kDeg);
    const auto place = [&](Vec3 v) {
        return Vec3{in.head.position.x + t.right.x * v.x + t.forward.x * v.z, in.head.position.y + v.y,
                    in.head.position.z + t.right.z * v.x + t.forward.z * v.z};
    };
    in.hand[0] = place(right);
    in.hand[1] = place(left);
    in.handValid[0] = in.handValid[1] = true;
}
float Run(TorsoEstimator& e, const TorsoInput& in, float seconds)
{
    const TorsoSettings s;
    float y = 0.0f;
    for (float t = 0.0f; t < seconds; t += 1.0f / 72.0f) { y = e.Update(in, 1.0f / 72.0f, s); }
    return y;
}

void TestTorsoEstimator()
{
    // The first sample sets the torso to the head.
    TorsoEstimator e;
    Require(std::fabs(WrapDeg(e.Update(Player(20), 0.01f, TorsoSettings{})) - 20.0f) < 1e-3f,
            "the first sample sets the torso");
    // Looking over the shoulder with the hands still forward (a two-handed hold in front): the
    // torso stays with the hands, well short of the head.
    TorsoEstimator over;
    TorsoInput rest = Player(0);
    Hands(rest, 0, {0.15f, -0.35f, 0.1f}, {-0.1f, -0.33f, 0.1f});
    Run(over, rest, 1.0f);
    TorsoInput look = Player(80);
    Hands(look, 0, {0.15f, -0.35f, 0.1f}, {-0.1f, -0.33f, 0.1f});
    const float held = WrapDeg(Run(over, look, 2.0f));
    Require(held > 5.0f && held < 45.0f, "hands held forward keep the torso while the head looks over the shoulder");
    // Hands down at the sides, head turned: the hanging hands anchor the torso too.
    TorsoEstimator sides;
    TorsoInput hang = Player(0);
    Hands(hang, 0, {0.22f, -0.75f, 0.0f}, {-0.22f, -0.75f, 0.0f});
    Run(sides, hang, 1.0f);
    TorsoInput hangLook = Player(60);
    Hands(hangLook, 0, {0.22f, -0.75f, 0.0f}, {-0.22f, -0.75f, 0.0f});
    const float anchored = WrapDeg(Run(sides, hangLook, 2.0f));
    Require(anchored > 5.0f && anchored < 40.0f, "hands hanging at the sides anchor the torso");
    // Without hands the head and its history win: a held turn is followed all the way.
    TorsoEstimator bare;
    Run(bare, Player(0), 0.5f);
    Require(std::fabs(WrapDeg(Run(bare, Player(50), 3.0f)) - 50.0f) < 1.5f, "a held head turn is followed");
    // A glance (the head away and back within a quarter second) turns the torso less than half
    // way, and it comes back.
    TorsoEstimator glance;
    Run(glance, Player(0), 0.5f);
    const float during = WrapDeg(Run(glance, Player(40), 0.12f));
    Require(during < 20.0f, "a glance turns the body less than half way");
    Require(std::fabs(WrapDeg(Run(glance, Player(0), 3.0f))) < 1.5f, "and the body comes back with the head");
    // Never more than 70 degrees from the head, whatever the hands say.
    TorsoEstimator cap;
    TorsoInput capRest = Player(0);
    Hands(capRest, 0, {0.15f, -0.35f, 0.1f}, {-0.1f, -0.33f, 0.1f});
    Run(cap, capRest, 1.0f);
    TorsoInput behind = Player(170);
    Hands(behind, 0, {0.15f, -0.35f, 0.1f}, {-0.1f, -0.33f, 0.1f});
    const float capped = WrapDeg(cap.Update(behind, 1.0f / 72.0f, TorsoSettings{}));
    Require(std::fabs(WrapDeg((capped - 170.0f) * kDeg)) <= 70.0f + 1e-2f, "never more than 70 degrees from the head");
    // Steering with the stick: the body faces the view.
    TorsoEstimator steer;
    TorsoInput walk = Player(0);
    Hands(walk, 0, {0.15f, -0.35f, 0.1f}, {-0.1f, -0.33f, 0.1f});
    Run(steer, walk, 1.0f);
    TorsoInput walkLook = Player(45);
    Hands(walkLook, 0, {0.15f, -0.35f, 0.1f}, {-0.1f, -0.33f, 0.1f});
    walkLook.steering = true;
    Require(std::fabs(WrapDeg(Run(steer, walkLook, 1.0f)) - 45.0f) < 1.0f, "steering faces the view");
    // Wrap-around: a head at -179 degrees from a torso at +179 is 2 degrees away.
    TorsoEstimator w;
    Run(w, Player(179), 0.5f);
    const float wrapped = WrapDeg(Run(w, Player(-179), 0.05f));
    Require(std::fabs(std::fabs(wrapped) - 179.5f) < 1.5f, "yaw differences wrap");
}

void TestShoulder()
{
    const Body body;
    const TorsoFrame t = TorsoFromYaw(0.0f);
    const Vec3 eye{0, 1.6f, 0};
    const Vec3 r = ShoulderTarget(eye, t, +1, body), l = ShoulderTarget(eye, t, -1, body);
    Require(NearVec(r, {0.185f, 1.37f, 0.09f}), "right shoulder: out, below and behind the eye");
    Require(NearVec(l, {-0.185f, 1.37f, 0.09f}), "left shoulder mirrors");
    // The clavicle reaches its target within its stretch, and stops short beyond it.
    const Vec3 root{0.03f, 1.38f, 0.025f};
    const Vec3 within = PlaceShoulder(root, 0.17f, r, 0.15f);
    Require(NearVec(within, r), "a target within the clavicle's stretch is reached");
    const Vec3 far{0.5f, 1.38f, 0.025f};
    const Vec3 limited = PlaceShoulder(root, 0.17f, far, 0.15f);
    Require(std::fabs(Dist(limited, root) - 0.17f * 1.15f) < 1e-4f, "beyond its stretch the clavicle stops");
    Require(std::fabs(limited.y - root.y) < 1e-4f && limited.x > root.x, "towards the target");
}

void TestElbow()
{
    const Vec3 s{0.185f, 1.37f, 0.09f};
    const TorsoFrame t = TorsoFromYaw(0.0f);
    const Vec3 pole = BodyPole(t, +1);
    Require(pole.y < -0.7f && pole.x > 0.3f && pole.z > 0.1f, "the anatomical elbow points down, out and back");
    // A reachable wrist: both segments keep their length, the elbow towards the pole.
    const Vec3 w{0.2f, 1.15f, -0.25f};
    const ArmSolution a = SolveElbow(s, w, 0.285f, 0.275f, pole, 1.1f);
    Require(std::fabs(Dist(a.elbow, s) - 0.285f) < 1e-4f && std::fabs(Dist(a.elbow, w) - 0.275f) < 1e-4f,
            "the segments meet at the elbow");
    Require(a.scale == 1.0f && !a.straight, "a reachable wrist needs no stretch");
    const Vec3 mid{(s.x + w.x) / 2, (s.y + w.y) / 2, (s.z + w.z) / 2};
    Require(a.elbow.y < mid.y, "the elbow hangs below the arm's axis");
    // Beyond reach the segments lengthen and stay connected.
    const Vec3 far{0.2f, 1.37f, -0.50f};
    const ArmSolution b = SolveElbow(s, far, 0.285f, 0.275f, pole, 1.1f);
    Require(b.scale > 1.0f && b.scale < 1.1f && !b.straight, "a little beyond reach: stretched, not straight");
    Require(std::fabs(Dist(b.elbow, far) - 0.275f * b.scale) < 1e-4f, "stretched segments still meet the wrist");
    const ArmSolution c = SolveElbow(s, Vec3{0.2f, 1.37f, -0.9f}, 0.285f, 0.275f, pole, 1.1f);
    Require(c.straight, "far beyond reach is reported");
}

void TestWristAngles()
{
    const Vec3 length{0, 0, -1}, palm{0, -1, 0};
    Require(std::fabs(WristFlexionDeg(length, length, palm)) < 1e-3f, "a straight wrist does not bend");
    const float c = std::cos(30 * kDeg), s = std::sin(30 * kDeg);
    // Flexed 30 degrees: the forearm, seen from the hand, rises out of the palm side.
    Require(std::fabs(WristFlexionDeg(Vec3{0, s, -c}, length, palm) - 30.0f) < 1e-2f, "flexion towards the palm");
    Require(std::fabs(WristFlexionDeg(Vec3{0, -s, -c}, length, palm) + 30.0f) < 1e-2f, "extension is negative");
    Require(std::fabs(WristDeviationDeg(Vec3{s, 0, -c}, length, palm) - 30.0f) < 1e-2f, "side deviation");
    Require(std::fabs(WristDeviationDeg(Vec3{0, s, -c}, length, palm)) < 1e-2f, "flexion is not deviation");
    // Right hand, palm down: the thumb is on length x palm = -X... (0,0,-1) x (0,-1,0) = (-1,0,0).
    // Deviated 20 degrees towards the thumb, the forearm leans away from it, to +X.
    const float c2 = std::cos(20 * kDeg), s2 = std::sin(20 * kDeg);
    Require(std::fabs(WristRadialDeg(Vec3{s2, 0, -c2}, length, palm, +1) - 20.0f) < 1e-2f, "radial is positive");
    Require(std::fabs(WristRadialDeg(Vec3{-s2, 0, -c2}, length, palm, +1) + 20.0f) < 1e-2f, "ulnar is negative");
    Require(std::fabs(WristRadialDeg(Vec3{-s2, 0, -c2}, length, palm, -1) - 20.0f) < 1e-2f, "the left mirrors");
}

Vec3 ElbowFor(const Vec3& s, const Vec3& w, const Vec3& length, const Vec3& palm)
{
    const Vec3 pole = ChooseElbowPole(s, w, 0.285f, 0.275f, 1.1f, length, palm, +1, BodyPole(TorsoFromYaw(0), +1),
                                      WristLimits{});
    return SolveElbow(s, w, 0.285f, 0.275f, pole, 1.1f).elbow;
}

void TestElbowFollowsHand()
{
    const Vec3 s{0.185f, 1.37f, 0.09f};
    const Vec3 w{0.2f, 1.15f, -0.25f};   // in front of the belly
    // Thumb up, palm facing in: the forearm runs level behind the hand, the
    // elbow at the body's side, not inside it.
    const Vec3 thumbUp = ElbowFor(s, w, Vec3{0, 0, -1}, Vec3{-1, 0, 0});
    Require(std::fabs(thumbUp.y - w.y) < 0.06f, "thumb up: a level forearm");
    Require(thumbUp.x > 0.15f, "thumb up: the elbow at the side, outside the body");
    // Palm down: the forearm close to the vertical plane of the hand -- the
    // elbow behind the wrist, out only by the wrist's resting ulnar deviation.
    const Vec3 palmDown = ElbowFor(s, w, Vec3{0, 0, -1}, Vec3{0, -1, 0});
    Require(std::fabs(palmDown.x - w.x) < 0.07f, "palm down: the elbow behind the wrist");
    const Vec3 f2{(w.x - palmDown.x) / 0.275f, (w.y - palmDown.y) / 0.275f, (w.z - palmDown.z) / 0.275f};
    const float r2 = WristRadialDeg(f2, Vec3{0, 0, -1}, Vec3{0, -1, 0}, +1);
    Require(r2 < 0.0f && r2 > -15.0f, "palm down: a little ulnar");
    for (const Vec3& e : {thumbUp, palmDown}) {
        Require(std::fabs(Dist(e, s) - 0.285f) < 1e-3f, "the elbow is on the arm");
    }
    const Vec3 f1{(w.x - thumbUp.x) / 0.275f, (w.y - thumbUp.y) / 0.275f, (w.z - thumbUp.z) / 0.275f};
    const float radial = WristRadialDeg(f1, Vec3{0, 0, -1}, Vec3{-1, 0, 0}, +1);
    Require(radial < 0.0f && radial > -15.0f, "thumb up: a little ulnar, as round a controller");
    // Continuity: a small turn of the hand moves the elbow a little.
    const float a = 0.05f;
    const Vec3 turned = ElbowFor(s, w, Vec3{0, 0, -1}, Vec3{-std::cos(a), -std::sin(a), 0});
    Require(Dist(turned, thumbUp) < 0.03f, "the elbow is continuous in the hand");
    // A hand that cannot decide (pointing along the arm) leaves the anatomical elbow.
    const Vec3 axis{w.x - s.x, w.y - s.y, w.z - s.z};
    const float l = Length(axis);
    const Vec3 along = ElbowFor(s, w, Vec3{axis.x / l, axis.y / l, axis.z / l}, Vec3{-1, 0, 0});
    Require(along.y < (s.y + w.y) / 2, "no information: the elbow hangs");
}

void TestFromTo()
{
    const Vec3 a{1, 0, 0}, b{0, 0.6f, 0.8f};
    Require(NearVec(Rotate(FromTo(a, b), a), b, 1e-5f), "FromTo turns from onto to");
    Require(NearVec(Rotate(FromTo(a, Vec3{-1, 0, 0}), a), Vec3{-1, 0, 0}, 1e-5f), "and the half turn");
    const Vec3 s = SmoothDirection(Vec3{1, 0, 0}, Vec3{0, 1, 0}, 0.01f, 0.05f);
    Require(s.x > s.y && s.y > 0.1f && std::fabs(Length(s) - 1) < 1e-5f, "smoothing moves part of the way");
}

} // namespace

int main()
{
    TestTorsoFrame();
    TestHeadYaw();
    TestTorsoEstimator();
    TestShoulder();
    TestElbow();
    TestWristAngles();
    TestElbowFollowsHand();
    TestFromTo();
    std::cout << "arm_pose: all tests passed\n";
    return 0;
}
