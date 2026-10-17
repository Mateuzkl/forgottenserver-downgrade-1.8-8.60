# BlackTek-inspired event fan-out — validation

Baseline: `7bc92ba6cc0c026f2089a162909b904265f7c9d7`.
Read the [audit](blacktek-combat-audit.md) for capture identity, provenance,
scope and ownership contracts. All testing here uses disposable in-memory world
fixtures; it does not load or rewrite the user's map, DB or production config.

## Local toolchain

WSL Ubuntu 24.04, GCC 13.3, CMake/Ninja, C++23, existing Lua 5.5 dependencies.
Production `tfs` and benchmark use Release `-O3`, native CPU and LTO. Normal test
library uses `ENABLE_TEST_LTO=OFF`; sanitizer build uses Debug, ASan, UBSan,
leak detection and no mimalloc. No dependencies were installed for this patch.
The existing Windows executable is not replaced by a Linux build.

## Test coverage

`test_combat_fanout` exercises the actual Game/Player/ProtocolSpectator/
ProtocolGame/output-copy route against a source-level oracle of the original
`7bc92ba6` checks, routing and serializer:

- Zero, 1, 10, 100, 300 and 600 independent output buffers, ordered events and
  effect IDs 0, 255, 256 and 65535 (no 8-bit truncation).
- Owner, cast and spies, different viewports/floors, spy viewport, expired owner
  with surviving followers, null snapshot entries, a lost player client and a
  protocol whose player has gone away.
- Lazy preparation with no visible recipient; ground/tile removal and a lost
  viewer are rechecked even after another recipient prepared the bytes.
- Instance filtering and teleport between events remain effective.
- Event-local storage goes out of scope without corrupting queued output;
  subsequent events do not inherit the previous ID.
- Output rollover preserves the previous message and equal bytes for followers.

The existing combat, condition lifetime/queue, Lua userdata, map/spectator and
connection-write lifetime suites remain relevant. This is not a new exhaustive
test of every combat Lua script or every GM/ghost/invisibility combination; the
effect path's existing position-only checks and instance caller filters are
preserved, not replaced with creature visibility rules.

## Reproduction

Run from the checkout in WSL, reusing the existing configured build directories:

```bash
cmake -S . -B build-tests-stressbots-audit
task_test_targets=()
for task_source in src/tests/test_*.cpp; do
  task_name="${task_source##*/}"
  task_test_targets+=("${task_name%.cpp}")
done
cmake --build build-tests-stressbots-audit --target \
  tfs bench_combat_fanout "${task_test_targets[@]}" -j2
env -u TFS_TEST_DB_HOST -u TFS_TEST_DB_USER -u TFS_TEST_DB_PASS \
  -u TFS_TEST_DB_NAME -u TFS_TEST_DB_PORT \
  ctest --test-dir build-tests-stressbots-audit --output-on-failure \
  --timeout 90 -R '^(test_|check_)'

./build-tests-stressbots-audit/src/benchs/bench_combat_fanout \
  --benchmark_repetitions=7 --benchmark_min_time=0.2s \
  --benchmark_enable_random_interleaving=true --benchmark_min_warmup_time=0.1 \
  --benchmark_out=build-tests-stressbots-audit/blacktek-fanout-final.json \
  --benchmark_out_format=json

cmake -S . -B build-asan-stressbots-audit
cmake --build build-asan-stressbots-audit --target \
  test_combat_fanout test_combat_packets test_combat \
  test_condition_shared_lifetime test_condition_damage_queue \
  test_connection_write_lifetime test_spectators test_map_spectators \
  test_lua_userdata_gc -j1
ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1 UBSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir build-asan-stressbots-audit --output-on-failure \
  --timeout 120 -R '^test_(combat_fanout|combat_packets|combat|condition_shared_lifetime|condition_damage_queue|connection_write_lifetime|spectators|map_spectators|lua_userdata_gc)$'
```

