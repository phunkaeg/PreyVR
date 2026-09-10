#include "preyvr/InputQueue.h"

#include <cstring>

namespace preyvr::input {
namespace {

constexpr unsigned long long kMask = kQueueCapacity - 1;
static_assert((kQueueCapacity & kMask) == 0, "capacity must be a power of two");

} // namespace

EventQueue::EventQueue()
{
    // Each cell starts stamped with its own index. A cell is writable when its
    // sequence equals the ticket, and readable when it is one past it -- which is
    // what lets a producer and the consumer decide, without a lock, whether a
    // slot is theirs yet.
    for (unsigned int i = 0; i < kQueueCapacity; ++i) {
        cells_[i].sequence.store(i, std::memory_order_relaxed);
    }
}

bool EventQueue::Push(const std::uint8_t* event)
{
    if (event == nullptr) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    Cell* cell = nullptr;
    unsigned long long position = enqueue_.load(std::memory_order_relaxed);
    for (;;) {
        cell = &cells_[position & kMask];
        const unsigned long long sequence = cell->sequence.load(std::memory_order_acquire);
        const long long difference =
            static_cast<long long>(sequence) - static_cast<long long>(position);
        if (difference == 0) {
            if (enqueue_.compare_exchange_weak(position, position + 1,
                                               std::memory_order_relaxed)) {
                break;
            }
        } else if (difference < 0) {
            // Full. Dropped and counted rather than queued without bound: an
            // input that arrives late is useless, and growth would turn a stuck
            // drain into memory pressure instead of a visible number.
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        } else {
            position = enqueue_.load(std::memory_order_relaxed);
        }
    }
    std::memcpy(cell->data, event, kEventSize);
    cell->sequence.store(position + 1, std::memory_order_release);
    pushed_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool EventQueue::PushPair(const std::uint8_t* events)
{
    if (!events) { dropped_.fetch_add(2); return false; }
    auto position=enqueue_.load(std::memory_order_relaxed);
    for (;;) {
        const auto a=cells_[position & kMask].sequence.load(std::memory_order_acquire);
        const auto b=cells_[(position+1) & kMask].sequence.load(std::memory_order_acquire);
        if (a==position && b==position+1) {
            if (enqueue_.compare_exchange_weak(position,position+2,std::memory_order_relaxed)) break;
        } else if (static_cast<long long>(a)-static_cast<long long>(position)<0 ||
                   static_cast<long long>(b)-static_cast<long long>(position+1)<0) {
            dropped_.fetch_add(2); return false;
        } else { position=enqueue_.load(std::memory_order_relaxed); }
    }
    auto& first=cells_[position & kMask];
    auto& second=cells_[(position+1) & kMask];
    std::memcpy(first.data,events,kEventSize);
    std::memcpy(second.data,events+kEventSize,kEventSize);
    second.sequence.store(position+2,std::memory_order_release);
    first.sequence.store(position+1,std::memory_order_release);
    pushed_.fetch_add(2,std::memory_order_relaxed);
    return true;
}

bool EventQueue::Pop(std::uint8_t* out)
{
    if (out == nullptr) {
        return false;
    }
    Cell* cell = nullptr;
    unsigned long long position = dequeue_.load(std::memory_order_relaxed);
    for (;;) {
        cell = &cells_[position & kMask];
        const unsigned long long sequence = cell->sequence.load(std::memory_order_acquire);
        const long long difference =
            static_cast<long long>(sequence) - static_cast<long long>(position + 1);
        if (difference == 0) {
            if (dequeue_.compare_exchange_weak(position, position + 1,
                                               std::memory_order_relaxed)) {
                break;
            }
        } else if (difference < 0) {
            return false;   // empty
        } else {
            position = dequeue_.load(std::memory_order_relaxed);
        }
    }
    std::memcpy(out, cell->data, kEventSize);
    cell->sequence.store(position + kQueueCapacity, std::memory_order_release);
    popped_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

} // namespace preyvr::input
