#include "preyvr/RenderFrameTable.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace {

using namespace preyvr::renderframe;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void Fill(float (&matrix)[kMatrixFloats], float value)
{
    for (unsigned int i = 0; i < kMatrixFloats; ++i) {
        matrix[i] = value;
    }
}

// --- the contract, stated deterministically ---------------------------------
//
// These carry the weight. A concurrency run can only ever say "no splice was
// observed"; these say what the thing is supposed to do.

void TestUnseenCharacterReadsFalse()
{
    MatrixTable table;
    float out[kMatrixFloats]{};
    Require(!table.Read(0x1000, out, nullptr), "an unseen character must not read");
    Require(table.Tracked() == 0, "an untouched table tracks nothing");
    // Zero is the free-slot sentinel and so cannot be a key. Silently accepting
    // it would make a null character alias every empty slot.
    float matrix[kMatrixFloats]{};
    Fill(matrix, 1.0f);
    Require(!table.Capture(0, matrix, true), "the zero key must be refused");
}

void TestRoundTrip()
{
    MatrixTable table;
    float matrix[kMatrixFloats]{};
    for (unsigned int i = 0; i < kMatrixFloats; ++i) {
        matrix[i] = static_cast<float>(i) * 1.5f;
    }
    Require(table.Capture(0x2000, matrix, true), "a first capture must publish");

    float out[kMatrixFloats]{};
    bool nearest = false;
    Require(table.Read(0x2000, out, &nearest), "a published sample must read back");
    Require(nearest, "the near flag must survive the round trip");
    for (unsigned int i = 0; i < kMatrixFloats; ++i) {
        Require(out[i] == matrix[i], "every element must round trip exactly");
    }
    Require(table.Tracked() == 1, "one character captured is one tracked");
}

void TestNearSampleSurvivesALaterNonNearDraw()
{
    MatrixTable table;
    float near[kMatrixFloats]{};
    Fill(near, 7.0f);
    float far[kMatrixFloats]{};
    Fill(far, 9.0f);

    Require(table.Capture(0x3000, near, true), "the near draw publishes");
    Require(!table.Capture(0x3000, far, false),
            "a later non-near draw must be refused, not silently accepted");

    float out[kMatrixFloats]{};
    bool nearest = false;
    Require(table.Read(0x3000, out, &nearest), "the sample must still be readable");
    Require(nearest, "the surviving sample must still be flagged near");
    Require(out[0] == 7.0f, "the near sample must survive, not the last draw of the frame");

    // The rule is directional. A *near* draw must still replace a near sample,
    // or the capture would freeze on the first frame and quietly stop tracking.
    float newer[kMatrixFloats]{};
    Fill(newer, 11.0f);
    Require(table.Capture(0x3000, newer, true), "a newer near draw must replace");
    Require(table.Read(0x3000, out, nullptr) && out[0] == 11.0f, "the newer near sample wins");

    // And with no near sample held, an ordinary draw is free to update.
    MatrixTable plain;
    Require(plain.Capture(0x3001, far, false), "a non-near draw publishes when nothing is held");
    Require(plain.Capture(0x3001, newer, false), "and a later non-near draw replaces it");
    Require(plain.Read(0x3001, out, nullptr) && out[0] == 11.0f, "the later sample wins");
}

void TestFullTableStopsTrackingRatherThanEvicting()
{
    MatrixTable table;
    float matrix[kMatrixFloats]{};
    for (unsigned int i = 0; i < kSlots; ++i) {
        Fill(matrix, static_cast<float>(i + 1));
        Require(table.Capture(0x4000 + i, matrix, true), "each of the slots must accept");
    }
    Require(table.Tracked() == kSlots, "a full table tracks every slot");

    Fill(matrix, 99.0f);
    Require(!table.Capture(0x5000, matrix, true), "a full table must refuse a new character");

    // The point of refusing: nothing already tracked was thrown away to make
    // room, so no reader lost its subject mid-frame.
    float out[kMatrixFloats]{};
    for (unsigned int i = 0; i < kSlots; ++i) {
        Require(table.Read(0x4000 + i, out, nullptr), "an existing character must survive");
        Require(out[0] == static_cast<float>(i + 1), "and must still hold its own sample");
    }
    Require(!table.Read(0x5000, out, nullptr), "the refused character must not read");
}

