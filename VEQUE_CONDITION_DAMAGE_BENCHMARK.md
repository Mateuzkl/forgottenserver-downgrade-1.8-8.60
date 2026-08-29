# ConditionDamage `damageList`: `std::list` vs `veque`

Date: 2026-08-29<br>
Branch: `perf/veque-condition-damage`<br>
Baseline: `3646d629` (`main`)<br>
Platform: WSL2 Ubuntu 24.04, Linux 6.18.33.2, x86-64<br>
Compiler: GCC 13.3.0<br>
Build system: CMake 3.28.3 + Ninja 1.11.1

## Conclusion

**SAFE TO MERGE**

The migration is restricted to `ConditionDamage::damageList`. The Linux Release build and the complete non-benchmark test suite pass on both the baseline and the branch. The dedicated behavior tests pass under Release and under AddressSanitizer plus UndefinedBehaviorSanitizer. No tested benchmark regressed: CPU time improved from 9.29% to 98.04%, depending on the workload. Valgrind found no invalid access or leak, and the queue-churn workload reduced allocation calls by 98.38%.

ThreadSanitizer compiled successfully after retaining its third-party warning as a warning, but its runtime aborted before `main` with `FATAL: ThreadSanitizer: unexpected memory mapping` under WSL2. Therefore TSan is explicitly inconclusive, not clean or failed. A native Windows toolchain was not available on this host.

## Scope and implementation

Only the following member changed:

```cpp
// before
std::list<IntervalInfo> damageList;

// after
veque::veque<IntervalInfo, veque::std_vector_traits> damageList;
```

`veque::std_vector_traits` is the upstream queue-oriented configuration: it permits efficient `push_back`, `front`, and `pop_front` while keeping contiguous blocks. The upstream single-header implementation is vendored with its Boost Software License 1.0. No other gameplay container, queue, API, or behavior was migrated.

Changed files:

- `src/condition.h`: include `veque` and change only `ConditionDamage::damageList`.
- `src/third_party/veque/veque.hpp`: upstream `veque` 1.4.0 header (commit `8764fa7d33727daf55dbd6c9e95ffe690a308980`).
- `src/third_party/veque/LICENSE`: upstream Boost Software License 1.0.
- `src/tests/test_condition_damage_queue.cpp`: behavioral/regression tests.
- `src/benchs/bench_condition_damage.cpp`: Google Benchmark A/B workload.
- `VEQUE_CONDITION_DAMAGE_BENCHMARK.md` and `.html`: reproducible results.

No production implementation in `src/condition.cpp` was changed. Its existing queue operations and serialization order remain intact.

## Safety audit

All 19 uses of `damageList` were inspected before the type change.

- No iterator is stored across a mutation.
- No pointer into the container is retained.
- The two `pop_front()` sites copy all values needed after removal into scalars first.
- References obtained from `front()` are not used after `pop_front()`.
- Range iteration does not mutate the container.
- Copy construction/assignment remains a deep value copy.
- Serialization still iterates front-to-back and writes exactly the same fields in the same order.
- Unserialization still appends every decoded `IntervalInfo` with `push_back()`.
- `clear()`, `empty()`, `size()`, iteration, `front()`, `push_back()`, and `pop_front()` are supported by the selected traits.

Measured object layout on this compiler:

| Type | Baseline | `veque` | Difference |
|---|---:|---:|---:|
| `ConditionDamage` | 96 bytes | 104 bytes | +8 bytes (+8.33%) |
| `IntervalInfo` | 12 bytes | 12 bytes | unchanged |

The +8-byte object cost is outweighed in the measured dynamic workloads by fewer allocations and lower peak memory.

## Tests added

Nine automated test cases cover all requested behaviors:

1. poison, fire, and energy;
2. several `IntervalInfo` entries;
3. first, intermediate, and last tick;
4. front removal and empty queue;
5. expired condition;
6. update of an existing condition, including weaker and forced update paths;
7. removal/destruction during execution;
8. independent `ConditionDamage` copy;
9. exact legacy serialization bytes and unserialization;
10. small and large damage values;
11. 10,000 simultaneous conditions;
12. operation-by-operation regression comparison against a local `std::list` reference model.

## Build and test results

Both A and B used the same Release settings: `-O3 -DNDEBUG`, project strict warnings (`-Wall -Wextra -Wnon-virtual-dtor -pedantic -Werror`), native optimization disabled, unity build disabled, and the project Release IPO/LTO configuration.

| Validation | Baseline | `veque` |
|---|---:|---:|
| Full Linux Release build | 233/233 targets | 233/233 targets |
| Warnings/errors | 0/0 | 0/0 |
| New queue tests | 9/9 pass | 9/9 pass |
| Complete non-benchmark CTest suite | 41/41 pass | 41/41 pass |
| Suite wall time | 4.66 s | 5.94 s |

The suite wall-time difference is not a benchmark: test processes and filesystem startup dominate it. Performance conclusions below use pinned, repeated Google Benchmark CPU measurements.

