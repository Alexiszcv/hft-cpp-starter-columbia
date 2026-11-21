#include "fix_batch.hpp"
#include "bench.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
constexpr std::size_t message_count = 256;
constexpr int repetitions = 2000;
constexpr int warmup = 200;
constexpr int runs = 11;
constexpr char soh = '\x01';
using Orders = std::array<NewOrder, message_count>;
using Clock = std::chrono::steady_clock;

// The "before" version changes only the price conversion. The temporary is
// bounded and NUL-terminated: strtod never reads beyond the caller's buffer.
bool library_price(const char* begin, const char* end, double& result) {
    const auto length = static_cast<std::size_t>(end - begin);
    char temporary[128];
    if (length == 0 || length >= sizeof temporary) return false;
    std::memcpy(temporary, begin, length);
    temporary[length] = '\0';
    char* stop = nullptr;
    errno = 0;
    result = std::strtod(temporary, &stop);
    return stop == temporary + length && errno != ERANGE && std::isfinite(result);
}

bool before(const char* buf, int len, NewOrder& out) {
    return fix_detail::parse_order(buf, len, out, library_price);
}

// Deliberately conventional baseline: own the message, split into strings,
// insert every field into a map, then look up and convert the desired fields.
// Retain the original ClOrdID address so output lifetimes match the fast parser.
bool naive(const char* buf, int len, NewOrder& out) {
    std::istringstream stream{std::string(buf, static_cast<std::size_t>(len))};
    std::map<int, std::string> fields;
    std::string field;
    std::size_t offset = 0;
    const char* identifier = nullptr;
    while (std::getline(stream, field, soh)) {
        const auto equal = field.find('=');
        if (equal == std::string::npos) return false;
        const int tag = std::stoi(field.substr(0, equal));
        fields.emplace(tag, field.substr(equal + 1));
        if (tag == 11) identifier = buf + offset + equal + 1;
        offset += field.size() + 1;
    }
    for (const int tag : {11, 55, 54, 38, 44})
        if (!fields.count(tag) || fields.at(tag).empty()) return false;
    const auto& symbol = fields.at(55);
    if (symbol.size() >= sizeof out.symbol || fields.at(54).size() != 1)
        return false;
    const auto quantity = std::stoull(fields.at(38));
    if (quantity > std::numeric_limits<std::uint32_t>::max()) return false;
    out = {};
    out.clordid = identifier;
    out.clordid_len = static_cast<int>(fields.at(11).size());
    std::memcpy(out.symbol, symbol.data(), symbol.size());
    out.side = fields.at(54)[0];
    out.qty = static_cast<std::uint32_t>(quantity);
    const auto& price = fields.at(44);
    return library_price(price.data(), price.data() + price.size(), out.price);
}

struct Fixture {
    std::string bytes;
    std::array<std::string, message_count> ids;
    std::array<std::string, message_count> symbols;
    std::array<std::uint32_t, message_count> quantities{};
    std::array<double, message_count> prices{};
};

Fixture make_fixture() {
    Fixture f;
    const std::array<const char*, 8> symbols{
        "AAPL", "MSFT", "IBM", "NVDA", "SPY", "BRK.B", "TSLA", "ABCDEFGHIJKLMNO"};
    for (std::size_t i = 0; i < message_count; ++i) {
        f.ids[i] = "ORD-" + std::to_string(100000 + i);
        f.symbols[i] = symbols[i % symbols.size()];
        f.quantities[i] = static_cast<std::uint32_t>(1 + (i * 7919) % 1000000);
        const auto cents = 100 + (i * 13751) % 200000;
        f.prices[i] = static_cast<double>(cents) / 100.0;
        char price[32];
        std::snprintf(price, sizeof price, "%zu.%02zu", cents / 100, cents % 100);
        const std::array<std::string, 5> fields{
            "11=" + f.ids[i], "55=" + f.symbols[i],
            std::string("54=") + (i % 2 ? '2' : '1'),
            "38=" + std::to_string(f.quantities[i]), std::string("44=") + price};
        std::string body = std::string("35=D") + soh + "49=CLIENT" + soh;
        for (std::size_t j = 0; j < fields.size(); ++j)
            body += fields[(i + j) % fields.size()] + soh;
        std::string message = std::string("8=FIX.4.2") + soh + "9=" +
                              std::to_string(body.size()) + soh + body;
        unsigned checksum = 0;
        for (const unsigned char byte : message) checksum += byte;
        char trailer[8];
        std::snprintf(trailer, sizeof trailer, "10=%03u%c", checksum % 256, soh);
        f.bytes += message + trailer;
    }
    return f;
}

template <class Parser>
void parse_batch(const Fixture& fixture, Orders& orders, Parser parser) {
    std::size_t offset = 0;
    std::size_t count = 0;
    while (offset < fixture.bytes.size()) {
        std::size_t length = 0;
        const char* start = fixture.bytes.data() + offset;
        if (count == orders.size() ||
            fix_detail::frame_length(start, fixture.bytes.size() - offset, length)
                != FixBatchStatus::complete ||
            !parser(start, static_cast<int>(length), orders[count]))
            throw std::runtime_error("benchmark message rejected");
        offset += length;
        ++count;
    }
    if (count != message_count) throw std::runtime_error("wrong batch count");
    // Make every output field observable without adding a checksum reduction
    // to the timed workload. Input preparation and output allocation are outside.
    asm volatile("" : : "m"(orders) : "memory");
}

