#pragma once

#include "preyvr/InputEvent.h"

// Turning controller state into menu actions.
//
// **This is the binding, and it is the product feature.** The end state is "no
// mouse": a controller has to move a highlight and confirm a choice, or the mod
// stops at the main menu. `InputPost` can already deliver a menu tap; nothing
// produced one from a controller, so the two halves were not connected.
//
// Two behaviours here exist because their absence is what makes controller menus
// feel broken:
//
// * **A stick is not a d-pad.** Fed straight through, one flick of a stick that
//   reports 90 times a second scrolls an entire list before the player's thumb
//   comes back. So a deflection produces one action, then nothing until an
//   initial delay has passed, and only then repeats at a slower rate -- the
//   shape every menu with a stick has used for thirty years.
// * **Hysteresis.** A stick resting near the threshold would otherwise chatter
//   between "deflected" and "centred" on sensor noise, emitting a stream of
//   actions the player did not ask for. Release is at a lower magnitude than
//   engage.
//
// Pure and time-driven: it is handed a delta rather than reading a clock, so the
// repeat behaviour is testable without waiting for real seconds to pass.
namespace preyvr::input {

struct ControllerMenuState {
    float stickX = 0.0f;
    float stickY = 0.0f;
    bool accept = false;
    bool cancel = false;
    bool start = false;
};

struct MenuNavigatorPolicy {
    float engage = 0.6f;          // deflection that starts a direction
    float release = 0.35f;        // and the lower magnitude that ends it
    float initialDelay = 0.45f;   // seconds held before the first repeat
    float repeatInterval = 0.16f; // and between repeats after that
};

class MenuNavigator {
public:
    MenuNavigator() = default;
    explicit MenuNavigator(MenuNavigatorPolicy policy) : policy_(policy) {}

    // Writes up to `capacity` actions and returns how many were produced.
    // `deltaSeconds` is the time since the previous call.
    //
    // Buttons fire on the **rising edge only**: a held button is one action, not
    // one per frame. A menu that advanced while a button stayed down would skip
    // past whatever the player was trying to select.
    unsigned int Update(const ControllerMenuState& state, float deltaSeconds,
                        MenuAction* out, unsigned int capacity);

    void Reset();

private:
    // Which way a stick is currently considered pushed, if any.
    enum class Direction { None, Up, Down, Left, Right };

    static Direction DirectionOf(float x, float y, float threshold);

    MenuNavigatorPolicy policy_{};
    Direction held_ = Direction::None;
    float heldFor_ = 0.0f;
    float untilRepeat_ = 0.0f;
    bool acceptWas_ = false;
    bool cancelWas_ = false;
    bool startWas_ = false;
};

struct ModalMenuState : ControllerMenuState {
    bool previousTab = false, nextTab = false, secondary = false, tertiary = false;
    bool previousPage = false, nextPage = false;
};

// Start can open a menu from gameplay. Navigation and inventory actions belong
// only to a visible modal; controls held during entry must first return neutral.
// Owned exclusively by the XR input service, including Reset.
class ModalMenuRouter {
public:
    unsigned int Update(const ModalMenuState& state, bool modal, float seconds,
                        MenuAction* out, unsigned int capacity);
    void Reset();
private:
    MenuNavigator navigator_;
    bool modal_ = false, startWas_ = false, blockStick_ = false;
    bool blocked_[8]{}, was_[6]{};
    bool tabPending_[2]{}, tabChord_=false;
};

} // namespace preyvr::input
