# Reactor heap maintenance (PR #333)

## Scope and policy

The previous policy rebuilt the entire owner heap as soon as a batch reached
64 entries, even with 100,000 or 200,000 outstanding timers. The absolute cutoff
is removed. `reactor_detail::shouldRebuildHeap(N, k)` compares estimated
incremental worst-case work with linear rebuild work. For `0 < k < N`,
`h = bit_width(N)` bounds the depth of the final heap. Comparing `k*h > N+k`
becomes `k > N/(h-1)`, without overflowing either multiplication or addition.
Empty heaps and batches at least as large as the existing heap use `make_heap`;
empty batches do nothing. This is a conservative cost model, not a claim that
both algorithms have identical constants or that every distribution has the
same crossover. In particular, dense increasing/equal batches can still favor
incremental insertion; preserving the linear worst-case path takes priority.

There is no additional allocation, scan, sampling, shared state or production
counter. Capacity growth, the `Task` representation and heap comparator are
unchanged. Only the choice between the two existing standard algorithms changes.

## Invariants reviewed

The existing prefix is a valid heap when ingestion starts. Both algorithms
extend it into a heap under the same strict `(fireAt, sequence)` comparator.
Keys, identifiers and callbacks are moved, not modified. The producer-side
scheduled heap, immediate FIFO, bounded ingestion and `undrainedTaskFrontier`
remain unchanged. Cancellation deduplication/retirement, expiry, callback limits,
time-budget deferral and the mutex-protected publication of transferred task
counts also remain unchanged. No thread boundary or gameplay code changes.
The integrated PR #332 statistics guards and PR #334 lifetime fixes are untouched.

Three new regression cases cover the policy including `SIZE_MAX`, actual
100,000-timer/64-arrival ingestion (including the incremental heap layout), and
mixed cancellation/deferral/new older timers/equal deadlines. Existing tests
cover concurrent producers, identifier wrap/reuse, expiry, bounded cancellation,
ownership release, admission and the scheduler/dispatcher pipeline.

## Reproduction

Configure the normal dependency environment with `BUILD_TESTING=ON`,
`ENABLE_TEST_LTO=OFF`, `BUILD_BENCHMARKING=ON` and `CMAKE_BUILD_TYPE=Release`.
Build `bench_reactor_heap` and the five test targets listed below. The benchmark
is intentionally not registered in CTest: elapsed times are not assertions.

```sh
./build/src/benchs/bench_reactor_heap > heap.csv
./build/src/benchs/bench_reactor_heap --integration > integration.csv
ctest --test-dir build --output-on-failure \
  -R '^test_(reactor|reactor_stress|reactor_config|dispatcher_admission|runtime_stats_gate)$'
```

The microbenchmark uses the real move-only `Task` type and comparator, fixed seed
12345, heaps of 0/64/1,000/10,000/100,000/200,000 entries and batches of
1/16/32/64/128/1,024, plus 32,768/200,000 where no larger than the existing heap.
It compares forced rebuild, forced incremental insertion, the previous policy
and the adaptive policy. Each case has three warmups and 21 measured samples;
CSV contains median and empirical p95 nanoseconds. Allocation, key generation,
construction of the existing heap and verification are outside the timed region.
Every resulting heap is verified and its root consumed through a volatile sink.
Nanosecond-scale small cases are timer-resolution/noise limited.

`--integration` seeds a future owner heap and scheduled inbox outside timing,
then calls the actual `drainInbox()` or `runOnce()`. Ingestion is unbounded for
these controlled batches; future callbacks are not executed. It includes producer
heap removal, moves, publication and scratch cleanup, but excludes producer
submission, allocations during initial setup, gameplay and callback cost.
The baseline executable was linked before changing production code at
`553a912da504ca400080feb699b2d3f291cff441`; the candidate uses this patch. Both
use identical keys, order, counts, compiler and build configuration.

## Results and validation

Linux Release validation passed all five selected CTest targets. The final
`test_reactor` executable also passed all 45 cases after adding the actual
incremental-layout assertion. This includes the 200,000-task concurrent-producer
and 250,000-task fairness regressions in `test_reactor_stress`.

Environment: Intel Core i5-10300H, Ubuntu 24.04 under WSL2, GCC 13.3.0,
`-O3 -DNDEBUG -march=native -mtune=native`, statistics compiled in and test LTO
disabled. Preliminary forced-algorithm comparisons confirmed the large-heap,
small-batch penalty; a full baseline integration run was also captured locally.
The final controlled before/after timing run was not completed: publication
was requested before the remaining validation finished. No final percentage
improvement is claimed. The reproducible probe is included for follow-up.

Windows/MSVC 19.51 Release and ASan/UBSan builds were still in progress at that
cutoff; their tests are not reported as passed. A TSan runtime probe aborted
before tests with `FATAL: ThreadSanitizer: unexpected memory mapping` in this
WSL environment. CodeRabbit CLI was not installed.

Results are component measurements, not a 400–600-player/stressbot gameplay
benchmark. They do not establish server capacity or end-to-end latency.