void TestSlotEnumerationAndReset()
{
    MatrixTable table;
    float matrix[kMatrixFloats]{};
    Fill(matrix, 3.0f);

    unsigned long long character = 0;
    bool nearest = false;
    Require(!table.SlotAt(0, &character, &nearest), "an unwritten slot reports nothing");
    Require(!table.SlotAt(kSlots, &character, &nearest), "an out-of-range index is refused");

    Require(table.Capture(0x6000, matrix, false), "capture for enumeration");
    Require(table.SlotAt(0, &character, &nearest), "a written slot reports");
    Require(character == 0x6000, "the slot names its character");
    Require(!nearest, "and reports the near verdict it was given");

    table.Reset();
    Require(table.Tracked() == 0, "reset forgets every character");
    float out[kMatrixFloats]{};
    Require(!table.Read(0x6000, out, nullptr), "and a reset character no longer reads");
}

// --- the near predicate, at the offsets it claims -----------------------------
//
// A fixture proves the *decoding*: that this code reads the field it says it
// reads, at the offset and bit it says. It cannot prove Prey's structures carry
// those fields there -- that is the landmark verifier's job. Both of this
// project's worst RE errors were a correct-looking decode of the wrong bytes, so
// the two are kept separate on purpose.

void TestNearPredicateReadsTheFieldsItClaims()
{
    unsigned char params[0x100]{};
    unsigned char character[0xB00]{};

    const auto setRenderFlags = [&params](std::uint32_t value) {
        std::memcpy(params + 0x80, &value, sizeof(value));
    };

    Require(!NearestFromRenderArguments(params, character), "cleared flags are not near");

    // FOB_NEAREST is bit 23 of the u32 at params+0x80.
    setRenderFlags(0x00800000u);
    Require(NearestFromRenderArguments(params, character), "the render flag alone says near");

    // A neighbouring bit must not be mistaken for it. Off by one in a bit index
    // is invisible in a decompiler listing and total in effect.
    setRenderFlags(0x00400000u);
    Require(!NearestFromRenderArguments(params, character), "bit 22 is not FOB_NEAREST");
    setRenderFlags(0x01000000u);
    Require(!NearestFromRenderArguments(params, character), "bit 24 is not FOB_NEAREST");
    setRenderFlags(0u);

    // The entity slot near bit is 0x02 at character+0xAC8.
    character[0xAC8] = 0x02;
    Require(NearestFromRenderArguments(params, character), "the slot flag alone says near");
    character[0xAC8] = 0x01;
    Require(!NearestFromRenderArguments(params, character), "bit 0 is not the near bit");
    character[0xAC8] = 0x04;
    Require(!NearestFromRenderArguments(params, character), "bit 2 is not the near bit");
    character[0xAC8] = 0x00;

    // Neighbouring bytes must not be read by mistake. An off-by-one on a struct
    // offset is exactly the class of error that cost this project two findings,
    // and it replicated cleanly both times before anyone noticed.
    character[0xAC7] = 0xFF;
    character[0xAC9] = 0xFF;
    Require(!NearestFromRenderArguments(params, character), "adjacent bytes must not be read");
    character[0xAC7] = 0x00;
    character[0xAC9] = 0x00;

    // Either argument alone can answer, and either may be absent.
    character[0xAC8] = 0x02;
    Require(NearestFromRenderArguments(nullptr, character), "a null params still allows a verdict");
    character[0xAC8] = 0x00;
    setRenderFlags(0x00800000u);
    Require(NearestFromRenderArguments(params, nullptr), "a null character still allows a verdict");
    Require(!NearestFromRenderArguments(nullptr, nullptr), "with nothing to read, not near");
}

