# HW7 — Fast order book and symbol map

Individual work in the personal course starter, branch `hw7`.
Implementation: `include/order_book.hpp`. Comparison: `hw7/bench.cpp`.

**Book.** One book represents one stock. Prices are rounded to one-cent ticks;
two flat arrays hold the total quantity at each price. For example, adding bids
at 100.00 and 100.02 makes 100.02 the best bid; cancelling that order restores
100.00. Cached best-level indices make both price reads O(1). An ID-to-order
`unordered_map` supplies the side, level and quantity needed for cancellation.
Only cancelling an emptied best level may scan the arrays; writes can allocate.

The 65,536-level band is centered on the first order, with a nonnegative base:
prices near 720 work without indexing past the array. Outside-band prices throw;
automatic rebasing is outside this homework. The two 64-bit quantity arrays use
1 MiB and avoid 32-bit aggregation overflow. Empty sides return 0, repeated IDs
replace their orders, unknown cancellations do nothing, and valid zero-quantity
adds cancel that ID.

**SymMap.** FNV-1a chooses a slot in a 4,096-entry array; collisions try the next
slot (linear probing). Keys occupy 16 bytes, padded with zeros, so even a
16-character symbol fits. `put` inserts/overwrites; `get` returns `uint64_t(-1)`
when absent. Oversized/null keys and full-table insertion throw. Probes are
bounded, including a missing lookup in a full table; deletion is not required.

## Measurements

Apple M1, macOS 26.4, Apple Clang 14.0.3, arm64/C++17, `-O3 -DNDEBUG`.
Median of 11 batches of 2,000,000 reads, each preceded by 200,001 warmup reads
from the supplied `tests/bench.hpp`. Case order rotates. Book: 2,000 bid levels.
Symbols: 2,048 eight-character keys, deterministic shuffled successful queries,
both tables at 0.5 load factor. Queries are prepared before timing. Compiler
barriers preserve the reads. Construction, updates and misses are not timed.

| Read implementation | ns/op | Million ops/s | Timer ticks/op |
|---|---:|---:|---:|
| Before: `std::map` best bid | 0.829 | 1,206.516 | 0.020 |
| After: flat `Book::best_bid` | 0.831 | 1,203.128 | 0.020 |
| Before: `std::unordered_map` get | 26.567 | 37.640 | 0.638 |
| After: `SymMap::get` | 12.457 | 80.279 | 0.311 |

The flat book has **no measurable read advantage** here: the descending
`std::map` already exposes its best bid through O(1) `begin()`. The symbol map is
**2.13x faster** on this workload. These are warm batch-throughput timings, not
exchange latencies. On M1, `cntvct_el0` counts 24 MHz **timer ticks, not CPU
cycles**; ns/op uses `steady_clock`. Counter values are measured in separate
batches, so their rounded medians need not convert exactly to the ns medians.

**The most useful change** was comparing padded keys with
`memcmp(..., 16)` instead of `std::array::operator==`: measured symbol lookup
fell from **32.520 to 12.457 ns/op**. With this compiler, array equality emitted
a byte loop, while constant-size `memcmp` emitted two 64-bit comparisons.
Hashing and linear probing stayed the same; no SIMD intrinsics were needed.

## Course CI validation

[GitHub Actions run 40150720884](https://github.com/Alexiszcv/hft-cpp-starter-columbia/actions/runs/40150720884)
tested `hw7@56aa45a` on 2026-10-25 (UTC), using a GitHub-hosted Ubuntu 26.04
runner and `g++ -std=c++17 -O2 -pthread`.

| Course check | Result | CI ns/op | Timed operation |
|---|---:|---:|---|
| Book: best bid/ask + cancel | Pass, 12/12 | 1.11649 | `best_bid()` + `best_ask()` |
| SymMap: put/get/absent | Pass, 8/8 | 8.34541 | One successful `get()` |

The HW7 checks earn all 20 internal autograder points. The overall 50/230
includes other assignments and is not the HW7 grade. Course tests were unchanged.
Each CI metric is one mean over 2,000,000 operations after 200,001 warmup calls,
using `steady_clock`. The book contains 2,000 orders across 50 prices; symbol
queries cycle through eight keys. These are separate from the local medians
above: different workloads, compilers and machines prevent a direct speedup
comparison between CI and local results.

## Reproduce

From this repository's root, finish compilation before timing:

```bash
make book
mkdir -p build
clang++ -std=c++17 -O3 -DNDEBUG -Iinclude -Itests hw7/bench.cpp -o build/hw7_bench
build/hw7_bench
```

To reproduce the earlier bytewise comparison without changing the source:

```bash
mkdir -p build/hw7_before
sed 's/std::memcmp(slot.key.data(), key.bytes.data(), 16) == 0/slot.key == key.bytes/g' include/order_book.hpp > build/hw7_before/order_book.hpp
clang++ -std=c++17 -O3 -DNDEBUG -Ibuild/hw7_before -Itests hw7/bench.cpp -o build/hw7_before/bench
build/hw7_before/bench
```

For CI, select **Actions → autograde → Run workflow → branch `hw7`**,
then inspect that run's Summary.

Additional local checks passed with UBSan: 20,000 random book updates and 20,000
symbol operations against reference models, empty sides, band limits, repeated
IDs, aggregation, 16-character keys, collisions and a full table. Results vary
across runs and machines.
