#include "preyvr/FrameClock.h"

#include <cstdlib>
#include <iostream>

using namespace preyvr::frameclock;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void TestEyeComesFromPresenterParity()
{
    const Timeline prev{10, 10, 10};
    Require(DecideEye(prev, {11, 11, 10}, 2).eye == 0, "present 10 is eye 0");
    Require(DecideEye(prev, {12, 12, 11}, 2).eye == 1, "present 11 is eye 1");
    Require(DecideEye(prev, {13, 13, 12}, 2).eye == 0, "present 12 is eye 0 again");
}

// **The reason this exists.** A private counter increments once per call; the
// presenter increments once per presented image. When a present is skipped the
// two disagree from then on, and the counter labels every subsequent eye wrongly
// while looking perfectly healthy.
void TestASkippedPresentDoesNotDesyncTheEye()
{
    // Engine and render advance twice while present advances once -- a skip.
    const auto a = DecideEye({10, 10, 10}, {11, 11, 11}, 4);
    const auto b = DecideEye({11, 11, 11}, {13, 13, 12}, 4);
    Require(a.eye == 1, "present 11 is eye 1");
    Require(b.eye == 0, "present 12 is eye 0 -- still correct after the skip");

    // A free-running counter would have advanced twice across that gap and
    // produced eye 1 here. This is precisely the failure it avoids.
    Require(a.eye != b.eye, "consecutive presents alternate");
}

void TestDriftBeyondToleranceRefusesRatherThanGuesses()
{
    const Timeline prev{10, 10, 10};
    const auto ok = DecideEye(prev, {20, 12, 10}, 2);
    Require(ok.eye == 0 && !ok.drifted, "2 frames of render-ahead is within tolerance");

    const auto bad = DecideEye(prev, {20, 15, 10}, 2);
    Require(bad.drifted, "5 frames of render-ahead is drift");
    Require(bad.eye == -1, "and a drifted timeline refuses to name an eye");
    Require(bad.drift == 5, "the drift is reported so a caller can act on it");
}

void TestRenderBehindPresentIsRefused()
{
    const auto d = DecideEye({10, 10, 10}, {11, 10, 12}, 4);
    Require(d.drifted, "render behind present is not a sane pipeline");
    Require(d.eye == -1, "so no eye is named");
    Require(d.drift < 0, "and the sign says which way");
}

void TestBackwardsClocksAreInvalidNotMerelyDrifted()
{
    const Timeline prev{10, 10, 10};
    Require(DecideEye(prev, {9, 10, 10}, 4).invalid, "engine went backwards");
    Require(DecideEye(prev, {10, 9, 10}, 4).invalid, "render went backwards");
    Require(DecideEye(prev, {10, 10, 9}, 4).invalid, "present went backwards");
    // Invalid is a distinct state from drifted: one is a broken clock, the other
    // a recoverable lead. Conflating them would have a caller skip presents
    // forever trying to fix a counter that is simply wrong.
    Require(!DecideEye(prev, {9, 10, 10}, 4).drifted, "not reported as mere drift");
    Require(DecideEye(prev, {9, 10, 10}, 4).eye == -1, "and no eye is named");
}

// Large values must not wrap the signed subtraction into a small positive
// number, which would turn a huge drift into an apparently healthy one.
void TestLargeCountersDoNotWrapTheDrift()
{
    const std::uint64_t big = 0xFFFFFFFFull;
    const auto d = DecideEye({big, big, big}, {big + 10, big + 10, big + 1}, 2);
    Require(d.drifted, "nine frames of lead is still drift at large counters");
    Require(d.drift == 9, "and the drift is the true difference");
}

// --- per-eye temporal history -------------------------------------------

struct FakeCamera {
    int id = 0;
};

void TestHistoryIsPerEyeNotShared()
{
    EyeHistory<FakeCamera> history;
    history.Record(0, FakeCamera{100});
    history.Record(1, FakeCamera{200});

    FakeCamera out{};
    Require(history.Previous(0, out) && out.id == 100, "eye 0 gets its own history");
    Require(history.Previous(1, out) && out.id == 200, "eye 1 gets its own");

    // **The defect this prevents.** With one shared slot, eye 0 would be handed
    // whatever eye 1 last wrote -- a different viewpoint -- and every temporal
    // effect would resolve against it.
    history.Record(1, FakeCamera{201});
    Require(history.Previous(0, out) && out.id == 100,
            "writing eye 1 does not disturb eye 0's history");
}

void TestFirstFrameOfAnEyeHasNoHistory()
{
    EyeHistory<FakeCamera> history;
    FakeCamera out{};
    Require(!history.Previous(0, out), "no history before the first record");
    history.Record(0, FakeCamera{7});
    Require(history.Previous(0, out), "eye 0 now has one");
    // Still nothing for eye 1 -- and the caller must leave the engine's own
    // value alone rather than borrow eye 0's, which is the whole point.
    Require(!history.Previous(1, out), "eye 1 still has none");
    Require(!history.Has(1), "and says so");
}

void TestResetClearsBothEyes()
{
    EyeHistory<FakeCamera> history;
    history.Record(0, FakeCamera{1});
    history.Record(1, FakeCamera{2});
    history.Reset();
    FakeCamera out{};
    Require(!history.Previous(0, out), "recentre clears eye 0");
    Require(!history.Previous(1, out), "and eye 1");
}

void TestOutOfRangeEyesAreIgnored()
{
    EyeHistory<FakeCamera> history;
    history.Record(-1, FakeCamera{5});
    history.Record(2, FakeCamera{6});
    FakeCamera out{};
    Require(!history.Previous(-1, out), "a refused eye stores nothing");
    Require(!history.Previous(2, out), "in either direction");
    Require(!history.Has(0) && !history.Has(1), "and does not spill into a real slot");
}

} // namespace

int main()
{
    TestEyeComesFromPresenterParity();
    TestASkippedPresentDoesNotDesyncTheEye();
    TestDriftBeyondToleranceRefusesRatherThanGuesses();
    TestRenderBehindPresentIsRefused();
    TestBackwardsClocksAreInvalidNotMerelyDrifted();
    TestLargeCountersDoNotWrapTheDrift();
    TestHistoryIsPerEyeNotShared();
    TestFirstFrameOfAnEyeHasNoHistory();
    TestResetClearsBothEyes();
    TestOutOfRangeEyesAreIgnored();
    std::cout << "frame clock tests passed\n";
    return 0;
}
