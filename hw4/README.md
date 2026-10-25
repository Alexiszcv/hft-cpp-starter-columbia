# HW4 — Course autograder validation

This note supplements the original HW4 submission. `include/pool.hpp` is copied
unchanged from that submission's `pool.hpp`; no allocator redesign was needed.
The pool owns one buffer and stores its free-list links inside unused slots.
`alloc()` removes the first free slot; `free()` returns it. Both are O(1).

Among HW1–HW6, the supplied `tests/run_ci.py` checks only HW4. HW1, HW2, HW3,
HW5 and HW6 are assessed separately; they do not need new autograder interfaces.
The completed HW4, HW7 and HW8 headers are now collected in this checkout so a
single course CI run can validate all three. Other unfinished challenge stubs
are outside this update. Course tests and the workflow are unchanged.

## Reproduce

```bash
make pool
make book
make rolling
```

The pool checks are `pool_basic`, `pool_full`, `pool_reuse` and `pool_pattern`.
The `pool_ns_per_op` metric times one `alloc()` + `free()` pair in a 16-byte,
eight-slot pool, with a compiler sink. The supplied `tests/bench.hpp` helper uses
`steady_clock`, 200,001 warmup pairs and 2,000,000 timed pairs. It reports one
mean, not a median of repeated runs, and does not time pool construction or a
full batch of allocations. These timings are separate from the original HW4
allocator and virtual-call comparisons.

## GitHub Actions

The consolidated CI run and its measured results will be recorded here once
validation completes. Local tests do not substitute for an actual CI result.
