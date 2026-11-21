#include "u64toa.hpp"
#include "bench.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t corpus_size = 2048;
constexpr int repetitions = 500;
constexpr int warmup = 50;
constexpr int runs = 11;
constexpr std::uint64_t seed = 0xC011AB1212345678ULL;
using Clock = std::chrono::steady_clock;

// Unchanged algorithm from the lab: generate reversed digits, then flip.
int before(std::uint64_t value, char* out) {
    char temporary[20];
    int n = 0;
    do {
        temporary[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value);
    for (int i = 0; i < n; ++i) out[i] = temporary[n - 1 - i];
    return n;
}

// An opaque consumer can inspect all n output bytes, not just the first digit.
// This compiler barrier emits no instructions and is identical for all variants.
inline void consume(const char* data, std::size_t n) {
    asm volatile("" : : "r"(data), "r"(n) : "memory");
}

struct Before {
    void operator()(std::uint64_t value) const {
        char out[32];
        const int n = before(value, out);
        consume(out, static_cast<std::size_t>(n));
    }
};
struct After {
    void operator()(std::uint64_t value) const {
        char out[32];
        const int n = u64toa(value, out);
        consume(out, static_cast<std::size_t>(n));
    }
};
// Intentional comparison with sprintf as requested by the homework. A uint64
// needs at most 20 digits + NUL, so the 32-byte destination is sufficient.
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
int sprintf_value(std::uint64_t value, char (&out)[32]) {
    return std::sprintf(out, "%" PRIu64, value);
}
#if defined(__APPLE__) && defined(__clang__)
#pragma clang diagnostic pop
#endif
struct Sprintf {
    void operator()(std::uint64_t value) const {
        char out[32];
        const int n = sprintf_value(value, out);
        consume(out, static_cast<std::size_t>(n));
    }
};
struct ToString {
    void operator()(std::uint64_t value) const {
        const auto text = std::to_string(value);
        consume(text.data(), text.size());
        // Include allocation (when the library needs it) and destruction, but
        // do not impose an extra buffer copy on this string-returning API.
    }
};

struct Corpus {
    const char* name;
    int min_digits;
    int max_digits;
    std::vector<std::uint64_t> values;
};

std::array<Corpus, 6> make_corpora() {
    std::array<Corpus, 6> corpora{{
        {"1_digit", 1, 1, {}}, {"2-4_digits", 2, 4, {}},
        {"5-9_digits", 5, 9, {}}, {"10-15_digits", 10, 15, {}},
        {"16-20_digits", 16, 20, {}}, {"mixed_1-20", 1, 20, {}}
    }};
    std::array<std::uint64_t, 20> powers{};
    powers[0] = 1;
    for (std::size_t i = 1; i < powers.size(); ++i) powers[i] = powers[i - 1] * 10;
    const auto low = [&](int digits) { return digits == 1 ? 0ULL : powers[digits - 1]; };
    const auto high = [&](int digits) {
        return digits == 20 ? std::numeric_limits<std::uint64_t>::max() : powers[digits] - 1;
    };
    std::mt19937_64 rng(seed);
    for (auto& corpus : corpora) {
        corpus.values.reserve(corpus_size);
        for (std::size_t i = 0; i < corpus_size; ++i) {
            const int digits = corpus.min_digits +
                               static_cast<int>(i % (corpus.max_digits - corpus.min_digits + 1));
            std::uniform_int_distribution<std::uint64_t> distribution(low(digits), high(digits));
            corpus.values.push_back(distribution(rng));
        }
        // Guarantee boundaries, in particular zero and UINT64_MAX in mixed.
        corpus.values.front() = low(corpus.min_digits);
        corpus.values.back() = high(corpus.max_digits);
        std::shuffle(corpus.values.begin(), corpus.values.end(), rng);
    }
    return corpora;
}

void validate(const Corpus& corpus) {
    for (const auto value : corpus.values) {
        const auto expected = std::to_string(value);
        char first[32], second[32], third[32];
        const std::array<const char*, 3> outputs{first, second, third};
        const std::array<int, 3> lengths{
            before(value, first), u64toa(value, second),
            sprintf_value(value, third)};
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            if (lengths[i] < 1 || lengths[i] > 20 ||
                static_cast<std::size_t>(lengths[i]) != expected.size() ||
                std::memcmp(outputs[i], expected.data(), expected.size()))
                throw std::runtime_error(std::string("output mismatch in ") + corpus.name);
        }
    }
}

