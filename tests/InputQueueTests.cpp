#include "preyvr/InputQueue.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using namespace preyvr::input;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Every byte of an event carries the same stamp, so an event assembled from two
// different pushes shows up as bytes that disagree -- the same detector shape the
// render-matrix seqlock uses, for the same reason: plausible-looking corruption
// is the kind nothing else catches.
void Stamp(std::uint8_t* event, std::uint8_t value)
{
    std::memset(event, value, kEventSize);
}

bool Coherent(const std::uint8_t* event)
{
    for (std::size_t i = 1; i < kEventSize; ++i) {
        if (event[i] != event[0]) {
            return false;
        }
    }
    return true;
}

void TestEmptyAndRoundTrip()
{
    EventQueue queue;
    std::uint8_t out[kEventSize]{};
    Require(!queue.Pop(out), "an empty queue must pop nothing");
    Require(!queue.Push(nullptr), "a null event must be refused");
    Require(queue.Dropped() == 1, "and counted as dropped rather than ignored");

    std::uint8_t event[kEventSize];
    Stamp(event, 0x5A);
    Require(queue.Push(event), "a push into space must succeed");
    Require(queue.Pop(out), "and must come back out");
    Require(std::memcmp(out, event, kEventSize) == 0, "byte for byte");
    Require(!queue.Pop(out), "and only once");
}

void TestOrderIsPreserved()
{
    // A press followed by its release must not arrive reversed, which would
    // leave the engine believing a button went up before it went down.
    EventQueue queue;
    for (std::uint8_t i = 1; i <= 8; ++i) {
        std::uint8_t event[kEventSize];
        Stamp(event, i);
        Require(queue.Push(event), "push");
    }
    for (std::uint8_t i = 1; i <= 8; ++i) {
        std::uint8_t out[kEventSize]{};
        Require(queue.Pop(out), "pop");
        Require(out[0] == i, "events must leave in the order they arrived");
    }
}

void TestFullQueueDropsAndCounts()
{
    EventQueue queue;
    std::uint8_t event[kEventSize];
    Stamp(event, 0x11);
    for (unsigned int i = 0; i < kQueueCapacity; ++i) {
        Require(queue.Push(event), "the queue must accept up to its capacity");
    }
    // **Dropped, not grown.** An input that arrives late is useless, and
    // unbounded growth would turn a stalled drain into memory pressure instead
    // of a number someone can read.
    Require(!queue.Push(event), "a full queue must refuse");
    Require(queue.Dropped() == 1, "and count the drop");

    std::uint8_t out[kEventSize]{};
    Require(queue.Pop(out), "draining one must free a slot");
    Require(queue.Push(event), "which the next push may use");
}

// A producer on another thread is the whole point: the file channel and, later,
// an XR controller binding both produce while the engine thread drains.
void TestConcurrentProducersNeverTearAnEvent()
{
    EventQueue queue;
    constexpr int kProducers = 3;
    constexpr int kPerProducer = 4000;

    std::atomic<bool> producing{true};
    std::atomic<long long> torn{0};
    std::atomic<long long> drained{0};

    std::thread consumer([&] {
        std::uint8_t out[kEventSize];
        while (producing.load(std::memory_order_acquire) || true) {
            if (queue.Pop(out)) {
                if (!Coherent(out)) {
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
                drained.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (!producing.load(std::memory_order_acquire)) {
                break;   // drained dry after the producers stopped
            }
        }
    });

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            std::uint8_t event[kEventSize];
            Stamp(event, static_cast<std::uint8_t>(p + 1));
            for (int i = 0; i < kPerProducer; ++i) {
                queue.Push(event);   // a drop under contention is legitimate
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    producing.store(false, std::memory_order_release);
    consumer.join();

    Require(drained.load() > 0, "the consumer must have seen traffic to judge");
    Require(torn.load() == 0, "no event may be assembled from two different pushes");
    Require(queue.Pushed() == static_cast<unsigned long long>(drained.load()),
            "every accepted push must be drained exactly once");
    Require(queue.Pushed() + queue.Dropped() ==
                static_cast<unsigned long long>(kProducers) * kPerProducer,
            "every request must be either accepted or counted as dropped");

    std::cout << "  queue: " << queue.Pushed() << " pushed, " << queue.Dropped()
              << " dropped, " << drained.load() << " drained, 0 torn\n";
}

} // namespace

int main()
{
    TestEmptyAndRoundTrip();
    TestOrderIsPreserved();
    TestFullQueueDropsAndCounts();
    TestConcurrentProducersNeverTearAnEvent();
    std::cout << "input queue tests passed\n";
    return 0;
}