### Sanitizers

| Tool | Result |
|---|---|
| ASan + UBSan | Dedicated 9/9 queue tests passed; zero sanitizer diagnostics and zero reported leaks. |
| Valgrind Memcheck | 0 errors; 0 definitely/indirectly lost bytes on both A and B. |
| TSan | Instrumented target built 142/142. Runtime unavailable on WSL2: `unexpected memory mapping` before `main`; inconclusive. |

The first parallel ASan/UBSan attempt exhausted the 7.7 GiB WSL link environment while several instrumented executables linked concurrently. Retrying the directly relevant target serially succeeded and ran cleanly. The complete suite was already validated in Release.

## Benchmark method

- Same host, compiler, dependencies, build type, flags, benchmark source, and CPU affinity.
- CPU pinned with `taskset -c 0`.
- Warm-up supplied by Google Benchmark calibration.
- Balanced order: A x5, B x5, B x5, A x5.
- 10 raw repetitions per scenario, `--benchmark_min_time=0.10s`.
- CPU time is reported in nanoseconds per benchmark iteration.
- For ten observations, nearest-rank p95 is the maximum.

Scenarios exercise actual `ConditionDamage` construction, queue population, ticking, copying, and stream serialization. Damage delivery is intentionally suppressed by a test creature so the measurement isolates condition queue behavior rather than combat scripting.

## CPU benchmark: before vs after

Lower is better.

| Scenario | Baseline mean ns | `veque` mean ns | Change | Baseline median | `veque` median | Baseline min | `veque` min | Baseline max/p95 | `veque` max/p95 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 condition, 4 ticks | 296.497 | 268.943 | **-9.29%** | 296.976 | 264.447 | 277.689 | 242.479 | 317.375 | 307.218 |
| 1 condition, 1,000 ticks | 79,778.377 | 55,109.172 | **-30.92%** | 79,174.574 | 55,119.905 | 74,100.676 | 51,385.905 | 86,715.380 | 58,003.352 |
| 100 creatures with DoT | 71,953.631 | 35,227.339 | **-51.04%** | 70,893.703 | 34,754.100 | 68,694.201 | 31,422.402 | 79,474.649 | 39,056.618 |
| 1,000 creatures with DoT | 762,328.703 | 342,220.721 | **-55.11%** | 772,104.487 | 338,682.905 | 704,350.000 | 328,899.361 | 814,847.059 | 382,213.099 |
| 10,000 conditions | 9,906,422.333 | 4,896,878.553 | **-50.57%** | 9,779,661.667 | 4,777,969.792 | 8,089,240.000 | 4,451,177.778 | 11,364,800.000 | 5,689,325.926 |
| Queue churn, 10,000 operations | 930,030.909 | 650,841.120 | **-30.02%** | 929,945.508 | 634,083.929 | 867,225.444 | 598,038.288 | 975,081.657 | 735,648.168 |
| Copy, 8 ticks | 152.320 | 27.172 | **-82.16%** | 150.471 | 27.193 | 132.622 | 25.839 | 178.187 | 28.408 |
| Copy, 1,000 ticks | 24,609.280 | 481.697 | **-98.04%** | 24,694.159 | 486.850 | 22,374.556 | 439.963 | 26,461.295 | 534.921 |
| Serialize/unserialize, 8 ticks | 908.886 | 814.591 | **-10.37%** | 903.231 | 811.406 | 838.555 | 763.007 | 1,043.691 | 909.708 |
| Serialize/unserialize, 256 ticks | 22,901.805 | 14,373.501 | **-37.24%** | 22,986.875 | 14,361.025 | 21,599.532 | 13,920.007 | 24,726.012 | 14,763.288 |

All ten measured scenarios improved. No significant CPU-time regression was observed.

The mean wall time of the complete benchmark process was 8.645 s for A and 9.085 s for B (+5.09%). That whole-process value includes executable startup, page faults, filesystem activity, scheduler noise, and reporting; it conflicts with every per-scenario CPU timer and is not used as the queue performance result.

## Allocation and memory measurements

Valgrind used the exact same queue-churn workload with exactly ten benchmark iterations.

| Metric | Baseline | `veque` | Change |
|---|---:|---:|---:|
| Heap allocation calls | 101,517 | 1,647 | **-98.38%** |
| Heap frees | 101,510 | 1,640 | **-98.38%** |
| Total bytes allocated | 21,572,465 | 22,301,025 | +3.38% |
| Definitely lost | 0 | 0 | unchanged |
| Indirectly lost | 0 | 0 | unchanged |
| Memcheck errors | 0 | 0 | unchanged |
| Massif peak heap + overhead | 18,556,768 B | 18,451,664 B | **-0.57%** |
| Mean peak RSS, two full runs | 13,444 KiB | 10,450 KiB | **-22.27%** |

