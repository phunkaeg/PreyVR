#include "preyvr/FrameTiming.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>

namespace preyvr::timing {
namespace {

// The index of the p-th percentile in a sorted run of `count` samples, using
// the nearest-rank definition. Nearest-rank is chosen over interpolation
// because these are latencies: reporting a p99 that no frame actually took
// invites arguing with the number instead of the frame.
std::size_t NearestRank(std::size_t count, unsigned int percent)
{
    if (count == 0) { return 0; }
    const std::size_t rank = (count * percent + 99) / 100;   // ceil(count * p/100)
    const std::size_t clamped = rank == 0 ? 1 : rank;
    return (clamped > count ? count : clamped) - 1;
}

} // namespace

void DurationSeries::AddMicroseconds(std::uint64_t microseconds)
{
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint32_t>::max();
    const auto value = static_cast<std::uint32_t>(microseconds > kMax ? kMax : microseconds);
    const unsigned long long written = mWritten.load(std::memory_order_relaxed);
    mSamples[written % kCapacity].store(value, std::memory_order_relaxed);
    mWritten.store(written + 1, std::memory_order_release);
}

void DurationSeries::AddNanoseconds(std::uint64_t nanoseconds)
{
    AddMicroseconds(nanoseconds / 1000ull);
}

DurationStats DurationSeries::Compute(std::uint32_t thresholdMicroseconds) const
{
    DurationStats out{};
    const unsigned long long written = mWritten.load(std::memory_order_acquire);
    if (written == 0) { return out; }

    const std::size_t count =
        static_cast<std::size_t>(written < kCapacity ? written : kCapacity);

    std::array<std::uint32_t, kCapacity> copy{};
    std::uint64_t sum = 0;
    unsigned long long above = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t value = mSamples[i].load(std::memory_order_relaxed);
        copy[i] = value;
        sum += value;
        if (thresholdMicroseconds != 0 && value > thresholdMicroseconds) { ++above; }
    }
    std::sort(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(count));

    out.count = count;
    out.total = written;
    out.p50 = copy[NearestRank(count, 50)];
    out.p95 = copy[NearestRank(count, 95)];
    out.p99 = copy[NearestRank(count, 99)];
    out.max = copy[count - 1];
    out.mean = static_cast<std::uint32_t>(sum / count);
    out.aboveThreshold = above;
    out.valid = true;
    return out;
}

void DurationSeries::RequestReset() { mResetRequested.store(true, std::memory_order_release); }

bool DurationSeries::ApplyPendingReset()
{
    if (!mResetRequested.exchange(false, std::memory_order_acq_rel)) { return false; }
    // Safe here and nowhere else: this runs on the only thread that writes, so
    // no sample can be in flight across the clear. The generation bump is what
    // lets a reader tell an empty new epoch from an empty old one.
    for (auto& sample : mSamples) { sample.store(0, std::memory_order_relaxed); }
    mWritten.store(0, std::memory_order_release);
    mGeneration.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

unsigned int DurationSeries::Generation() const
{
    return mGeneration.load(std::memory_order_acquire);
}

void IntervalSeries::Mark(std::uint64_t nanoseconds)
{
    const std::uint64_t previous = mPrevious.exchange(nanoseconds, std::memory_order_acq_rel);
    mLastNs.store(nanoseconds, std::memory_order_relaxed);
    // No origin yet, or a clock that went backwards. Neither is an interval, and
    // inventing one would put a fabricated sample in a latency distribution.
    if (previous == 0 || nanoseconds <= previous) {
        if (previous == 0) { mFirstNs.store(nanoseconds, std::memory_order_relaxed); }
        return;
    }

    const std::uint64_t elapsed = nanoseconds - previous;
    mSeries.AddNanoseconds(elapsed);

    const std::uint32_t budget = mBudget.load(std::memory_order_relaxed);
    if (budget != 0 && elapsed / 1000ull > budget) {
        mOverBudget.fetch_add(1, std::memory_order_relaxed);
    }
}

IntervalStats IntervalSeries::Compute() const
{
    IntervalStats out{};
    out.budgetMicroseconds = mBudget.load(std::memory_order_relaxed);
    out.duration = mSeries.Compute(out.budgetMicroseconds);
    out.windowOverBudget = out.duration.aboveThreshold;
    out.lifetimeOverBudget = mOverBudget.load(std::memory_order_relaxed);
    out.generation = mSeries.Generation();

    // The window's own duration, summed from its samples rather than taken from
    // wall time: the window is the last N intervals, which is not the last N
    // seconds, and using the session's elapsed time as the denominator is the
    // mistake that turned a 512-sample window into "a 30-second sample".
    out.windowSeconds = static_cast<double>(out.duration.mean) *
                        static_cast<double>(out.duration.count) / 1.0e6;
    const std::uint64_t first = mFirstNs.load(std::memory_order_relaxed);
    const std::uint64_t last = mLastNs.load(std::memory_order_relaxed);
    if (last > first) {
        out.sessionSeconds = static_cast<double>(last - first) / 1.0e9;
    }
    return out;
}

void IntervalSeries::RequestReset() { mSeries.RequestReset(); }

bool IntervalSeries::ApplyPendingReset()
{
    if (!mSeries.ApplyPendingReset()) { return false; }
    mPrevious.store(0, std::memory_order_release);
    mFirstNs.store(0, std::memory_order_relaxed);
    mLastNs.store(0, std::memory_order_relaxed);
    mOverBudget.store(0, std::memory_order_relaxed);
    return true;
}

void IntervalSeries::SetBudgetMicroseconds(std::uint32_t microseconds)
{
    mBudget.store(microseconds, std::memory_order_relaxed);
}

std::uint32_t IntervalSeries::BudgetMicroseconds() const
{
    return mBudget.load(std::memory_order_relaxed);
}

std::uint64_t MonotonicNanoseconds()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace preyvr::timing
