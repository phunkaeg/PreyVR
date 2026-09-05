#include "preyvr/MenuNavigator.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace preyvr::input;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

unsigned int Step(MenuNavigator& nav, const ControllerMenuState& state, float dt,
                  MenuAction* out, unsigned int capacity = 8)
{
    return nav.Update(state, dt, out, capacity);
}

// **The bug this prevents.** A stick reporting 90 times a second, fed straight
// through, scrolls an entire list before a thumb comes back to centre.
void TestOneFlickIsOneAction()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState pushed;
    pushed.stickY = 1.0f;

    Require(Step(nav, pushed, 0.011f, out) == 1, "a deflection produces one action");
    Require(out[0] == MenuAction::Up, "and it is the direction pushed");

    // Held, but not yet past the initial delay: silence.
    unsigned int extra = 0;
    for (int frame = 0; frame < 20; ++frame) {   // 20 * 11ms = 220ms < 450ms
        extra += Step(nav, pushed, 0.011f, out);
    }
    Require(extra == 0, "holding must not repeat before the initial delay");
}

void TestHoldingRepeatsAfterTheDelay()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState pushed;
    pushed.stickY = -1.0f;

    Require(Step(nav, pushed, 0.0f, out) == 1, "the first push acts immediately");
    Require(out[0] == MenuAction::Down, "downward stick means Down");

    // Past the initial delay, repeats begin.
    unsigned int repeats = Step(nav, pushed, 0.5f, out);
    Require(repeats >= 1, "a repeat must fire once the initial delay has passed");
    Require(out[0] == MenuAction::Down, "and repeat the held direction");

    // A long frame must not swallow repeats that came due inside it.
    const unsigned int burst = Step(nav, pushed, 0.5f, out);
    Require(burst >= 2, "a long frame must deliver every repeat that fell inside it");
}

void TestHysteresisStopsChatter()
{
    // A stick resting between the release and engage thresholds must hold its
    // state rather than flickering, which with one threshold emits a stream of
    // actions nobody asked for.
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState pushed;
    pushed.stickY = 1.0f;
    Require(Step(nav, pushed, 0.0f, out) == 1, "engage");

    ControllerMenuState between;
    between.stickY = 0.45f;   // below engage 0.6, above release 0.35
    unsigned int fired = 0;
    for (int frame = 0; frame < 10; ++frame) {
        fired += Step(nav, between, 0.001f, out);
    }
    Require(fired == 0, "sitting between the thresholds must not re-trigger");

    ControllerMenuState centred;
    Require(Step(nav, centred, 0.001f, out) == 0, "releasing emits nothing");
    Require(Step(nav, pushed, 0.001f, out) == 1, "and the next push acts again");
}

void TestDiagonalPicksOneAxis()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState diagonal;
    diagonal.stickX = 0.75f;
    diagonal.stickY = 0.70f;
    const unsigned int count = Step(nav, diagonal, 0.0f, out);
    Require(count == 1, "a diagonal must not fire two directions at once");
    Require(out[0] == MenuAction::Right, "the dominant axis wins");
}

void TestButtonsFireOnTheRisingEdgeOnly()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState held;
    held.accept = true;

    Require(Step(nav, held, 0.016f, out) == 1, "a press acts");
    Require(out[0] == MenuAction::Accept, "and is Accept");
    unsigned int extra = 0;
    for (int frame = 0; frame < 30; ++frame) {
        extra += Step(nav, held, 0.016f, out);
    }
    Require(extra == 0, "a held button must not act once per frame");

    ControllerMenuState released;
    Require(Step(nav, released, 0.016f, out) == 0, "releasing emits nothing");
    Require(Step(nav, held, 0.016f, out) == 1, "and pressing again acts once more");
}

void TestNonFiniteInputIsIgnored()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState bad;
    bad.stickX = std::nanf("");
    Require(Step(nav, bad, 0.016f, out) == 0, "a NaN stick must produce nothing");

    ControllerMenuState pushed;
    pushed.stickY = 1.0f;
    Require(Step(nav, pushed, std::nanf(""), out) == 1,
            "a NaN delta must not stop a real deflection being seen");
}

void TestResetTreatsAHeldButtonAsFresh()
{
    MenuNavigator nav;
    MenuAction out[8];
    ControllerMenuState held;
    held.accept = true;
    Step(nav, held, 0.016f, out);
    Step(nav, held, 0.016f, out);
    nav.Reset();
    // After a menu opens, a button already down is the player's first input.
    Require(Step(nav, held, 0.016f, out) == 1, "a reset makes a held button a fresh press");
}

} // namespace

int main()
{
    TestOneFlickIsOneAction();
    TestHoldingRepeatsAfterTheDelay();
    TestHysteresisStopsChatter();
    TestDiagonalPicksOneAxis();
    TestButtonsFireOnTheRisingEdgeOnly();
    TestNonFiniteInputIsIgnored();
    TestResetTreatsAHeldButtonAsFresh();
    std::cout << "menu navigator tests passed\n";
    return 0;
}
