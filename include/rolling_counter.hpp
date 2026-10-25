#pragma once
#include <cstdint>
#include <deque>

// Single-threaded. Events arrive in timestamp order; count's clock never decreases.
struct RollingCounter {
    explicit RollingCounter(uint64_t window_ns) : window_(window_ns) {}

    void add(uint64_t ts_ns) { events_.push_back(ts_ns); }

    uint64_t count(uint64_t now_ns) {
        // Before the window is full, the mathematical cutoff is negative.
        if (now_ns >= window_) {
            const uint64_t cutoff = now_ns - window_;
            while (!events_.empty() && events_.front() <= cutoff)
                events_.pop_front();
        }
        return static_cast<uint64_t>(events_.size());
    }

private:
    uint64_t window_;
    std::deque<uint64_t> events_;
};
