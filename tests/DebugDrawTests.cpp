#include "preyvr/BodyEquipment.h"
#include "preyvr/DebugDraw.h"
#include "preyvr/DebugOverlayScene.h"
#include "preyvr/MedkitSlot.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>

using namespace preyvr;
using namespace preyvr::debugdraw;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
Quaternion AboutY(float a) { return {0, std::sin(a / 2), 0, std::cos(a / 2)}; }
Quaternion AboutX(float a) { return {std::sin(a / 2), 0, 0, std::cos(a / 2)}; }
constexpr float kDeg = 3.14159265f / 180.0f;

EyeView Square()
{
    EyeView eye{};
    eye.left = -1; eye.right = 1; eye.up = 1; eye.down = -1;
    eye.width = 1000; eye.height = 1000;
    return eye;
}

void TestProjection()
{
    const EyeView eye = Square();
    auto p = Project(eye, {0, 0, -1});
    Require(p && Near(p->x, 500) && Near(p->y, 500) && Near(p->depth, 1), "straight ahead lands on the centre");
    p = Project(eye, {.5f, 0, -1});
    Require(p && Near(p->x, 750) && Near(p->y, 500), "+X is right");
    p = Project(eye, {0, .5f, -2});
    Require(p && Near(p->x, 500) && Near(p->y, 375) && Near(p->depth, 2), "+Y is up (smaller pixel y), divided by depth");
    Require(!Project(eye, {0, 0, 1}), "behind the eye is refused");
    Require(!Project(eye, {0, 0, -.01f}), "inside the near plane is refused");
    Require(Near(FocalPixels(eye), 500), "focal length in pixels");

    // The frustum as submitted can be asymmetric: the projection must use it as is.
    EyeView skew = Square();
    skew.left = -1.2f; skew.right = .8f; skew.up = .9f; skew.down = -1.1f;
    p = Project(skew, {0, 0, -1});
    Require(p && Near(p->x, 600) && Near(p->y, 450), "an asymmetric frustum moves the centre");

    // A turned and displaced eye: what it looks at is still the image centre.
    EyeView turned = Square();
    turned.pose = {AboutY(90 * kDeg), {1, 1.6f, 0}};
    p = Project(turned, {0, 1.6f, 0});
    Require(p && Near(p->x, 500) && Near(p->y, 500) && Near(p->depth, 1), "yaw 90 degrees looks along -X");
    turned.pose = {AboutX(-30 * kDeg), {0, 1.6f, 0}};
    p = Project(turned, {0, 1.6f - std::sin(30 * kDeg), -std::cos(30 * kDeg)});
    Require(p && Near(p->x, 500) && Near(p->y, 500), "pitching down looks down");
}

void TestNearClip()
{
    const EyeView eye = Square();
    Vec3 a{0, 0, 1}, b{0, 0, -3};
    Require(ClipToNear(eye, a, b) && Near(a.z, -kNearMetres) && Near(b.z, -3), "a segment through the eye is cut at the near plane");
    a = {0, 0, 1}; b = {0, 0, 2};
    Require(!ClipToNear(eye, a, b), "a segment behind the eye vanishes");
}

void TestLines()
{
    const EyeView eye = Square();
    Scene scene;
    scene.lines.push_back({{-.5f, 0, -1}, {.5f, 0, -1}, {255, 0, 0, 255}, .01f, 1.0f});
    std::vector<Vertex> v;
    auto stats = Tessellate(scene, eye, v, 10000);
    Require(v.size() == 6 && stats.lines == 1 && !stats.truncated, "one segment is one quad");
    // 1 cm at 1 m with a 500 px focal length is 5 px: 2.5 px half width + 1 px feather.
    Require(Near(v[0].extent, 3.5f) && Near(std::fabs(v[0].y - 500), 3.5f), "physical width becomes pixels at depth");
    Require(Near(v[0].x, 250) && Near(v[2].x, 750), "the quad spans the projected ends");
    Require(v[0].kind == 0.0f && Near(std::fabs(v[0].u), 1), "edge vertices carry the across coordinate");

    // A far line never thins below its pixel floor.
    scene.lines[0] = {{-50, 0, -100}, {50, 0, -100}, {255, 0, 0, 255}, .01f, 3.0f};
    v.clear();
    Tessellate(scene, eye, v, 10000);
    Require(v.size() == 6 && Near(v[0].extent, 2.5f), "the pixel floor holds at distance");

    // Dashes are bounded however long the ray.
    // Off the view axis, so every dash projects to a visible segment.
    scene.lines[0] = {{.2f, -.1f, -1}, {.2f, -.1f, -201}, {255, 0, 0, 255}, .005f, 1.0f, .001f};
    v.clear();
    Tessellate(scene, eye, v, 1000000);
    Require(v.size() <= 401 * 6 && v.size() >= 50 * 6, "a dashed 200 m ray stays within 400 dashes");

    // The budget is a hard limit.
    v.clear();
    stats = Tessellate(scene, eye, v, 60);
    Require(v.size() <= 60 && stats.truncated, "the vertex budget truncates instead of overflowing");

    // Lines wholly outside the image cost nothing.
    scene.lines[0] = {{5, 0, -1}, {6, 0, -1}, {255, 0, 0, 255}, .005f, 1.0f};
    v.clear();
    Tessellate(scene, eye, v, 10000);
    Require(v.empty(), "an off-image line is culled");
}

