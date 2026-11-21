# HW11 — A high-performance FIX parser

`include/fix_parser.hpp` implements the exact course `NewOrder` / `parse_new_order`
interface. `include/fix_batch.hpp` adds allocation-free parsing of concatenated
messages into a caller-owned array. `bench.cpp` compares three implementations;
`check.cpp` contains additional correctness and allocation checks. Course tests
are unchanged.

## Implementation and supported input

The parser scans fields from left to right, dispatches on the numeric tag and
extracts only 11/55/54/38/44. Numeric slices are read again for conversion; there
is no split, map, or dynamically allocated temporary. ClOrdID is a pointer and
length into the input; symbol is copied into its fixed 16-byte array. The input
must remain alive and unchanged while any ClOrdID view is used.

Every field must have a positive numeric tag, `=`, a nonempty value and a trailing
SOH. Required fields must appear exactly once. Symbols have 1–15 bytes, side is
`1` or `2`, quantity is an integer in [0, UINT32_MAX], and price uses
`[+-]?digits[.digits]` with a finite double result. Quantity/tag overflow, missing
or duplicated required fields, embedded NULs and malformed numbers are rejected.
Unknown fields are skipped; tag 35, when present, must be `D`. A failed parse
leaves the output unchanged. Decimal conversion uses floating-point arithmetic,
so normal double rounding applies. Exponents, binary data fields and repeating
groups are outside this assignment's supported subset.

The batch parser requires `8=FIX.4.x` (x = 0–4) or `8=FIXT.1.1`, then tag 9,
then `35=D`. BodyLength locates the `10=ddd<SOH>` trailer: it counts bytes after
tag 9's SOH through the SOH before tag 10, as specified by the
[FIX Session Layer standard, section 10](https://www.fixtrading.org/wp-content/uploads/download-manager-files/FIX_Session_Layer_June_2020.pdf).
Trailer syntax and the range 000–255 are checked; its checksum sum is not
recomputed. This is an order extractor with framing, not a full FIX session engine.

`parse_new_order_batch(buf, len, orders, capacity)` returns `status`, `consumed`
bytes and `count` orders. `complete` means all input was consumed; `incomplete`
means retain the suffix starting at `consumed` and append more received bytes;
`output_full` means drain the output and resume from that suffix; `malformed`
means stop on invalid input. Only successfully emitted frames are consumed.
Use the existing order views **before** moving, compacting or reusing the buffer.

The course test's sample declares BodyLength 76 although its body is 45 bytes.
Consequently, the single-message field parser deliberately does not validate
8/9/10, while the batch helper checks framing. The benchmark creates its own
messages with correct BodyLength and checksum values.

## Local performance — 2026-11-21

macOS 26.4, arm64, Apple Clang 14.0.3, C++17 `-O2 -pthread`.
One operation is **framing + parsing one message**, using the same driver and
function-pointer calls for all variants. Each batch contains 256 varied messages
(21,901 bytes), including alternating sides, different prices/quantities/symbols,
reordered fields and an ignored field. The corpus is reused in cache. Inputs and
output storage are prepared before timing; naive-parser allocations are timed.
All fields and ClOrdID addresses are checked before measurement, and all output
fields are kept observable during it. The public batch API is checked separately.

Each of 11 trials has 200 warmup batches and 2,000 timed batches (512,000 messages).
Variant order rotates between trials. Results are medians of batch means;
throughput is derived from the median time. Full observations are in `results.txt`.

- Naive split/string/map: **2,047.027 ns/op**, 49.128 timer ticks/op,
  **0.489 million messages/s**, 41.793 MB/s.
- Before: forward scan + bounded stack-buffer `strtod`: **218.179 ns/op**,
  5.236 timer ticks/op, **4.583 million messages/s**, 392.113 MB/s.
- After: same scan + manual decimal conversion: **178.786 ns/op**,
  4.291 timer ticks/op, **5.593 million messages/s**, 478.510 MB/s.

Overall speedup is **11.450x versus naive** and **1.220x versus before**.
The largest improvement came from replacing string splitting and map construction
with direct parsing of the original buffer: this removed temporary ownership,
heap allocations and tree lookups, reducing 2,047.027 to 218.179 ns/op even before
the decimal optimization. Replacing `strtod` with bounded decimal arithmetic then
removed the temporary price copy and general-purpose conversion overhead, reducing
time by another approximately 18%. These comparisons describe this corpus and
machine, not network latency or a universal speedup over every string parser.

For the requested cycles/op metric, this ARM machine exposes a **24 MHz virtual
timer, not a CPU-cycle counter**. Its measured ticks are reported honestly above;
ns/op from `steady_clock` is the portable timing result. No CPU cycles are inferred
from clock frequency. On x86, the same benchmark reports serialized `__rdtsc`
reference ticks. `before_strtod` and the naive baseline are benchmark references
for valid inputs, not validators for the full production input contract.

## Validation and reproduction

- `make fix`: course test passes (95.5193 ns/op in the initial Apple Clang run;
  this is field parsing alone on the course's one repeated message).
- `make fix-check`: **2,546 checks pass**, including 2,000 deterministic numeric
  cases, exact-size buffers, all byte cuts of a two-message stream and resumption,
  malformed input, output capacity and zero counted C++ heap allocations during
  10,000 iterations of single-message and batch parsing.
- The same checks pass under **AddressSanitizer + UndefinedBehaviorSanitizer**.
- Full local course grader with Homebrew Clang 23.1.5: **FIX 15/15** internal
  points; overall 180/230. Remaining failures are u64toa (HW12), shared-memory
  ring and arena-client build, outside HW11. This is a local run, not a CI run.

From the repository root (the ZIP preserves this layout):

```bash
make fix
make fix-check
make fix-bench CXX=c++ CXXFLAGS='-std=c++17 -O2 -pthread -Iinclude -Itests'
```

Optional memory checks, using a compiler with working sanitizer runtimes:

```bash
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iinclude hw11/check.cpp -o /tmp/hw11_sanitized
/tmp/hw11_sanitized
```

In the complete course starter, `CXX=/opt/homebrew/opt/llvm/bin/clang++ make test`
reproduces the full local grader run. The submission ZIP includes the two headers,
benchmark, checks, this note, raw measurements, Makefile and the two unchanged
course test/helper files needed by the three HW11 commands above.
