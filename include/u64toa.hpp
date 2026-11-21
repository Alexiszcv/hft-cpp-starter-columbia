#pragma once
#include <cstdint>

namespace u64toa_detail {
// Week 12 lab, step 4: one lookup emits a pair of decimal digits.
inline constexpr char digits[201] =
    "00010203040506070809101112131415161718192021222324"
    "25262728293031323334353637383940414243444546474849"
    "50515253545556575859606162636465666768697071727374"
    "75767778798081828384858687888990919293949596979899";

// Emit a leading block, with 0 <= v < 10^8, without unnecessary leading zeros.
inline int leading_block(std::uint32_t v, char* out) {
    const char* pairs = digits;
    if (v < 10) {                     // includes zero, which must emit one digit
        out[0] = static_cast<char>('0' + v);
        return 1;
    }
    if (v < 100) {
        const unsigned index = 2 * static_cast<unsigned>(v);
        out[0] = pairs[index];
        out[1] = pairs[index + 1];
        return 2;
    }

    const int length = v < 10000 ? 3 + (v >= 1000)
                     : v < 1000000 ? 5 + (v >= 100000)
                     : 7 + (v >= 10000000);
    char* p = out + length;
    while (v >= 100) {
        const unsigned remainder = static_cast<unsigned>(v % 100);
        v /= 100;
        p -= 2;
        p[0] = pairs[2 * remainder];
        p[1] = pairs[2 * remainder + 1];
    }
    // Emit the most significant 1–2 digits without a leading zero.
    if (v < 10) {
        *--p = static_cast<char>('0' + v);
    } else {
        p -= 2;
        const unsigned index = 2 * static_cast<unsigned>(v);
        p[0] = pairs[index];
        p[1] = pairs[index + 1];
    }
    return length;
}

// Each non-leading block needs exactly eight digits, including its zero pairs.
// Four fixed iterations let the compiler unroll these 32-bit conversions.
inline void eight_digits(std::uint32_t v, char* out) {
    for (int i = 8; i > 0; i -= 2) {
        const unsigned index = 2 * (v % 100);
        v /= 100;
        out[i - 2] = digits[index];
        out[i - 1] = digits[index + 1];
    }
}
} // namespace u64toa_detail

// HW12 — fast uint64 -> decimal text. Write digits and return their count.
// Caller supplies at least 20 writable bytes. No NUL terminator is written.
inline int u64toa(std::uint64_t v, char* out) {
    constexpr std::uint64_t block = 100000000;
    if (v < block)
        return u64toa_detail::leading_block(static_cast<std::uint32_t>(v), out);

    // Extend the lab's digit batching: split into at most three blocks, then
    // emit each block using the same two-digit table. Only the leading block
    // has variable width. All writes go directly into the destination.
    const auto low = static_cast<std::uint32_t>(v % block);
    v /= block;
    if (v < block) {
        const int length = u64toa_detail::leading_block(static_cast<std::uint32_t>(v), out);
        u64toa_detail::eight_digits(low, out + length);
        return length + 8;
    }
    const auto middle = static_cast<std::uint32_t>(v % block);
    v /= block; // the leading block is at most 1844 for UINT64_MAX
    const int length = u64toa_detail::leading_block(static_cast<std::uint32_t>(v), out);
    u64toa_detail::eight_digits(middle, out + length);
    u64toa_detail::eight_digits(low, out + length + 8);
    return length + 16;
}

// Bonus: append a numeric order field, e.g. 38=100<SOH>. Caller supplies at
// least 32 writable bytes (10 tag digits + '=' + 20 value digits + SOH).
// Returns the byte count without a NUL; tag 0 returns 0 without writing.
inline int encode_u64_field(std::uint32_t tag, std::uint64_t value, char* out) {
    if (tag == 0) return 0;
    int length = u64toa(tag, out);
    out[length++] = '=';
    length += u64toa(value, out + length);
    out[length++] = '\x01';
    return length;
}
