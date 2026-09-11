#pragma once

#include "preyvr/InputEvent.h"

#include <atomic>
#include <cstdint>

// A bounded queue for input events, so nothing calls the engine's input path
// from whatever thread happened to produce a request.
//
// **The defect this exists to close.** `PostInputEvent` synchronously walks the
// console listeners, the exclusive listener, the input-blocking query and the
// normal listeners, then the action map. It is not an engine-provided
// asynchronous queue. Calling it from the file-polling worker meant a thread the
// engine knows nothing about reentering that walk while the real input update
// could be running -- a race that an SEH guard and a pair of atomic counters
// cannot address, because neither is a thread-ownership contract.
//
// Producers therefore only *enqueue*. A drain on a validated engine thread does
// the calling. Requests that arrive faster than they drain are dropped and
// counted rather than queued without bound: a menu tap that arrives late is
// useless, and unbounded growth would turn a stuck drain into memory pressure.
//
// Multi-producer and single-consumer: the file channel produces today, and an XR
// controller binding will produce alongside it.
namespace preyvr::input {

inline constexpr unsigned int kQueueCapacity = 32;   // power of two, required

class EventQueue {
public:
    EventQueue();

    // Copies `kEventSize` bytes. False when the queue is full or the pointer is
    // null; a false return is a dropped event, counted.
    bool Push(const std::uint8_t* event);
    // Reserve adjacent press/release tickets together; a full queue drops both.
    bool PushPair(const std::uint8_t* events, std::uint64_t menuEpoch = 0);

    // Copies one event out. False when empty.
    bool Pop(std::uint8_t* out, std::uint64_t* menuEpoch = nullptr);

    unsigned long long Dropped() const { return dropped_.load(std::memory_order_relaxed); }
    unsigned long long Pushed() const { return pushed_.load(std::memory_order_relaxed); }
    unsigned long long Popped() const { return popped_.load(std::memory_order_relaxed); }

private:
    struct Cell {
        std::atomic<unsigned long long> sequence{0};
        std::uint8_t data[kEventSize]{};
        std::uint64_t menuEpoch=0; // queue metadata, never native event bytes
    };

    Cell cells_[kQueueCapacity];
    std::atomic<unsigned long long> enqueue_{0};
    std::atomic<unsigned long long> dequeue_{0};
    std::atomic<unsigned long long> dropped_{0};
    std::atomic<unsigned long long> pushed_{0};
    std::atomic<unsigned long long> popped_{0};
};

} // namespace preyvr::input
