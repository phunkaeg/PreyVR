#include "preyvr/MenuNavigator.h"

#include <cmath>

namespace preyvr::input {

// A diagonal resolves to the dominant axis rather than firing both: a slightly
// off-axis flick would otherwise move the highlight down *and* across and land
// somewhere the player did not aim.
MenuNavigator::Direction MenuNavigator::DirectionOf(float x, float y, float threshold)
{
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return Direction::None;
    }
    const float absX = std::fabs(x);
    const float absY = std::fabs(y);
    if (absX < threshold && absY < threshold) {
        return Direction::None;
    }
    if (absX >= absY) {
        return x > 0.0f ? Direction::Right : Direction::Left;
    }
    return y > 0.0f ? Direction::Up : Direction::Down;
}

unsigned int MenuNavigator::Update(const ControllerMenuState& state, float deltaSeconds,
                                   MenuAction* out, unsigned int capacity)
{
    if (out == nullptr || capacity == 0) {
        return 0;
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) {
        deltaSeconds = 0.0f;
    }
    unsigned int count = 0;

    const auto emit = [&](MenuAction action) {
        if (count < capacity) {
            out[count] = action;
            ++count;
        }
    };

    // --- stick, as a d-pad with repeat ---------------------------------------
    //
    // Engage and release use different thresholds. With one threshold a stick
    // resting near it chatters on noise and emits a stream of actions.
    const float threshold = held_ == Direction::None ? policy_.engage : policy_.release;
    const Direction now = DirectionOf(state.stickX, state.stickY, threshold);

    if (now != held_) {
        held_ = now;
        heldFor_ = 0.0f;
        untilRepeat_ = policy_.initialDelay;
        if (now != Direction::None) {
            switch (now) {
                case Direction::Up: emit(MenuAction::Up); break;
                case Direction::Down: emit(MenuAction::Down); break;
                case Direction::Left: emit(MenuAction::Left); break;
                case Direction::Right: emit(MenuAction::Right); break;
                case Direction::None: break;
            }
        }
    } else if (held_ != Direction::None) {
        heldFor_ += deltaSeconds;
        untilRepeat_ -= deltaSeconds;
        // A loop rather than a single fire: a long frame must not swallow a
        // repeat that was due inside it, or the highlight stutters exactly when
        // the game is struggling.
        while (untilRepeat_ <= 0.0f && count < capacity) {
            switch (held_) {
                case Direction::Up: emit(MenuAction::Up); break;
                case Direction::Down: emit(MenuAction::Down); break;
                case Direction::Left: emit(MenuAction::Left); break;
                case Direction::Right: emit(MenuAction::Right); break;
                case Direction::None: break;
            }
            untilRepeat_ += policy_.repeatInterval;
        }
    }

    // --- buttons, rising edge only -------------------------------------------
    if (state.accept && !acceptWas_) {
        emit(MenuAction::Accept);
    }
    if (state.cancel && !cancelWas_) {
        emit(MenuAction::Cancel);
    }
    if (state.start && !startWas_) {
        emit(MenuAction::Start);
    }
    acceptWas_ = state.accept;
    cancelWas_ = state.cancel;
    startWas_ = state.start;

    return count;
}

void MenuNavigator::Reset()
{
    held_ = Direction::None;
    heldFor_ = 0.0f;
    untilRepeat_ = 0.0f;
    // Buttons are cleared too. After a reset the next frame's held button is a
    // fresh press: treating it as still-held would swallow the player's first
    // input after a menu opens.
    acceptWas_ = false;
    cancelWas_ = false;
    startWas_ = false;
}

} // namespace preyvr::input
