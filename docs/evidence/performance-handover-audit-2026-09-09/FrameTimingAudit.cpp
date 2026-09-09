#include "preyvr/FrameTiming.h"

#include <cstdlib>
#include <iostream>

using namespace preyvr::timing;

void Require(bool value, const char* why)
{
    if (!value) { std::cerr << "FAIL " << why << '\n'; std::exit(1); }
}

int main()
{
    IntervalSeries samples;
    samples.SetDeadlineMicroseconds(11111);
    std::uint64_t now = 1'000'000'000;
    samples.Mark(now);
    Require(!samples.Compute().valid, "origin alone must not produce data");

    // A slow prefix followed by an entirely fast final window. A report that
    // pairs these percentiles with a lifetime miss count mixes populations.
    for (int i = 0; i < 2000; ++i) { now += 20'000'000; samples.Mark(now); }
    for (int i = 0; i < 512; ++i) { now += 10'000'000; samples.Mark(now); }
    const auto stats = samples.Compute();
    Require(stats.count == 512 && stats.total == 2512, "window versus lifetime");
    Require(stats.p50 == 10000 && stats.max == 10000, "only fast tail retained");
    Require(samples.MissedDeadlineCount() == 2000, "slow prefix still in misses");
    std::cout << "mixed_window: n=" << stats.count << " total=" << stats.total
              << " p50_us=" << stats.p50 << " max_us=" << stats.max
              << " lifetime_over_budget=" << samples.MissedDeadlineCount()
              << " window_over_budget=0 window_seconds=5.12\n";

    samples.Reset();
    Require(!samples.Compute().valid && samples.MissedDeadlineCount() == 0,
            "quiescent reset clears both populations");
    samples.Mark(now);
    for (int i = 0; i < 1000; ++i) {
        now += (i % 2 == 0) ? 11'110'000 : 11'112'000;
        samples.Mark(now);
    }
    Require(samples.MissedDeadlineCount() == 500, "threshold counts interval jitter");
    std::cout << "threshold_control: intervals=1000 configured_budget_us=11111"
                 " alternating_us=11110,11112 over_budget=500"
                 " compositor_drops=NOT_MEASURED\n";
    std::cout << "PASS: offline instrument semantics only; no game/GPU benchmark\n";
}
