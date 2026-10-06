#include "preyvr/DebugOverlayScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace preyvr::debugdraw {
namespace {

constexpr float kNoticeSeconds = 1.2f;

bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

std::string Fixed(float value, int decimals)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", decimals, static_cast<double>(value));
    return text;
}

bool NoticeLive(const Slot& slot) { return slot.notice != Notice::None && slot.noticeAge >= 0 && slot.noticeAge < kNoticeSeconds; }

const char* NoticeText(Notice notice)
{
    switch (notice) {
    case Notice::Ready: return "READY";
    case Notice::Success: return "OK";
    case Notice::Empty: return "EMPTY";
    case Notice::Denied: return "DENIED";
    default: return "";
    }
}

void AddRay(Scene& scene, const HandRay& ray, Color color, unsigned mask, bool rightHand)
{
    if (!ray.valid || !Finite(ray.origin) || !Finite(ray.target)) { return; }
    // The right hand without a weapon still has the game's aim ray, but no shot
    // follows it: drawn faint so it is not mistaken for a weapon's.
    if (rightHand && ray.role != RayRole::Weapon) { color = WithAlpha(color, 120); }
    // The path a shot (or a power) takes: from the hand to where the game's ray ends.
    scene.lines.push_back({ray.origin, ray.target, color, .0045f, 2.0f});
    // A soft halo makes a thin ray readable against bright surfaces.
    scene.lines.push_back({ray.origin, ray.target, WithAlpha(color, 50), .012f, 4.0f});
    Marker end{};
    end.centre = ray.target;
    end.color = color;
    end.filled = ray.hit;
    end.radius = ray.hit ? .018f : .03f;
    end.minPixels = ray.hit ? 5.0f : 7.0f;
    scene.markers.push_back(end);
    if (ray.hit) {
        Marker halo = end;
        halo.filled = false;
        halo.radius = .04f;
        halo.minPixels = 10.0f;
        halo.color = WithAlpha(color, 150);
        scene.markers.push_back(halo);
    }
    if (mask & kLabels) {
        Label label{};
        label.anchor = ray.target;
        label.color = color;
        label.offsetX = .9f;
        label.offsetY = -.5f;
        const char* role = ray.role == RayRole::Psi ? "PSI " : !rightHand ? "L " : ray.role == RayRole::Weapon ? "" : "NO WEAPON ";
        label.text = std::string(role) + (ray.hit ? Fixed(ray.distance, 2) + " m" : "no hit " + Fixed(ray.distance, 0) + " m");
        label.minPixels = 18.0f;
        label.maxPixels = 30.0f;
        scene.labels.push_back(label);
    }
}

void AddSlot(Scene& scene, const Slot& slot, unsigned mask)
{
    if (!slot.valid || !Finite(slot.centre)) { return; }
    const Color color = SlotColor(slot);
    const float distance = SlotHandDistance(slot);
    const bool inside = distance <= slot.radius;
    Sphere sphere{};
    sphere.centre = slot.centre;
    sphere.radius = slot.radius;
    sphere.outline = color;
    sphere.fill = WithAlpha(slot.stored ? kStored : color, slot.owned || NoticeLive(slot) ? 70 : inside ? 55 : 24);
    scene.spheres.push_back(sphere);
    if (slot.stored) {
        Marker stored{};
        stored.centre = slot.centre;
        stored.radius = .025f;
        stored.color = kStored;
        scene.markers.push_back(stored);
    }
    // The hand that slot measures, and a tether once it is within reach.
    if (Finite(slot.hand)) {
        Marker hand{};
        hand.centre = slot.hand;
        hand.radius = .012f;
        hand.color = inside ? color : kIdle;
        hand.minPixels = 4.0f;
        scene.markers.push_back(hand);
        if (distance < slot.radius * 3) {
            Line tether{slot.hand, slot.centre, WithAlpha(color, 200), .002f, 1.5f};
            tether.dash = inside ? 0.0f : .015f;
            scene.lines.push_back(tether);
        }
    }
    if (mask & kLabels) {
        Label label{};
        label.anchor = Add(slot.centre, Vec3{0, slot.radius + .02f, 0});
        label.color = color;
        label.centred = true;
        label.offsetY = -2.3f;
        // What the slot will do next, in the words of its own gesture.
        const bool medkit = slot.name == "MEDKIT";
        std::string state;
        if (NoticeLive(slot)) { state = NoticeText(slot.notice); }
        else if (slot.owned) { state = medkit ? (slot.triggerArmed ? "TRIGGER: USE" : "RELEASE TRIGGER") : "HELD"; }
        else if (inside) {
            state = slot.gripPressed && !slot.armed ? "RELEASE GRIP"
                    : medkit ? "SQUEEZE: SELECT" : slot.stored ? "SQUEEZE: DRAW/STOW" : "SQUEEZE: STORE";
        }
        else { state = medkit ? "" : slot.stored ? "STORED" : "EMPTY"; }
        label.text = slot.name + (state.empty() ? "" : "  " + state) + "\n" + Fixed(distance * 100, 0) + " cm";
        label.minPixels = 18.0f;
        label.maxPixels = 32.0f;
        scene.labels.push_back(label);
    }
}

