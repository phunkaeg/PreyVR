#include "preyvr/HackingInput.h"

#include "preyvr/Locomotion.h"
#include <algorithm>
#include <cmath>

namespace preyvr::hacking {
namespace {
bool Valid(const Settings& s)
{
    return std::isfinite(s.deadzone) && s.deadzone >= 0 && s.deadzone < 1 &&
        std::isfinite(s.changeEpsilon) && s.changeEpsilon >= 0 && s.changeEpsilon < 1 &&
        s.maxAgeNs > 0 && s.maxGapNs > 0;
}
bool SameContext(const Input& a, const Input& b)
{
    return a.epoch == b.epoch && a.reference == b.reference && a.generation == b.generation;
}
std::uint8_t Buttons(const Input& i)
{
    return static_cast<std::uint8_t>((i.a?ButtonA:0) | (i.b?ButtonB:0) | (i.x?ButtonX:0) | (i.y?ButtonY:0));
}
}

bool InputState::CleanupPending() const
{
    return primeMask_ != 0 || sentButtons_ != 0 || sentX_ != 0 || sentY_ != 0;
}
bool InputState::OwnsControls() const
{
    return sentButtons_ != 0 || sentX_ != 0 || sentY_ != 0;
}

Output InputState::Propose(float x, float y, std::uint8_t buttons)
{
    Output out{};
    out.x = x; out.y = y; out.ready = ready_;
    if (++serial_ == 0) { ++serial_; }
    out.proposal = serial_;
    if ((primeMask_ & AxisX) || (x == 0 && sentX_ != 0) || std::fabs(x-sentX_) > epsilon_) { out.axisMask |= AxisX; }
    if ((primeMask_ & AxisY) || (y == 0 && sentY_ != 0) || std::fabs(y-sentY_) > epsilon_) { out.axisMask |= AxisY; }
    out.pressMask = static_cast<std::uint8_t>(buttons & ~sentButtons_);
    out.releaseMask = static_cast<std::uint8_t>(sentButtons_ & ~buttons);
    pending_ = out;
    return out;
}

Output InputState::Suspend()
{
    ready_ = havePrevious_ = false;
    previous_ = {};
    // Only release channels actually delivered. A failed initial zero does not
    // create native ownership; the next active context primes both axes anew.
    primeMask_ = 0;
    return Propose(0,0,0);
}

Output InputState::Update(const Input& i, const Settings& s)
{
    if (!Valid(s) || !i.valid || !i.active || !std::isfinite(i.stickX) || !std::isfinite(i.stickY) ||
        i.sequence == 0 || i.stampNs == 0 || i.nowNs < i.stampNs || i.nowNs-i.stampNs > s.maxAgeNs) {
        return Suspend();
    }
    epsilon_ = s.changeEpsilon;
    if (havePrevious_ && !SameContext(i,previous_)) {
        ready_ = false; primeMask_ = AxisX|AxisY; havePrevious_ = false;
    }
    if (havePrevious_) {
        if (i.nowNs < previous_.nowNs || i.sequence < previous_.sequence || i.stampNs < previous_.stampNs ||
            (i.sequence == previous_.sequence && i.stampNs != previous_.stampNs) ||
            (i.sequence != previous_.sequence && i.stampNs == previous_.stampNs)) {
            return Suspend();
        }
        if (i.sequence == previous_.sequence &&
            (i.stickX != previous_.stickX || i.stickY != previous_.stickY || Buttons(i) != Buttons(previous_))) {
            return Suspend(); // duplicate publications cannot mutate their controls
        }
        if (i.stampNs-previous_.stampNs > s.maxGapNs) {
            ready_ = false; primeMask_ = AxisX|AxisY; havePrevious_ = false;
        }
    }
    if (!havePrevious_) { ready_ = false; primeMask_ = AxisX|AxisY; }
    const float rawX = std::clamp(i.stickX,-1.0f,1.0f), rawY = std::clamp(i.stickY,-1.0f,1.0f);
    const auto buttons = Buttons(i);
    const bool neutral = buttons == 0 && std::hypot(rawX,rawY) <= s.deadzone;
    // Cleanup may be partially acknowledged. A neutral source is not permission
    // to press into a new movie while old native controls are still held.
    if (!ready_ && !CleanupPending() && neutral) { ready_ = true; }
    previous_ = i; havePrevious_ = true;
    if (!ready_) { return Propose(0,0,0); }
    float x = 0, y = 0;
    if (!locomotion::ShapeStick(rawX,rawY,s.deadzone,&x,&y)) { return Suspend(); }
    return Propose(x,y,buttons);
}

void InputState::Commit(const Output& accepted)
{
    if (accepted.proposal == 0 || accepted.proposal != pending_.proposal) { return; }
    const auto axes = static_cast<std::uint8_t>(accepted.axisMask & pending_.axisMask);
    const auto presses = static_cast<std::uint8_t>(accepted.pressMask & pending_.pressMask);
    const auto releases = static_cast<std::uint8_t>(accepted.releaseMask & pending_.releaseMask);
    if (axes & AxisX) { sentX_ = pending_.x; primeMask_ &= static_cast<std::uint8_t>(~AxisX); }
    if (axes & AxisY) { sentY_ = pending_.y; primeMask_ &= static_cast<std::uint8_t>(~AxisY); }
    sentButtons_ = static_cast<std::uint8_t>((sentButtons_ | presses) & ~releases);
    pending_.axisMask &= static_cast<std::uint8_t>(~axes);
    pending_.pressMask &= static_cast<std::uint8_t>(~presses);
    pending_.releaseMask &= static_cast<std::uint8_t>(~releases);
}

} // namespace preyvr::hacking
