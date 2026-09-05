#pragma once

#include <atomic>

// The capture table behind `RenderFrame`, extracted from the hook so it can be
// tested.
//
// **Why it is its own translation unit.** It used to live inside
// `src/dll/RenderFrame.cpp`, which pulls MinHook and the game module, so no
// threaded test could reach it. The first version of the publication was wrong
// in a way that mattered: `valid=false; memcpy; valid=true` lets a reader
// observe `true`, begin its copy, and then race the writer's *next* `false`. It
// detected nothing and only looked like it did -- and a matrix spliced from two
// frames reads as entirely plausible numbers, so no counter and no wearer would
// have reported it.
//
// The invariant the seqlock buys: **a successful read returns a matrix that was
// published by exactly one call to `Capture`.** Under contention a read fails
// rather than returning a blend of two.
namespace preyvr::renderframe {

inline constexpr unsigned int kSlots = 8;
inline constexpr unsigned int kMatrixFloats = 12;

class MatrixTable {
public:
    // Publishes `matrix` for `character`, allocating a slot on first sight.
    //
    // False when the table is full, when the arguments are unusable, or when
    // this draw was refused in favour of a sample already held -- see the near
    // rule below. A full table stops tracking *new* characters rather than
    // evicting a live one out from under a reader.
    //
    // **A later non-near draw does not replace a near sample.** One character
    // can be drawn more than once per frame; the near draw is the one this
    // project selects, so without the rule the capture would be decided by
    // draw order within the frame.
    bool Capture(unsigned long long character, const float matrix[kMatrixFloats], bool nearest);

    // The most recent complete sample for `character`. False when unseen, or
    // when a writer held the slot for the whole retry budget -- refusing is
    // correct, because a stale frame is recoverable and a spliced one is not.
    bool Read(unsigned long long character, float out[kMatrixFloats], bool* nearest) const;

    // One slot by index, for dumping the table over a text channel. False when
    // that slot has never been written.
    bool SlotAt(unsigned int index, unsigned long long* character, bool* nearest) const;

    unsigned int Tracked() const;

    // Tests only -- there is no live moment at which forgetting every tracked
    // character is the right thing to do.
    void Reset();

private:
    struct Slot {
        std::atomic<unsigned long long> character{0};
        // Odd means a write is in flight; zero means never written, which is why
        // no separate validity flag is needed.
        std::atomic<unsigned long long> sequence{0};
        float matrix[kMatrixFloats]{};
        std::atomic<bool> nearest{false};
    };

    Slot slots_[kSlots];
};

} // namespace preyvr::renderframe
