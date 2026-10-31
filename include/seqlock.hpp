#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

// One writer, any number of readers. T is a fully initialized, trivially
// copyable value with no uninitialized padding (such as the benchmark Quote).
template <class T>
struct SeqLock {
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "This implementation requires lock-free 64-bit atomics");

    SeqLock() { write(T{}); }

    void write(const T& value) {
        std::array<std::uint64_t, words> packed{};
        std::memcpy(packed.data(), &value, sizeof(T));
        const auto version = sequence_.load(std::memory_order_relaxed);
        sequence_.store(version + 1, std::memory_order_relaxed); // odd: writing
        std::atomic_thread_fence(std::memory_order_release);
        for (std::size_t i = 0; i < words; ++i)
            data_[i].store(packed[i], std::memory_order_relaxed);
        sequence_.store(version + 2, std::memory_order_release); // even: published
    }

    // One bounded attempt. On false, out is unchanged and the caller may retry.
    bool read(T& out) const {
        const auto before = sequence_.load(std::memory_order_acquire);
        if (before & 1) return false;
        std::array<std::uint64_t, words> packed;
        for (std::size_t i = 0; i < words; ++i)
            packed[i] = data_[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (sequence_.load(std::memory_order_relaxed) != before) return false;
        std::memcpy(&out, packed.data(), sizeof(T));
        return true;
    }

private:
    static constexpr std::size_t words = (sizeof(T) + sizeof(std::uint64_t) - 1)
                                       / sizeof(std::uint64_t);
    std::atomic<std::uint64_t> sequence_{0};
    std::array<std::atomic<std::uint64_t>, words> data_;
};
