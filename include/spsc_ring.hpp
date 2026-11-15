#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

// HW10 / Project Phase 3 — single-producer/single-consumer lock-free ring buffer.
struct SPSCRing {
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "SPSCRing requires lock-free size_t atomics");

    explicit SPSCRing(std::size_t capacity_pow2)
        : cap_(capacity_pow2), mask_(capacity_pow2 - 1) {
        if (cap_ == 0 || (cap_ & mask_) != 0)
            throw std::invalid_argument("capacity must be a nonzero power of two");
        buf_.resize(cap_);
    }

    // Exactly one producer calls push; exactly one consumer calls pop.
    bool push(std::uint64_t v) {
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);
        if (t - h == cap_) return false;
        buf_[t & mask_] = v;
        tail_.store(t + 1, std::memory_order_release); // Publish the payload.
        return true;
    }

    bool pop(std::uint64_t& out) {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_acquire);
        if (h == t) return false;
        out = buf_[h & mask_];
        head_.store(h + 1, std::memory_order_release); // Release the slot for reuse.
        return true;
    }

    // Observations may become stale while the other thread advances.
    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    bool full() const {
        return tail_.load(std::memory_order_acquire) -
               head_.load(std::memory_order_acquire) == cap_;
    }

private:
    const std::size_t cap_, mask_;
    std::vector<std::uint64_t> buf_;
    alignas(64) std::atomic<std::size_t> head_{0}; // Consumer writes.
    alignas(64) std::atomic<std::size_t> tail_{0}; // Producer writes.
};