void AddWrist(Scene& scene, const Wrist& wrist, unsigned mask)
{
    if (!wrist.enabled || !Finite(wrist.card.position)) { return; }
    const Color color = wrist.visible ? kActive : wrist.gate ? kWarn : kMuted;
    const Quaternion q = Normalize(wrist.card.orientation);
    const Vec3 x = Rotate(q, {wrist.width * .5f, 0, 0});
    const Vec3 y = Rotate(q, {0, wrist.height * .5f, 0});
    const Vec3 c = wrist.card.position;
    const Vec3 corners[4] = {Add(c, Add(x, y)), Add(c, Sub(y, x)), Sub(c, Add(x, y)), Add(c, Sub(x, y))};
    for (int i = 0; i < 4; ++i) {
        Line edge{corners[i], corners[(i + 1) % 4], color, .002f, 1.8f};
        edge.dash = wrist.gate ? 0.0f : .01f;
        scene.lines.push_back(edge);
    }
    // The card's visible face, the axis the facing test measures.
    scene.lines.push_back({c, Add(c, Rotate(q, {0, 0, .06f})), color, .002f, 1.8f});
    if (mask & kLabels) {
        Label label{};
        label.anchor = Add(c, Scale(y, 1.3f));
        label.color = color;
        label.centred = true;
        label.offsetY = -2.3f;
        label.height = .012f;
        label.minPixels = 16.0f;
        label.maxPixels = 28.0f;
        label.text = std::string("WRIST ") + (wrist.visible ? "VISIBLE" : wrist.gate ? "HIDDEN" : "GATED") +
                     "\nface " + Fixed(wrist.facing, 2) + "/" + Fixed(wrist.facingNeeded, 2) +
                     " view " + Fixed(wrist.viewing, 2) + "/" + Fixed(wrist.viewingNeeded, 2);
        scene.labels.push_back(label);
    }
}

void AddForegrip(Scene& scene, const Foregrip& grip, unsigned mask)
{
    if (!grip.valid || !Finite(grip.start) || !Finite(grip.end)) { return; }
    const Color color = grip.held ? kActive : grip.inRegion ? kNear : WithAlpha(kLeft, 170);
    const Vec3 axis = Sub(grip.end, grip.start);
    const float length = Length(axis);
    const Vec3 unit = length > 1e-5f ? Scale(axis, 1 / length) : Vec3{0, 0, -1};
    const Vec3 u = Perpendicular(unit), v = Cross(unit, u);
    AppendCircle(scene, grip.start, u, v, grip.radius, color, .0015f, 28);
    AppendCircle(scene, grip.end, u, v, grip.radius, color, .0015f, 28);
    for (int i = 0; i < 4; ++i) {
        const float angle = 1.5707963f * static_cast<float>(i);
        const Vec3 offset = Add(Scale(u, grip.radius * std::cos(angle)), Scale(v, grip.radius * std::sin(angle)));
        scene.lines.push_back({Add(grip.start, offset), Add(grip.end, offset), color, .0015f, 1.2f});
    }
    if (grip.held && Finite(grip.socket)) {
        Marker socket{};
        socket.centre = grip.socket;
        socket.radius = .015f;
        socket.color = kActive;
        scene.markers.push_back(socket);
    }
    if (mask & kLabels) {
        Label label{};
        label.anchor = grip.end;
        label.color = color;
        label.offsetX = .8f;
        label.height = .014f;
        label.minPixels = 16.0f;
        label.maxPixels = 28.0f;
        label.text = std::string("FOREGRIP ") + (grip.held ? "HELD" : grip.inRegion ? "SQUEEZE" : "READY");
        scene.labels.push_back(label);
    }
}

}  // namespace

float SlotHandDistance(const Slot& slot)
{
    const float d = Length(Sub(slot.hand, slot.centre));
    return std::isfinite(d) ? d : 1e9f;
}

Color SlotColor(const Slot& slot)
{
    if (NoticeLive(slot)) {
        switch (slot.notice) {
        case Notice::Success: case Notice::Ready: return kActive;
        case Notice::Empty: return kWarn;
        case Notice::Denied: return kDenied;
        default: break;
        }
    }
    if (slot.owned) { return kActive; }
    if (SlotHandDistance(slot) <= slot.radius) {
        // Inside with grip already held: the gesture refuses on purpose (no
        // activation by drifting in), which is worth showing, not hiding.
        return slot.gripPressed && !slot.armed ? kWarn : kNear;
    }
    return slot.stored ? kStored : kIdle;
}

