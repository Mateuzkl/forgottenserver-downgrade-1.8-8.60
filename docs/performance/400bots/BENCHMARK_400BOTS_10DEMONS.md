# Validation: 400 stress bots + 10 Stress Demons

## Environment and input

Windows host, Intel Core i5-10300H (4 cores / 8 logical processors), WSL Ubuntu
24.04, GCC 13.3, CMake 3.28.3, Ninja, Lua 5.5. Existing dependencies and build
directories from the previous audit were reused, not installed or replaced.
Release/LTO production and non-LTO test library use the prior configuration;
Debug ASan/UBSan is a separate build. No gameplay server, database or stress
client was launched. The local Windows executable was not replaced.

Read the full supplied `CODEX_TFS18_CANARY_AI_COMBAT_PATHFINDING_400BOTS_10DEMONS.md`.
The source SHA and Canary clone SHA are in `FINDINGS.md`; the input log is
fingerprinted there. Log totals are observational evidence, not a baseline /
candidate replay. No call graph or flamegraph was fabricated from those totals.

## Reproducible commands

Run from the repository root in PowerShell / WSL as appropriate:

```powershell
./tools/analyze-performance.ps1 -Path ./data/logs/stats/server_20261015_122850.log
./tools/tests/test-analyze-performance.ps1
```

```bash
cmake -S . -B build-tests-stressbots-audit
cmake --build build-tests-stressbots-audit --target tfs \
  test_combat_packets test_performance_metrics test_monster_target_state \
  test_combat test_condition_damage_queue test_condition_shared_lifetime \
  test_reactor bench_combat_packets -j2

# Build every test_*.cpp target before the full CTest run.
env -u TFS_TEST_DB_HOST -u TFS_TEST_DB_USER -u TFS_TEST_DB_PASS \
  -u TFS_TEST_DB_NAME -u TFS_TEST_DB_PORT \
  ctest --test-dir build-tests-stressbots-audit --output-on-failure \
  --timeout 90 --output-log combat-400-full-tests.log -R '^(test_|check_)'

./build-tests-stressbots-audit/src/benchs/bench_combat_packets \
  --benchmark_min_time=0.3s --benchmark_min_warmup_time=0.1 \
  --benchmark_repetitions=7 --benchmark_enable_random_interleaving=true \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=build-tests-stressbots-audit/combat-packets-400.json \
  --benchmark_out_format=json

cmake -S . -B build-asan-stressbots-audit
cmake --build build-asan-stressbots-audit --target test_combat_packets \
  test_performance_metrics test_monster_target_state test_combat \
  test_condition_shared_lifetime test_condition_damage_queue -j1
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-asan-stressbots-audit --output-on-failure \
  --timeout 120 --output-log combat-400-asan-tests.log \
  -R '^test_(combat_packets|performance_metrics|monster_target_state|combat|condition_shared_lifetime|condition_damage_queue)$'
```

Full initial configuration/dependency paths are documented in
`../1000-stressbots-validation.md`. The reused sanitizer build has
`ENABLE_ASAN=ON`, Debug, `CMAKE_CXX_FLAGS=-fsanitize=undefined`, mimalloc off.
External dependencies are not all rebuilt with sanitizers.

## Rejected packet prototype: measured results

Seven repetitions, random interleaving, no concurrent builds during measurement.
Same position, effect ID, payload and append order; number of serialized payloads
per iteration is shown below. Both models are optimized together. The baseline
temporary is NOT artificially forced to escape, allowing dead-store elimination.

| Payloads / iteration | Existing model CPU median (ns) | Direct-write prototype (ns) | Existing / prototype CPU CV |
| ---: | ---: | ---: | ---: |
| 1 | 1.20 | 8.95 | 2.55% / 2.71% |
| 100 | 164 | 1,014 | 3.87% / 2.15% |
| 400 | 625 | 4,083 | 2.51% / 2.46% |

Decision: reject the direct-write production change. These are an isolated
serialization model, not timings for actual ProtocolGame methods or networked
bots. The installed Google Benchmark 1.8.3 library warns it was built as DEBUG;
the benchmark executable is Release/LTO. No server gain is inferred.
Raw local JSON: `build-tests-stressbots-audit/combat-packets-400.json` (ignored).

## Local correctness results

