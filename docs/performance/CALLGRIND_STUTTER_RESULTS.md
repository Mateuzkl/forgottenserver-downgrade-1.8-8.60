# Callgrind stutter investigation results

## Conclusion

The supplied profile proves that `TaskReactor::runOnce` is the inclusive
execution root, not the expensive leaf. Its dominant runtime branch is follow
pathfinding (`Creature::goToFollowCreature` ~479M and
`Map::getPathMatching` ~477M), followed by creature/attack processing and
movement. Code inspection confirmed the storm mechanism: every successful
follower step requested a complete path again, `Player::onCreatureMove`
repeated the base request, and queued work had no target generation or generic
failure backoff.

The branch removes those mechanisms without changing speed, cooldowns, target
rules, spell values, map data or protocol behavior. Reactor work is also
bounded and cancellable under backlog, but reactor tuning is not presented as
the root-cause fix.

## Supplied before evidence

| Function | Inclusive instructions in supplied capture |
| --- | ---: |
| `Creature::goToFollowCreature` | ~479M |
| `Map::getPathMatching` | ~477M |
| `Game::checkCreatures` | ~402M |
| `Creature::onWalk` | ~329M |
| `Monster::onWalk` | ~279M |
| `Game::internalMoveCreature` | ~256M |
| `Map::moveCreature` | ~255M |
| `Creature::onAttacking` | ~244M |
| `Monster::doAttacking` | ~227M |
| `CombatSpell::castSpell` | ~221M |
| `TaskReactor::runOnce` | ~1.540B inclusive |

The screenshot also shows startup Lua loading. No raw `callgrind-before.out`
was supplied, so it cannot be reprocessed or normalized against a new capture.

## Changes

### Follow/pathfinding

- One pending follow request per creature.
- Per-creature generation rejects work queued before target/map invalidation.
- Unique creature lifetime token prevents an old callback from resolving to a
  new object after ID reuse.
- Successful remaining path is reused when only the follower advances.
- Stationary success is reused until start, target or identity changes.
- Failed identical searches use bounded backoff: 250, 500, 1000 and 2000 ms.
- Target changes and blocked movement invalidate/reset the correct state.
- A* nodes retain immutable per-search tile walk cost, avoiding another
  `Map::getTile` when an already discovered node is reopened.
- Per-creature counters cover requests, success/failure, nodes, tile reads,
  length, reuse, invalidation, consecutive failures and elapsed time.

### Creature loop

`checkCreatureLists` now assigns `creatureId % EVENT_CREATURECOUNT`. Logical
frequency and `EVENT_CREATURE_THINK_INTERVAL` are unchanged; assignment is
deterministic and requires no RNG.

### Movement/spectators

`SpectatorVec::addSpectators` merges spectators with `std::sort` and
`std::unique`. Empty/self merges fast-return.
Movement stack-position storage reserves only the number of player spectators.
Spectator cache lifetime/invalidation is unchanged.

### Combat

Combat rules were not changed. Timers were added to spell/combat/area paths.
Delayed chain combat now validates caster and target lifetime tokens so an old
chain step cannot affect a reused creature ID.

### Reactor

- Cycle time budget begins before inbox drain, heap drain and sort.
- Preprocessing and ready batches are bounded.
- Immediate and scheduled inbox work is selected by global sequence.
- Scheduled work deferred by budget returns to the scheduled heap with its
  identifier intact; later cancellation remains effective.
- Cancellation is not rejected because producer inbox is full.
- Backpressure applies to total producer work and returns rejection to
  `Dispatcher`; follow state rolls back a rejected enqueue.
- Active scheduled identifiers are unique across wrap while still live.
- Shutdown changes its predicate under the condition-variable mutex, fixing a
  reproducible lost wakeup.

### Ownership

See `POINTER_OWNERSHIP_AUDIT.md`. Follow, delayed condition and delayed chain
callbacks carry ID plus lifetime token rather than retaining a long-lived
`shared_ptr`. Synchronous raw pointers remain observers. An ASan/UBSan run also
found and fixed an empty item-registry sentinel UB unrelated to profiling.

## Instrumentation

Set `performanceMetricsEnabled = true`. Every five seconds the server emits
aggregate calls, total, average, maximum, p50, p95 and p99 for reactor stages,
queue latency and requested hotspots. It also emits queue/backlog,
deferred/expired/dropped tasks and path request/result/node/tile/reuse counters.
No per-call logging is performed. Disabled mode avoids timestamp reads in hot
functions.

## Validation

- Release build: passed with GCC 13.3, C++23.
- `ctest --test-dir build-tests --output-on-failure`: 12/12 passed.
- Reactor test repeated 20 times: passed; includes deferred cancellation,
  backpressure, ordering, time budget and shutdown wakeup.
- ASan + UBSan: 12/12 passed with leak detection and halt-on-error.
- Valgrind Memcheck: reactor 20 tests, follow 7 tests and creature lifetime 7
  tests; zero errors, zero definite leaks, all blocks freed.
- TSan: build completed; execution unavailable in this WSL kernel/runtime due
  `FATAL: ThreadSanitizer: unexpected memory mapping`. This is not a clean TSan
  pass and must be rerun on native Linux.

## Local idle runtime sample after change

Server reached online state and emitted three steady five-second windows with
no players/pathfinding workload:

| Metric | Observed after startup window |
| --- | ---: |
| Queue latency p95 histogram bound | 0.524–1.049 ms |
| Queue latency p99 histogram bound | 0.524–4.194 ms |
| Queue latency actual maximum | 0.305–3.335 ms |
| Maximum callback | 0.551 ms |
| Maximum `runOnce` | 4.155 ms |
| Queue/backlog | 12 / 12 |
| Deferred / dropped | 0 / 0 |
| Path requests | 0 (idle) |

Startup window is excluded because it contained loader backlog. This idle
sample validates instrumentation and basic runtime stability only; it does not
prove pathfinding/combat improvement.

## Before/after status

| Function / metric | Before | After same workload | Status |
| --- | ---: | ---: | --- |
| `Map::getPathMatching` | ~477M | not captured | workload required |
| `Map::getTile` | ~803,148 calls in branch | not captured | workload required |
| `Creature::goToFollowCreature` | ~479M | not captured | workload required |
| `Game::checkCreatures` | ~402M | idle timing only | not comparable |
| movement/combat leaves | supplied values above | not captured | workload required |
| p95/p99 queue latency | absent in supplied capture | idle values above | not comparable |
| path requests/failures/tiles per second | absent | zero at idle | not comparable |

`callgrind-after.out`, `flamegraph-before.svg` and `flamegraph-after.svg` were
not fabricated. Producing a valid comparison requires the missing raw before
capture plus the same map, players/bots, monsters, duration and scripted
pathfinding/combat workload. Use `scripts/profiling/capture_callgrind.sh` for
the six isolated captures and `capture_flamegraph.sh` for matching perf data.

## Configuration recommendation

Do not increase `1000 / 25 ms / 200000`. Keep production values until the same
stress workload compares them against `500 / 5 ms / 100000` and
`250 / 3 ms / 50000`. Select using p95/p99 queue latency, actual maximum,
maximum callback, backlog, deferred/dropped counts and CPU—not inclusive
reactor percentage.

## Remaining risks

- Native-Linux TSan run is still required.
- Multiplayer follow/no-path/corridor and area-combat load must produce the raw
  before/after artifacts before a percentage reduction is claimed.
- Thread-local spectator merge storage retains peak capacity per game thread;
  monitor RAM in extreme spectator scenarios.
- Failure backoff is intentionally capped at two seconds; verify unreachable
  target responsiveness with production maps.