// --- the splice detector ----------------------------------------------------
//
// Every element of a published matrix carries the same generation stamp, so a
// sample assembled from two different writes shows up as a matrix whose elements
// disagree. That is the exact failure the old publication allowed and that no
// counter could see: the numbers stay entirely plausible.

struct StressResult {
    long long reads = 0;
    long long splices = 0;
    long long refusals = 0;
};

template <typename Table>
StressResult RunStress(Table& table, int generations, int readerCount)
{
    constexpr unsigned long long kKey = 0xABCD;
    std::atomic<bool> running{true};
    std::atomic<long long> reads{0};
    std::atomic<long long> splices{0};
    std::atomic<long long> refusals{0};

    std::vector<std::thread> readers;
    readers.reserve(static_cast<std::size_t>(readerCount));
    for (int r = 0; r < readerCount; ++r) {
        readers.emplace_back([&] {
            while (running.load(std::memory_order_acquire)) {
                float out[kMatrixFloats]{};
                if (!table.Read(kKey, out, nullptr)) {
                    refusals.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                reads.fetch_add(1, std::memory_order_relaxed);
                bool coherent = true;
                for (unsigned int i = 1; i < kMatrixFloats; ++i) {
                    if (out[i] != out[0]) {
                        coherent = false;
                        break;
                    }
                }
                if (!coherent) {
                    splices.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (int generation = 1; generation <= generations; ++generation) {
        float matrix[kMatrixFloats]{};
        Fill(matrix, static_cast<float>(generation));
        table.Capture(kKey, matrix, true);
    }

    running.store(false, std::memory_order_release);
    for (auto& reader : readers) {
        reader.join();
    }

    StressResult result;
    result.reads = reads.load();
    result.splices = splices.load();
    result.refusals = refusals.load();
    return result;
}

// The publication this replaced, kept only so the detector above can be shown to
// work. The pause is what makes its window observable on demand rather than
// occasionally -- the real table gets no such help, so this is a demonstration
// that the check fires, not a like-for-like race comparison.
class UnsafePublicationTable {
public:
    bool Capture(unsigned long long, const float matrix[kMatrixFloats], bool)
    {
        valid_.store(false, std::memory_order_release);
        std::memcpy(matrix_, matrix, sizeof(float) * 6);
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        std::memcpy(matrix_ + 6, matrix + 6, sizeof(float) * 6);
        valid_.store(true, std::memory_order_release);
        return true;
    }

    bool Read(unsigned long long, float out[kMatrixFloats], bool*) const
    {
        if (!valid_.load(std::memory_order_acquire)) {
            return false;
        }
        std::memcpy(out, matrix_, sizeof(matrix_));
        return true;
    }

private:
    std::atomic<bool> valid_{false};
    float matrix_[kMatrixFloats]{};
};

void TestDetectorActuallyDetects()
{
    UnsafePublicationTable unsafe;
    const StressResult result = RunStress(unsafe, 400, 3);
    Require(result.reads > 0, "the detector must have read something to judge");
    // If this ever fails, the concurrent test below is proving nothing and must
    // not be trusted.
    Require(result.splices > 0,
            "the old publication must be caught splicing, or the detector is blind");
    std::cout << "  detector self-check: " << result.splices << " splice(s) in "
              << result.reads << " read(s) of the old publication\n";
}

void TestSeqlockNeverSplices()
{
    MatrixTable table;
    const StressResult result = RunStress(table, 200000, 3);
    Require(result.reads > 0, "the readers must have observed the writer at all");
    Require(result.splices == 0, "a successful read must never blend two publications");
    std::cout << "  seqlock: 0 splices in " << result.reads << " read(s), "
              << result.refusals << " refusal(s)\n";
}


// --- the per-frame transform override ----------------------------------------

void Identity(float m[12])
{
    for (int i = 0; i < 12; ++i) { m[i] = 0.0f; }
    m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f;
}

void TestOffsetLandsOnTheTranslationColumn()
{
    float m[12];
    Identity(m);
    Require(ApplyRenderMatrixOverride(m, 0.25f, -0.5f, 1.5f, 0, 0, 0, 1), "identity turn, offset only");
    // Translation is the COLUMN at 3,7,11. Writing 0,1,2 instead would scale and
    // shear the object rather than move it, which reads as a corrupted model.
    Require(m[3] == 0.25f && m[7] == -0.5f && m[11] == 1.5f, "offset goes to indices 3,7,11");
    Require(m[0] == 1.0f && m[5] == 1.0f && m[10] == 1.0f, "the basis is untouched by an offset");
}

void TestTurnRotatesTheBasisNotTheTranslation()
{
    float m[12];
    Identity(m);
    m[3] = 5.0f; m[7] = 6.0f; m[11] = 7.0f;
    // 90 degrees about Z: X column goes to +Y.
    const float s = std::sin(3.14159265f * 0.25f), c = std::cos(3.14159265f * 0.25f);
    Require(ApplyRenderMatrixOverride(m, 0, 0, 0, 0, 0, s, c), "quarter turn about Z");

    Require(std::fabs(m[0]) < 1e-4f && std::fabs(m[4] - 1.0f) < 1e-4f,
            "the X column must rotate into Y");
    // **The object turns about its own origin.** Rotating the translation too
    // would fling a near object across the screen, since its translation is
    // camera-relative and large.
    Require(m[3] == 5.0f && m[7] == 6.0f && m[11] == 7.0f,
            "the translation must not be rotated");
}

void TestBasisStaysOrthonormalUnderRotation()
{
    float m[12];
    Identity(m);
    const float s = std::sin(0.4f), c = std::cos(0.4f);
    Require(ApplyRenderMatrixOverride(m, 0, 0, 0, 0.3f * s, s, 0.1f * s, c), "arbitrary turn");
    const auto col = [&](int i, float v[3]) { v[0] = m[i]; v[1] = m[i+4]; v[2] = m[i+8]; };
    float x[3], y[3], z[3];
    col(0, x); col(1, y); col(2, z);
    const auto dot = [](const float a[3], const float b[3]) {
        return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
    };
    // A rotation that quietly scales would change every distance the renderer
    // draws, and an inverse-by-transpose elsewhere would then be wrong.
    Require(std::fabs(dot(x, x) - 1.0f) < 1e-3f, "X stays unit length");
    Require(std::fabs(dot(y, y) - 1.0f) < 1e-3f, "Y stays unit length");
    Require(std::fabs(dot(x, y)) < 1e-3f, "the columns stay perpendicular");
    Require(std::fabs(dot(x, z)) < 1e-3f, "and so do X and Z");
}

void TestRefusalsLeaveTheMatrixUntouched()
{
    float m[12];
    Identity(m);
    m[3] = 4.0f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Require(!ApplyRenderMatrixOverride(m, nan, 0, 0, 0, 0, 0, 1), "a NaN offset is refused");
    Require(m[3] == 4.0f, "and the matrix is left exactly as it was");
    Require(!ApplyRenderMatrixOverride(m, 0, 0, 0, 0, 0, 0, 0), "a zero quaternion is refused");
    Require(m[3] == 4.0f, "still untouched");
    m[5] = nan;
    Require(!ApplyRenderMatrixOverride(m, 1, 0, 0, 0, 0, 0, 1),
            "an already-corrupt matrix is refused rather than propagated");
    Require(!ApplyRenderMatrixOverride(nullptr, 0, 0, 0, 0, 0, 0, 1), "null is refused");
}

} // namespace

int main()
{
    TestUnseenCharacterReadsFalse();
    TestRoundTrip();
    TestNearSampleSurvivesALaterNonNearDraw();
    TestFullTableStopsTrackingRatherThanEvicting();
    TestSlotEnumerationAndReset();
    TestNearPredicateReadsTheFieldsItClaims();
    TestOffsetLandsOnTheTranslationColumn();
    TestTurnRotatesTheBasisNotTheTranslation();
    TestBasisStaysOrthonormalUnderRotation();
    TestRefusalsLeaveTheMatrixUntouched();
    TestDetectorActuallyDetects();
    TestSeqlockNeverSplices();
    std::cout << "render frame table tests passed\n";
    return 0;
}
