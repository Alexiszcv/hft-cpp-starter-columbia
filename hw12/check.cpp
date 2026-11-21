#include "u64toa.hpp"

#include <array>
#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <random>

// Fixtures are prepared before counting calls to the C++ heap allocator.
static bool count_allocations = false;
static std::size_t allocations = 0;
void* operator new(std::size_t n) {
    if (count_allocations) ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static unsigned checks = 0;
static void require(bool ok, const char* description) {
    ++checks;
    if (!ok) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

static void check_value(std::uint64_t value) {
    char expected[20];
    const auto reference = std::to_chars(expected, expected + sizeof(expected), value);
    require(reference.ec == std::errc{}, "independent std::to_chars reference");
    const auto expected_len = reference.ptr - expected;

    // Both canaries and the unused suffix must survive: no terminating NUL is
    // part of u64toa's contract, and no byte past the returned length is writable.
    std::array<char, 22> guarded;
    guarded.fill('#');
    const int len = u64toa(value, guarded.data() + 1);
    require(len >= 1 && len <= 20, "returned length is in [1,20]");
    require(len == expected_len && std::memcmp(guarded.data() + 1, expected, len) == 0,
            "decimal digits match std::to_chars");
    require(guarded[0] == '#', "leading canary");
    for (std::size_t i = static_cast<std::size_t>(len) + 1; i < guarded.size(); ++i)
        require(guarded[i] == '#', "no writes after the returned length");

    // ASan catches a write of a 21st byte for UINT64_MAX on this exact-size array.
    char exact[20];
    std::memset(exact, '#', sizeof(exact));
    require(u64toa(value, exact) == len && std::memcmp(exact, expected, len) == 0,
            "caller needs only 20 bytes");
}

static void check_field(std::uint32_t tag, std::uint64_t value) {
    char expected[40];
    const int expected_len = std::snprintf(expected, sizeof(expected),
                                          "%" PRIu32 "=%" PRIu64 "\x01", tag, value);
    std::array<char, 34> guarded;
    guarded.fill('#');
    const int len = encode_u64_field(tag, value, guarded.data() + 1);
    require(len >= 4 && len <= 32 && len == expected_len,
            "tag=value SOH encoded length");
    require(std::memcmp(guarded.data() + 1, expected, len) == 0,
            "field bytes match snprintf reference");
    require(guarded[0] == '#', "field leading canary");
    for (std::size_t i = static_cast<std::size_t>(len) + 1; i < guarded.size(); ++i)
        require(guarded[i] == '#', "field writes no NUL or extra bytes");
    char exact[32];
    require(encode_u64_field(tag, value, exact) == len &&
                std::memcmp(exact, expected, len) == 0, "field fits in exactly 32 bytes");
}

int main() {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    check_value(0);
    check_value(maximum);
    check_value(maximum - 1);
    for (std::uint64_t power = 1;; power *= 10) {
        check_value(power - 1);
        check_value(power);
        check_value(power + 1);
        if (power > maximum / 10) break;
    }
    // Exhaust all two-digit pairs, including zero pairs within larger numbers.
    for (std::uint64_t value = 0; value < 10000; ++value) check_value(value);

    std::mt19937_64 random(12);
    std::uint64_t bound = 9;
    for (;;) {
        std::uniform_int_distribution<std::uint64_t> distribution(0, bound);
        for (int i = 0; i < 1000; ++i) check_value(distribution(random));
        if (bound == maximum) break;
        bound = bound > (maximum - 9) / 10 ? maximum : bound * 10 + 9;
    }

    check_field(1, 0);
    check_field(38, 100);
    check_field(11, maximum);
    check_field(std::numeric_limits<std::uint32_t>::max(), maximum);
    std::array<char, 32> invalid;
    invalid.fill('#');
    require(encode_u64_field(0, maximum, invalid.data()) == 0, "tag zero is rejected");
    for (char c : invalid) require(c == '#', "invalid tag leaves output unchanged");

    char batch[128];
    std::memset(batch, '#', sizeof(batch));
    int len = encode_u64_field(11, maximum, batch);
    len += encode_u64_field(38, 100, batch + len);
    len += encode_u64_field(54, 1, batch + len);
    const char expected[] = "11=18446744073709551615\x01" "38=100\x01" "54=1\x01";
    require(len == static_cast<int>(sizeof(expected) - 1) &&
                std::memcmp(batch, expected, sizeof(expected) - 1) == 0,
            "multiple fields concatenate using returned lengths");
    for (std::size_t i = static_cast<std::size_t>(len); i < sizeof(batch); ++i)
        require(batch[i] == '#', "concatenation preserves unused buffer suffix");

    std::array<std::uint64_t, 1024> values{};
    for (auto& value : values) value = random();
    allocations = 0;
    count_allocations = true;
    std::uint64_t observed = 0;
    for (int repeat = 0; repeat < 100; ++repeat) {
        for (auto value : values) {
            char digits[20], field[32];
            const int digits_len = u64toa(value, digits);
            const int field_len = encode_u64_field(38, value, field);
            observed += digits_len + field_len + digits[0] + field[0];
        }
    }
    count_allocations = false;
    require(observed != 0 && allocations == 0,
            "conversion and field encoding make zero C++ heap allocations");
    std::cout << "PASS: " << checks << " checks; zero C++ heap allocations while encoding.\n";
}
