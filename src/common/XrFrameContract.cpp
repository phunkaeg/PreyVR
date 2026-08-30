#include "preyvr/XrFrameContract.h"

namespace preyvr::xrframe {

void FrameContract::SetSessionRunning(bool running)
{
    if (sessionRunning_ && !running) {
        // Losing the session mid-frame leaves nothing to submit into, so the
        // in-flight frame is dropped rather than carried across the gap.
        Reset();
    }
    sessionRunning_ = running;
}

Result FrameContract::Fail(Result result)
{
    ++protocolErrors_;
    return result;
}

Result FrameContract::OnWaited(
    std::int64_t predictedDisplayTime, bool shouldRender, std::uint32_t threadId)
{
    if (!sessionRunning_) {
        return Fail(Result::notRunning);
    }
    if (phase_ != Phase::idle) {
        // A second wait inside one frame is the failure that silently halves the
        // frame rate, so it is reported distinctly from a generic ordering error.
        return Fail(phase_ == Phase::waited ? Result::alreadyWaited : Result::outOfOrder);
    }

    phase_ = Phase::waited;
    predictedDisplayTime_ = predictedDisplayTime;
    runtimeWantsRender_ = shouldRender;
    waitThreadId_ = threadId;
    views_ = CachedViews{};
    return Result::ok;
}

Result FrameContract::OnViewsLocated(std::int64_t displayTime, bool leftValid, bool rightValid)
{
    if (phase_ != Phase::waited) {
        return Fail(Result::outOfOrder);
    }
    if (displayTime != predictedDisplayTime_) {
        // Locating at any time other than the predicted one makes the world lag
        // the head by a variable amount. Refused rather than accepted with a
        // warning, because the symptom is "tracking feels bad" and nobody ever
        // traces that back to a timestamp.
        return Fail(Result::outOfOrder);
    }

    views_.displayTime = displayTime;
    views_.leftValid = leftValid;
    views_.rightValid = rightValid;
    return Result::ok;
}

Result FrameContract::OnBegun()
{
    if (phase_ != Phase::waited) {
        return Fail(Result::outOfOrder);
    }
    // Begin is legal even when the runtime asked us not to render -- the
    // begin/end pair is still required -- but rendering without cached views is
    // not, so that case is caught here rather than producing an eyeless frame.
    if (runtimeWantsRender_ && !(views_.leftValid && views_.rightValid)) {
        return Fail(Result::noViewsCached);
    }
    phase_ = Phase::begun;
    return Result::ok;
}

Result FrameContract::OnSubmitted(std::uint32_t threadId)
{
    if (phase_ != Phase::begun) {
        return Fail(Result::outOfOrder);
    }
    if (threadId == waitThreadId_) {
        // XR-005 puts the wait on the game thread and the submit on the render
        // thread. Seeing both on one thread means the design has been collapsed,
        // which is worth failing loudly about while it is still cheap to fix.
        phase_ = Phase::idle;
        return Fail(Result::wrongThread);
    }

    if (ShouldRenderThisFrame()) {
        ++completedFrames_;
    } else {
        ++skippedFrames_;
    }
    phase_ = Phase::idle;
    return Result::ok;
}

bool FrameContract::ShouldRenderThisFrame() const
{
    return sessionRunning_ && runtimeWantsRender_ && views_.leftValid && views_.rightValid;
}

void FrameContract::Reset()
{
    phase_ = Phase::idle;
    runtimeWantsRender_ = false;
    predictedDisplayTime_ = 0;
    views_ = CachedViews{};
    waitThreadId_ = 0;
}

} // namespace preyvr::xrframe
