#include "preyvr/FrameTiming.h"

#include <cstdlib>
#include <iostream>

using namespace preyvr::timing;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void TestEmptySeriesSaysNothingRatherThanZero()
{
    DurationSeries series;
    const auto stats = series.Compute();
    // **Zero is a plausible-looking lie.** A p99 of 0 us reads as a very fast
    // frame; the truth is that no frame was measured at all.
    Require(!stats.valid, "an empty series must not be valid");
    Require(stats.count == 0, "an empty series has no samples");
}

void TestPercentilesAreNearestRankOverKnownData()
{
    DurationSeries series;
    for (std::uint32_t i = 1; i <= 100; ++i) { series.AddMicroseconds(i); }
    const auto stats = series.Compute();
    Require(stats.valid, "100 samples is valid");
    Require(stats.count == 100, "all 100 are in range");
    Require(stats.total == 100, "total matches");
    Require(stats.p50 == 50, "p50 of 1..100 is 50");
    Require(stats.p95 == 95, "p95 of 1..100 is 95");
    Require(stats.p99 == 99, "p99 of 1..100 is 99");
    Require(stats.max == 100, "max is 100");
    Require(stats.mean == 50, "mean of 1..100 truncates to 50");
}

// A reported percentile must be a value some frame actually took. Interpolation
// would invent one, and then the number cannot be traced back to a frame.
void TestPercentileIsAlwaysAnObservedValue()
{
    DurationSeries series;
    series.AddMicroseconds(10);
    series.AddMicroseconds(20);
    series.AddMicroseconds(1000);
    const auto stats = series.Compute();
    const bool observed = stats.p95 == 10 || stats.p95 == 20 || stats.p95 == 1000;
    Require(observed, "p95 must be one of the recorded samples");
    Require(stats.p99 == 1000, "the worst sample dominates the tail");
    Require(stats.max == 1000, "max is the worst sample");
}

// The ring keeps the most recent kCapacity samples. A profiler that averaged in
// startup hitches forever would never show a change made mid-session.
void TestRingKeepsTheRecentWindow()
{
    DurationSeries series;
    for (std::uint32_t i = 0; i < DurationSeries::kCapacity; ++i) {
        series.AddMicroseconds(9999);
    }
    for (std::uint32_t i = 0; i < DurationSeries::kCapacity; ++i) {
        series.AddMicroseconds(5);
    }
    const auto stats = series.Compute();
    Require(stats.count == DurationSeries::kCapacity, "window is the capacity");
    Require(stats.total == DurationSeries::kCapacity * 2, "total counts everything ever seen");
    Require(stats.max == 5, "the old hitches have rolled out of the window");
    Require(stats.p99 == 5, "and out of the tail");
}

void TestSaturatesInsteadOfWrapping()
{
    DurationSeries series;
    series.AddMicroseconds(0xFFFFFFFFull * 4ull);
    const auto stats = series.Compute();
    // Wrapping would turn a multi-hour stall into a few microseconds, which is
    // the one reading that would make a catastrophic frame look perfect.
    Require(stats.max == 0xFFFFFFFFu, "an absurd duration saturates at the maximum");
}

void TestNanosecondsConvert()
{
    DurationSeries series;
    series.AddNanoseconds(11'110'000ull);   // 11.11 ms, the 90 Hz interval
    const auto stats = series.Compute();
    Require(stats.p50 == 11110, "11.11 ms is 11110 us");
}

void TestFirstMarkEstablishesTheOriginAndRecordsNothing()
{
    IntervalSeries intervals;
    intervals.Mark(1'000'000'000ull);
    Require(!intervals.Compute().valid, "one timestamp is not an interval");
    intervals.Mark(1'011'110'000ull);
    const auto stats = intervals.Compute();
    Require(stats.valid, "two timestamps make one interval");
    Require(stats.count == 1, "exactly one interval");
    Require(stats.p50 == 11110, "and it is 11.11 ms");
}

// A clock that repeats or goes backwards must contribute nothing. Recording a
// zero would drag every percentile down and make the session look faster than
// it was.
void TestBackwardsClockIsRefused()
{
    IntervalSeries intervals;
    intervals.Mark(5'000'000'000ull);
    intervals.Mark(4'000'000'000ull);   // backwards
    Require(!intervals.Compute().valid, "a backwards step is not an interval");
    intervals.Mark(4'000'000'000ull);   // repeated
    Require(!intervals.Compute().valid, "a repeated timestamp is not an interval");
}

void TestMissedDeadlinesAreCountedSeparately()
{
    IntervalSeries intervals;
    intervals.SetDeadlineMicroseconds(11111);   // 90 Hz
    std::uint64_t now = 1'000'000'000ull;
    intervals.Mark(now);
    for (int i = 0; i < 99; ++i) {              // 99 good frames
        now += 11'000'000ull;
        intervals.Mark(now);
    }
    Require(intervals.MissedDeadlineCount() == 0, "good frames miss nothing");
    now += 40'000'000ull;                       // one 40 ms hitch
    intervals.Mark(now);
    Require(intervals.MissedDeadlineCount() == 1, "the hitch is counted");

    // **And this is why the count exists.** One dropped frame in a hundred is
    // invisible at p95 and p99, and it is exactly what a wearer notices.
    const auto stats = intervals.Compute();
    Require(stats.p95 == 11000, "p95 is unmoved by a single hitch");
    Require(stats.max == 40000, "only max and the counter see it");
}

void TestResetClearsEverything()
{
    IntervalSeries intervals;
    intervals.SetDeadlineMicroseconds(1000);
    intervals.Mark(1'000'000'000ull);
    intervals.Mark(1'100'000'000ull);
    Require(intervals.MissedDeadlineCount() == 1, "premise: one miss recorded");
    intervals.Reset();
    Require(!intervals.Compute().valid, "reset clears the samples");
    Require(intervals.MissedDeadlineCount() == 0, "reset clears the miss count");
    Require(intervals.DeadlineMicroseconds() == 1000, "but keeps the configured deadline");

    // The origin is cleared too, so the first mark after a reset does not
    // record the gap across the reset as if it were a frame.
    intervals.Mark(9'000'000'000ull);
    Require(!intervals.Compute().valid, "the first mark after reset is an origin");
}

void TestMonotonicClockAdvances()
{
    const std::uint64_t a = MonotonicNanoseconds();
    Require(a != 0, "the clock returns something");
    std::uint64_t b = a;
    for (int i = 0; i < 1000000 && b == a; ++i) { b = MonotonicNanoseconds(); }
    Require(b >= a, "the clock never goes backwards");
}

} // namespace

int main()
{
    TestEmptySeriesSaysNothingRatherThanZero();
    TestPercentilesAreNearestRankOverKnownData();
    TestPercentileIsAlwaysAnObservedValue();
    TestRingKeepsTheRecentWindow();
    TestSaturatesInsteadOfWrapping();
    TestNanosecondsConvert();
    TestFirstMarkEstablishesTheOriginAndRecordsNothing();
    TestBackwardsClockIsRefused();
    TestMissedDeadlinesAreCountedSeparately();
    TestResetClearsEverything();
    TestMonotonicClockAdvances();
    std::cout << "frame timing tests passed\n";
    return 0;
}
