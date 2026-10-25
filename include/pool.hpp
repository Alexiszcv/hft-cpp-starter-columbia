#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>

// Single-threaded, fixed-size storage. Objects must fit in obj_size and have
// alignment <= alignof(std::max_align_t). Destroy objects before free().
struct Pool {
    explicit Pool(std::size_t obj_size, std::size_t capacity) {
        constexpr std::size_t alignment = alignof(std::max_align_t);
        const std::size_t size = std::max(obj_size, sizeof(Node));
        const std::size_t limit = std::numeric_limits<std::size_t>::max();
        if (size > limit - (alignment - 1)) throw std::bad_array_new_length();
        const std::size_t stride = (size + alignment - 1) / alignment * alignment;
        if (capacity > limit / stride) throw std::bad_array_new_length();
        if (capacity == 0) return;

        // A byte array provides raw storage, aligned for fundamental types.
        buffer_.reset(new std::byte[stride * capacity]);
        for (std::size_t i = capacity; i > 0; --i)
            head_ = ::new (buffer_.get() + (i - 1) * stride) Node{head_};
    }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    void* alloc() noexcept {
        if (!head_) return nullptr;
        Node* slot = head_;
        head_ = slot->next;
        slot->~Node();
        return slot;
    }

    // p must be nullptr or a currently allocated slot from this pool.
    // Foreign pointers and double frees are caller errors.
    void free(void* p) noexcept {
        if (p) head_ = ::new (p) Node{head_};
    }

private:
    struct Node { Node* next; }; // Stored inside each FREE slot.
    std::unique_ptr<std::byte[]> buffer_;
    Node* head_ = nullptr;
};