void TestSpheresMarkersLabels()
{
    const EyeView eye = Square();
    Scene scene;
    scene.spheres.push_back({{0, 0, -2}, .14f, {255, 255, 255, 255}, {255, 255, 255, 40}});
    std::vector<Vertex> v;
    Tessellate(scene, eye, v, 100000);
    // The fill fan comes first: hub, then two rim points per triangle.
    Require(v.size() > 3 * 48 && v[0].kind == 0.0f, "a sphere fills its silhouette");
    const float expected = std::tan(std::asin(.14f / 2.0f)) * 500;
    for (int i = 0; i < 48; ++i) {
        const auto& rim = v[3 * i + 1];
        Require(Near(std::hypot(rim.x - 500, rim.y - 500), expected, .5f), "on axis the silhouette is the angular radius");
    }
    // Off axis and close, every rim point is where a line of sight grazes the sphere.
    for (Vec3 c : {Vec3{1.2f, -.4f, -1}, Vec3{.3f, -.6f, -.4f}, Vec3{0, 0, -.2f}}) {
        Vec3 rimCentre{}, normal{};
        float rimRadius = 0;
        Require(SphereSilhouette({0, 0, 0}, c, .14f, rimCentre, rimRadius, normal), "outside the sphere: a silhouette");
        const Vec3 u = Perpendicular(normal), w = Cross(normal, u);
        for (int i = 0; i < 12; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / 12;
            const Vec3 t = Add(rimCentre, Add(Scale(u, rimRadius * std::cos(a)), Scale(w, rimRadius * std::sin(a))));
            Require(Near(Length(Sub(t, c)), .14f, 1e-4f), "rim points lie on the sphere");
            Require(Near(Dot(Sub(t, c), t), 0, 1e-4f), "rim points are tangent to the line of sight");
        }
    }
    Vec3 unusedCentre{}, unusedNormal{};
    float unusedRadius = 0;
    Require(!SphereSilhouette({0, 0, 0}, {0, 0, -.1f}, .14f, unusedCentre, unusedRadius, unusedNormal),
            "inside the sphere there is no silhouette");

    scene = {};
    Marker marker{};
    marker.centre = {0, 0, -100};
    marker.radius = .01f;
    marker.minPixels = 6;
    scene.markers.push_back(marker);
    v.clear();
    Tessellate(scene, eye, v, 1000);
    Require(v.size() == 6 && Near(v[0].extent, 6), "markers keep a pixel floor");

    scene = {};
    Label label{};
    label.anchor = {0, 0, -1};
    label.text = "AB C\nD";
    scene.labels.push_back(label);
    v.clear();
    Tessellate(scene, eye, v, 10000);
    // Plate + shadow and glyph for each of the four visible characters.
    Require(v.size() == 6 * (1 + 2 * 4), "a label is a plate plus two quads per visible glyph");
    Require(v[6].kind == 1.0f && v[6].u >= 0 && v[6].u <= 1, "glyph quads sample the atlas");
    const float h = std::fabs(v[8].y - v[6].y);
    Require(h >= label.minPixels - .01f && h <= label.maxPixels + .01f, "glyph height stays within its clamp");

    // Anchored near the right edge: the block slides in instead of being cut.
    scene.labels[0].anchor = {.98f, 0, -1};
    scene.labels[0].text = "HIP R  SQUEEZE: STORE";
    v.clear();
    Tessellate(scene, eye, v, 10000);
    float right = 0;
    for (const auto& vertex : v) { right = std::max(right, vertex.x); }
    Require(!v.empty() && right <= eye.width, "a label near the edge stays inside the image");
    scene.labels[0].keepInside = false;
    v.clear();
    Tessellate(scene, eye, v, 10000);
    right = 0;
    for (const auto& vertex : v) { right = std::max(right, vertex.x); }
    Require(right > eye.width, "without keepInside the label is where it was anchored");

    // The headset shows less than the image: labels stay inside what it shows.
    EyeView narrow = Square();
    SetShownFrustum(narrow, -.8f, .5f, .9f, -.9f);
    Require(Near(narrow.shownX1, 750) && Near(narrow.shownX0, 100) && Near(narrow.shownY0, 50), "shown frustum in pixels");
    scene.labels[0].keepInside = true;
    scene.labels[0].anchor = {.45f, 0, -1};
    v.clear();
    Tessellate(scene, narrow, v, 10000);
    right = 0;
    for (const auto& vertex : v) { right = std::max(right, vertex.x); }
    Require(right <= 750, "a label stays inside the displayed frustum");

    // An anchor outside what is shown: no fragment at the edge.
    scene.labels[0].anchor = {.7f, 0, -1};
    v.clear();
    const auto hidden = Tessellate(scene, narrow, v, 10000);
    Require(v.empty() && hidden.culled == 1, "a label about something out of view is not drawn");
    scene.labels[0].anchor = {.45f, 0, -1};

    // Two labels at one anchor do not overlap.
    scene.labels.push_back(scene.labels[0]);
    v.clear();
    Tessellate(scene, eye, v, 10000);
    const std::size_t perLabel = v.size() / 2;
    float topA = 1e9f, bottomA = -1e9f, topB = 1e9f, bottomB = -1e9f;
    for (std::size_t i = 0; i < 6; ++i) {
        topA = std::min(topA, v[i].y); bottomA = std::max(bottomA, v[i].y);
        topB = std::min(topB, v[perLabel + i].y); bottomB = std::max(bottomB, v[perLabel + i].y);
    }
    Require(bottomA <= topB || bottomB <= topA, "colliding labels are stacked, not overlapped");
}

