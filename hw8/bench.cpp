#include "rolling_counter.hpp"
#include "bench.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

using Clock = std::chrono::steady_clock;
constexpr long iterations = 2'000'000;
constexpr long warmup = iterations / 10 + 1;
constexpr std::size_t sample_count = 200'000;
constexpr uint64_t window_ns = 1'000;

static double percentile(const std::vector<double>& sorted, double p) {
    return sorted[static_cast<std::size_t>(std::ceil(p * sorted.size())) - 1];
}

static void print_percentiles(const char* name, std::vector<double>& samples) {
    std::sort(samples.begin(), samples.end());
    std::cout << name << ": p50=" << percentile(samples, 0.50)
              << " p99=" << percentile(samples, 0.99)
              << " p99.9=" << percentile(samples, 0.999) << " ns\n";
}

int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "One operation = add(t) + count(t), then increment t by 1 ns.\n"
              << "Window: " << window_ns << " ns; warmup: " << warmup
              << "; timed pairs per batch: " << iterations << ".\n";

    // Use the unchanged course helper for amortized throughput timing.
    std::vector<double> means;
    for (int run = 0; run < 11; ++run) {
        RollingCounter counter(window_ns);
        uint64_t t = 0;
        means.push_back(ns_per_op([&] {
            counter.add(t);
            const auto n = counter.count(t);
            doNotOptimize(n);
            ++t;
        }, iterations));
    }
    std::sort(means.begin(), means.end());
    std::cout << "Median of 11 batch means: " << means[5] << " ns/pair; "
              << 1000.0 / means[5] << " million pairs/s.\n";

    // Sample buffers are allocated before timing; sorting happens afterwards.
    std::vector<double> samples(sample_count), timer_samples(sample_count);
    RollingCounter counter(window_ns);
    uint64_t t = 0;
    auto step = [&] {
        counter.add(t);
        const auto n = counter.count(t);
        doNotOptimize(n);
        ++t;
    };
    for (long i = 0; i < warmup; ++i) step();
    for (double& ns : samples) {
        const auto begin = Clock::now();
        step();
        const auto end = Clock::now();
        ns = std::chrono::duration<double, std::nano>(end - begin).count();
    }
    // Expose the timer's floor; do not subtract percentiles from each other.
    for (double& ns : timer_samples) {
        const auto begin = Clock::now();
        clobber();
        const auto end = Clock::now();
        ns = std::chrono::duration<double, std::nano>(end - begin).count();
    }
    std::cout << "Individual samples: " << sample_count
              << "; raw timings include clock overhead and quantization.\n";
    print_percentiles("add/count pair", samples);
    print_percentiles("Empty timer", timer_samples);
}