The higher cumulative allocated-byte count comes from geometric contiguous growth, while allocation count and peak memory both fall. Memcheck's seven 528-byte process-lifetime blocks were identical in A and B and were not definitely or indirectly lost.

## Cache and branch measurements

Collected with Valgrind Cachegrind (`--cache-sim=yes --branch-sim=yes`) for the same exact ten-iteration queue-churn workload. These are simulated counts, not hardware `perf` counters.

| Metric | Baseline | `veque` | Change |
|---|---:|---:|---:|
| Instructions | 59,395,166 | 36,310,154 | **-38.86%** |
| Data references | 26,253,337 | 19,772,947 | **-24.68%** |
| D1 misses | 239,533 | 182,086 | **-23.99%** |
| Last-level data misses | 48,785 | 48,092 | **-1.42%** |
| Combined cache references | 248,249 | 190,923 | **-23.09%** |
| Combined last-level misses | 54,762 | 54,089 | **-1.23%** |
| Branches | 8,992,831 | 6,818,159 | **-24.18%** |
| Branch mispredicts | 478,374 | 52,358 | **-89.05%** |
| Branch mispredict rate | 5.3% | 0.8% | -4.5 pp |

I1 misses increased from 8,716 to 8,837 (+1.39%) and last-level instruction misses from 5,977 to 5,997 (+0.33%); these tiny absolute increases did not produce a runtime regression.

## Commands actually executed

Representative commands (A used `/home/mateus/tfs-veque-a`, B used `/home/mateus/tfs-veque-b`):

```bash
cmake -S . -B build-veque-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DBUILD_BENCHMARKING=ON \
  -DENABLE_NATIVE_OPTIMIZATIONS=OFF -DENABLE_UNITY_BUILD=OFF
cmake --build build-veque-release -j8
ctest --test-dir build-veque-release --output-on-failure -E '^bench_'
./build-veque-release/src/tests/test_condition_damage_queue

taskset -c 0 ./build-veque-release/src/benchs/bench_condition_damage \
  --benchmark_min_time=0.10s --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=false --benchmark_out=run.json \
  --benchmark_out_format=json

cmake -S . -B build-veque-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DBUILD_BENCHMARKING=OFF \
  -DENABLE_ASAN=ON -DENABLE_NATIVE_OPTIMIZATIONS=OFF \
  -DENABLE_UNITY_BUILD=OFF \
  -DCMAKE_CXX_FLAGS=-fsanitize=undefined \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=undefined
cmake --build build-veque-asan --target test_condition_damage_queue -j1
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ./build-veque-asan/src/tests/test_condition_damage_queue

cmake -S . -B build-veque-tsan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DBUILD_BENCHMARKING=OFF \
  -DENABLE_TSAN=ON -DENABLE_NATIVE_OPTIMIZATIONS=OFF \
  -DENABLE_UNITY_BUILD=OFF -DCMAKE_CXX_FLAGS=-Wno-error=tsan
cmake --build build-veque-tsan --target test_condition_damage_queue -j1
TSAN_OPTIONS=halt_on_error=1:abort_on_error=1:second_deadlock_stack=1 \
  ./build-veque-tsan/src/tests/test_condition_damage_queue

valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=99 \
  ./build-veque-release/src/benchs/bench_condition_damage \
  --benchmark_filter='bench_queue_churn/operations:10000$' \
  --benchmark_min_time=10x
valgrind --tool=massif --stacks=no --massif-out-file=massif.out \
  ./build-veque-release/src/benchs/bench_condition_damage \
  --benchmark_filter='bench_queue_churn/operations:10000$' \
  --benchmark_min_time=10x
valgrind --tool=cachegrind --cache-sim=yes --branch-sim=yes \
  --cachegrind-out-file=cachegrind.out \
  ./build-veque-release/src/benchs/bench_condition_damage \
  --benchmark_filter='bench_queue_churn/operations:10000$' \
  --benchmark_min_time=10x
```

## Limitations and remaining risks

- Results are from one WSL2 host, one compiler, and one CPU affinity. Native Linux production hardware may have different absolute results.
- Ten observations make p95 coarse; it equals the maximum by nearest-rank definition.
- TSan could not start because of a WSL2 shadow-memory mapping conflict. No TSan result is claimed.
- No MSVC/Windows build was possible because this host exposes no `cl.exe`, MSBuild, Ninja for Windows, or vcpkg installation.
- Hardware `perf stat` and heaptrack were not available; Cachegrind, Massif, Memcheck, `/usr/bin/time`, and Google Benchmark supplied the reported metrics.
- Benchmarks isolate the actual `ConditionDamage` queue mechanics with combat damage suppressed. They are not a live networked game-server load test.
- As with any vector-like segmented container, element addresses and iterators must not survive mutation. The current production call sites do not retain them; future code must preserve that rule.

Within the requested scope and measured environment, behavior is equivalent, the migration is memory-safe in ASan/UBSan and Valgrind, and the benchmark shows no regression. Therefore the branch is **SAFE TO MERGE**.
