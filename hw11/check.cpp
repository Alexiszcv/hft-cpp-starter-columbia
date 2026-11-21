#include "fix_batch.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <random>
#include <string>

// Count C++ heap allocations only while parsing already-built fixtures.
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
static void require(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

static std::string soh(std::string text) {
    for (char& c : text) if (c == '|') c = '\x01';
    return text;
}

static std::string fields(const std::string& id = "ORD123",
                          const std::string& symbol = "AAPL",
                          const std::string& side = "1",
                          const std::string& qty = "100",
                          const std::string& price = "185.50") {
    return soh("11=" + id + "|55=" + symbol + "|54=" + side +
               "|38=" + qty + "|44=" + price + "|");
}

// BodyLength includes the SOH immediately before tag 10. CheckSum covers every
// preceding byte, including tags 8 and 9, reduced modulo 256.
static std::string frame(const std::string& body,
                         const std::string& version = "FIX.4.2") {
    std::string text = soh("8=" + version + "|9=" +
                           std::to_string(body.size()) + "|") + body;
    unsigned checksum = 0;
    for (unsigned char c : text) checksum += c;
    char tail[8];
    std::snprintf(tail, sizeof(tail), "10=%03u", checksum % 256);
    return text + tail + '\x01';
}

static bool parse(const std::string& input, NewOrder& out) {
    return parse_new_order(input.data(), static_cast<int>(input.size()), out);
}

static void reject(const std::string& input, const char* description) {
    NewOrder out{};
    out.clordid = "unchanged";
    out.clordid_len = 9;
    std::strcpy(out.symbol, "SENTINEL");
    out.side = '2';
    out.qty = 123456;
    out.price = -12.25;
    const NewOrder before = out;
    require(!parse(input, out), description);
    require(out.clordid == before.clordid && out.clordid_len == before.clordid_len &&
                std::strcmp(out.symbol, before.symbol) == 0 &&
                out.side == before.side && out.qty == before.qty &&
                out.price == before.price,
            "a rejected message leaves output unchanged");
}

int main() {
    NewOrder out{};
    const std::string simple = fields();
    require(parse(simple, out), "minimal five-field message");
    require(out.clordid == simple.data() + 3 && out.clordid_len == 6,
            "ClOrdID is a view into the original input");
    require(std::strcmp(out.symbol, "AAPL") == 0 && out.side == '1' &&
                out.qty == 100 && std::abs(out.price - 185.50) < 1e-12,
            "all five parsed values");

    const std::string reordered = soh("44=-0.125|999=ignored|38=4294967295|"
                                     "54=2|55=123456789012345|11=X|35=D|");
    require(parse(reordered, out), "field order, unknown field and boundary values");
    require(out.qty == std::numeric_limits<uint32_t>::max() && out.side == '2' &&
                std::strlen(out.symbol) == 15 && out.price == -0.125,
            "maximum quantity, maximum symbol and negative decimal");
    require(parse(fields("X", "Y", "1", "0", "+12"), out) &&
                out.qty == 0 && out.price == 12, "zero quantity and signed integer price");
    require(parse(fields("X", "Y", "1", "0007", "000.2500"), out) &&
                out.qty == 7 && out.price == 0.25, "leading and trailing numeric zeroes");

    require(!parse_new_order(nullptr, 1, out), "null input");
    require(!parse_new_order(nullptr, 0, out), "null empty input");
    require(!parse_new_order(simple.data(), 0, out), "zero length");
    require(!parse_new_order(simple.data(), -1, out), "negative length");
    reject("", "empty message");
    reject(soh("11=ORD123|55=AAPL|54=1|38=100|"), "missing required field");
    for (const char* tag : {"11", "55", "54", "38", "44"}) {
        reject(simple + soh(std::string(tag) + "=1|"), "duplicate required field");
    }
    reject(fields(""), "empty ClOrdID");
    reject(fields("X", ""), "empty symbol");
    reject(fields("X", "1234567890123456"), "symbol does not fit");
    for (const char* side : {"", "0", "3", "12", "x"}) {
        reject(fields("X", "Y", side), "invalid side");
    }
    for (const char* qty : {"", "-1", "+1", "1.5", "1x", " 1", "4294967296",
                            "999999999999999999999999999999999999"}) {
        reject(fields("X", "Y", "1", qty), "invalid or overflowing quantity");
    }
    for (const char* price : {"", "+", "-", ".", ".5", "1.", "1.2.3", "1e2",
                              "NaN", "inf", " 1", "1 ", "1x", "--1"}) {
        reject(fields("X", "Y", "1", "1", price), "invalid decimal price");
    }
    reject(fields("X", "Y", "1", "1", std::string(400, '9')),
           "non-finite decimal price");
    reject(soh("=value|") + simple, "empty tag");
    reject(soh("0=value|") + simple, "zero tag");
    reject(soh("A=value|") + simple, "non-numeric tag");
    reject(soh("999999999999999999999999999999=value|") + simple, "overflowing tag");
    reject(soh("11ORD123|") + simple, "missing equals sign");
    reject(simple + soh("999=|"), "empty unknown field");
    reject(simple + soh("35=F|"), "wrong message type");
    reject(simple + "999=unfinished", "unfinished trailing field");
    reject(simple + std::string(1, '\0'), "trailing nul is not a FIX field");

    // Every proper prefix is invalid, even if memory after len is accessible.
    // Exact-size allocations also let ASan detect accidental reads beyond len.
    for (std::size_t n = 1; n < simple.size(); ++n) {
        std::unique_ptr<char[]> exact(new char[n]);
        std::memcpy(exact.get(), simple.data(), n);
        require(!parse_new_order(exact.get(), static_cast<int>(n), out),
                "truncated bounded input");
    }
    std::unique_ptr<char[]> exact(new char[simple.size()]);
    std::memcpy(exact.get(), simple.data(), simple.size());
    require(parse_new_order(exact.get(), static_cast<int>(simple.size()), out),
            "complete input requires no terminating nul");

    std::mt19937 rng(11);
    for (int i = 0; i < 2000; ++i) {
        const auto qty = static_cast<uint32_t>(rng());
        const unsigned cents = rng() % 100000000;
        const bool negative = (rng() & 1) != 0;
        char price[32];
        std::snprintf(price, sizeof(price), "%s%u.%02u", negative ? "-" : "",
                      cents / 100, cents % 100);
        const std::string input = fields("ORDER" + std::to_string(i), "MSFT", "2",
                                         std::to_string(qty), price);
        require(parse(input, out) && out.qty == qty &&
                    std::abs(out.price - (negative ? -1.0 : 1.0) * cents / 100.0) < 1e-8,
                "deterministic randomized quantities and decimal prices");
    }

    const std::string first = frame(soh("35=D|") + simple);
    const std::string second = frame(soh("35=D|") + fields("ORDER2", "MSFT", "2", "7", "42"));
    const std::string batch = first + second;
    NewOrder orders[2]{};
    auto result = parse_new_order_batch(batch.data(), batch.size(), orders, 2);
    require(result.status == FixBatchStatus::complete && result.count == 2 &&
                result.consumed == batch.size(), "two concatenated FIX messages");
    require(orders[0].qty == 100 && orders[1].qty == 7 && orders[1].side == '2',
            "batch values and order");
    require(orders[1].clordid == batch.data() + first.size() + second.find("ORDER2"),
            "second message view points into the caller's batch");
    result = parse_new_order_batch(batch.data(), batch.size(), orders, 1);
    require(result.status == FixBatchStatus::output_full && result.count == 1 &&
                result.consumed == first.size(), "capacity stops before the next message");
    result = parse_new_order_batch(batch.data(), batch.size(), nullptr, 0);
    require(result.status == FixBatchStatus::output_full && !result.count && !result.consumed,
            "zero capacity consumes nothing");
    result = parse_new_order_batch(nullptr, 0, nullptr, 0);
    require(result.status == FixBatchStatus::complete && !result.count && !result.consumed,
            "empty batch");
    require(parse_new_order_batch(nullptr, 1, orders, 2).status == FixBatchStatus::malformed,
            "null nonempty batch");
    require(parse_new_order_batch(batch.data(), batch.size(), nullptr, 1).status ==
                FixBatchStatus::malformed, "null output with nonzero capacity");

    for (std::size_t n = 1; n < batch.size(); ++n) {
        std::unique_ptr<char[]> prefix(new char[n]);
        std::memcpy(prefix.get(), batch.data(), n);
        result = parse_new_order_batch(prefix.get(), n, orders, 2);
        const std::size_t complete = n >= first.size() ? 1 : 0;
        require(result.count == complete && result.consumed == (complete ? first.size() : 0),
                "each TCP cut consumes only complete frames");
        require(result.status == (n == first.size() ? FixBatchStatus::complete :
                                  FixBatchStatus::incomplete), "each TCP cut status");
        const auto resumed = parse_new_order_batch(batch.data() + result.consumed,
                                                   batch.size() - result.consumed,
                                                   orders, 2);
        require(resumed.status == FixBatchStatus::complete && resumed.count == 2 - complete &&
                    resumed.consumed == batch.size() - result.consumed,
                "retained suffix parses after remaining bytes arrive");
    }
    const auto malformed_frame = [&](const std::string& input, const char* why) {
        const auto r = parse_new_order_batch(input.data(), input.size(), orders, 2);
        require(r.status == FixBatchStatus::malformed && !r.count && !r.consumed, why);
    };
    malformed_frame(soh("8=BAD|9=1|x10=000|"), "invalid BeginString");
    malformed_frame(soh("8=FIX.4.2|9=x|"), "non-numeric BodyLength");
    malformed_frame(soh("8=FIX.4.2|9=|"), "empty BodyLength");
    malformed_frame(soh("8=FIX.4.2|9=999999999999999999999999999999|"),
                    "overflowing BodyLength");
    malformed_frame(soh("8=FIX.4.2|9=1|x10=000|"), "body must end in SOH");
    malformed_frame(frame(soh("35=F|") + simple), "batch rejects non-NewOrder message");
    std::string bad_checksum = first;
    bad_checksum.replace(bad_checksum.size() - 4, 3, "256");
    malformed_frame(bad_checksum, "checksum outside byte range");
    bad_checksum.replace(bad_checksum.size() - 4, 3, "0x0");
    malformed_frame(bad_checksum, "non-digit checksum");
    bad_checksum = first;
    bad_checksum.back() = 'x';
    malformed_frame(bad_checksum, "checksum needs trailing SOH");
    const std::string bad_second = first + frame(soh("35=D|") + fields("X", "Y", "3"));
    result = parse_new_order_batch(bad_second.data(), bad_second.size(), orders, 2);
    require(result.status == FixBatchStatus::malformed && result.count == 1 &&
                result.consumed == first.size(), "bad second message preserves parsed prefix");
    const std::string fixt = frame(soh("35=D|") + simple, "FIXT.1.1");
    require(parse_new_order_batch(fixt.data(), fixt.size(), orders, 2).status ==
                FixBatchStatus::complete, "FIXT.1.1 framing");

    allocations = 0;
    count_allocations = true;
    bool all_ok = true;
    for (int i = 0; i < 10000; ++i) {
        all_ok = parse_new_order(simple.data(), static_cast<int>(simple.size()), out) && all_ok;
        result = parse_new_order_batch(batch.data(), batch.size(), orders, 2);
        all_ok = result.status == FixBatchStatus::complete && all_ok;
    }
    count_allocations = false;
    require(all_ok && allocations == 0, "parsing and framing make zero C++ heap allocations");
    std::cout << "PASS: " << checks << " checks; zero C++ heap allocations while parsing.\n";
}