Rebuild all selected test executables before running CTest after library changes.
Do not benchmark while another compilation or load test is running. The benchmark
includes recipient traversal, owner weak-lock, view/ground checks, separate output
buffers, append/copy and equal reset/observation overhead in both variants. The
baseline is the pinned source-level oracle, not an independently deployed old
server binary. Cast/spy cases use one of each per owner (three wire recipients).
Output buffers are preallocated: this measures buffered append, not fresh socket
queue admission. It excludes sockets, encryption, live queue pressure and actual
combat logic.

## Results

Linux Release, native Windows and sanitizer validation are complete. These are
component/regression checks, not live-server results.

### Build and regression gates

| Gate | Completed result |
| --- | --- |
| Linux production Release/LTO build | Passed (`tfs` and benchmark) |
| Linux full normal CTest selection | 55/55 passed, 14.09 s (50 executables + 5 static checks) |
| New event fan-out regressions | 7 cases passed within `test_combat_fanout` |
| Read-only Perf parser regression | Passed, including pt-BR and incomplete-window handling |
| Windows production Release/LTO build | Passed with MSVC 19.51.36260.0 / CMake 4.3.1-msvc1 / SDK 10.0.26100 |
| Windows selected native CTest | 5/5 passed, 0.47 s: combat, fan-out, packets, condition ownership and connection-write lifetime |
| ASan + UBSan + leak detection | 9/9 passed, 7.97 s |
| Same sanitizer selection with stack-use-after-return detection | 9/9 passed, 7.72 s; no sanitizer diagnostics |

The normal DB round-trip body was deliberately skipped: `TFS_TEST_DB_*` was unset.
Passing its test executable is not a database integration pass. No real DB,
production server or bot process was started. Windows dependencies were reused
from the existing `vcpkg_installed/x64-windows/x64-windows` prefix; no manifest
installation was enabled. The build emitted the existing `WIN32_LEAN_AND_MEAN`
macro redefinition warning and the disabled-manifest notice; neither was changed
as an unrelated cleanup.