- Production executable rebuilt successfully in Release/LTO after rejecting the
  packet prototype; all 49 test executables were rebuilt against the final code.
- Full CTest: **54/54 passed** (49 executables + five static checks), 13.03 s.
  The DB roundtrip inside `test_inbox_db_roundtrip` was deliberately **skipped**;
  no `TFS_TEST_DB_*` environment variables were provided.
- Packet regressions: five cases; metrics regressions: three cases. Existing
  monster target-state tests plus the new 400-candidate case passed.
- PowerShell parser checks passed, including pt-BR culture and truncated input;
  real log analysis consistently found 288 complete windows and one exclusion.
- Separate Debug build with ASan + UBSan completed. All **6/6 selected CTest
  targets passed**, 5.85 s, with `detect_leaks=1` and `halt_on_error=1`, without
  sanitizer diagnostics in these runs. This is not a global leak/UAF guarantee.
- All 14 preserved-file checks (including the pre-existing absent log ignore
  file) matched the earlier snapshot: maps, archives and local raid/store edits
  were not changed. The previous stash remains at its original hash.
- `git diff --check` passes for this patch (Git's LF/CRLF notices are not compiler
  diagnostics). No production configuration, executable, map or DB was replaced.

Raw local logs: `combat-400-full-tests.log` and `combat-400-asan-tests.log` in
the repository root, plus `Testing/Temporary/LastTest.log` in each build.
These and the source runtime logs are not committed. Visual Studio 18 Community
was detected, but a Windows dependency configuration/MSVC build was not validated.

A fixture initially used an unowned stack Tile, violating the
existing weak tile contract; the fixture now holds a shared Tile. Production
tile ownership was not weakened to satisfy the test.

## Real workload matrix: NOT EXECUTED

The supplied capture changes population and lacks completed-actions/s, process
RSS, per-thread CPU, exact build flags and matched repetitions. It cannot fill
the missing measurements below. All A-I rows therefore remain pending controlled
before/after replay, with the source capture only cited as context.

| ID | Private scenario | Before/after x3 | Completed actions/s, CPU/thread, RSS | Reactor p95/p99, backlog | Paths, targets, effect attempts | Gameplay / ghosts / timeouts |
| --- | --- | --- | --- | --- | --- | --- |
| A | No bots or Demons | PENDING | PENDING | PENDING | PENDING | PENDING |
| B | 400 idle bots, no Demons | PENDING | PENDING | PENDING | PENDING | PENDING |
| C | 400 moving bots, no Demons | PENDING | PENDING | PENDING | PENDING | PENDING |
| D | 400 bots, 10 Demons, no chase/spells in isolated fixture | PENDING | PENDING | PENDING | PENDING | PENDING |
| E | Moving bots/chase, no spells in isolated fixture | PENDING | PENDING | PENDING | PENDING | PENDING |
| F | Spells with controlled movement in isolated fixture | PENDING | PENDING | PENDING | PENDING | PENDING |
| G | 400 moving bots + 10 complete Stress Demons | PENDING | PENDING | PENDING | PENDING | PENDING |
| H | Same, concentrated distribution | PENDING | PENDING | PENDING | PENDING | PENDING |
| I | Same, dispersed distribution | PENDING | PENDING | PENDING | PENDING | PENDING |

Use private cloned map/DB/config only. Warm up, then capture 60-120 s per run,
three runs, identical seeds/distributions and actual completed action rates.
Do not weaken public network limits, reduce spells or enlarge reactor budgets.
Record profile-enabled and profile-disabled runs to estimate diagnostic overhead.
Use the exact Stress Demon script, not the ordinary Demon.

```bash
# Substitute the PID of the isolated test server, never production.
ps -L -p <PID> -o pid,tid,psr,pcpu,stat,comm
pidstat -t -u -r -p <PID> 1 60
perf record -F 99 -g -p <PID> -- sleep 60
perf report --stdio --no-children
```

Build a separate symbolized `RelWithDebInfo`/frame-pointer binary for profiling.
Do not compare Debug/ASan performance to Release. If WSL perf counters are
unavailable, record that limitation and collect on supported Linux/Windows.
Windows/MSVC, real 8.60 clients, prolonged memory/RSS, TSan, 400/1,000-player
capacity and complete lag resolution remain unverified. There is no new
concurrency proposal requiring stale-job cancellation tests in this patch.
