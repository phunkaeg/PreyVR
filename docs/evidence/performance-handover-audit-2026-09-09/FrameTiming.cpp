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

DurationStats DurationSeries::Compute() const
{
    DurationStats out{};
    const unsigned long long written = mWritten.load(std::memory_order_acquire);
    if (written == 0) { return out; }

    const std::size_t count =
        static_cast<std::size_t>(written < kCapacity ? written : kCapacity);

    std::array<std::uint32_t, kCapacity> copy{};
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t value = mSamples[i].load(std::memory_order_relaxed);
        copy[i] = value;
        sum += value;
    }
    std::sort(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(count));

    out.count = count;
    out.total = written;
    out.p50 = copy[NearestRank(count, 50)];
    out.p95 = copy[NearestRank(count, 95)];
    out.p99 = copy[NearestRank(count, 99)];
    out.max = copy[count - 1];
    out.mean = static_cast<std::uint32_t>(sum / count);
    out.valid = true;
    return out;
}

void DurationSeries::Reset()
{
    // Written before the counter, so a concurrent reader that sees the new
    // count cannot then read a stale sample.
    for (auto& sample : mSamples) { sample.store(0, std::memory_order_relaxed); }
    mWritten.store(0, std::memory_order_release);
}

void IntervalSeries::Mark(std::uint64_t nanoseconds)
{
    const std::uint64_t previous = mPrevious.exchange(nanoseconds, std::memory_order_acq_rel);
    // No origin yet, or a clock that went backwards. Neither is an interval, and
    // inventing one would put a fabricated sample in a latency distribution.
    if (previous == 0 || nanoseconds <= previous) { return; }

    const std::uint64_t elapsed = nanoseconds - previous;
    mSeries.AddNanoseconds(elapsed);

    const std::uint32_t deadline = mDeadline.load(std::memory_order_relaxed);
    if (deadline != 0 && elapsed / 1000ull > deadline) {
        mMissed.fetch_add(1, std::memory_order_relaxed);
    }
}

void IntervalSeries::Reset()
{
    mSeries.Reset();
    mPrevious.store(0, std::memory_order_release);
    mMissed.store(0, std::memory_order_relaxed);
}

void IntervalSeries::SetDeadlineMicroseconds(std::uint32_t microseconds)
{
    mDeadline.store(microseconds, std::memory_order_relaxed);
}

std::uint32_t IntervalSeries::DeadlineMicroseconds() const
{
    return mDeadline.load(std::memory_order_relaxed);
}

unsigned long long IntervalSeries::MissedDeadlineCount() const
{
    return mMissed.load(std::memory_order_relaxed);
}

std::uint64_t MonotonicNanoseconds()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace preyvr::timing
