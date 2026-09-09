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

void TestThresholdCountIsOptIn()
{
    DurationSeries series;
    for (int i = 0; i < 10; ++i) { series.AddMicroseconds(100); }
    for (int i = 0; i < 5; ++i) { series.AddMicroseconds(300); }
    Require(series.Compute().aboveThreshold == 0,
            "no threshold means not counted, never 'none exceeded'");
    Require(series.Compute(200).aboveThreshold == 5, "five samples are above 200");
    Require(series.Compute(300).aboveThreshold == 0, "the comparison is strict");
}

void TestFirstMarkEstablishesTheOriginAndRecordsNothing()
{
    IntervalSeries intervals;
    intervals.Mark(1'000'000'000ull);
    Require(!intervals.Compute().duration.valid, "one timestamp is not an interval");
    intervals.Mark(1'011'110'000ull);
    const auto stats = intervals.Compute();
    Require(stats.duration.valid, "two timestamps make one interval");
    Require(stats.duration.count == 1, "exactly one interval");
    Require(stats.duration.p50 == 11110, "and it is 11.11 ms");
}

// A clock that repeats or goes backwards must contribute nothing. Recording a
// zero would drag every percentile down and make the session look faster than
// it was.
void TestBackwardsClockIsRefused()
{
    IntervalSeries intervals;
    intervals.Mark(5'000'000'000ull);
    intervals.Mark(4'000'000'000ull);   // backwards
    Require(!intervals.Compute().duration.valid, "a backwards step is not an interval");
    intervals.Mark(4'000'000'000ull);   // repeated
    Require(!intervals.Compute().duration.valid, "a repeated timestamp is not an interval");
}

// **Window and lifetime are different populations, and conflating them is the
// defect an audit caught in a shipped handover.** 2000 slow intervals then 512
// fast ones: the window sees only the fast, the lifetime counter remembers the
// slow. Both are right; a ratio mixing them is not.
void TestWindowAndLifetimePopulationsAreReportedSeparately()
{
    IntervalSeries intervals;
    intervals.SetBudgetMicroseconds(11111);
    std::uint64_t now = 1'000'000'000ull;
    intervals.Mark(now);
    for (int i = 0; i < 2000; ++i) { now += 20'000'000ull; intervals.Mark(now); }
    for (int i = 0; i < 512; ++i) { now += 10'000'000ull; intervals.Mark(now); }

    const auto stats = intervals.Compute();
    Require(stats.duration.count == 512, "the window holds only its capacity");
    Require(stats.duration.total == 2512, "the lifetime total remembers everything");
    Require(stats.duration.p50 == 10000, "the window sees only the fast intervals");
    Require(stats.windowOverBudget == 0, "nothing in the window is over budget");
    Require(stats.lifetimeOverBudget == 2000, "but 2000 were, across the session");

    // The window's own duration, not the session's: 512 x 10 ms = 5.12 s.
    Require(stats.windowSeconds > 5.0 && stats.windowSeconds < 5.3,
            "window seconds describes the window, not the run");
    Require(stats.sessionSeconds > 45.0, "session seconds is much longer");
}

// Threshold semantics, not compositor drops.
void TestOverBudgetIsAThresholdCountNotADropCount()
{
    IntervalSeries intervals;
    intervals.SetBudgetMicroseconds(11111);
    // Non-zero on purpose: zero is the "no origin yet" sentinel, so a test that
    // starts there quietly loses its first interval.
    std::uint64_t now = 1'000'000'000ull;
    intervals.Mark(now);
    for (int i = 0; i < 1000; ++i) {
        now += (i % 2 == 0) ? 11'110'000ull : 11'112'000ull;
        intervals.Mark(now);
    }
    Require(intervals.Compute().lifetimeOverBudget == 500,
            "half the intervals are one microsecond over the threshold");

    // And one long stall counts ONCE however many display periods it spans.
    IntervalSeries stall;
    stall.SetBudgetMicroseconds(11111);
    stall.Mark(1'000'000'000ull);
    stall.Mark(2'000'000'000ull);   // a full second, ~90 periods at 90 Hz
    Require(stall.Compute().lifetimeOverBudget == 1,
            "a one-second stall is one over-budget interval, not ninety");
}

void TestASingleHitchHidesFromPercentilesButNotTheCounter()
{
    IntervalSeries intervals;
    intervals.SetBudgetMicroseconds(11111);
    std::uint64_t now = 1'000'000'000ull;
    intervals.Mark(now);
    for (int i = 0; i < 99; ++i) { now += 11'000'000ull; intervals.Mark(now); }
    Require(intervals.Compute().lifetimeOverBudget == 0, "good frames exceed nothing");
    now += 40'000'000ull;
    intervals.Mark(now);
    const auto stats = intervals.Compute();
    Require(stats.lifetimeOverBudget == 1, "the hitch is counted");
    Require(stats.duration.p95 == 11000, "p95 is unmoved by a single hitch");
    Require(stats.duration.max == 40000, "only max and the counter see it");
}

// **Reset must not take effect until the recording thread applies it.** A clear
// run from another thread can race an in-flight writer, letting a fresh epoch
// inherit a stale sample.
void TestResetIsDeferredToTheRecordingThread()
{
    DurationSeries series;
    series.AddMicroseconds(5000);
    const unsigned int before = series.Generation();

    series.RequestReset();
    Require(series.Compute().valid, "requesting a reset does not clear anything");
    Require(series.Compute().count == 1, "the sample is still there");
    Require(series.Generation() == before, "and the generation has not moved");

    Require(series.ApplyPendingReset(), "applying performs the pending reset");
    Require(!series.Compute().valid, "now it is cleared");
    Require(series.Generation() == before + 1, "and the generation advanced");

    Require(!series.ApplyPendingReset(), "a second apply has nothing to do");
    Require(series.Generation() == before + 1, "so the generation does not move again");
}

void TestResetClearsEverything()
{
    IntervalSeries intervals;
    intervals.SetBudgetMicroseconds(1000);
    intervals.Mark(1'000'000'000ull);
    intervals.Mark(1'100'000'000ull);
    Require(intervals.Compute().lifetimeOverBudget == 1, "premise: one over budget");

    intervals.RequestReset();
    Require(intervals.Compute().lifetimeOverBudget == 1, "still pending, nothing cleared");
    Require(intervals.ApplyPendingReset(), "the recording thread applies it");

    Require(!intervals.Compute().duration.valid, "reset clears the samples");
    Require(intervals.Compute().lifetimeOverBudget == 0, "and the over-budget count");
    Require(intervals.BudgetMicroseconds() == 1000, "but keeps the configured budget");

    // The origin is cleared too, so the first mark after a reset does not record
    // the gap across the reset as if it were a frame.
    intervals.Mark(9'000'000'000ull);
    Require(!intervals.Compute().duration.valid, "the first mark after reset is an origin");
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
    TestThresholdCountIsOptIn();
    TestFirstMarkEstablishesTheOriginAndRecordsNothing();
    TestBackwardsClockIsRefused();
    TestWindowAndLifetimePopulationsAreReportedSeparately();
    TestOverBudgetIsAThresholdCountNotADropCount();
    TestASingleHitchHidesFromPercentilesButNotTheCounter();
    TestResetIsDeferredToTheRecordingThread();
    TestResetClearsEverything();
    TestMonotonicClockAdvances();
    std::cout << "frame timing tests passed\n";
    return 0;
}
