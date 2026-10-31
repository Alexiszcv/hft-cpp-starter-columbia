# HW9 — Seqlock: one writer, many readers

## Implementation

`include/seqlock.hpp` publishes a quote: bid/ask prices and their quantities.
The writer marks the sequence **odd**, updates the data, then publishes an
**even** sequence. `read(out)` copies the data and accepts it only if the
sequence stayed unchanged and even. Otherwise it returns `false`, leaves `out`
unchanged, and the caller retries. Readers acquire no mutex.

The 24-byte quote uses three atomic 64-bit words. Release publication and the
reader's initial acquire load make completed updates visible. A release fence
before payload stores and an acquire fence after payload loads prevent
accepting mixed updates under the
[C++17 fence synchronization rule](https://timsong-cpp.github.io/cppwp/n4659/atomics.fences#2).
Requirements: one writer, fully initialized trivially copyable data without
padding, lock-free 64-bit atomics, and no complete sequence wrap during a read.
Repeated retries can starve a reader.

## Measured results

Apple M1 MacBook Air, 8 GB, macOS 26.4; Apple Clang 14.0.3, C++17,
`-O3 -DNDEBUG -pthread`. `hw9/bench.cpp` compares against `std::shared_mutex`
with one continuously writing thread. Each column is the median of nine
200 ms samples, each preceded by a separate 50 ms warm-up trial. Case order
alternates; threads are not pinned. The course's `tests/bench.hpp` supplies the
optimizer sink and counter.

| Store | Readers | ns/read | Timer ticks/read | Mreads/s | Mwrites/s | Retries |
|---|---:|---:|---:|---:|---:|---:|
| `shared_mutex` | 1 | 219.025 | 5.257 | 4.565 | 13.396 | 0% |
| Seqlock | 1 | 690.505 | 16.572 | 1.448 | 16.881 | 99.190% |
| `shared_mutex` | 4 | 957.141 | 22.971 | 4.178 | 2.649 | 0% |
| Seqlock | 4 | 18.310 | 0.439 | 218.383 | 7.967 | 83.697% |

Cost = summed reader elapsed time / successful reads, including retries,
loop overhead and scheduling. Throughput = accepted reads / wall time from
start signal through joins. Reads may repeat the same update. Retries are the
percentage of rejected attempts. On M1 the counter is a **24 MHz timer, not CPU
cycles**; ns/read comes from `steady_clock`. True CPU cycles were not measured.

**Most useful change:** removing shared-lock acquisition eliminates reader
updates to shared mutex bookkeeping. The seqlock is **52.27× faster with four
readers**, but **3.15× slower with one**, where 99.19% of attempts fail.
These compare complete algorithms under saturation: writer rates differ, and
scheduling/cache contention also affect the results.

## Correctness and ThreadSanitizer

The test encodes update `n` as `{n, n + 0.5, n, n XOR 0xa5a55a5a}` and checks
these relationships on every accepted read. One writer and four readers
produced **24,534,794 seqlock reads with zero mixed values**. Every reader saw
multiple updates; final-value, initial-state, sequential and rejected-output
checks passed, as did UndefinedBehaviorSanitizer.

A classic seqlock copying ordinary shared data has a C++ data race: rejecting
the copy afterwards does not fix it, and TSan can report it. Here shared words
are atomic; sequence checks and fences ensure coherence
([WG21 explanation](https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2022/p1478r8.html)).
**TSan remains unvalidated locally:** both this program and an independent
empty-main control exit with status 139 before output on the installed runtime.

## Reproduce

From the repository root, with the supplied `tests/bench.hpp`:

```sh
mkdir -p build/hw9
clang++ -std=c++17 -O3 -DNDEBUG -pthread -Wall -Wextra -Wpedantic \
  -Iinclude -Itests hw9/bench.cpp -o build/hw9/bench
./build/hw9/bench          # checks, then benchmark
./build/hw9/bench --check  # checks only

# Requires a working ThreadSanitizer installation:
clang++ -std=c++17 -O1 -g -pthread -fsanitize=thread \
  -Iinclude -Itests hw9/bench.cpp -o build/hw9/tsan
./build/hw9/tsan --check
```

The checked course CI has no HW9 test; these are local measurements.
Submission: `include/seqlock.hpp`, `hw9/bench.cpp`, this README.
