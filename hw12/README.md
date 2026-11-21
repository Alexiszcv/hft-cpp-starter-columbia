# HW12 — Fast uint64 to decimal and numeric field encoder

## Implementation

`include/u64toa.hpp` preserves the course interface: `int u64toa(uint64_t, char*)`.
It writes 1–20 decimal digits and returns their count, without a NUL terminator.
The caller provides at least 20 writable bytes; use 21 bytes and append `out[n]=0`
if a C string is needed. Zero produces `0`; UINT64_MAX produces
`18446744073709551615`. Conversion uses no heap allocation or locale processing.

The implementation follows `labs/week12.md`: the benchmark's **before** version
is the lab's `/10` reverse-then-flip algorithm; the final version uses the lab's
`00`–`99` lookup table and `/100` digit batching. It extends that optimization by
splitting large values into at most three base-100,000,000 blocks. The leading
block has 1–8 digits; each following block writes exactly eight digits using four
pairs. This keeps most arithmetic in uint32_t and writes directly into the output.
For example, UINT64_MAX becomes `1844 | 67440737 | 09551615`, retaining the zero
inside the last block. No value as large as 10^20 needs to be represented.

The bonus `encode_u64_field(uint32_t tag, uint64_t value, char* out)` writes
`tag=value<SOH>`, returns its length and needs at most **32 writable bytes**.
It writes no NUL; tag zero returns zero without writing. Multiple fields can be
appended using the returned lengths. Example: tag 38 and value 100 produce
`38=100\x01` (seven bytes).

## Measured results — 2026-11-21

macOS 26.4, arm64, Apple Clang 14.0.3, C++17 `-O2 -pthread`. The benchmark uses
six corpora of 2,048 values generated at runtime, including zero and UINT64_MAX.
Digit lengths are approximately balanced within each range and shuffled with a
fixed seed. Input generation and validation are outside timing. Each trial has
50 warmup repetitions and 500 timed repetitions (**1,024,000 conversions**).
Results are the **median of 11 trials**, rotating variant order. Inputs are reused
in cache; these are amortized conversion times, not network latency.

Nanoseconds per conversion, comparing final / lab-before / sprintf / std::to_string:

- 1 digit: **1.548 / 2.379 / 54.026 / 8.749**.
- 2–4 digits: **4.436 / 8.744 / 66.229 / 18.855**.
- 5–9 digits: **7.172 / 17.227 / 77.146 / 19.903**.
- 10–15 digits: **11.403 / 33.470 / 89.173 / 21.084**.
- 16–20 digits: **12.629 / 44.476 / 94.938 / 23.038**.
- Mixed 1–20 digits: **7.413 / 25.486 / 86.267 / 23.644**.

For the mixed corpus, the final implementation reaches **134.907 million values/s**:
**3.438x faster than the lab baseline**, **11.638x faster than sprintf** and
**3.190x faster than std::to_string**. Corresponding timer ticks/op are
**0.178 / 0.612 / 2.070 / 0.567**, respectively. `results.txt` contains all trials,
per-range throughput, timer ticks and speedups, not only the mixed summary.

The most useful change was batching the conversion into fixed-width eight-digit
blocks, each emitted using the lab's two-digit table. It reduces long chains of
64-bit divisions and replaces variable-length reversal/copying with direct pair
writes and fixed-size loops on 32-bit blocks. The benefit is especially visible
for 16–20 digits: 44.476 ns for the lab baseline versus 12.629 ns for the final
algorithm. Small numbers keep a short path instead of paying for every block.

All output digits and lengths are observable to the compiler. `sprintf` includes
its required NUL write; `std::to_string` includes string construction/destruction
without an extra imposed copy. It need not allocate on every call: small-string
storage is library-dependent. The final code beats both references in each
measured range on this machine; that is not a claim about every compiler or CPU.

For cycles/op, the provided ARM counter is a **24 MHz virtual timer, not a CPU
cycle counter**. The benchmark labels these as timer ticks and uses
`steady_clock` ns/op as the portable timing metric. It does not invent CPU cycles
from frequency. On x86 the same program measures serialized TSC reference ticks.

## Validation and reproduction

- Unchanged course tests: edge cases and **200,000 pseudo-random values pass**.
- Additional checks: **534,882 assertions pass**, covering 10,000 exhaustive small
  integers, all powers-of-ten boundaries, 20,000 values across magnitudes,
  exact-size buffers/canaries, field concatenation and zero-tag rejection.
- No counted C++ heap allocations during 102,400 conversions plus 102,400 field
  encodings; AddressSanitizer and UndefinedBehaviorSanitizer checks pass.
- Full local grader with Homebrew Clang 23.1.5: **HW12 20/20 internal points**
  (8 points for edge cases + 12 for fuzz), overall **200/230**. Remaining failures concern the
  shared-memory ring and arena-client build. This is local validation, not a CI run.

The official timing test repeats one constant and makes only the length and first
character observable. Its timing is therefore kept separate from the varied-input,
all-digits benchmark above. Course test files were not modified.

From the repository root, or the extracted ZIP with its directory layout intact:

```bash
make u64toa
make u64toa-check
make u64toa-bench CXX=c++ CXXFLAGS='-std=c++17 -O2 -pthread -Iinclude -Itests'
```

Memory checks with a compiler providing working sanitizer runtimes (tested with
Homebrew Clang 23.1.5):

```bash
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iinclude hw12/check.cpp -o /tmp/hw12_sanitized
/tmp/hw12_sanitized
```

In the complete starter, `CXX=/opt/homebrew/opt/llvm/bin/clang++ make test` runs
the full local grader. GitHub Actions runs the course grader after a push. The ZIP
contains the header, benchmark/check sources, this note, raw results, Makefile,
and the unchanged course test and benchmark helper needed by the three commands.
