# HW10 — SPSC lock-free ring buffer

`include/spsc_ring.hpp` implements the course's `uint64_t` interface for exactly
one producer and one consumer. Construction rejects zero/non-power-of-two
capacities and allocates the storage; operations perform no allocation or locking.

Unsigned counters track occupancy as `tail - head`; `index & (capacity - 1)` selects
a slot, using all capacity slots. Each counter is `alignas(64)`, as in the lab.
A compile-time assertion requires lock-free `size_t` atomics. Operations take O(1)
work; `push`/`pop` return false immediately when full/empty.

The producer writes the payload before publishing `tail` with release; the
consumer acquires `tail` before reading it. The consumer then releases `head`,
which the producer acquires before reusing that slot. These two happens-before
relationships protect the non-atomic payload. Each thread loads its own counter
with relaxed ordering. `empty()`/`full()` are observations that may become stale;
callers must check the result of `push`/`pop`.

## Validation and measurements

The unchanged course grader, run locally with only the HW10 header present and
Homebrew Clang 23.1.5, awards **100/100**: compilation 10/10 and all seven
behavior/sanitizer checks 90/90. This is a local result, not a remote CI run.

| Check | Result |
|---|---|
| `basic`, `empty_pop`, `full`, `fifo`, `wrap` | All pass |
| Concurrent FIFO, no loss/duplication | 5 runs × 2,000,000 items pass |
| ThreadSanitizer, 200,000 items | Pass; zero warnings |

Additional UBSan checks pass for invalid capacities, capacities 1/2/4/16,
full-capacity fill/drain cycles, `UINT64_MAX` payloads, and unchanged output after
a failed pop. The supplied tests were not edited.

On macOS 26.4/arm64, Apple Clang 14.0.3, C++17 `-O2 -pthread`, the supplied
concurrency test used 65,536 slots and two unpinned threads. Five consecutive
throughputs were **13,584,357; 13,013,268; 13,219,851; 13,759,755; 14,088,756**
items/s. Median: **13,584,357 items/s** (13.58 million). The course metric
`spsc_ops_per_sec` counts transferred items, each requiring one push and one pop;
timing includes thread startup/join and retries. Results depend on scheduling.

## Reproduction

Run from the repository root or the extracted submission:

```bash
c++ -std=c++17 -O2 -pthread -Iinclude tests/spsc_correctness.cpp -o /tmp/hw10_corr
/tmp/hw10_corr
c++ -std=c++17 -O2 -pthread -Iinclude tests/spsc_concurrency.cpp -o /tmp/hw10_conc
/tmp/hw10_conc

# Tested local TSan compiler; use a working g++/clang++ on other machines.
/opt/homebrew/opt/llvm/bin/clang++ -std=c++17 -O1 -g -pthread -fsanitize=thread \
  -Iinclude tests/spsc_concurrency.cpp -o /tmp/hw10_tsan
SPSC_N=200000 /tmp/hw10_tsan
CXX=/opt/homebrew/opt/llvm/bin/clang++ python3 tests/run_ci.py
```

The system Apple Clang 14 TSan runtime crashes on this machine even for an empty
`main`; the installed Homebrew Clang 23 runtime passes. In the full repository,
`make spsc` runs the functional checks and `CXX=/opt/homebrew/opt/llvm/bin/clang++
make test` also grades other assignments. Their failures are outside HW10; the
submission archive contains only the HW10 header, unchanged course tests/grader,
and this report.