Full normal test output: ignored `blacktek-full-tests.log`; native output:
`blacktek-msvc-tests.log` (plus each build's `Testing/Temporary/LastTest.log`).
Sanitizer output: `blacktek-asan-tests.log` and `blacktek-asan-stack-tests.log`.
These cover the nine selected executables, not every server/script path, and
external dependencies are not all sanitizer-instrumented. Passing them does not
certify the whole server free of leaks or lifetime bugs.
Linux candidate `tfs` SHA-256:
`E0EC0EDB06DEDD265677FB1E51E1D2B7063C4EB28848F0FC5961B842FC608F16`.
Windows candidate `build-msvc-blacktek-audit/Release/tfs.exe` SHA-256:
`D5C925C61C01C4B461B2130EE8315D74429EAFB862127E8A34DE72E8FF0E5FC5`.
The existing root Windows runtime was not replaced. The three audited OTBM map
hashes and the pre-existing raid/store changes still match the protected input.

Native reproduction uses the installed Visual Studio CMake/CTest, `Release`,
the existing dependency prefix via `CMAKE_PREFIX_PATH`, `BUILD_TESTING=ON`,
`BUILD_BENCHMARKING=OFF`, `SKIP_GIT=ON`, `USE_MIMALLOC=OFF`, and one build job.
Prepend that prefix's `bin` directory to the test process PATH for matching DLLs.
Run the five-test regex from the native result row; do not start `tfs.exe` as a
unit test or overwrite the user's runtime to validate the build.

### Release/LTO in-memory delivery benchmark

Seven repetitions, 0.1 s warm-up, 0.2 s minimum measurement per repetition,
random interleaving, no concurrent build. CPU median per event, microseconds:

| Owners | Cast + spy per owner | Wire recipients | Original oracle | Compact payload |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 0 | 0.00123 | 0.00129 |
| 1 | 0 | 1 | 1.108 | 1.124 |
| 1 | 1 + 1 | 3 | 3.426 | 1.305 |
| 10 | 0 | 10 | 11.389 | 2.011 |
| 10 | 1 + 1 | 30 | 33.740 | 3.795 |
| 100 | 0 | 100 | 115.471 | 10.919 |
| 100 | 1 + 1 | 300 | 346.492 | 29.099 |
| 300 | 0 | 300 | 358.547 | 31.705 |
| 300 | 1 + 1 | 900 | 1,251.316 | 159.229 |
| 600 | 0 | 600 | 799.923 | 84.068 |
| 600 | 1 + 1 | 1,800 | 2,872.451 | 395.570 |

CPU CV for the 600/0 case is 8.37% / 5.20% (original/candidate); for 600/1,
7.13% / 8.07%. The original 300/1 case is noisy (24.54% CV), so do not interpret
its ratio as precise. The installed Google Benchmark 1.8.3 library reports that
it was built as DEBUG; the benchmark executable and production library are
Release/LTO. Host scheduling/memory pressure and this library limit precision.

A second, focused run (nine repetitions, 0.2 s warm-up, 0.5 s minimum,
random interleaving, no build) confirms 600/0 CPU medians of 793.157 us versus
74.756 us, approximately 10.61x lower component time; CV is 7.34% / 5.78%.
At one recipient the confirmation is 1,118.82 ns versus 1,175.56 ns (+5.1%,
about 57 ns; CV 3.04% / 4.81%), not a single-recipient speedup. At zero it is
1.22 ns versus 1.25 ns. Retaining the small fixed bookkeeping trade-off is
explicit: the measured dense fan-out benefit is substantial, and the initial
microsecond-scale zero/one regressions below are gone. No claim of universal
per-call improvement or a 10.61x faster server is made.

The final implementation logically serializes once per eligible event rather
than once per eligible recipient; empty/invisible-only events never prepare it.
That invariant is visible in the guarded preparation and exercised by tests;
heap allocation counts were not separately instrumented. Wire payload is still
8 bytes per delivered effect per connection, with no recipients/effects dropped.

Ignored raw artifacts: `build-tests-stressbots-audit/blacktek-fanout-final.json`
and `blacktek-fanout-confirm.json`. SHA-256 respectively:
`C48F1CAAF77315F66C07D538E40AAA656BED32B62A2C35326944DAE801FEDD30`,
`D46AD510545969B70441BC850620E7CF820174218198C31FC8284E10AC742D12`.
Benchmark executable SHA-256:
`2492A1C24CCB1E5D0F08D5C85D916E416C429625C874DCB16D097BFCF199BEC3`.

An initial implementation cached `optional<NetworkMessage>` itself. Although its
large fan-out case improved, measurement caught regressions at zero/one recipient
(CPU medians approximately 1,253/2,496 ns versus 1/1,149 ns). Inspection of the
optimized binary confirmed a large memset at event entry and another at first
preparation. That cache representation was rejected and replaced with the
compact optional eight-byte payload, preserving the existing encoder and output
copy. Initial raw results remain in the ignored build directory as
`blacktek-fanout.json`; only the final representation is eligible for acceptance.

## Required live-load follow-up

Run the same binary/config/map/seed and useful actions with diagnostics off and
on, three repetitions per S0–S6 scenario: idle, movement-only, ordinary combat,
conditions, AoE, full 600 bots + 10 Stress Demons, then 1,000 only after the prior
gates pass. Record process CPU/RSS, completed attacks/actions per second, output
bytes/backpressure, queue latency, callback/creature-check tails and new nested
combat/condition metrics. Compare controlled baseline/candidate binaries, not
different historical captures. Use exclusive sampling stacks or correctly nested
exclusive timing before assigning CPU percentages within target application.

No live stress population or database was started here. Real 600-bot before/after
measurements remain a separate gate; there is no capacity or lag-elimination
guarantee from these build/tests and the in-memory benchmark.
