#include "WristLane.h"

#include "AimTakeover.h"
#include "DebugOverlay.h"
#include "Haptics.h"
#include "Logger.h"
#include "SceneQuery.h"
#include "VrOptionsRuntime.h"
#include "XrInput.h"
#include "preyvr/LatestSnapshot.h"
#include "preyvr/StereoCamera.h"
#include "preyvr/WristHolo.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace preyvr::dll {
namespace {

using wrist::Card;

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Length(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
Quaternion Conj(Quaternion q) { return {-q.x, -q.y, -q.z, q.w}; }

// ---- settings (command channel; read on every thread) ----------------------
std::atomic<unsigned> gAnchor{1};          // 1 the drawn forearm, 0 Funk's grip card
std::atomic<int> gAlongMm{75}, gHoverMm{42}, gWidthMm{125}, gTiltDeg{40};
std::atomic<int> gEnterFacing{500}, gExitFacing{220}, gEnterViewing{820}, gExitViewing{620};  // permille
std::atomic<int> gDwellMs{80};
std::atomic<unsigned> gForce{0};           // 1: skip the look test (captures from any pose)
std::atomic<unsigned> gFace{1};            // move the status meters off the face HUD
std::atomic<unsigned> gNativeScale{400};   // percent of the movie's own scale
std::atomic<unsigned> gNativeBackground{0}; // the meters' own backdrop/line (0: the glass instead)
std::atomic<unsigned> gOcclusion{3};       // 1 scene segments, 2 the right hand and its weapon
std::atomic<unsigned> gHaptic{1};          // a faint tick on the left controller as it unfolds

wrist::Placement ReadPlacement()
{
    wrist::Placement p{};
    p.along = gAlongMm.load() * 0.001f;
    p.hover = gHoverMm.load() * 0.001f;
    p.width = gWidthMm.load() * 0.001f;
    p.maxTiltDeg = static_cast<float>(gTiltDeg.load());
    return p;
}
wrist::Thresholds ReadThresholds()
{
    wrist::Thresholds t{};
    t.enterFacing = gEnterFacing.load() * 0.001f;
    t.exitFacing = gExitFacing.load() * 0.001f;
    t.enterViewing = gEnterViewing.load() * 0.001f;
    t.exitViewing = gExitViewing.load() * 0.001f;
    t.dwell = gDwellMs.load() * 0.001f;
    return t;
}

// ---- the arm, per rendered frame (animation thread -> XR / game) -----------
std::mutex gRingMutex;
std::array<WristArmFrame, 16> gRing{};
std::size_t gRingNext = 0;
std::atomic<unsigned long long> gPublished{0};

// Exact frame first: the eye images were rendered for `time`. Otherwise the
// newest older one within 60 ms (the animation ran ahead or a frame was
// skipped), else nothing: a card from some other frame is not on the sleeve.
enum class Match { exact, older, none };
Match FindArm(long long time, WristArmFrame& out)
{
    std::lock_guard lock(gRingMutex);
    const WristArmFrame* best = nullptr;
    for (const auto& f : gRing) {
        if (f.displayTime == 0) { continue; }
        if (f.displayTime == time) { out = f; return Match::exact; }
        if (f.displayTime < time && time - f.displayTime <= 60000000ll &&
            (!best || f.displayTime > best->displayTime)) { best = &f; }
    }
    if (!best) { return Match::none; }
    out = *best;
    return Match::older;
}
bool NewestArm(WristArmFrame& out)
{
    std::lock_guard lock(gRingMutex);
    const WristArmFrame* best = nullptr;
    for (const auto& f : gRing) {
        if (f.displayTime != 0 && f.left && (!best || f.displayTime > best->displayTime)) { best = &f; }
    }
    if (!best) { return false; }
    out = *best;
    return true;
}

wrist::Arm ToArm(const WristArmFrame& f) { return {f.wrist, f.elbow, f.palm}; }

// ---- scene occlusion (game thread) -----------------------------------------
struct Occlusion {
    bool occluded = false;
    std::uint64_t ns = 0;
};
LatestSnapshot<Occlusion> gScene;
std::atomic<bool> gCandidate{false};
std::atomic<unsigned long long> gSceneQueries{0}, gSceneHits{0};

// OpenXR app space -> engine world: the inverse of WorldToOpenXr (AnimIkTakeover).
Vec3 XrToEngine(const GameplayPoseFrame& f, Vec3 p)
{
    const Vec3 engine = Rotate(stereo::kOpenXrToEngine, Sub(p, f.tracking.head.position));
    return Add(f.cameraCentre, Rotate(stereo::YawQuaternion(f.yaw), engine));
}

// ---- XR thread state ---------------------------------------------------------
wrist::Presenter gPresenter;
long long gLastFrameTime = 0;
bool gHaveLastCentre = false;
Vec3 gLastCentre{};
std::atomic<unsigned long long> gExact{0}, gOlder{0}, gMissing{0}, gShownFrames{0};
std::atomic<unsigned> gGateBits{0};
std::atomic<unsigned long long> gOpenings{0};
bool gWasShowing = false;

struct Last {
    bool eligible = false, armValid = false, cardValid = false, sceneOccluded = false, handOccluded = false;
    wrist::View view{};
    float alpha = 0, tilt = 0, roll = 0, speed = 0;
    Vec3 centre{}, axis{}, dorsal{};
    long long armTime = 0, submitted = 0;
};
std::mutex gLastMutex;
Last gLast;

}  // namespace

void PublishWristArm(const WristArmFrame& frame)
{
    std::lock_guard lock(gRingMutex);
    gRing[gRingNext] = frame;
    gRingNext = (gRingNext + 1) % gRing.size();
    gPublished.fetch_add(1, std::memory_order_relaxed);
}

void UpdateWristLane(const GameplayPoseFrame& frame, bool tracking)
{
    if (!(gOcclusion.load() & 1) || !gCandidate.load() || !tracking || !frame.cameraCentreValid) {
        gScene.Publish({false, MonotonicNanoseconds()});
        return;
    }
    WristArmFrame arm{};
    if (!NewestArm(arm)) { return; }
    const Card card = wrist::PlaceCard(ToArm(arm), frame.tracking.head, ReadPlacement());
    if (!card.valid) { return; }
    const Vec3 eye = frame.cameraCentre;
    const float half = ReadPlacement().width * 0.45f;
    bool occluded = false;
    // The centre and both ends along the forearm: an arm half in a wall shows
    // the card over the wall from one end.
    for (const float s : {0.0f, -half, half}) {
        const Vec3 target = XrToEngine(frame, Add(card.pose.position, Scale(card.axis, s)));
        const Vec3 d = Sub(target, eye);
        const float length = Length(d);
        if (!(length > 0.12f) || length > 2.0f) { continue; }
        const Vec3 unit = Scale(d, 1.0f / length);
        // From just ahead of the eye to just short of the card: the card is not
        // physical, and the start must not be inside the head's own geometry.
        const Vec3 a = Add(eye, Scale(unit, 0.05f));
        const Vec3 b = Sub(target, Scale(unit, 0.015f));
        scene::Hit hit{};
        gSceneQueries.fetch_add(1, std::memory_order_relaxed);
        if (QuerySegment(frame, a, b, hit) && hit.distance >= 0.0f) {
            occluded = true;
            gSceneHits.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }
    gScene.Publish({occluded, MonotonicNanoseconds()});
}

WristDecision DecideWrist(bool eligible, const TrackingFrame& latest, long long submittedTime,
                          long long frameTime, float aspect)
{
    WristDecision decision{};
    float dt = gLastFrameTime ? static_cast<float>(frameTime - gLastFrameTime) * 1e-9f : 0.0f;
    gLastFrameTime = frameTime;
    if (!std::isfinite(dt) || dt < 0.0f || dt > 0.1f) { dt = 0.0f; }

    WristArmFrame arm{};
    const Match match = FindArm(submittedTime, arm);
    (match == Match::exact ? gExact : match == Match::older ? gOlder : gMissing).fetch_add(1, std::memory_order_relaxed);
    const bool armValid = match != Match::none && arm.left;
    Card card{};
    if (armValid) { card = wrist::PlaceCard(ToArm(arm), latest.head, ReadPlacement()); }
    wrist::View view = card.valid ? wrist::Measure(card, latest.head) : wrist::View{};
    if (gForce.load() && card.valid) { view.facing = 1.0f; view.viewing = 1.0f; }

    // The right hand and its weapon between the eye and the card.
    bool handOccluded = false;
    if ((gOcclusion.load() & 2) && card.valid && arm.right) {
        const Vec3 eye = latest.head.position, c = card.pose.position;
        handOccluded = wrist::SegmentNearPoint(eye, c, arm.rightWrist, 0.05f) ||
                       wrist::SegmentNearPoint(eye, c, arm.rightGrip, 0.06f);
        if (!handOccluded && arm.weapon) {
            handOccluded = wrist::SegmentNearSegment(eye, c, Sub(arm.rightGrip, Scale(arm.rightAim, 0.12f)),
                                                     Add(arm.rightGrip, Scale(arm.rightAim, 0.40f)), 0.05f);
        }
    }
    Occlusion scene{};
    const bool sceneFresh = gScene.TryRead(scene) &&
        MonotonicNanoseconds() - scene.ns < 150000000ull;
    const bool sceneOccluded = sceneFresh && scene.occluded;

    float speed = 0.0f;
    const auto& left = latest.hands[static_cast<unsigned>(Hand::left)];
    if (left.gripVelocityValid) {
        speed = Length(left.gripLinearVelocity);
    } else if (card.valid && gHaveLastCentre && dt > 0.0f) {
        speed = Length(Sub(card.pose.position, gLastCentre)) / dt;
    }
    gHaveLastCentre = card.valid;
    if (card.valid) { gLastCentre = card.pose.position; }

    wrist::Conditions conditions{};
    conditions.eligible = eligible && card.valid;
    conditions.occluded = sceneOccluded || handOccluded;
    conditions.handSpeed = speed;
    gPresenter.Update(view, conditions, dt, ReadThresholds());
    if (gPresenter.Showing() && !gWasShowing) {
        gOpenings.fetch_add(1, std::memory_order_relaxed);
        if (gHaptic.load()) { QueueHaptic(Hand::left, haptics::Event::WristOpened, latest); }
    }
    gWasShowing = gPresenter.Showing();
    gCandidate.store(gPresenter.Candidate() || gPresenter.Alpha() > 0.0f);

    const float size = WristSizePercent() * 0.01f;
    decision.active = card.valid && gPresenter.Alpha() > 0.004f;
    decision.alpha = gPresenter.Alpha();
    decision.pose = card.pose;
    decision.width = ReadPlacement().width * size * gPresenter.Scale();
    decision.height = aspect > 0.0f ? decision.width / aspect : decision.width;
    // Warm the native capture while the look is forming, so the first faded-in
    // frame already has this frame's meters.
    decision.capture = eligible && card.valid && (gPresenter.Candidate() || decision.active);
    if (decision.active) { gShownFrames.fetch_add(1, std::memory_order_relaxed); }
    if (card.valid) {
        const auto t = ReadThresholds();
        const bool shown = gPresenter.Showing();
        DebugOverlayWristCard(conditions.eligible && !conditions.occluded, decision.active, card.pose, decision.width,
                              decision.height, view.distance, view.facing, view.viewing,
                              shown ? t.exitFacing : t.enterFacing, shown ? t.exitViewing : t.enterViewing);
    }

    {
        std::lock_guard lock(gLastMutex);
        gLast.eligible = eligible;
        gLast.armValid = armValid;
        gLast.cardValid = card.valid;
        gLast.sceneOccluded = sceneOccluded;
        gLast.handOccluded = handOccluded;
        gLast.view = view;
        gLast.alpha = gPresenter.Alpha();
        gLast.tilt = card.tiltDeg;
        gLast.roll = card.rollDeg;
        gLast.speed = speed;
        gLast.centre = card.pose.position;
        gLast.axis = card.axis;
        gLast.dorsal = card.dorsal;
        gLast.armTime = arm.displayTime;
        gLast.submitted = submittedTime;
    }
    return decision;
}

bool WristLegacyAnchor() { return gAnchor.load() == 0; }
void NoteWristGate(unsigned bits) { gGateBits.store(bits, std::memory_order_relaxed); }
unsigned WristNativeScalePercent() { return gNativeScale.load(); }
bool WristNativeBackground() { return gNativeBackground.load() != 0; }
bool WristMovesStatusOffFace() { return gFace.load() != 0 && WristDisplayEnabled(); }

void ResetWristLane()
{
    gPresenter.Reset();
    gWasShowing = false;
    gLastFrameTime = 0;
    gHaveLastCentre = false;
    gCandidate = false;
}

std::string WristReport()
{
    Last l{};
    {
        std::lock_guard lock(gLastMutex);
        l = gLast;
    }
    char b[1024];
    std::snprintf(b, sizeof(b),
        " enabled=%d anchor=%u place=%d,%d,%d,%d gate=%d,%d,%d,%d dwell=%d force=%u face=%u nativeScale=%u/%u occlusion=%u"
        " | gate=%u eligible=%d arm=%d card=%d alpha=%.2f facing=%.2f viewing=%.2f dist=%.3f tilt=%.1f roll=%.1f speed=%.2f"
        " sceneOcc=%d handOcc=%d | centre=%.4f,%.4f,%.4f axis=%.3f,%.3f,%.3f dorsal=%.3f,%.3f,%.3f"
        " | armLagMs=%.1f published=%llu exact=%llu older=%llu missing=%llu shown=%llu openings=%llu sceneQ=%llu sceneHits=%llu",
        WristDisplayEnabled() ? 1 : 0, gAnchor.load(), gAlongMm.load(), gHoverMm.load(), gWidthMm.load(), gTiltDeg.load(),
        gEnterFacing.load(), gExitFacing.load(), gEnterViewing.load(), gExitViewing.load(), gDwellMs.load(),
        gForce.load(), gFace.load(), gNativeScale.load(), gNativeBackground.load(), gOcclusion.load(),
        gGateBits.load(), l.eligible ? 1 : 0, l.armValid ? 1 : 0, l.cardValid ? 1 : 0, l.alpha, l.view.facing, l.view.viewing,
        l.view.distance, l.tilt, l.roll, l.speed, l.sceneOccluded ? 1 : 0, l.handOccluded ? 1 : 0,
        l.centre.x, l.centre.y, l.centre.z, l.axis.x, l.axis.y, l.axis.z, l.dorsal.x, l.dorsal.y, l.dorsal.z,
        l.armTime ? (l.submitted - l.armTime) * 1e-6 : -1.0, gPublished.load(), gExact.load(), gOlder.load(),
        gMissing.load(), gShownFrames.load(), gOpenings.load(), gSceneQueries.load(), gSceneHits.load());
    return b;
}

bool ExecuteWristCommand(const std::vector<std::string>& args, std::ostringstream& out)
{
    if (args.empty() || args[0].rfind("wrist.", 0) != 0) { return false; }
    const std::string& verb = args[0];
    const auto num = [&](std::size_t i, int fallback) {
        return i < args.size() ? std::atoi(args[i].c_str()) : fallback;
    };
    if (verb == "wrist.report") {
        out << "wrist.report result=0" << WristReport();
    } else if (verb == "wrist.anchor") {
        gAnchor = num(1, 1) ? 1u : 0u;
        out << "wrist.anchor result=0 anchor=" << gAnchor.load();
    } else if (verb == "wrist.place") {
        // wrist.place <along mm> <hover mm> <width mm> <max tilt deg>
        gAlongMm = std::clamp(num(1, gAlongMm.load()), -50, 200);
        gHoverMm = std::clamp(num(2, gHoverMm.load()), 0, 150);
        gWidthMm = std::clamp(num(3, gWidthMm.load()), 40, 300);
        gTiltDeg = std::clamp(num(4, gTiltDeg.load()), 0, 90);
        out << "wrist.place result=0" << WristReport();
    } else if (verb == "wrist.gate") {
        // wrist.gate <enter facing> <exit facing> <enter viewing> <exit viewing> [dwell ms]   (permille)
        gEnterFacing = std::clamp(num(1, gEnterFacing.load()), -1000, 1000);
        gExitFacing = std::clamp(num(2, gExitFacing.load()), -1000, 1000);
        gEnterViewing = std::clamp(num(3, gEnterViewing.load()), -1000, 1000);
        gExitViewing = std::clamp(num(4, gExitViewing.load()), -1000, 1000);
        gDwellMs = std::clamp(num(5, gDwellMs.load()), 0, 2000);
        out << "wrist.gate result=0" << WristReport();
    } else if (verb == "wrist.force") {
        gForce = num(1, 1) ? 1u : 0u;
        out << "wrist.force result=0 force=" << gForce.load();
    } else if (verb == "wrist.face") {
        gFace = num(1, 1) ? 1u : 0u;
        out << "wrist.face result=0 face=" << gFace.load();
    } else if (verb == "wrist.native") {
        // wrist.native <scale % of the movie's own> [background 0/1]
        gNativeScale = static_cast<unsigned>(std::clamp(num(1, 400), 100, 600));
        gNativeBackground = num(2, static_cast<int>(gNativeBackground.load())) ? 1u : 0u;
        out << "wrist.native result=0 scale=" << gNativeScale.load() << " background=" << gNativeBackground.load();
    } else if (verb == "wrist.haptic") {
        gHaptic = num(1, 1) ? 1u : 0u;
        out << "wrist.haptic result=0 haptic=" << gHaptic.load();
    } else if (verb == "wrist.occlusion") {
        gOcclusion = static_cast<unsigned>(std::clamp(num(1, 3), 0, 3));
        out << "wrist.occlusion result=0 mask=" << gOcclusion.load();
    } else {
        out << verb << " result=1 detail=unknown_verb";
    }
    return true;
}

}  // namespace preyvr::dll