void validate(const Fixture& fixture, const Orders& orders) {
    for (std::size_t i = 0; i < message_count; ++i) {
        const auto& o = orders[i];
        const char* expected_id = fixture.bytes.data() +
                                 fixture.bytes.find("11=" + fixture.ids[i]) + 3;
        if (o.clordid != expected_id ||
            o.clordid_len != static_cast<int>(fixture.ids[i].size()) ||
            std::memcmp(o.clordid, fixture.ids[i].data(), fixture.ids[i].size()) ||
            std::strcmp(o.symbol, fixture.symbols[i].c_str()) ||
            o.side != (i % 2 ? '2' : '1') || o.qty != fixture.quantities[i] ||
            std::abs(o.price - fixture.prices[i]) > 1e-9)
            throw std::runtime_error("benchmark output mismatch");
    }
}

std::uint64_t counter() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_lfence();
    const auto value = cycle_count();
    _mm_lfence();
    return value;
#elif defined(__aarch64__)
    asm volatile("isb" : : : "memory");
    return cycle_count();
#else
    return cycle_count();
#endif
}

struct Sample { double ns; double ticks; };

template <class Parser>
Sample measure(const Fixture& fixture, Orders& orders, Parser parser) {
    for (int i = 0; i < warmup; ++i) parse_batch(fixture, orders, parser);
    const auto start = Clock::now();
    const auto tick_start = counter();
    for (int i = 0; i < repetitions; ++i) parse_batch(fixture, orders, parser);
    const auto tick_end = counter();
    const auto finish = Clock::now();
    constexpr double operations = repetitions * message_count;
    return {std::chrono::duration<double, std::nano>(finish - start).count() / operations,
            static_cast<double>(tick_end - tick_start) / operations};
}

double median(std::array<double, runs> values) {
    std::sort(values.begin(), values.end());
    return values[runs / 2];
}
} // namespace

int main() {
    try {
        const Fixture fixture = make_fixture();
        Orders orders{};
        using Parser = bool (*)(const char*, int, NewOrder&);
        const std::array<Parser, 3> parsers{naive, before, parse_new_order};
        const std::array<const char*, 3> names{"naive_split_map", "before_strtod", "after_decimal"};
        for (const auto parser : parsers) {
            parse_batch(fixture, orders, parser);
            validate(fixture, orders);
        }
        const auto batch = parse_new_order_batch(fixture.bytes.data(), fixture.bytes.size(),
                                                orders.data(), orders.size());
        if (batch.status != FixBatchStatus::complete || batch.count != message_count ||
            batch.consumed != fixture.bytes.size())
            throw std::runtime_error("public batch API mismatch");
        validate(fixture, orders);

        std::cout << std::fixed << std::setprecision(3)
                  << "Validated 256 varied orders for all three parsers and the batch API.\n"
                  << "One operation = frame + parse one message; " << fixture.bytes.size()
                  << " bytes/batch, " << repetitions << " batches/run, " << warmup
                  << " warmup batches, median of " << runs << " runs.\n";
#if defined(__x86_64__) || defined(__i386__)
        std::cout << "Counter: x86 TSC ticks (reference cycles), not frequency-scaled core cycles.\n";
#elif defined(__aarch64__)
        std::cout << "Counter: arm64 virtual timer ticks, " << cycle_count_hz()
                  << " Hz; these are NOT CPU cycles. Use ns/op as the time metric.\n";
#else
        std::cout << "Counter fallback: steady_clock nanoseconds, NOT CPU cycles.\n";
#endif
        std::array<std::array<double, runs>, 3> ns{}, ticks{};
        for (int run = 0; run < runs; ++run) {
            // Rotate execution order to reduce systematic thermal/order bias.
            for (int position = 0; position < 3; ++position) {
                const int variant = (run + position) % 3;
                const auto sample = measure(fixture, orders, parsers[variant]);
                ns[variant][run] = sample.ns;
                ticks[variant][run] = sample.ticks;
                std::cout << "RUN " << run + 1 << ' ' << names[variant]
                          << " ns/op=" << sample.ns << " ticks/op=" << sample.ticks << '\n';
            }
        }
        for (std::size_t variant = 0; variant < parsers.size(); ++variant) {
            const auto time = median(ns[variant]);
            std::cout << names[variant] << ": " << time << " ns/op, "
                      << median(ticks[variant]) << " ticks/op, " << 1000.0 / time
                      << " million messages/s, "
                      << (fixture.bytes.size() / static_cast<double>(message_count)) * 1000.0 / time
                      << " MB/s\n";
        }
        std::cout << "Speedup vs naive: " << median(ns[0]) / median(ns[2]) << "x\n"
                  << "Speedup vs before: " << median(ns[1]) / median(ns[2]) << "x\n";
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
