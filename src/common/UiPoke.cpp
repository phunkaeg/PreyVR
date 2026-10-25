#include "preyvr/UiPoke.h"

#include "preyvr/StereoCamera.h"
#include <algorithm>
#include <cmath>

namespace preyvr::ui {
namespace {
bool Finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
float Length(Vec3 p) { return std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z); }
bool Valid(const Surface& s)
{
    return IsPoseUsable(s.panel.pose, {true,true,true,true,0}, 1) &&
        std::isfinite(s.panel.width) && std::isfinite(s.panel.height) &&
        s.panel.width > 0 && s.panel.height > 0 &&
        std::isfinite(s.angle) && s.angle >= 0 && s.angle <= 1.2f;
}
bool Valid(const PokeSettings& s)
{
    return std::isfinite(s.frontHover) && std::isfinite(s.armDistance) &&
        std::isfinite(s.releaseDistance) && std::isfinite(s.pressDistance) &&
        std::isfinite(s.maxPenetration) && std::isfinite(s.maxFingerJump) &&
        std::isfinite(s.maxSurfaceMove) && std::isfinite(s.maxSurfaceTurn) &&
        s.frontHover >= s.armDistance && s.armDistance >= s.releaseDistance &&
        s.releaseDistance > 0 && s.pressDistance <= 0 &&
        s.maxPenetration > -s.pressDistance && s.maxFingerJump > 0 &&
        s.maxSurfaceMove > 0 && s.maxSurfaceTurn > 0 &&
        s.maxAgeNs > 0 && s.maxGapNs > 0;
}
float Turn(const Quaternion& a, const Quaternion& b)
{
    const Quaternion x = Normalize(a), y = Normalize(b);
    const float dot = std::fabs(x.x*y.x+x.y*y.y+x.z*y.z+x.w*y.w);
    return 2.0f*std::acos(std::clamp(dot, 0.0f, 1.0f));
}
bool SurfaceChanged(const Surface& a, const Surface& b, const PokeSettings& s)
{
    // Presentation changes invalidate the capture rather than scaling an old
    // click into a new widget. Sub-threshold pose motion is handled again by
    // the physical crossing test below.
    return std::fabs(a.panel.width-b.panel.width) > .0001f ||
        std::fabs(a.panel.height-b.panel.height) > .0001f ||
        std::fabs(a.angle-b.angle) > .0001f ||
        Length(Sub(a.panel.pose.position,b.panel.pose.position)) > s.maxSurfaceMove ||
        Turn(a.panel.pose.orientation,b.panel.pose.orientation) > s.maxSurfaceTurn;
}
bool SameContext(const PokeInput& a, const PokeInput& b)
{
    return a.epoch == b.epoch && a.reference == b.reference && a.generation == b.generation;
}
}

std::optional<PokePoint> ProjectPoke(const Surface& s, Vec3 fingertip)
{
    if (!Valid(s) || !Finite(fingertip)) { return {}; }
    const Vec3 local = Rotate(stereo::Conjugate(s.panel.pose.orientation),
                              Sub(fingertip,s.panel.pose.position));
    if (!Finite(local)) { return {}; }
    float u = 0, front = local.z;
    Vec3 normal{0,0,1};
    if (s.angle == 0) {
        u = .5f + local.x/s.panel.width;
    } else {
        // Work relative to the visible centre in double precision. Subtracting
        // two large float radii loses the sign for nearly-flat cylinders.
        const double r = static_cast<double>(s.panel.width)/s.angle;
        const double x = local.x, z = local.z;
        const double rho = std::hypot(x,r-z);
        if (!std::isfinite(rho) || rho < 1e-8) { return {}; }
        const double theta = std::atan2(x,r-z);
        u = static_cast<float>(.5 + theta/s.angle);
        front = static_cast<float>((2*r*z-z*z-x*x)/(r+rho));
        normal = {static_cast<float>(-std::sin(theta)),0,static_cast<float>(std::cos(theta))};
    }
    const float v = .5f - local.y/s.panel.height;
    const Vec3 point = SurfacePoint(s,u,v).position;
    normal = Rotate(s.panel.pose.orientation,normal);
    if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(front) || !Finite(point) || !Finite(normal)) { return {}; }
    return PokePoint{u,v,front,point,normal,u>=0&&u<=1&&v>=0&&v<=1};
}

void Poke::Reset()
{
    previous_ = {}; output_ = {};
    havePrevious_ = armed_ = down_ = false;
}

PokeOutput Poke::Update(const PokeInput& i, const PokeSettings& s)
{
    const auto contact = ProjectPoke(i.surface,i.tip);
    if (!Valid(s) || !i.valid || !contact || i.stampNs == 0 || i.sequence == 0 ||
        i.nowNs < i.stampNs || i.nowNs-i.stampNs > s.maxAgeNs) {
        Reset(); return output_;
    }
    if (havePrevious_ && !SameContext(i,previous_)) { Reset(); }
    if (havePrevious_ && i.nowNs < previous_.nowNs) { Reset(); return output_; }
    if (havePrevious_ && i.sequence == previous_.sequence && i.stampNs == previous_.stampNs) {
        if (Length(Sub(i.tip,previous_.tip)) < 1e-6f && !SurfaceChanged(i.surface,previous_.surface,s)) {
            previous_.nowNs = i.nowNs;
            return output_;
        }
        Reset(); return output_; // a changed publication cannot reuse an old sequence
    }
    if (havePrevious_ && (i.sequence <= previous_.sequence || i.stampNs <= previous_.stampNs)) {
        Reset(); return output_;
    }
    if (havePrevious_ && (i.stampNs-previous_.stampNs > s.maxGapNs ||
        Length(Sub(i.tip,previous_.tip)) > s.maxFingerJump || SurfaceChanged(i.surface,previous_.surface,s))) {
        Reset();
    }

    const bool inside = contact->inside;
    const bool near = inside && contact->frontDistance <= s.frontHover &&
        contact->frontDistance >= -s.maxPenetration;
    if (!near) { armed_ = down_ = false; }
    else if (!havePrevious_) { armed_ = contact->frontDistance >= s.armDistance; }
    else if (down_) {
        if (contact->frontDistance >= s.releaseDistance) {
            down_ = false;
            armed_ = contact->frontDistance >= s.armDistance;
        }
    } else {
        if (contact->frontDistance >= s.armDistance) { armed_ = true; }
        if (armed_ && contact->frontDistance <= s.pressDistance) {
            const auto old = ProjectPoke(previous_.surface,previous_.tip);
            const auto oldOnCurrent = ProjectPoke(i.surface,previous_.tip);
            // Both surfaces must say the old finger was in front. A panel
            // moving over a stationary fingertip cannot create this crossing.
            down_ = old && oldOnCurrent && old->inside && oldOnCurrent->inside &&
                old->frontDistance > s.pressDistance && oldOnCurrent->frontDistance > s.pressDistance &&
                Length(Sub(i.tip,previous_.tip)) >= .001f;
            armed_ = false;
        }
    }
    output_ = {near,inside,down_,contact};
    previous_ = i; havePrevious_ = true;
    return output_;
}

} // namespace preyvr::ui
