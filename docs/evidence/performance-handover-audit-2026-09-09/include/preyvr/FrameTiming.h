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
    unsigned long long count = 0;   // samples considered in this snapshot
    unsigned long long total = 0;   // samples ever recorded
    std::uint32_t p50 = 0;
    std::uint32_t p95 = 0;
    std::uint32_t p99 = 0;
    std::uint32_t max = 0;
    std::uint32_t mean = 0;
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

    DurationStats Compute() const;
    void Reset();

private:
    std::atomic<std::uint32_t> mSamples[kCapacity] = {};
    std::atomic<unsigned long long> mWritten{0};
};

// Frame-to-frame interval, derived from successive timestamps rather than
// recorded directly, so a caller cannot accidentally report a duration as an
// interval. The first timestamp establishes the origin and records nothing --
// an interval measured against zero is not a slow frame, it is no data.
class IntervalSeries {
public:
    void Mark(std::uint64_t nanoseconds);
    DurationStats Compute() const { return mSeries.Compute(); }
    void Reset();

    // Intervals longer than this are recorded but also counted separately: a
    // missed display deadline is the symptom the wearer actually feels, and a
    // p99 hides it by construction.
    void SetDeadlineMicroseconds(std::uint32_t microseconds);
    std::uint32_t DeadlineMicroseconds() const;
    unsigned long long MissedDeadlineCount() const;

private:
    DurationSeries mSeries;
    std::atomic<std::uint64_t> mPrevious{0};
    std::atomic<std::uint32_t> mDeadline{0};
    std::atomic<unsigned long long> mMissed{0};
};

// The monotonic clock these are meant to be fed from. One definition, so two
// call sites cannot disagree about the epoch or the unit.
std::uint64_t MonotonicNanoseconds();

} // namespace preyvr::timing
