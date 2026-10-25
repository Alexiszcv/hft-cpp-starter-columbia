# HW4 — Course autograder validation

This note supplements the original HW4 submission. `include/pool.hpp` is copied
unchanged from the existing local `HW/HW4/pool.hpp`; no redesign was needed.
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

[Run 40237655710](https://github.com/Alexiszcv/hft-cpp-starter-columbia/actions/runs/40237655710)
validated `main@c65be5f` on 2026-10-25 (UTC), on GitHub-hosted Ubuntu 26.04
with `g++ -std=c++17 -O2 -pthread`.

| HW4 course check | Result |
|---|---:|
| Pool: basic alloc | Pass, 5/5 |
| Pool: capacity limit | Pass, 5/5 |
| Pool: slot reuse | Pass, 5/5 |
| Pool: churn pattern | Pass, 10/10 |

**`pool_ns_per_op`: 0.955937 ns per alloc/free pair.**
All four checks pass, earning 25/25 internal autograder points for HW4's pool.
This validates the pool portion; the rest of HW4 is covered by the original
submission and write-up.

The same run also passes both HW7 checks (20/20) and both HW8 checks (20/20):

| Metric | ns/op | Timed operation |
|---|---:|---|
| `book_bbo_ns_per_op` | 1.01750 | `best_bid()` + `best_ask()` |
| `symmap_get_ns_per_op` | 11.54390 | One successful lookup among eight symbols |
| `rolling_ns_per_op` | 3.79156 | One `add(t)` + `count(t)` pair |

These eight checks earn **65/65 internal points**. The overall **95/230** also
includes other assignments and project components; it is not the combined
CourseWorks grade. Earlier HW7/HW8 reports retain their own run references and
measurements. Differences between runs are not evidence of code changes or a
speedup.

Use **Actions → autograde → Run workflow → branch `main`** for the cumulative
report. The `hw7` and `hw8` branches remain individual homework snapshots.
