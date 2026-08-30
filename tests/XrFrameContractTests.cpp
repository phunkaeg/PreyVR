#include "preyvr/XrFrameContract.h"

#include <cstdlib>
#include <iostream>

namespace {

using namespace preyvr::xrframe;

constexpr std::uint32_t kGameThread = 44208;   // observed live 2026-08-29
constexpr std::uint32_t kRenderThread = 54600;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// One complete, correct frame.
Result RunGoodFrame(FrameContract& contract, std::int64_t time)
{
    Result r = contract.OnWaited(time, true, kGameThread);
    if (r != Result::ok) { return r; }
    r = contract.OnViewsLocated(time, true, true);
    if (r != Result::ok) { return r; }
    r = contract.OnBegun();
    if (r != Result::ok) { return r; }
    return contract.OnSubmitted(kRenderThread);
}

void TestHappyPath()
{
    FrameContract contract;
    contract.SetSessionRunning(true);

    for (std::int64_t i = 1; i <= 5; ++i) {
        Require(RunGoodFrame(contract, i * 1000) == Result::ok, "a well-formed frame is accepted");
    }
    Require(contract.CompletedFrames() == 5, "five frames completed");
    Require(contract.ProtocolErrors() == 0, "a correct sequence produces no protocol errors");
    Require(contract.CurrentPhase() == Phase::idle, "the contract returns to idle");
}

void TestDoubleWaitIsCaughtDistinctly()
{
    // This is the one that silently halves the frame rate rather than erroring,
    // so it gets its own result value.
    FrameContract contract;
    contract.SetSessionRunning(true);

    Require(contract.OnWaited(1000, true, kGameThread) == Result::ok, "the first wait is accepted");
    Require(contract.OnWaited(2000, true, kGameThread) == Result::alreadyWaited,
        "a second wait in the same frame is reported as such, not as a generic ordering error");
    Require(contract.ProtocolErrors() == 1, "the violation is counted");
}

void TestViewsMustMatchThePredictedTime()
{
    // Locating at a different time makes the world lag the head by a variable
    // amount, which reads as bad tracking and is nearly untraceable in the field.
    FrameContract contract;
    contract.SetSessionRunning(true);

    Require(contract.OnWaited(1000, true, kGameThread) == Result::ok, "wait accepted");
    Require(contract.OnViewsLocated(1001, true, true) == Result::outOfOrder,
        "locating at a time other than the predicted one is refused");
    Require(contract.OnViewsLocated(1000, true, true) == Result::ok,
        "locating at the predicted time is accepted");
}

void TestRenderingWithoutViewsIsRefused()
{
    FrameContract contract;
    contract.SetSessionRunning(true);

    Require(contract.OnWaited(1000, true, kGameThread) == Result::ok, "wait accepted");
    Require(contract.OnBegun() == Result::noViewsCached,
        "beginning a render frame with no cached views is refused");

    // A single invalid eye is still no good: half a stereo pair is not a frame.
    Require(contract.OnViewsLocated(1000, true, false) == Result::ok, "locate reports validity");
    Require(contract.OnBegun() == Result::noViewsCached, "one invalid eye is still refused");
    Require(!contract.ShouldRenderThisFrame(), "a half-valid pair does not render");
}

void TestSkippedFrameStillPairs()
{
    // A runtime that says not to render still expects a begin/end pair. Skipping
    // the pair is a protocol error that some runtimes tolerate and others do not.
    FrameContract contract;
    contract.SetSessionRunning(true);

    Require(contract.OnWaited(1000, false, kGameThread) == Result::ok, "wait accepted");
    Require(contract.OnBegun() == Result::ok,
        "begin is legal without views when the runtime asked us not to render");
    Require(!contract.ShouldRenderThisFrame(), "a skipped frame does not render");
    Require(contract.OnSubmitted(kRenderThread) == Result::ok, "the pair still completes");
    Require(contract.SkippedFrames() == 1, "the skip is counted separately from a completion");
    Require(contract.CompletedFrames() == 0, "a skipped frame is not a completed one");
    Require(contract.ProtocolErrors() == 0, "skipping correctly is not an error");
}

void TestThreadSplitIsEnforced()
{
    // XR-005 puts the wait on the game thread and the submit on the render
    // thread, which is the shape the live capture found. Collapsing them is worth
    // failing loudly about while it is still cheap to change.
    FrameContract contract;
    contract.SetSessionRunning(true);

    Require(contract.OnWaited(1000, true, kGameThread) == Result::ok, "wait accepted");
    Require(contract.OnViewsLocated(1000, true, true) == Result::ok, "views cached");
    Require(contract.OnBegun() == Result::ok, "begin accepted");
    Require(contract.OnSubmitted(kGameThread) == Result::wrongThread,
        "submitting from the waiting thread is refused");
    Require(contract.CurrentPhase() == Phase::idle,
        "a rejected submit still releases the frame rather than wedging the state machine");
}

void TestOutOfOrderCalls()
{
    FrameContract contract;
    contract.SetSessionRunning(true);

    Require(contract.OnBegun() == Result::outOfOrder, "begin before wait is refused");
    Require(contract.OnSubmitted(kRenderThread) == Result::outOfOrder, "submit before begin is refused");
    Require(contract.OnViewsLocated(0, true, true) == Result::outOfOrder,
        "locating before wait is refused");
}

void TestSessionState()
{
    FrameContract contract;
    Require(contract.OnWaited(1000, true, kGameThread) == Result::notRunning,
        "waiting without a running session is refused");

    contract.SetSessionRunning(true);
    Require(contract.OnWaited(1000, true, kGameThread) == Result::ok, "wait accepted once running");

    // Losing the session mid-frame must not leave the machine wedged in `waited`
    // forever: the frame it was waiting on no longer exists.
    contract.SetSessionRunning(false);
    Require(contract.CurrentPhase() == Phase::idle, "losing the session drops the in-flight frame");

    contract.SetSessionRunning(true);
    Require(RunGoodFrame(contract, 2000) == Result::ok, "the contract recovers after session loss");
}

void TestResetIsIdempotent()
{
    FrameContract contract;
    contract.SetSessionRunning(true);
    Require(contract.OnWaited(1000, true, kGameThread) == Result::ok, "wait accepted");
    contract.Reset();
    contract.Reset();
    Require(contract.CurrentPhase() == Phase::idle, "reset returns to idle and stays there");
    Require(RunGoodFrame(contract, 3000) == Result::ok, "a frame runs cleanly after reset");
}

} // namespace

int main()
{
    TestHappyPath();
    TestDoubleWaitIsCaughtDistinctly();
    TestViewsMustMatchThePredictedTime();
    TestRenderingWithoutViewsIsRefused();
    TestSkippedFrameStillPairs();
    TestThreadSplitIsEnforced();
    TestOutOfOrderCalls();
    TestSessionState();
    TestResetIsIdempotent();
    std::cout << "PreyVR XR frame contract tests passed\n";
    return 0;
}
