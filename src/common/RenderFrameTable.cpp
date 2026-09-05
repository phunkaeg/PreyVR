#include "preyvr/RenderFrameTable.h"

#include <cstring>

namespace preyvr::renderframe {
namespace {

// Bounded, because an unbounded spin inside a render hook would trade a torn
// matrix for a stalled frame. Eight is far more than a memcpy of 48 bytes needs;
// exhausting it means the writer is running continuously, and the honest answer
// there is "no reading", not a guess.
constexpr int kReadAttempts = 8;

} // namespace

bool MatrixTable::Capture(unsigned long long character, const float matrix[kMatrixFloats],
                          bool nearest)
{
    // Zero is the free-slot sentinel, so it cannot also be a tracked key.
    if (character == 0 || matrix == nullptr) {
        return false;
    }

    int chosen = -1;
    for (unsigned int i = 0; i < kSlots; ++i) {
        if (slots_[i].character.load(std::memory_order_relaxed) == character) {
            chosen = static_cast<int>(i);
            break;
        }
    }
    if (chosen < 0) {
        for (unsigned int i = 0; i < kSlots; ++i) {
            unsigned long long expected = 0;
            if (slots_[i].character.compare_exchange_strong(expected, character,
                                                            std::memory_order_acq_rel)) {
                chosen = static_cast<int>(i);
                break;
            }
        }
    }
    if (chosen < 0) {
        return false;
    }

    Slot& slot = slots_[static_cast<unsigned int>(chosen)];
    if (!nearest && slot.nearest.load(std::memory_order_acquire)) {
        return false;
    }

    const auto sequence = slot.sequence.load(std::memory_order_relaxed);
    slot.sequence.store(sequence + 1, std::memory_order_release);   // odd: writing
    std::memcpy(slot.matrix, matrix, sizeof(slot.matrix));
    slot.nearest.store(nearest, std::memory_order_relaxed);
    slot.sequence.store(sequence + 2, std::memory_order_release);   // even: settled
    return true;
}

bool MatrixTable::Read(unsigned long long character, float out[kMatrixFloats],
                       bool* nearest) const
{
    if (character == 0 || out == nullptr) {
        return false;
    }
    for (unsigned int i = 0; i < kSlots; ++i) {
        const Slot& slot = slots_[i];
        if (slot.character.load(std::memory_order_acquire) != character) {
            continue;
        }
        for (int attempt = 0; attempt < kReadAttempts; ++attempt) {
            const auto before = slot.sequence.load(std::memory_order_acquire);
            if (before == 0ull) {
                return false;               // never written
            }
            if ((before & 1ull) != 0ull) {
                continue;                   // writer mid-copy
            }
            float copy[kMatrixFloats];
            std::memcpy(copy, slot.matrix, sizeof(copy));
            const bool isNear = slot.nearest.load(std::memory_order_relaxed);
            // The second read of the sequence must not be hoisted above the
            // copy, or the check would be answering a question about the wrong
            // moment.
            std::atomic_thread_fence(std::memory_order_acquire);
            if (slot.sequence.load(std::memory_order_acquire) == before) {
                std::memcpy(out, copy, sizeof(copy));
                if (nearest != nullptr) {
                    *nearest = isNear;
                }
                return true;
            }
        }
        return false;
    }
    return false;
}

bool MatrixTable::SlotAt(unsigned int index, unsigned long long* character, bool* nearest) const
{
    if (index >= kSlots) {
        return false;
    }
    const Slot& slot = slots_[index];
    if (slot.sequence.load(std::memory_order_acquire) == 0ull) {
        return false;
    }
    if (character != nullptr) {
        *character = slot.character.load(std::memory_order_acquire);
    }
    if (nearest != nullptr) {
        *nearest = slot.nearest.load(std::memory_order_acquire);
    }
    return true;
}

unsigned int MatrixTable::Tracked() const
{
    unsigned int count = 0;
    for (unsigned int i = 0; i < kSlots; ++i) {
        if (slots_[i].sequence.load(std::memory_order_acquire) != 0ull) {
            ++count;
        }
    }
    return count;
}

void MatrixTable::Reset()
{
    for (unsigned int i = 0; i < kSlots; ++i) {
        slots_[i].character.store(0, std::memory_order_relaxed);
        slots_[i].sequence.store(0, std::memory_order_relaxed);
        slots_[i].nearest.store(false, std::memory_order_relaxed);
        std::memset(slots_[i].matrix, 0, sizeof(slots_[i].matrix));
    }
}

} // namespace preyvr::renderframe
