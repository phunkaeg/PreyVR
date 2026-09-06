#include "preyvr/RenderFrameTable.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace preyvr::renderframe {
namespace {

// Bounded, because an unbounded spin inside a render hook would trade a torn
// matrix for a stalled frame. Eight is far more than a memcpy of 48 bytes needs;
// exhausting it means the writer is running continuously, and the honest answer
// there is "no reading", not a guess.
constexpr int kReadAttempts = 8;

} // namespace

bool NearestFromRenderArguments(const void* params, const void* character)
{
    if (params != nullptr) {
        std::uint32_t flags = 0;
        std::memcpy(&flags, static_cast<const std::uint8_t*>(params) + kRenderFlagsOffset,
                    sizeof(flags));
        if ((flags & kNearestFlagBit) != 0u) {
            return true;
        }
    }
    if (character != nullptr) {
        const std::uint8_t slotFlags =
            *(static_cast<const std::uint8_t*>(character) + kSlotFlagsOffset);
        if ((slotFlags & kSlotNearBit) != 0u) {
            return true;
        }
    }
    // Either argument alone can say yes; neither saying yes is a no. The engine
    // *clears* the flag on this path rather than leaving it, so a stale bit on a
    // pooled render object cannot make the answer wrong.
    return false;
}

bool ApplyRenderMatrixOverride(float matrix[12], float offsetX, float offsetY, float offsetZ,
                               float turnX, float turnY, float turnZ, float turnW)
{
    if (matrix == nullptr) {
        return false;
    }
    for (int i = 0; i < 12; ++i) {
        if (!std::isfinite(matrix[i])) {
            return false;
        }
    }
    if (!std::isfinite(offsetX) || !std::isfinite(offsetY) || !std::isfinite(offsetZ) ||
        !std::isfinite(turnX) || !std::isfinite(turnY) || !std::isfinite(turnZ) ||
        !std::isfinite(turnW)) {
        return false;
    }
    const float norm = std::sqrt(turnX*turnX + turnY*turnY + turnZ*turnZ + turnW*turnW);
    if (norm < 1.0e-6f) {
        return false;   // a degenerate rotation would collapse the basis
    }
    const float qx = turnX / norm, qy = turnY / norm, qz = turnZ / norm, qw = turnW / norm;

    // Rotate each basis column. Written out rather than routed through a matrix
    // multiply so the column convention stays visible: getting it wrong here
    // transposes the object and reads as a mirrored model.
    const auto rotate = [&](float x, float y, float z, float& ox, float& oy, float& oz) {
        const float tx = 2.0f * (qy * z - qz * y);
        const float ty = 2.0f * (qz * x - qx * z);
        const float tz = 2.0f * (qx * y - qy * x);
        ox = x + qw * tx + (qy * tz - qz * ty);
        oy = y + qw * ty + (qz * tx - qx * tz);
        oz = z + qw * tz + (qx * ty - qy * tx);
    };

    float out[12];
    for (int col = 0; col < 3; ++col) {
        rotate(matrix[col], matrix[col + 4], matrix[col + 8],
               out[col], out[col + 4], out[col + 8]);
    }
    // Translation is not rotated: the object turns about its own origin, so its
    // placement in the world is unchanged by `turn` and moved only by the offset.
    out[3] = matrix[3] + offsetX;
    out[7] = matrix[7] + offsetY;
    out[11] = matrix[11] + offsetZ;

    for (int i = 0; i < 12; ++i) {
        if (!std::isfinite(out[i])) {
            return false;   // refuse whole rather than write a partial transform
        }
    }
    std::memcpy(matrix, out, sizeof(out));
    return true;
}

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
