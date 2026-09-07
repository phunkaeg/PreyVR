#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

namespace preyvr {
inline bool FreshSample(std::uint64_t now, std::uint64_t stamp,
                        std::uint64_t maximumAge = 200000000ull) {
    return stamp != 0 && now >= stamp && now - stamp <= maximumAge;
}

inline std::uint64_t MonotonicNanoseconds()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// All payload access is synchronized. A seqlock around ordinary C++ objects
// still has undefined behavior when a reader overlaps a writer. Readers here
// refuse contention instead of waiting on a producer in an engine callback.
template<class T> class LatestSnapshot {
public:
    void Publish(const T& value)
    {
        std::lock_guard lock(mutex_);
        value_ = value;
        present_ = true;
    }
    bool TryRead(T& out) const
    {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || !present_) { return false; }
        out = value_;
        return true;
    }
    void Clear()
    {
        std::lock_guard lock(mutex_);
        present_ = false;
    }
private:
    mutable std::mutex mutex_;
    T value_{};
    bool present_ = false;
};

} // namespace preyvr