void TestGestureTruth()
{
    // The drawn slot centre must be exactly where the gesture detects a hand.
    std::mt19937 random(7);
    std::uniform_real_distribution<float> angle(-3.1f, 3.1f), offset(-.5f, .5f);
    for (int trial = 0; trial < 200; ++trial) {
        equipment::HolsterGesture gesture;
        const float yaw = angle(random);
        const Pose head{AboutY(yaw), {offset(random), 1.6f + offset(random) * .2f, offset(random)}};
        // Establish the torso with the hand away and the grip released.
        gesture.Update(head, {head.position.x + 1, head.position.y, head.position.z}, false, true, .011f);
        Require(gesture.BodyValid() && gesture.Armed(), "a released grip arms the gesture");
        const int zone = trial % 2;
        const Vec3 centre = gesture.ZoneCentre(head.position, zone);
        // Inside by a small margin, then squeeze.
        const Vec3 hand{centre.x + .05f, centre.y, centre.z};
        Require(gesture.Update(head, hand, false, true, .011f) < 0, "no squeeze, no action");
        Require(gesture.Update(head, hand, true, true, .011f) == zone, "squeezing at the drawn centre selects that slot");
        Require(gesture.OwnsGrip(), "the slot owns the grip after selection");
    }
    // Just outside the drawn radius: nothing.
    equipment::HolsterGesture gesture;
    const Pose head{{}, {0, 1.6f, 0}};
    gesture.Update(head, {1, 1.6f, 0}, false, true, .011f);
    const Vec3 c = gesture.ZoneCentre(head.position, 0);
    Require(gesture.Update(head, {c.x + equipment::HolsterGesture::Radius + .005f, c.y, c.z}, true, true, .011f) < 0,
            "a hand outside the drawn sphere does not select");

    equipment::MedkitSlot medkit;
    medkit.Update(head, {1, 1.6f, 0}, false, false, true, .011f);
    const Vec3 m = medkit.Zone().ZoneCentre(head.position, 0);
    Require(Near(m.x, -.25f) && Near(m.y, 1.6f - .65f), "the medkit slot is the left hip");
    medkit.Update(head, m, true, false, true, .011f);
    Require(medkit.OwnsGrip() && medkit.TriggerArmed(), "selected with the trigger released: armed");
}

