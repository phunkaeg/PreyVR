#include "preyvr/Locomotion.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

using namespace preyvr::locomotion;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool Near(float a, float b, float epsilon = 1e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

void TestDeadzoneIsRadialAndRescales()
{
    float x = -1.0f;
    float y = -1.0f;

    Require(ShapeStick(0.1f, 0.0f, 0.15f, &x, &y), "a small reading is still a valid one");
    Require(x == 0.0f && y == 0.0f, "inside the deadzone must be exactly zero");

    // Rescaled from the deadzone edge, not from the origin: a stick that has just
    // crossed the threshold must start moving the player from a standstill rather
    // than jumping to 0.15 of full speed.
    Require(ShapeStick(0.15f + 1e-6f, 0.0f, 0.15f, &x, &y), "just outside the deadzone");
    Require(Near(x, 0.0f, 1e-3f), "the first live reading must start from zero, not from the deadzone");

    Require(ShapeStick(0.5f, 0.0f, 0.15f, &x, &y), "a mid reading");
    Require(Near(x, (0.5f - 0.15f) / 0.85f), "the rescale must be linear from the deadzone edge");
    Require(y == 0.0f, "an axis that was not deflected must stay at zero");

    Require(ShapeStick(1.0f, 0.0f, 0.15f, &x, &y), "a full reading");
    Require(Near(x, 1.0f), "full deflection must reach full value");
}

void TestDiagonalsAreNotFaster()
{
    // The bug this is here to prevent: clamping each axis independently leaves a
    // diagonal at magnitude sqrt(2), so the player sprints only when moving
    // cornerwise. The radial form keeps every direction at the same speed.
    float dx = 0.0f;
    float dy = 0.0f;
    const float unit = 0.70710678f;
    Require(ShapeStick(unit, unit, 0.15f, &dx, &dy), "a full diagonal");
    const float magnitude = std::sqrt(dx * dx + dy * dy);
    Require(Near(magnitude, 1.0f), "a full diagonal must be full speed, not more");

    // A square-gated stick reports past the unit circle; that must be clamped,
    // not passed through as an axis value real hardware never produces.
    float ox = 0.0f;
    float oy = 0.0f;
    Require(ShapeStick(1.0f, 1.0f, 0.15f, &ox, &oy), "an over-range diagonal");
    Require(Near(std::sqrt(ox * ox + oy * oy), 1.0f), "over-range must clamp to full, not exceed it");
}

void TestUnusableReadingsAreRefused()
{
    float x = 5.0f;
    float y = 5.0f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Require(!ShapeStick(nan, 0.0f, 0.15f, &x, &y), "NaN must be refused");
    Require(!ShapeStick(0.0f, std::numeric_limits<float>::infinity(), 0.15f, &x, &y),
            "infinity must be refused");
    Require(x == 5.0f && y == 5.0f, "a refused reading must not write an output");
    Require(!ShapeStick(0.5f, 0.0f, 1.0f, &x, &y), "a deadzone at the rim must be refused");
    Require(!ShapeStick(0.5f, 0.0f, -0.1f, &x, &y), "a negative deadzone must be refused");
}

void TestFirstUpdateEmitsBothAxes()
{
    StickAxis axis;
    AxisEvent events[2];
    const unsigned int count = axis.Update(0.0f, 0.0f, events);
    Require(count == 2, "the first update must state both axes explicitly");
    Require(events[0].keyId == kKeyThumbLX && events[1].keyId == kKeyThumbLY,
            "the key ids must be the gamepad stick axes");
    Require(events[0].deviceType == kDeviceGamepad, "the event must claim to be a gamepad");
    Require(events[0].state == kStateChanged, "the event must use the analog changed state");
}

void TestOnlyChangesAreEmitted()
{
    StickAxis axis;
    AxisEvent events[2];
    axis.Update(0.0f, 0.0f, events);

    Require(axis.Update(0.0f, 0.0f, events) == 0, "an unchanged stick must emit nothing");

    const unsigned int moved = axis.Update(0.6f, 0.0f, events);
    Require(moved == 1, "only the axis that moved is emitted");
    Require(events[0].keyId == kKeyThumbLX, "and it is the one that actually moved");
}

void TestReleasingTheStickEmitsZero()
{
    // **The bug this exists to prevent.** Emitting only while the stick is
    // outside the deadzone means the last thing the engine hears is "0.8
    // forward" and nothing ever contradicts it, so the player walks into a wall
    // until they nudge the stick again. It is the kind of failure that looks
    // like a physics problem and is an input one.
    StickAxis axis;
    AxisEvent events[2];
    axis.Update(0.0f, 0.0f, events);
    axis.Update(0.0f, 0.9f, events);
    Require(axis.LastSentY() > 0.5f, "the stick was pushed and the value was sent");

    const unsigned int released = axis.Update(0.0f, 0.0f, events);
    Require(released == 1, "letting go must emit exactly the axis that changed");
    Require(events[0].keyId == kKeyThumbLY, "and it must be the axis that was deflected");
    Require(events[0].value == 0.0f, "releasing must send a hard zero, not merely stop sending");
    Require(axis.LastSentY() == 0.0f, "and the sent state must record it");
}

void TestABadReadingDoesNotBecomeTheBaseline()
{
    StickAxis axis;
    AxisEvent events[2];
    axis.Update(0.0f, 0.8f, events);
    const float before = axis.LastSentY();

    Require(axis.Update(std::numeric_limits<float>::quiet_NaN(), 0.0f, events) == 0,
            "a NaN reading emits nothing");
    Require(axis.LastSentY() == before, "and must not latch itself as the last sent value");

    // The important consequence: the next good reading is still compared against
    // the real baseline, so a release after a glitch still sends its zero.
    const unsigned int released = axis.Update(0.0f, 0.0f, events);
    Require(released == 1 && events[0].value == 0.0f, "a release after a glitch still emits zero");
}

void TestResetForcesAFullRestatement()
{
    StickAxis axis;
    AxisEvent events[2];
    axis.Update(0.0f, 0.0f, events);
    Require(axis.Update(0.0f, 0.0f, events) == 0, "settled");
    axis.Reset();
    Require(axis.Update(0.0f, 0.0f, events) == 2,
            "after a reset the engine's stored axis is not ours to assume");
}

} // namespace

int main()
{
    TestDeadzoneIsRadialAndRescales();
    TestDiagonalsAreNotFaster();
    TestUnusableReadingsAreRefused();
    TestFirstUpdateEmitsBothAxes();
    TestOnlyChangesAreEmitted();
    TestReleasingTheStickEmitsZero();
    TestABadReadingDoesNotBecomeTheBaseline();
    TestResetForcesAFullRestatement();
    std::cout << "locomotion tests passed\n";
    return 0;
}
