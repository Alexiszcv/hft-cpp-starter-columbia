# HW9 — Seqlock: one writer, many readers

## Implementation and correctness

`include/seqlock.hpp` implements `SeqLock<T>`. `hw9/bench.cpp` tests it and
compares it with `MutexQuote`, which protects the same 24-byte quote with
`std::shared_mutex`. A quote contains the best buying/selling prices (`bid`,
`ask`) and their quantities (`bq`, `aq`). Readers must receive all four fields
from one update, even if the writer changes them during a read.

The sequence number describes publication: **even = stable, odd = writing**.
For example, the writer changes version 44 to 45, stores the new quote, then
publishes version 46. `read(out)` loads the version, copies the data locally,
and checks the version again. It succeeds only if both versions match and are
even; otherwise it returns `false`, leaves `out` unchanged, and the caller
retries. Readers acquire no mutex and never write to shared storage.

The payload uses three atomic 64-bit words, not a concurrently accessed plain
`Quote`. A final **release** store publishes the even version; the reader's
initial **acquire** load ensures it sees that publication's data or newer data.
A release fence after marking the version odd and an acquire fence after
loading the words ensure that observing any later writer's word prevents
accepting the old version. This uses the C++17
[atomic fence synchronization rule](https://timsong-cpp.github.io/cppwp/n4659/atomics.fences#2).

Assumptions: exactly one writer; fully initialized, trivially copyable data
without padding (checked for `Quote`); lock-free 64-bit atomics (checked at
compile time); no full sequence-counter wrap during one read. Each attempt is
bounded, but repeated retries can starve a reader under continuous writing.

The correctness test encodes update `n` as `{n, n + 0.5, n, n XOR 0xa5a55a5a}`.
Every accepted quote must satisfy all three relationships. One writer and four
readers produced **24,534,794 accepted seqlock reads with zero mixed values**;
the mutex control produced 923,367. Every reader observed at least two changes;
the final quote matched the writer's last update. Initial state, 1,000 sequential
round trips, and unchanged output after rejected attempts are also checked.
The same checks passed UndefinedBehaviorSanitizer.

## Measurements

Machine: Apple M1 MacBook Air, 8 GB RAM, macOS 26.4; Apple Clang 14.0.3,
C++17, `-O3 -DNDEBUG -pthread`. Each case uses one continuous, unthrottled writer.
Each reported column is the median of nine 200 ms samples, each preceded by a
separate, discarded 50 ms warm-up trial; case order alternates. Threads are not
pinned. The unchanged course `tests/bench.hpp` supplies the optimizer sink and
hardware counter.

| Store | Readers | ns / successful read | Counter ticks / read | Million reads/s | Million writes/s | Rejected attempts |
|---|---:|---:|---:|---:|---:|---:|
| `shared_mutex` | 1 | 219.025 | 5.257 | 4.565 | 13.396 | 0% |
| Seqlock | 1 | 690.505 | 16.572 | 1.448 | 16.881 | 99.190% |
| `shared_mutex` | 4 | 957.141 | 22.971 | 4.178 | 2.649 | 0% |
| Seqlock | 4 | 18.310 | 0.439 | 218.383 | 7.967 | 83.697% |

**What is timed:** summed reader-thread elapsed time divided by successful
reads, including rejected attempts, loop overhead and scheduling delays.
Throughput is total accepted reads divided by wall time from the start signal
through thread joins. Setup and correctness checks are outside performance
timing. These are amortized costs under contention, not individual-call latency
percentiles. Successful reads may return the same generation repeatedly:
reads/s counts snapshots read, not distinct market updates. On this M1,
`cycle_count()` measures a **24 MHz timer, not CPU
cycles**; `steady_clock` gives ns/op. Fractional timer ticks result from averaging
many reads. True CPU cycles/op were not measured.

**Most useful change:** removing shared-lock acquisition from every reader
avoids updates to mutex bookkeeping shared by all readers. With four readers,
the measured cost falls by **52.27×**, from 957.141 to 18.310 ns/success. However,
with one reader it is **3.15× slower**: 99.19% of attempts are rejected. The
unthrottled writers achieve different update rates, so this compares the two
complete algorithms under saturation, not at a matched publication frequency.
Scheduling and cache contention also affect these machine-specific results.

## ThreadSanitizer and reproduction

A classic seqlock copying an ordinary shared `T` races with the writer. Checking
the sequence afterwards does not undo that C++ data race, which TSan can report.
Our shared words are atomic; temporary copies are thread-local. The fences and
version checks establish snapshot coherence, which a clean TSan run alone
would not prove. See the discussion of seqlocks and atomic payloads in
[WG21 P1478R8](https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2022/p1478r8.html).

**Local TSan limitation:** the instrumented executable exits with signal 11
(status 139) before producing output. An independent empty-main TSan executable
also fails before entering `main`, including outside the sandbox. Thus no
successful TSan validation is claimed for the installed runtime.

From the HW9 repository root, with the course's `tests/bench.hpp` present:

```sh
mkdir -p build/hw9
clang++ -std=c++17 -O3 -DNDEBUG -pthread -Wall -Wextra -Wpedantic \
  -Iinclude -Itests hw9/bench.cpp -o build/hw9/bench
./build/hw9/bench          # correctness checks, then the measurements above
./build/hw9/bench --check  # correctness only

clang++ -std=c++17 -O1 -g -pthread -fsanitize=undefined \
  -fno-sanitize-recover=all -Iinclude -Itests hw9/bench.cpp -o build/hw9/ubsan
./build/hw9/ubsan --check

# Run with a working ThreadSanitizer installation:
clang++ -std=c++17 -O1 -g -pthread -fsanitize=thread \
  -Iinclude -Itests hw9/bench.cpp -o build/hw9/tsan
./build/hw9/tsan --check
```

Both repositories' `labs/week09.md` and CI files were checked. Their existing
autograder has no HW9 seqlock test: these are local results, not GitHub Actions
scores. Submission: `include/seqlock.hpp`, `hw9/bench.cpp`, this README.
