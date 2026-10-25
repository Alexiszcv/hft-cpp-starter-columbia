// HW7: read-only lookups; initialization and updates are outside the timers.
#include "order_book.hpp"
#include "bench.hpp"
#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>

int main() {
    constexpr long iterations = 2'000'000;
    constexpr int repetitions = 11, symbol_count = 2048;
    auto book = std::make_unique<Book>();
    // Descending order makes begin() the best bid: no artificial tree scan.
    std::map<int, uint64_t, std::greater<int>> tree;
    for (int i = 0; i < 2000; ++i) {
        const int tick = 10000 + i;
        book->add(i, 'B', tick * 0.01, 1);
        tree[tick] += 1;
    }
    auto symbols = std::make_unique<SymMap>();
    std::unordered_map<std::string, uint64_t> standard;
    standard.max_load_factor(0.5f);
    standard.reserve(symbol_count);
    std::vector<std::string> keys;
    for (int i = 0; i < symbol_count; ++i) {
        const std::string digits = std::to_string(i);
        keys.push_back("SYM" + std::string(5 - digits.size(), '0') + digits);
        symbols->put(keys.back().c_str(), i);
        standard.emplace(keys.back(), i);
    }
    std::vector<int> queries(symbol_count);
    std::iota(queries.begin(), queries.end(), 0);
    std::mt19937 random(7);
    std::shuffle(queries.begin(), queries.end(), random);
    if (book->best_bid() != tree.begin()->first * 0.01) return 1;
    for (const auto& key : keys)
        if (symbols->get(key.c_str()) != standard.at(key)) return 1;

    std::size_t flat_query = 0, standard_query = 0;
    auto flat_book = [&] {
        doNotOptimize(book.get());
        doNotOptimize(book->best_bid());
    };
    auto tree_book = [&] {
        doNotOptimize(&tree);
        doNotOptimize(tree.empty() ? 0.0 : tree.begin()->first * 0.01);
    };
    auto flat_get = [&] {
        const auto& key = keys[queries[flat_query++ & (symbol_count - 1)]];
        doNotOptimize(symbols->get(key.c_str()));
    };
    auto standard_get = [&] {
        const auto& key = keys[queries[standard_query++ & (symbol_count - 1)]];
        const auto it = standard.find(key);
        doNotOptimize(it == standard.end() ? uint64_t(-1) : it->second);
    };

    std::array<std::vector<double>, 4> times, ticks;
    auto measure = [&](int which, auto& operation) {
        flat_query = standard_query = 0;
        times[which].push_back(ns_per_op(operation, iterations));
        flat_query = standard_query = 0;
        ticks[which].push_back(cycles_per_op(operation, iterations));
    };
    for (int run = 0; run < repetitions; ++run) {
        for (int offset = 0; offset < 4; ++offset) {
            switch ((run + offset) % 4) {
                case 0: measure(0, tree_book); break;
                case 1: measure(1, flat_book); break;
                case 2: measure(2, standard_get); break;
                case 3: measure(3, flat_get); break;
            }
        }
    }
    const auto median = [](std::vector<double> values) {
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    };
    std::cout << "11 repetitions, 2,000,000 lookups/run, 10% warmup; all symbol queries hit.\n"
              << "Book: 2,000 bid levels. Symbols: 2,048 keys, 8 chars; load factors "
              << "flat=0.5, standard=" << standard.load_factor() << ".\n"
              << "Counter frequency: " << cycle_count_hz()
              << " Hz (arm64: timer ticks, NOT CPU cycles; x86: TSC ticks).\n";
    const char* names[] = {"std::map best_bid", "Book best_bid",
                           "std::unordered_map get", "SymMap get"};
    std::cout << std::fixed << std::setprecision(3)
              << "Implementation,ns/op,Mops/s,counter_ticks/op\n";
    for (int i = 0; i < 4; ++i) {
        const double ns = median(times[i]);
        std::cout << names[i] << ',' << ns << ',' << 1000.0 / ns << ','
                  << median(ticks[i]) << '\n';
    }
    std::cout << "Speedups: Book=" << median(times[0]) / median(times[1])
              << "x; SymMap=" << median(times[2]) / median(times[3]) << "x\n";
}
