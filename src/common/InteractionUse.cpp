#include "preyvr/InteractionUse.h"

#include <algorithm>
#include <cmath>

namespace preyvr::use {
namespace {

constexpr float kPi = 3.14159265358979f;

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

float Alpha(float cutoff, float dt)
{
    const float tau = 1.0f / (2.0f * kPi * std::max(cutoff, 1e-3f));
    return 1.0f / (1.0f + tau / dt);
}

}  // namespace

const char* OwnerName(Owner owner)
{
    switch (owner) {
    case Owner::Use: return "use";
    case Owner::Reload: return "reload";
    case Owner::Legacy: return "legacy";
    case Owner::Hold: return "hold";
    case Owner::Draw: return "draw";
    default: return "none";
    }
}

void UseButton::Reset()
{
    *this = UseButton{};
}

ButtonOutput UseButton::Update(const ButtonInput& in)
{
    ButtonOutput out{};
    // A hand acts only on a fresh press: a grip still held from a menu, a
    // medkit or a foregrip does nothing until it has been let go once.
    const bool leftEdge = in.leftGrip && leftArmed_;
    const bool rightEdge = in.rightGrip && rightArmed_;
    if (!in.leftGrip) { leftArmed_ = true; }
    if (!in.rightGrip) { rightArmed_ = true; }
    auto release = [&] {
        if (down_) { out.released = true; }
        down_ = false;
        owner_ = Owner::None;
    };

    if (!in.gameplay) {
        if (owner_ == Owner::Hold && in.carrying) {
            // A menu over a held object: it stays held (the game keeps carrying
            // it), and the grip is judged again when play resumes.
            if (in.rightGrip) { rightArmed_ = false; }
            out.owner = owner_;
            return out;
        }
        // A menu opening, a death, the lane switching off: let go now, and do
        // not act on a grip that was already down when gameplay returns.
        release();
        clear_ = waited_ = guard_ = 0;
        if (in.leftGrip) { leftArmed_ = false; }
        if (in.rightGrip) { rightArmed_ = false; }
        out.owner = owner_;
        return out;
    }

    if (!in.leftHand) {
        // The original layout: the right grip is the native use/reload button.
        if (owner_ == Owner::Legacy) {
            if (!in.rightGrip || in.rightBusy) {
                release();
                if (in.rightGrip) { rightArmed_ = false; }
            }
        } else if (owner_ != Owner::None) {
            release();   // switched layout mid-press
        } else if (rightEdge) {
            rightArmed_ = false;
            if (in.rightBusy) { out.busy = true; }
            else { owner_ = Owner::Legacy; down_ = true; out.pressed = true; }
        }
        if (in.leftGrip) { leftArmed_ = false; }
        out.down = down_;
        out.owner = owner_;
        return out;
    }

    switch (owner_) {
    case Owner::None:
        if (guard_ > 0) { --guard_; out.suppressTarget = true; }
        if (leftEdge && !rightEdge && in.carrying && in.holdToHold) {
            // Carrying something no press of ours picked up (it started after
            // the grip opened, or before this layout): the next squeeze lets go.
            leftArmed_ = false;
            out.releaseCarry = true;
        } else if (leftEdge && rightEdge) {
            // Both grips in the same frame: a recentre or a two-hand hold.
            leftArmed_ = rightArmed_ = false;
            out.busy = true;
        } else if (leftEdge) {
            leftArmed_ = false;
            if (in.leftBusy || in.rightGrip) { out.busy = true; }
            else if (in.target && guard_ == 0) {
                owner_ = Owner::Use;
                down_ = true;
                out.pressed = true;
                pressedTarget_ = in.targetId;
                tap_ = in.holdToHold && in.tapOnly ? kTapFrames : 0;
            }
            else { out.noTarget = true; }
        } else if (rightEdge) {
            rightArmed_ = false;
            if (in.rightBusy || in.leftGrip) { out.busy = true; }
            else if (in.holstered && !in.carrying) { owner_ = Owner::Draw; out.drawWeapon = true; }
            else { owner_ = Owner::Reload; clear_ = 0; waited_ = 0; out.suppressTarget = true; }
        }
        break;
    case Owner::Use:
        if (in.carrying && in.holdToHold) {
            // The press picked something up, and now the grip holds it. The
            // button goes up: the game acts on its next PRESS (a drop), never
            // on this release, and nothing else needs it down any more.
            if (down_) { out.released = true; }
            down_ = false;
            owner_ = Owner::Hold;
            if (!in.leftGrip) {
                // Picked up and let go within the same frame.
                owner_ = Owner::None;
                out.releaseCarry = true;
            }
            if (in.rightGrip) { rightArmed_ = false; }
            break;
        }
        if (tap_ > 0 && --tap_ == 0 && down_) {
            // A pickup's tap: up again, the grip may stay closed.
            down_ = false;
            out.released = true;
        }
        if (in.holdToHold && down_ && pressedTarget_ && in.targetId != pressedTarget_) {
            // Taken (an item into the inventory): the press is spent. The grip
            // may stay closed; the button does not, so no hoover follows.
            down_ = false;
            out.released = true;
        }
        // Held for as long as the grip is: a hold-to-carry object or a body
        // needs the button down after the press, even once the target changes.
        if (!in.leftGrip || in.leftBusy) {
            release();
            if (in.leftGrip) { leftArmed_ = false; }
        }
        if (in.rightGrip) { rightArmed_ = false; }
        break;
    case Owner::Reload:
        out.suppressTarget = true;
        if (in.leftGrip) { leftArmed_ = false; }
        if (!down_) {
            if (!in.rightGrip || in.rightBusy) {
                owner_ = Owner::None;   // let go before the selection cleared: nothing sent
                break;
            }
            clear_ = in.target ? 0 : clear_ + 1;
            if (clear_ >= kClearFrames) {
                down_ = true;
                out.pressed = true;
            } else if (++waited_ > kGiveUpFrames) {
                // Something keeps a target selected (a scripted forced pick).
                // Pressing now would USE it, so send nothing.
                owner_ = Owner::None;
                out.gaveUp = true;
            }
        } else if (!in.rightGrip || in.rightBusy) {
            release();
            guard_ = kGuardFrames;
            if (in.rightGrip) { rightArmed_ = false; }
        }
        break;
    case Owner::Legacy:
        release();
        break;
    case Owner::Draw:
        // The X button never goes down: holding on would holster again.
        if (!in.rightGrip) { owner_ = Owner::None; }
        if (in.leftGrip) { leftArmed_ = false; }
        break;
    case Owner::Hold:
        if (!in.carrying) {
            // The carry ended without us: thrown with the trigger, broken,
            // knocked out of the hand. A grip still closed acts only once reopened.
            owner_ = Owner::None;
            if (in.leftGrip) { leftArmed_ = false; }
        } else if (!in.leftGrip) {
            owner_ = Owner::None;
            out.releaseCarry = true;
        }
        if (in.rightGrip) { rightArmed_ = false; }
        break;
    }
    if (owner_ == Owner::Reload) { out.suppressTarget = true; }
    out.down = down_;
    out.owner = owner_;
    return out;
}

float OneEuro::Update(float value, float dt, float minCutoff, float beta, float derivativeCutoff)
{
    if (!primed_ || !(dt > 0) || !std::isfinite(value)) {
        primed_ = std::isfinite(value);
        value_ = value;
        derivative_ = 0;
        return value;
    }
    const float rawDerivative = (value - value_) / dt;
    derivative_ += Alpha(derivativeCutoff, dt) * (rawDerivative - derivative_);
    const float cutoff = minCutoff + beta * std::fabs(derivative_);
    value_ += Alpha(cutoff, dt) * (value - value_);
    return value_;
}

void RayFilter::Reset()
{
    primed_ = false;
    for (auto& f : origin_) { f.Reset(); }
    for (auto& f : direction_) { f.Reset(); }
}

Ray RayFilter::Update(const Ray& raw, float dt, const FilterSettings& settings)
{
    if (!Finite(raw.origin) || !Finite(raw.direction) || !(Length(raw.direction) > .5f)) {
        Reset();
        return raw;
    }
    if (!settings.enabled) {
        Reset();
        return raw;
    }
    const bool jump = primed_ && (AngleDegrees(raw.direction, last_.direction) > settings.resetDegrees ||
                                  Length(Sub(raw.origin, last_.origin)) > settings.resetMetres);
    if (!primed_ || jump || !(dt > 0) || dt > .25f) {
        Reset();
        primed_ = true;
        for (int i = 0; i < 3; ++i) {
            origin_[i].Update((&raw.origin.x)[i], 0, 0, 0, 0);
            direction_[i].Update((&raw.direction.x)[i], 0, 0, 0, 0);
        }
        last_ = raw;
        held_ = raw;
        return raw;
    }
    Ray out{};
    for (int i = 0; i < 3; ++i) {
        (&out.origin.x)[i] = origin_[i].Update((&raw.origin.x)[i], dt, settings.positionMinCutoff,
                                               settings.positionBeta, settings.derivativeCutoff);
        (&out.direction.x)[i] = direction_[i].Update((&raw.direction.x)[i], dt, settings.minCutoff, settings.beta,
                                                     settings.derivativeCutoff);
    }
    const float length = Length(out.direction);
    if (!(length > 1e-4f)) {
        Reset();
        return raw;
    }
    out.direction = Scale(out.direction, 1.0f / length);
    // Compared against the raw sample: a filter that drifted far from the
    // hand (it cannot, but a NaN upstream could) restarts next frame.
    last_ = raw;
    // Backlash: hold until the smoothed ray leaves the dead band, then drag
    // the held ray along at the band's edge.
    const float off = AngleDegrees(out.direction, held_.direction);
    if (off > settings.backlashDegrees) {
        const float t = (off - settings.backlashDegrees) / off;   // fraction of the way to follow
        Vec3 d = Add(held_.direction, Scale(Sub(out.direction, held_.direction), t));
        const float dl = Length(d);
        if (dl > 1e-4f) { held_.direction = Scale(d, 1.0f / dl); }
    }
    const Vec3 moved = Sub(out.origin, held_.origin);
    const float distance = Length(moved);
    if (distance > settings.backlashMetres) {
        held_.origin = Add(held_.origin, Scale(moved, (distance - settings.backlashMetres) / distance));
    }
    return held_;
}

float AngleDegrees(Vec3 a, Vec3 b)
{
    const float la = Length(a), lb = Length(b);
    if (!(la > 0) || !(lb > 0)) { return 180.0f; }
    // atan2 of |a x b| and a.b stays exact at small angles, where acos does not.
    const Vec3 c{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return std::atan2(Length(c), Dot(a, b)) * 180.0f / kPi;
}

std::optional<float> RayBoxEntry(Vec3 origin, Vec3 direction, Vec3 boxMin, Vec3 boxMax)
{
    float near = 0.0f, far = 1e30f;
    for (int i = 0; i < 3; ++i) {
        const float o = (&origin.x)[i], d = (&direction.x)[i];
        const float lo = (&boxMin.x)[i], hi = (&boxMax.x)[i];
        if (std::fabs(d) < 1e-9f) {
            if (o < lo || o > hi) { return std::nullopt; }
            continue;
        }
        float t0 = (lo - o) / d, t1 = (hi - o) / d;
        if (t0 > t1) { std::swap(t0, t1); }
        near = std::max(near, t0);
        far = std::min(far, t1);
        if (near > far) { return std::nullopt; }
    }
    return near;
}

Vec3 PointerEnd(Vec3 origin, Vec3 direction, Vec3 boxMin, Vec3 boxMax, bool* onLine)
{
    if (const auto t = RayBoxEntry(origin, direction, boxMin, boxMax)) {
        if (onLine) { *onLine = true; }
        return Add(origin, Scale(direction, *t));
    }
    if (onLine) { *onLine = false; }
    const Vec3 centre = Scale(Add(boxMin, boxMax), .5f);
    const float t = std::max(0.0f, Dot(Sub(centre, origin), direction));
    const Vec3 along = Add(origin, Scale(direction, t));
    return {std::clamp(along.x, boxMin.x, boxMax.x), std::clamp(along.y, boxMin.y, boxMax.y),
            std::clamp(along.z, boxMin.z, boxMax.z)};
}

}  // namespace preyvr::use
