#pragma once

#include <cstdint>

// The OpenXR per-frame call discipline, as a state machine that can be tested
// without an XR runtime.
//
// XR-005 prescribes the shape: wait once, cache what the wait returned, and
// submit from the end-of-frame hook where the observer already sits. This type
// enforces that shape rather than trusting the calling code to remember it,
// because every one of the ways it goes wrong produces a *plausible* image
// rather than an error:
//
//   - calling xrWaitFrame twice in a frame silently halves the frame rate, since
//     the second call blocks until the next display opportunity
//   - locating views at a time other than the one xrWaitFrame predicted makes
//     the world lag the head by a variable amount, which reads as "the tracking
//     feels bad" and is very hard to attribute
//   - locating each eye separately means the two eyes come from different
//     instants, which reads as depth instability rather than as a timing bug
//   - submitting without a matching begin is a protocol error the runtime may
//     or may not report
//
// None of those crash. All of them are cheap to prevent here.
//
// The threading split is part of the contract, not an implementation detail:
// Prey's camera work happens on the game thread (44208 in the live capture) and
// presentation on the render thread (54600), and XR-005 puts the wait on the
// former and the submit on the latter. The contract records which thread did
// what so a violation is reported rather than debugged later.
namespace preyvr::xrframe {

enum class Phase {
    idle = 0,      // nothing in flight; the next legal call is Wait
    waited = 1,    // xrWaitFrame returned; views may be located and cached
    begun = 2,     // xrBeginFrame accepted; rendering may proceed
};

enum class Result {
    ok = 0,
    outOfOrder = 1,     // the call is not legal in the current phase
    alreadyWaited = 2,  // a second wait in the same frame
    noViewsCached = 3,  // begin or submit without a located view pair
    wrongThread = 4,    // the submit came from the thread that waited, or vice versa
    notRunning = 5,     // the session is not in a state that renders
};

// What xrLocateViews produced, cached once so both eyes and every consumer in
// the frame agree on a single instant.
struct CachedViews {
    std::int64_t displayTime = 0;
    bool leftValid = false;
    bool rightValid = false;
};

class FrameContract {
public:
    // Session state. A runtime can ask us to stop rendering at any time (the
    // headset is removed, the app loses focus), and continuing to submit then is
    // both wrong and a good way to be throttled.
    void SetSessionRunning(bool running);
    bool SessionRunning() const { return sessionRunning_; }

    // Deliberately permit wait and submit on the same thread.
    //
    // OpenXR allows it; XR-005 does not, and the difference matters. During
    // bring-up the whole XR frame runs on Prey's render thread, because that is
    // the only place the backbuffer is valid, and splitting the wait onto the
    // game thread would add cross-thread pose caching before there is anything
    // to cache. So the rule is **opted out of explicitly, with a reason**, rather
    // than deleted or quietly not enforced.
    //
    // Defaults to false. It must go back to false before per-eye submission
    // ships: the split is what keeps head-to-photon latency down, and a
    // single-threaded pipeline that works is exactly the kind of thing that
    // survives to release by never being revisited.
    void AllowSingleThreaded(bool allow) { allowSingleThreaded_ = allow; }
    bool SingleThreadedAllowed() const { return allowSingleThreaded_; }

    // xrWaitFrame returned. `shouldRender` is the runtime's own answer, which
    // must be honoured -- a runtime that says not to render this frame still
    // expects a begin/end pair.
    Result OnWaited(std::int64_t predictedDisplayTime, bool shouldRender, std::uint32_t threadId);

    // xrLocateViews completed for *both* eyes at the cached display time.
    // Deliberately takes both at once: an API that lets the caller locate one eye
    // makes locating them at different times the easy mistake.
    Result OnViewsLocated(std::int64_t displayTime, bool leftValid, bool rightValid);

    Result OnBegun();

    // Submission, from the render thread.
    Result OnSubmitted(std::uint32_t threadId);

    // True only when the runtime asked for rendering, the session is running, and
    // a valid pair of views has been cached for this frame.
    bool ShouldRenderThisFrame() const;

    Phase CurrentPhase() const { return phase_; }
    const CachedViews& Views() const { return views_; }
    std::int64_t PredictedDisplayTime() const { return predictedDisplayTime_; }

    std::uint64_t CompletedFrames() const { return completedFrames_; }
    std::uint64_t SkippedFrames() const { return skippedFrames_; }
    std::uint64_t ProtocolErrors() const { return protocolErrors_; }

    // Drops any in-flight frame and returns to idle. For session loss, where the
    // in-flight frame is gone and pretending otherwise would wedge the state
    // machine permanently.
    void Reset();

private:
    Result Fail(Result result);

    Phase phase_ = Phase::idle;
    bool sessionRunning_ = false;
    bool allowSingleThreaded_ = false;
    bool runtimeWantsRender_ = false;
    std::int64_t predictedDisplayTime_ = 0;
    CachedViews views_{};
    std::uint32_t waitThreadId_ = 0;
    std::uint64_t completedFrames_ = 0;
    std::uint64_t skippedFrames_ = 0;
    std::uint64_t protocolErrors_ = 0;
};

} // namespace preyvr::xrframe
