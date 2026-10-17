# Findings: combat stalls with stress bots and Stress Demons

Date: 2026-10-15. Source baseline: `70db192284874ca30b43ab24a9281dba657c9a96`,
branch `perf/tfs18-1000-stressbots`. The preceding spectator/GC patch remains
intact. The supplied Markdown was read in full and checked against this checkout.

## What the supplied runtime evidence establishes

Read-only input: `data/logs/stats/server_20261015_122850.log`, SHA-256
`13CE18D07B907D55B5B7B2DEC8232B35B1D446D634E9422E9C5A298C3F272745`.
The reproducible parser `tools/analyze-performance.ps1` finds 288 complete
report windows and excludes the final truncated window. The file does not
identify its executable's source SHA or compiler flags. Its changing population
and ongoing logins are not a controlled, constant 400-bot / 10-monster replay.

| Metric | Calls | Inclusive total (ms) | Maximum call (ms) |
| --- | ---: | ---: | ---: |
| Game::checkCreatures | 9,461 | 434,170.074 | 2,843.821 |
| Combat::doAreaCombat | 14,862 | 274,216.950 | 367.683 |
| Combat::area.applyTargets | 14,862 | 229,379.185 | 352.712 |
| Creature::executeConditions | 299,861 | 127,752.813 | 80.723 |
| Map::getSpectators | 1,365,563 | 8,260.605 | 0.766 |
| Map::getPathMatching | 6,154 | 130.791 | 3.224 |

These are overlapping inclusive wall durations, NOT independent CPU totals.
Do not add parent/child rows or convert them into exclusive CPU percentages.
Wall durations can also include blocking and operating-system scheduling delays.
Queue latency measures waiting, not CPU execution. Reported p95/p99 values are
per-window histogram bucket bounds, not exact population percentiles.

The capture records 71,124,004 `effectRecipients` in 214 AoE windows, up to
1,155,988 in one window, and reactor `backlog_max` reaches 585. That counter is
incremented for instance-matching spectator attempts before protocol visibility
checks; it is not a count of delivered network packets or bytes.

Example at `12:36:53.959`: checkCreatures totals 4,361.997 ms, area target
application 3,003.749 ms, while all 32 A* requests total 0.784 ms. This supports
prioritizing combat/condition processing over rewriting A* for this capture.
The largest individual checkCreatures stall is at `12:47:36.227` (2,843.821 ms),
but that entire stall cannot be explained by this window's 558.442 ms of area
target application alone. Player think, condition damage, cleanup, scripts and
other nested work must be separated before claiming one complete root cause.

`dispatcher_slow.log` / `dispatcher_very_slow.log` independently identify
checkCreatures. The Lua/SQL/special files were inspected as supporting evidence;
some contain older dates and their aggregates must not be merged indiscriminately
with this timestamped capture. Login/SQL activity also prevents a pure combat A/B.
At `12:53:12` the server reports a missing stats directory and disables file
logging. No source-code cause for that external filesystem change is established.

## Exact Stress Demon, rather than the ordinary Demon

The current definition is `data/monsters/demons/stress_demon.lua`, spawned by
`data/scripts/talkactions/god/stress/spawn_stress_demon.lua`. It has six radius-5
damage-condition attacks, speed/drunk/outfit conditions, two damaging AoEs,
short 700-1,200 ms spell intervals and 100% target-change chance per 1,000 ms.
Its purpose deliberately combines conditions, casts and spectator fan-out.
No attacks, intervals, areas, RNG, damage or cooldowns were reduced.

The sibling StressBotBenchmark implementation was inspected read-only. It
supports different workload modes, attack/healing spells, movement and packet
limits. The exact configuration/seed/action completion trace used for the
capture was not supplied. No bot process, game server or database was started.

## Rejected optimization: do not infer cost from a large buffer declaration

`NetworkMessage` contains a zero-initialized 65,500-byte array. A first prototype
replaced temporary packets with primitive writes into the existing output
buffer. However, a fair optimized serialization comparison, without forcing
the temporary buffer to escape, was faster for the existing construct/append
model. At 400 repetitions its CPU median was approximately 625 ns versus
4,083 ns for the direct-write prototype. Release optimization can eliminate
unused temporary stores; individual primitive output writes also retain checks.

The prototype was REMOVED from production. Its isolated comparison remains in
`bench_combat_packets.cpp` as a reproducible rejected hypothesis. It does not
model the complete ProtocolGame call path, sockets or 400 connected bots.
The installed Google Benchmark library also reports a DEBUG-library warning.
Neither variant proves runtime server capacity. No "64 KiB saved per packet"
or lag-elimination claim is made from a source declaration alone.

## Canary reference and architectural decisions

A shallow, sparse, study-only clone was made outside the working repository:
`%TEMP%/codex-canary-reference-20261015`. Inspected SHA:
`09fde48ffa9afaf5c424bde460ec0c4c7c734e51`. No code/setup script was executed.

- [Bounded compute service](https://github.com/opentibiabr/canary/blob/09fde48ffa9afaf5c424bde460ec0c4c7c734e51/src/game/scheduling/monster_compute_service.cpp):
  finite admission, visible/background fairness and lifecycle management.
- [Target ranking](https://github.com/opentibiabr/canary/blob/09fde48ffa9afaf5c424bde460ec0c4c7c734e51/src/creatures/monsters/monster_targeting.cpp):
  copied candidate data, cancellation and ID results.
- [Pathfinding](https://github.com/opentibiabr/canary/blob/09fde48ffa9afaf5c424bde460ec0c4c7c734e51/src/creatures/monsters/monster_pathfinding.cpp):
  navigation snapshots and monster movement traits.
- [Monster integration](https://github.com/opentibiabr/canary/blob/09fde48ffa9afaf5c424bde460ec0c4c7c734e51/src/creatures/monsters/monster.cpp):
  movement refresh coalescing, target/path generations and result validation.

Adapted: the measurement-first approach, explicit reason counters and bounded
telemetry cardinality. No Canary implementation was copied or cherry-picked.
Rejected for this patch: asynchronous map access, raw-pointer jobs, new thread
pools, target refresh coalescing, negative path caches and changed repath limits.
The current logs do not justify those gameplay/lifetime risks.
There are semantic differences as well: TFS's nearest-target pass uses the sum
of X/Y distances, while this Canary ranker uses their maximum plus faction
weighting. TFS's factory returns `unique_ptr<Monster>`, not a mechanically
interchangeable shared-owner job contract. TFS already has edge refresh,
thread-local A* workspace, direct-chase distance 10 and repath interval 400 ms;
none of these existing optimizations was reintroduced or retuned.

The existing OTS Statistics integration is retained, not reimplemented.
Reference: [OTS Statistics by kondra (otclient@otclient.ovh)](https://github.com/gesior/forgottenserver-gesior/commit/0ad4be6cab3e335e6f7dc767722f70ee0c849be8).

## Conclusion and scope boundary

Measured bottleneck region: combat target application, condition processing
and long creature-check callbacks delaying other work. Exact exclusive CPU
hotspot and the remaining multi-second stall are NOT established without a
call graph / expanded runtime metrics. This patch supplies that discrimination,
deterministic regressions and a read-only analyzer. It does not claim the
reported gameplay lag is solved, or support for 1,000/2,000 players.
