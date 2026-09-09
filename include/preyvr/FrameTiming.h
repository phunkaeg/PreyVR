#pragma once

#include <atomic>
#include <cstdint>

// Where the frame time actually goes.
//
// **The first falsifiable question is which stage is limiting**, and nothing in
// this project can answer it yet. The performance plan names the candidates --
// scene GPU work, CPU work, or frame scheduling -- and a guess between them is
// worth nothing: reclaiming pixels helps only if pixels are the constraint, and
// a mod that waits on `xrWaitFrame` for 8 ms would show identical symptoms while
// rendering fewer pixels changed nothing.
//
// **Recording must not itself cost what it measures.** Samples are unsigned
// microseconds in a fixed ring of plain atomics: one relaxed store and one
// release increment per sample, no allocation, no lock, no sorting in the hot
// path. Percentiles are computed only when someone asks, on a copy.
//
// **The reader may race the writer, and that is deliberate.** Locking would put
// the reader's cost into the render thread, which is the one thing a profiler
// must never do. A snapshot taken mid-write can contain a sample from the
// previous lap of the ring; over hundreds of samples that shifts a percentile
// by less than the thing being measured. What it cannot do is tear a value:
// each sample is a single atomic word.
namespace preyvr::timing {

struct DurationStats {
    // **These two are different populations and reporting only the first was a
    // real defect.** `count` is the rolling window -- at most `kCapacity`, so a
    // few seconds at frame rate -- while `total` is everything since the epoch.
    // A handover once described a 512-sample window as "a 30-second sample";
    // both numbers were individually right and the comparison drawn from them
    // was not. Emit both, always.
    unsigned long long count = 0;   // samples in this snapshot's window
    unsigned long long total = 0;   // samples ever recorded this generation
    std::uint32_t p50 = 0;
    std::uint32_t p95 = 0;
    std::uint32_t p99 = 0;
    std::uint32_t max = 0;
    std::uint32_t mean = 0;
    // Samples in this window strictly above the threshold passed to Compute.
    // Zero threshold means "not counted" rather than "none exceeded".
    unsigned long long aboveThreshold = 0;
    bool valid = false;             // false when nothing has been recorded
};

class DurationSeries {
public:
    static constexpr std::size_t kCapacity = 512;

    // Saturates rather than wrapping: a 72-minute stall recorded as 3 us would
    // be worse than one recorded as "the largest thing this can say".
    void AddMicroseconds(std::uint64_t microseconds);

    // Nanoseconds are what the monotonic clock gives; converting at the call
    // site invites each caller to divide differently.
    void AddNanoseconds(std::uint64_t nanoseconds);

    DurationStats Compute(std::uint32_t thresholdMicroseconds = 0) const;

    // **Reset is REQUESTED from any thread and APPLIED on the recording one.**
    // Clearing the ring from the command thread races an in-flight writer: it
    // can read the old count, lose to the reset, then publish old-count-plus-one,
    // so a fresh epoch silently inherits a stale sample. Atomics prevent torn
    // values; they do not make a multi-word clear an atomic epoch transition.
    void RequestReset();
    // Call at a frame boundary on the thread that records. Returns true when a
    // reset was actually applied.
    bool ApplyPendingReset();
    unsigned int Generation() const;

private:
    std::atomic<std::uint32_t> mSamples[kCapacity] = {};
    std::atomic<unsigned long long> mWritten{0};
    std::atomic<bool> mResetRequested{false};
    std::atomic<unsigned int> mGeneration{0};
};

// Frame-to-frame interval, derived from successive timestamps rather than
// recorded directly, so a caller cannot accidentally report a duration as an
// interval. The first timestamp establishes the origin and records nothing --
// an interval measured against zero is not a slow frame, it is no data.
// **"Over budget", not "missed frame".** This counts intervals longer than a
// configured threshold. It is NOT a compositor drop count: no compositor is
// consulted, a single long interval counts once however many display periods it
// spans, and an interval a hair over the threshold counts the same as a stall.
// Naming it a missed frame invites a claim the instrument cannot support.
struct IntervalStats {
    DurationStats duration{};
    // Over budget within the rolling window, and since the epoch. Different
    // populations; a ratio built from one and a denominator from the other is
    // meaningless, which is why both are here.
    unsigned long long windowOverBudget = 0;
    unsigned long long lifetimeOverBudget = 0;
    std::uint32_t budgetMicroseconds = 0;
    // Wall time actually covered by the window, so an events-per-second figure
    // has a real denominator instead of the nominal collection time.
    double windowSeconds = 0.0;
    double sessionSeconds = 0.0;
    unsigned int generation = 0;
};

class IntervalSeries {
public:
    void Mark(std::uint64_t nanoseconds);
    IntervalStats Compute() const;
    void RequestReset();
    bool ApplyPendingReset();

    void SetBudgetMicroseconds(std::uint32_t microseconds);
    std::uint32_t BudgetMicroseconds() const;

private:
    DurationSeries mSeries;
    std::atomic<std::uint64_t> mPrevious{0};
    std::atomic<std::uint64_t> mFirstNs{0};
    std::atomic<std::uint64_t> mLastNs{0};
    std::atomic<std::uint32_t> mBudget{0};
    std::atomic<unsigned long long> mOverBudget{0};
};

// The monotonic clock these are meant to be fed from. One definition, so two
// call sites cannot disagree about the epoch or the unit.
std::uint64_t MonotonicNanoseconds();

} // namespace preyvr::timing
