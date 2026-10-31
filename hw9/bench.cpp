#include "seqlock.hpp"
#include "bench.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

struct Quote { double bid, ask; std::uint32_t bq, aq; };
static_assert(sizeof(Quote) == 24 && offsetof(Quote, ask) == 8 &&
              offsetof(Quote, bq) == 16 && offsetof(Quote, aq) == 20,
              "The tested Quote representation must have no padding");
constexpr std::uint32_t salt = 0xa5a55a5a;
using Clock = std::chrono::steady_clock;

static Quote quote(std::uint64_t generation) {
    const auto n = static_cast<std::uint32_t>(generation);
    return {double(n), double(n) + 0.5, n, n ^ salt};
}
static bool valid(const Quote& q) {
    return q.bid == double(q.bq) && q.ask == double(q.bq) + 0.5 &&
           q.aq == (q.bq ^ salt);
}
static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

// Same public operations and Quote; readers acquire a shared lock each time.
struct MutexQuote {
    void write(const Quote& q) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        value_ = q;
    }
    bool read(Quote& q) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        q = value_;
        return true;
    }
private:
    mutable std::shared_mutex mutex_;
    Quote value_{};
};

struct ReaderStats {
    std::uint64_t reads = 0, attempts = 0, errors = 0, ticks = 0, changes = 0;
    double ns = 0;
};
struct Result {
    double ns_per_read, ticks_per_read, million_reads_s, million_writes_s, retry_pct;
    std::uint64_t reads, errors;
};

template <class Store, bool Check = false>
static Result trial(int readers, int milliseconds) {
    Store store;
    store.write(quote(0));
    std::atomic<int> ready{0};
    std::atomic<bool> start{false}, stop{false};
    std::array<ReaderStats, 4> stats{};
    std::uint64_t writes = 0;
    auto gate = [&] {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    };
    std::thread writer([&] {
        gate();
        std::uint64_t n = 0;
        while (!stop.load(std::memory_order_relaxed)) store.write(quote(++n));
        writes = n;
    });
    std::vector<std::thread> threads;
    for (int id = 0; id < readers; ++id) threads.emplace_back([&, id] {
        gate();
        ReaderStats local;
        Quote q = quote(0);
        const auto begin = Clock::now();
        const auto first_tick = cycle_count();
        while (!stop.load(std::memory_order_relaxed)) {
            const Quote previous = q;
            ++local.attempts;
            if (store.read(q)) {
                ++local.reads;
                if constexpr (Check) {
                    local.errors += !valid(q);
                    local.changes += q.bq != previous.bq;
                }
                doNotOptimize(q);
            } else if constexpr (Check) {
                local.errors += std::memcmp(&previous, &q, sizeof(q)) != 0;
            }
        }
        local.ticks = cycle_count() - first_tick;
        local.ns = std::chrono::duration<double, std::nano>(Clock::now() - begin).count();
        stats[id] = local; // publish only once; no shared hot-loop counter
    });
    while (ready.load(std::memory_order_acquire) != readers + 1) std::this_thread::yield();
    const auto begin = Clock::now();
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    stop.store(true, std::memory_order_relaxed);
    for (auto& thread : threads) thread.join();
    writer.join();
    const double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
    ReaderStats total;
    for (int id = 0; id < readers; ++id) {
        require(stats[id].reads > 0, "A reader made no progress");
        if constexpr (Check)
            require(stats[id].changes >= 2, "A reader did not observe multiple updates");
        total.reads += stats[id].reads;
        total.attempts += stats[id].attempts;
        total.errors += stats[id].errors;
        total.ns += stats[id].ns;
        total.ticks += stats[id].ticks;
    }
    require(writes > 0, "Writer made no progress");
    Quote last{};
    require(store.read(last) && valid(last) && last.bq == std::uint32_t(writes),
            "Final published quote is incorrect");
    require(total.errors == 0, "Mixed quote or output changed on failed read");
    return {total.ns / total.reads, double(total.ticks) / total.reads,
            total.reads / seconds / 1e6, writes / seconds / 1e6,
            100.0 * (total.attempts - total.reads) / total.attempts,
            total.reads, total.errors};
}

static double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}
static void report(const char* name, int readers, const std::vector<Result>& runs) {
    std::vector<double> ns, ticks, reads, writes, retries;
    for (const auto& r : runs) {
        ns.push_back(r.ns_per_read); ticks.push_back(r.ticks_per_read);
        reads.push_back(r.million_reads_s); writes.push_back(r.million_writes_s);
        retries.push_back(r.retry_pct);
    }
    std::cout << name << ',' << readers << ',' << median(ns) << ',' << median(ticks)
              << ',' << median(reads) << ',' << median(writes) << ',' << median(retries) << '\n';
}

int main(int argc, char** argv) try {
    require(argc == 1 || (argc == 2 && std::string(argv[1]) == "--check"),
            "Usage: hw9_bench [--check]");
    SeqLock<Quote> initial;
    Quote q{1, 2, 3, 4};
    require(initial.read(q) && q.bid == 0 && q.ask == 0 && q.bq == 0 && q.aq == 0,
            "Initial value is incorrect");
    for (std::uint64_t n = 0; n < 1000; ++n) {
        initial.write(quote(n));
        require(initial.read(q) && valid(q) && q.bq == n, "Sequential round trip failed");
    }
    const auto seq = trial<SeqLock<Quote>, true>(4, 250);
    const auto mutex = trial<MutexQuote, true>(4, 250);
    std::cout << "CHECK|one_writer_four_readers|pass|seqlock_reads=" << seq.reads
              << "|mutex_reads=" << mutex.reads
              << "|mixed=0|updates_per_reader>=2|final_value=correct\n";
    if (argc == 2) return 0;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "CONFIG|runs=9|warmup_ms=50|sample_ms=200|writer=continuous|quote_bytes=24\n"
              << "COUNTER|ticks_per_second=" << cycle_count_hz() << '\n'
              << "store,readers,reader_ns/success,reader_counter_ticks/success,aggregate_Mreads/s,writer_Mupdates/s,retry_percent\n";
    for (int readers : {1, 4}) {
        std::vector<Result> seq_runs, mutex_runs;
        for (int run = 0; run < 9; ++run) {
            // Rotate case order; give each measured sample a discarded warmup.
            for (int step = 0; step < 2; ++step) {
                if ((run + step) % 2 == 0) {
                    trial<SeqLock<Quote>>(readers, 50);
                    seq_runs.push_back(trial<SeqLock<Quote>>(readers, 200));
                } else {
                    trial<MutexQuote>(readers, 50);
                    mutex_runs.push_back(trial<MutexQuote>(readers, 200));
                }
            }
        }
        report("shared_mutex", readers, mutex_runs);
        report("seqlock", readers, seq_runs);
    }
} catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
}
