# Patch notes: bounded AI/combat diagnostics and regression coverage

Baseline: `70db192284874ca30b43ab24a9281dba657c9a96`.
This is a diagnostic patch, not an unmeasured gameplay optimization.

## Production changes

- `performance_metrics.h/.cpp`: extend the existing optional aggregator with
  14 timing scopes, 25 fixed AI counters and a 64-bucket target-count histogram.
  No per-monster/player/position series, retained world pointers or new workers.
  Counters saturate at UINT64_MAX instead of wrapping. Percentile rank arithmetic
  avoids intermediate multiplication overflow. New counters/histograms reset
  when reported, using atomic exchanges like the existing aggregator.
- `monster.cpp`: measure move callbacks, full/edge refresh, target pruning,
  discovery, search and follow-path computation. Classify full-refresh reasons
  exclusively in predicate order (summon, teleport, floor, missing follow).
  Count candidate admission, canUseAttack calls in the initial candidate pass,
  search success/failure, path reuse, direct chase, primary/fallback A* attempts
  and outcomes, active attack ticks, spells iterated and casts performed.
  Target histogram samples are taken at refresh completion and search entry;
  they are not a unique-monster population sample. The initial-pass candidate
  counters do not pretend to include every fallback scan/LOS call.
- `player.cpp`, `creature.cpp`, `condition.cpp`, `game.cpp`: separate player
  think, creature think-event callbacks, condition insertion/merge, condition
  damage, combat blocking/health application and cleanup from parent timings.
- `protocolgame.h`: friend access for packet regression tests only.
  `protocolgame.cpp` is unchanged from baseline: the slower experimental
  direct-output implementation was reverted before publication.

All extra instrumentation is gated by `performanceMetricsEnabled`; it does not
allocate per action when disabled. Enabled profiling necessarily adds clocks
and counters: repeat real load with profiling disabled to quantify its overhead.
Aggregation remains approximate at concurrent report boundaries, as with the
existing metrics; do not interpret independent atomic snapshots as a transaction.

## Correctness and lifetime contracts

No new jobs/callback captures, borrowed pointers, shared owners or caches.
All world access remains on its existing game-thread path. Target selection,
RNG order, weak-reference revalidation after spells, independent condition
clones, target/friend mutation order, instancing, PZ, visibility, path rules,
backpressure and protocol bytes are unchanged. The small `isSummon()` snapshot
is reused synchronously after the existing move callback, without crossing a
script call. The prior GC and spectator-consumption fixes remain intact.

No changes to maps, spawns, production config, DB, custom scripts, output rate
limits, reactor budgets, or monster spell definitions. The pre-existing local
raid/store edits and deleted `data/logs/.gitignore` are outside this patch.
The user's prior stash is not applied, dropped or rewritten.

## Tests and tools

- `test_performance_metrics`: disabled collection, fixed-cardinality/sentinel
  handling, saturation, bounded target histogram and report-window reset.
- `test_monster_target_state`: an in-memory 400-candidate search checks counters
  with profiling off/on and retains valid same-instance target selection.
  Existing movement, teleport, instance, summon and target regressions remain.
- `test_combat_packets`: compare actual send methods to the previous serializer,
  including queued-packet ordering, hidden/zero-max health, U16 IDs, speed cast,
  viewport and missing ground, 400 repeated effect payloads, output rollover,
  Latin-1 UTF-8 conversion, invalid/unrepresentable input and string limits.
  These are protocol fixtures, not 400 network clients.
- `tools/analyze-performance.ps1`: read-only, invariant-culture log analysis;
  excludes incomplete windows, keeps inclusive totals separate and fingerprints
  input. `tools/tests/test-analyze-performance.ps1` checks truncation, counters
  and pt-BR decimal parsing with a synthetic fixture.
- `bench_combat_packets`: records the rejected direct-write model alongside the
  optimized baseline model. It adds no experimental production packet path.

See `BENCHMARK_400BOTS_10DEMONS.md` for actual validation and remaining gates.
Final local results: 54/54 normal CTest entries, 6/6 selected ASan/UBSan/LSan
targets, and the PowerShell analyzer checks passed. The DB roundtrip was skipped;
no live 400-bot capacity test or Windows binary deployment was performed.
Rollback: revert only this diagnostic commit after publication; no data migration
or map/config restoration is needed. It is additive to the previous GC patch.

## Remaining work, deliberately not guessed

Run the same private workload with the expanded scopes and a symbolized CPU
profile. Separate health/block handling, condition damage, player think/scripts,
cleanup and serialization/output buffering. Then propose one measured change
with identical completed actions, recipients and gameplay. No refresh coalescing,
worker queue/stale-job architecture, path cache, or global pointer conversion is
introduced merely because Canary has such facilities.