void TestWristMeasureMatchesVisibility()
{
    // MeasureWrist must reproduce the visibility rule exactly.
    std::mt19937 random(11);
    std::uniform_real_distribution<float> a(-3.1f, 3.1f), o(-.6f, .6f);
    for (int trial = 0; trial < 2000; ++trial) {
        const Pose head{Normalize(Multiply(AboutY(a(random)), AboutX(a(random) * .4f))), {0, 1.6f, 0}};
        const Pose grip{Normalize(Multiply(AboutY(a(random)), AboutX(a(random)))), {o(random), 1.2f + o(random), o(random)}};
        const Pose wrist = equipment::WristPose(grip);
        for (bool was : {false, true}) {
            const auto m = equipment::MeasureWrist(head, wrist);
            const bool expected = m.distance >= .23f && m.distance <= 1.f && m.facing > equipment::WristFacingNeeded(was) &&
                                  m.viewing > equipment::WristViewingNeeded(was);
            Require(expected == equipment::WristVisible(head, wrist, was), "the drawn wrist metrics are the visibility rule");
        }
    }
}

void TestSceneLayers()
{
    OverlayFrame f{};
    f.headValid = true;
    f.head = {{}, {0, 1.6f, 0}};
    f.rays[1] = {true, RayRole::Weapon, {.2f, 1.4f, -.3f}, {0, 0, -1}, {.2f, 1.4f, -3}, true, 2.7f};
    f.rays[0] = {true, RayRole::Psi, {-.2f, 1.4f, -.3f}, {0, 0, -1}, {-.2f, 1.4f, -10}, false, 9.7f};
    f.holstersEnabled = true;
    f.holsters[0] = Slot{true, {.25f, .95f, 0}, .14f, {.25f, .95f, .05f}, false, true, false, false, false, Notice::None, 0, "HIP R"};
    f.holsters[1] = Slot{true, {-.2f, 1.3f, -.13f}, .14f, {.2f, 1.4f, -.3f}, false, true, false, true, false, Notice::None, 0, "CHEST L"};
    Scene scene;
    BuildOverlayScene(f, kRays, scene);
    Require(!scene.lines.empty() && scene.spheres.empty() && scene.labels.empty(), "rays only: no slots, no text");
    Require(scene.markers.size() == 3, "a hit draws a dot and a halo, a miss a ring");
    BuildOverlayScene(f, kBody, scene);
    Require(scene.spheres.size() == 2, "both holster slots are drawn");
    BuildOverlayScene(f, kBody | kLabels | kStatus, scene);
    Require(scene.labels.size() == 3, "a label per slot plus the status panel");
    const auto status = OverlayStatusText(f, kAllLayers);
    Require(status.find("hit 2.70 m") != std::string::npos && status.find("psi") != std::string::npos,
            "the status panel reports both rays");
    f.holstersEnabled = false;
    BuildOverlayScene(f, kBody, scene);
    Require(scene.spheres.empty(), "disabled holsters draw nothing");
}

void TestSlotColours()
{
    Slot s{};
    s.valid = true;
    s.centre = {0, 1, 0};
    s.hand = {1, 1, 0};
    s.armed = true;
    Require(SlotColor(s).r == kIdle.r && SlotColor(s).b == kIdle.b, "away: idle");
    s.hand = {.05f, 1, 0};
    Require(SlotColor(s).g == kNear.g && SlotColor(s).b == kNear.b, "inside with a released grip: ready to squeeze");
    s.gripPressed = true;
    s.armed = false;
    Require(SlotColor(s).r == kWarn.r && SlotColor(s).g == kWarn.g, "inside with the grip already held: warn, will not act");
    s.owned = true;
    Require(SlotColor(s).g == kActive.g && SlotColor(s).r == kActive.r, "owned: active");
    s.notice = Notice::Denied;
    s.noticeAge = .3f;
    Require(SlotColor(s).r == kDenied.r && SlotColor(s).g == kDenied.g, "a fresh refusal flashes red");
    s.noticeAge = 5;
    Require(SlotColor(s).g == kActive.g, "an old notice no longer colours the slot");
}

}  // namespace

int main()
{
    TestProjection();
    TestNearClip();
    TestLines();
    TestSpheresMarkersLabels();
    TestGestureTruth();
    TestWristMeasureMatchesVisibility();
    TestSceneLayers();
    TestSlotColours();
    std::cout << "debug_draw: all tests passed\n";
    return 0;
}