template <class Converter>
void convert_corpus(const Corpus& corpus, Converter converter) {
    for (const auto value : corpus.values) converter(value);
}

std::uint64_t counter() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_lfence();
    const auto ticks = cycle_count();
    _mm_lfence();
    return ticks;
#elif defined(__aarch64__)
    asm volatile("isb" : : : "memory");
    return cycle_count();
#else
    return cycle_count();
#endif
}

struct Sample { double ns; double ticks; };

template <class Converter>
Sample measure(const Corpus& corpus, Converter converter) {
    for (int i = 0; i < warmup; ++i) convert_corpus(corpus, converter);
    const auto start = Clock::now();
    const auto tick_start = counter();
    for (int i = 0; i < repetitions; ++i) convert_corpus(corpus, converter);
    const auto tick_end = counter();
    const auto finish = Clock::now();
    constexpr double operations = corpus_size * repetitions;
    return {std::chrono::duration<double, std::nano>(finish - start).count() / operations,
            static_cast<double>(tick_end - tick_start) / operations};
}

Sample measure_variant(const Corpus& corpus, int variant) {
    // Dispatch once per entire trial; no function-pointer call per conversion.
    switch (variant) {
        case 0: return measure(corpus, Before{});
        case 1: return measure(corpus, After{});
        case 2: return measure(corpus, Sprintf{});
        default: return measure(corpus, ToString{});
    }
}

double median(std::array<double, runs> values) {
    std::sort(values.begin(), values.end());
    return values[runs / 2];
}
} // namespace

int main() {
    try {
        const auto corpora = make_corpora();
        for (const auto& corpus : corpora) validate(corpus);
        std::cout << std::fixed << std::setprecision(3)
                  << "Validated all four variants on " << corpora.size() * corpus_size
                  << " runtime-generated values (including 0 and UINT64_MAX).\n"
                  << "One operation = convert one uint64 and expose all digits + length.\n"
                  << "Seed: " << seed << "; " << corpus_size << " values/corpus, "
                  << repetitions << " repetitions/trial, " << warmup
                  << " warmup repetitions, median of " << runs << " trials.\n"
                  << "Digit lengths are approximately balanced within each corpus, then shuffled.\n"
                  << "sprintf includes its NUL; u64toa does not require one. "
                     "std::to_string includes creation/destruction without an extra copy.\n";
#if defined(__x86_64__) || defined(__i386__)
        std::cout << "Counter: x86 TSC ticks (reference cycles), not frequency-scaled CPU cycles.\n";
#elif defined(__aarch64__)
        std::cout << "Counter: arm64 virtual timer ticks, " << cycle_count_hz()
                  << " Hz; NOT CPU cycles. Use ns/op as the time metric.\n";
#else
        std::cout << "Counter fallback: steady_clock nanoseconds, NOT CPU cycles.\n";
#endif
        const std::array<const char*, 4> names{"before_reverse", "after_pairs", "sprintf", "std_to_string"};
        for (const auto& corpus : corpora) {
            std::array<std::array<double, runs>, 4> ns{}, ticks{};
            for (int run = 0; run < runs; ++run) {
                for (int position = 0; position < 4; ++position) {
                    const int variant = (run + position) % 4;
                    const auto sample = measure_variant(corpus, variant);
                    ns[variant][run] = sample.ns;
                    ticks[variant][run] = sample.ticks;
                    std::cout << "RUN " << run + 1 << ' ' << corpus.name << ' ' << names[variant]
                              << " ns/op=" << sample.ns << " ticks/op=" << sample.ticks << '\n';
                }
            }
            for (std::size_t variant = 0; variant < names.size(); ++variant) {
                const auto time = median(ns[variant]);
                std::cout << "MEDIAN " << corpus.name << ' ' << names[variant]
                          << " ns/op=" << time << " ticks/op=" << median(ticks[variant])
                          << " million_values/s=" << 1000.0 / time << '\n';
            }
            std::cout << "SPEEDUP " << corpus.name
                      << " vs_before=" << median(ns[0]) / median(ns[1])
                      << " vs_sprintf=" << median(ns[2]) / median(ns[1])
                      << " vs_std_to_string=" << median(ns[3]) / median(ns[1]) << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
