# HW8 — Rolling event counter

Individual work in the personal course starter, branch `hw8`.
Implementation: `include/rolling_counter.hpp`; benchmark: `hw8/bench.cpp`.

## Implementation

`RollingCounter(window_ns)` counts recorded events newer than `now_ns - window_ns`.
A `std::deque<uint64_t>` stores their timestamps, oldest first. `add(ts)` appends
one timestamp. `count(now)` removes expired timestamps from the front and returns
the remaining size; it does not scan the surviving events.

For a 1,000 ns window and events at 0, 400 and 900 ns:

| Query | Expired timestamps | Count |
|---|---|---:|
| `count(999)` | None: the cutoff is -1 | 3 |
| `count(1000)` | 0: the cutoff is 0 | 2 |
| `count(1400)` | 400: the cutoff is 400 | 1 |

The boundary is strict: a timestamp equal to the cutoff expires. Subtraction is
performed only when `now >= window`, avoiding unsigned underflow while handling
equality correctly. A zero-length window expires all events at or before `now`.
As in the course lab, events arrive in timestamp order and queries use a
non-decreasing clock. This is a single-threaded counter.

Each timestamp is appended once and removed once: **O(1) amortized per operation**.
An individual `count` can still take O(k) when k events expire together. Memory
tracks retained events; the deque can allocate as it grows. It avoids an arbitrary
fixed capacity and a custom ring implementation, both unnecessary here.

## Local measurements

Apple M1 MacBook Air, 8 GB RAM, macOS 26.4, Apple Clang 14.0.3, arm64/C++17,
`-O3 -DNDEBUG`. One measured operation is **one `add(t)` + `count(t)` pair**,
followed by incrementing t by 1 ns, matching the course test's workload.
The window is 1,000 ns: after warmup, approximately 1,000 events remain live and
one event expires per pair. Large expiry bursts are not represented by this stream.

| Measurement | Mean ns/pair | Raw p50 ns | Raw p99 ns | Raw p99.9 ns |
|---|---:|---:|---:|---:|
| Interleaved add/count | 4.785 | 41 | 42 | 42 |
| Empty timer reference | — | 41 | 42 | 42 |

The mean column is the **median of 11 batch means**, each measuring 2,000,000
pairs after 200,001 warmup pairs with the unchanged `tests/bench.hpp` helper.
This corresponds to **208.974 million pairs/s**. Construction is outside timing;
any deque allocations during operations are included.

Percentiles use a separate series of 200,000 individually timed pairs after the
same warmup, with `steady_clock` and nearest-rank selection. Sample storage is
preallocated and sorting happens afterwards. These raw timings include clock
overhead and its roughly 41–42 ns quantization on this machine. Their equality
with the empty timer shows that actual per-pair latency percentiles are not
resolved here; the batch mean is more informative. No timer values are subtracted.

## Validation and reproduction

Both unchanged course checks pass locally: `rc_window` and `rc_mixed`.
Additional UBSan checks passed for empty/zero-length windows, exact boundaries,
duplicate timestamps, large expiry bursts, `UINT64_MAX` and 15,000 randomized
steps compared with an independent counting reference.

From the repository root:

```bash
make rolling
mkdir -p build
clang++ -std=c++17 -O3 -DNDEBUG -Iinclude -Itests hw8/bench.cpp -o build/hw8_bench
build/hw8_bench
```

## GitHub Actions — pending

To be completed after the `hw8` branch is pushed and tested; no CI result is
claimed yet. Select **Actions → autograde → Run workflow → branch `hw8`**.
Record the run link, the two **RollingCounter** test rows and
`rolling_ns_per_op`. The course CI uses `g++ -std=c++17 -O2 -pthread` and reports
one mean over 2,000,000 add/count pairs after 200,001 warmup pairs; it does not
report percentiles. Its overall score also includes other assignments.
