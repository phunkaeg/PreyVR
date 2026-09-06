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

// RenderCHR's own near predicate (R-089), as pure field reads so the **offsets
// and bit positions** can be tested rather than only argued about. The function
// itself performs this at `0x81D127`/`0x81D141` before setting or clearing
// `FOB_NEAREST`, which is what makes it readable at entry and removes the last
// injector dependency from routine testing.
//
// This is the half that can be checked offline. That Prey's structures really
// carry these fields at these offsets is a *static* claim, gated by the landmark
// verifier -- a fixture proves the decoding, never the layout. This project has
// twice mistaken one for the other (an `rsi` byte offset read as a limb id, an
// FOV field read one slot across), so the distinction is load-bearing.
//
// Pointers are raw and unvalidated: the caller is responsible for the fault
// guard, because structured exception handling cannot live in a translation unit
// a portable test links.
inline constexpr unsigned int kRenderFlagsOffset = 0x80;   // in SRendParams
inline constexpr unsigned int kNearestFlagBit = 0x00800000; // FOB_NEAREST, bit 23
inline constexpr unsigned int kSlotFlagsOffset = 0xAC8;    // in the character
inline constexpr unsigned int kSlotNearBit = 0x02;

bool NearestFromRenderArguments(const void* params, const void* character);

// --- the per-frame transform seam -------------------------------------------
//
// **`RenderCHR` copies its matrix argument straight into `CRenderObject+0x00`**
// -- twelve float stores, verified by decompiling `0x81D0D0`. So editing those
// twelve floats at the hook's entry *is* a per-frame transform for that
// character, which is what `SetAttAbsoluteDefault` could never be: that writes
// an attachment default the engine samples at attach time (H-017), and a mount
// written once cannot track a moving controller.
//
// Layout is row-major 3x4 with the **basis vectors as columns** (R-088): X at
// indices 0,4,8; Y at 1,5,9; Z at 2,6,10; translation at 3,7,11.
//
// `offsetMetres` is added to the translation column. `turn` rotates the basis
// columns, which rotates the object about its own origin rather than about the
// world -- the same choice `RotateJointAboutPivot` makes, and for the same
// reason: rotating about the world origin would fling a near object across the
// screen.
//
// False when the matrix is unusable, leaving it untouched; a refusal must not
// half-write a transform the renderer is about to consume.
bool ApplyRenderMatrixOverride(float matrix[12], float offsetX, float offsetY, float offsetZ,
                               float turnX, float turnY, float turnZ, float turnW);

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