void BuildOverlayScene(const OverlayFrame& frame, unsigned mask, Scene& scene)
{
    scene.Clear();
    if (mask & kRays) {
        AddRay(scene, frame.rays[1], kRight, mask, true);
        AddRay(scene, frame.rays[0], frame.rays[0].role == RayRole::Psi ? kPsi : kLeft, mask, false);
    }
    if (mask & kRaw) {
        for (int h = 0; h < 2; ++h) {
            const auto& c = frame.hands[h];
            if (!c.valid) { continue; }
            AppendAxes(scene, c.grip, .06f, .002f);
            // The left ray already is this aim ray; drawing it twice only hides it.
            if (h == 0 && (mask & kRays) && frame.rays[0].valid) { continue; }
            const Vec3 forward = Rotate(Normalize(c.aim.orientation), {0, 0, -1});
            Line aim{c.aim.position, Add(c.aim.position, Scale(forward, 2.0f)), WithAlpha(kIdle, 150), .0015f, 1.2f};
            aim.dash = .02f;
            scene.lines.push_back(aim);
        }
    }
    if (mask & kBody) {
        if (frame.holstersEnabled) {
            for (const auto& slot : frame.holsters) { AddSlot(scene, slot, mask); }
        }
        if (frame.medkitEnabled) { AddSlot(scene, frame.medkit, mask); }
    }
    if (mask & kWrist) { AddWrist(scene, frame.wrist, mask); }
    if (mask & kForegrip) { AddForegrip(scene, frame.foregrip, mask); }
    if ((mask & kStatus) && frame.headValid) {
        Label status{};
        // About 25 degrees below the gaze: out of the aiming area, a glance down to read.
        status.anchor = Compose(frame.head, Pose{{}, {0, -.50f, -1.05f}}).position;
        status.text = OverlayStatusText(frame, mask);
        status.color = Color{235, 235, 225, 235};
        status.centred = true;
        status.height = .021f;
        status.minPixels = 17.0f;
        status.maxPixels = 30.0f;
        // First, so it holds its place and the other labels step around it.
        scene.labels.insert(scene.labels.begin(), status);
    }
}

std::string OverlayStatusText(const OverlayFrame& f, unsigned mask)
{
    auto ray = [](const HandRay& r) -> std::string {
        if (!r.valid) { return "-"; }
        return (r.hit ? "hit " + Fixed(r.distance, 2) + " m" : "no hit (" + Fixed(r.distance, 0) + " m)");
    };
    auto slot = [](const Slot& s) -> std::string {
        if (!s.valid) { return s.name + " -"; }
        const float d = SlotHandDistance(s);
        std::string state = s.owned ? "held" : d <= s.radius ? "IN" : "out";
        if (s.stored) { state += "+stored"; }
        if (NoticeLive(s)) { state += std::string(" ") + NoticeText(s.notice); }
        return s.name + " " + state + " " + Fixed(d * 100, 0) + "cm";
    };
    char head[64];
    std::snprintf(head, sizeof(head), "PREYVR DEBUG  dbg.draw %u", mask);
    std::string text = head;
    text += "\nR  " + ray(f.rays[1]) + (f.rays[1].valid && f.rays[1].role != RayRole::Weapon ? " (no weapon)" : "");
    text += "   L " + std::string(f.rays[0].role == RayRole::Psi ? "psi " : "") + ray(f.rays[0]);
    const char* psi[] = {"native", "head", "left"};
    text += "   psi " + std::string(psi[std::min(f.psiMode, 2u)]);
    if (f.sceneQueryFault) { text += "  QUERY FAULT"; }
    text += "\nHolsters " + std::string(f.holstersEnabled ? "on  " + slot(f.holsters[0]) + "  " + slot(f.holsters[1]) : "off");
    text += "\nMedkit " + std::string(f.medkitEnabled ? "on  " + slot(f.medkit) +
                                                            (f.medkit.owned ? (f.medkit.triggerArmed ? " trig:armed" : " trig:release") : "")
                                                      : "off");
    text += "\nWrist " + std::string(!f.wrist.enabled ? "off" : f.wrist.visible ? "VISIBLE" : f.wrist.gate ? "hidden" : "gated") +
            (f.wrist.enabled ? "  face " + Fixed(f.wrist.facing, 2) + " view " + Fixed(f.wrist.viewing, 2) +
                                   " d " + Fixed(f.wrist.distance, 2)
                             : "");
    text += "   Foregrip " + std::string(!f.foregrip.valid ? "-" : f.foregrip.held ? "HELD" : f.foregrip.inRegion ? "in" : "ready");
    if (!f.extra.empty()) { text += "\n" + f.extra; }
    return text;
}

}  // namespace preyvr::debugdraw
