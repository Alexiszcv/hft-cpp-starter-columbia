#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>

// One symbol, one-cent ticks; a fixed band centered on the first order.
struct Book {
    void add(uint64_t id, char side, double px, uint32_t qty) {
        const double rounded = std::round(px * 100.0);
        if ((side != 'B' && side != 'S') || !std::isfinite(rounded) ||
            rounded < 1 || rounded > std::numeric_limits<int>::max())
            throw std::invalid_argument("invalid side or price");
        const int tick = static_cast<int>(rounded);
        const int base = based_ ? base_tick_ : std::max(0, tick - levels / 2);
        const int slot = tick - base;
        if (slot < 0 || slot >= levels)
            throw std::out_of_range("price outside the book's fixed band");
        if (qty == 0) { cancel(id); return; }
        cancel(id); // An existing ID is replaced, including its old quantity.
        orders_.emplace(id, Order{side, slot, qty});
        base_tick_ = base;
        based_ = true;
        if (side == 'B') {
            bids_[slot] += qty;
            best_bid_ = std::max(best_bid_, slot);
        } else {
            asks_[slot] += qty;
            if (best_ask_ < 0 || slot < best_ask_) best_ask_ = slot;
        }
    }

    void cancel(uint64_t id) {
        const auto it = orders_.find(id);
        if (it == orders_.end()) return;
        const Order order = it->second;
        orders_.erase(it);
        if (order.side == 'B') {
            bids_[order.slot] -= order.qty;
            if (order.slot == best_bid_ && bids_[order.slot] == 0)
                while (best_bid_ >= 0 && bids_[best_bid_] == 0) --best_bid_;
        } else {
            asks_[order.slot] -= order.qty;
            if (order.slot == best_ask_ && asks_[order.slot] == 0) {
                while (best_ask_ < levels && asks_[best_ask_] == 0) ++best_ask_;
                if (best_ask_ == levels) best_ask_ = -1;
            }
        }
    }

    double best_bid() const { return price(best_bid_); }
    double best_ask() const { return price(best_ask_); }

private:
    static constexpr int levels = 1 << 16;
    struct Order { char side; int slot; uint32_t qty; };
    std::array<uint64_t, levels> bids_{}, asks_{};
    std::unordered_map<uint64_t, Order> orders_; // Used only by add/cancel.
    int base_tick_ = 0, best_bid_ = -1, best_ask_ = -1;
    bool based_ = false;
    double price(int slot) const {
        return slot < 0 ? 0.0 : (base_tick_ + slot) * 0.01;
    }
};

// Fixed-capacity open addressing; symbols are padded to exactly 16 bytes.
struct SymMap {
    void put(const char* sym, uint64_t id) {
        const auto key = encode(sym);
        std::size_t i = key.bucket;
        for (std::size_t n = 0; n < capacity; ++n, i = (i + 1) & (capacity - 1)) {
            auto& slot = slots_[i];
            if (!slot.used || std::memcmp(slot.key.data(), key.bytes.data(), 16) == 0) {
                slot.key = key.bytes;
                slot.id = id;
                slot.used = true;
                return;
            }
        }
        throw std::overflow_error("symbol table is full");
    }

    uint64_t get(const char* sym) const {
        const auto key = encode(sym);
        std::size_t i = key.bucket;
        for (std::size_t n = 0; n < capacity; ++n, i = (i + 1) & (capacity - 1)) {
            const auto& slot = slots_[i];
            if (!slot.used) break;
            if (std::memcmp(slot.key.data(), key.bytes.data(), 16) == 0) return slot.id;
        }
        return static_cast<uint64_t>(-1);
    }

private:
    static constexpr std::size_t capacity = 4096;
    struct Slot { uint64_t id = 0; std::array<char, 16> key{}; bool used = false; };
    struct Key { std::array<char, 16> bytes{}; std::size_t bucket = 0; };
    std::array<Slot, capacity> slots_{};
    static Key encode(const char* sym) {
        if (!sym) throw std::invalid_argument("null symbol");
        Key key;
        uint64_t h = 14695981039346656037ull; // FNV-1a
        for (std::size_t n = 0; sym[n]; ++n) {
            if (n == 16) throw std::length_error("symbol exceeds 16 characters");
            key.bytes[n] = sym[n];
            h = (h ^ static_cast<unsigned char>(sym[n])) * 1099511628211ull;
        }
        key.bucket = h & (capacity - 1);
        return key;
    }
};
