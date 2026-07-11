# Callgrind runtime hotspots

## Evidence received

The supplied Callgrind capture reports `1,539,574,139` inclusive instructions
under `TaskReactor::runOnce`. This is an execution root, not a root cause. Its
dominant children are runtime callbacks:

- follow/pathfinding: `Creature::goToFollowCreature` ~479M and
  `Map::getPathMatching` ~477M;
- creature/attack loop: `Game::checkCreatures` ~402M,
  `Creature::onAttacking` ~244M, `Monster::doAttacking` ~227M and
  `CombatSpell::castSpell` ~221M;
- movement: `Monster::onWalk` ~279M, `Creature::onWalk` ~329M,
  `Game::internalMoveCreature` ~256M and `Map::moveCreature` ~255M;
- tile lookup: about 803,148 calls in the captured pathfinding branch.

Startup frames such as `lua_loadfile` exist in the same capture. They must not
be attributed to runtime stutters without a collection reset after the server
becomes online.

## Reproducible capture matrix

Build once with `RelWithDebInfo`, `-O2 -g -fno-omit-frame-pointer`, unity build
disabled and native optimizations disabled. Keep map, config, players, monsters
and duration identical between revisions.

| Output | Isolated workload |
| --- | --- |
| `callgrind-startup.out.<pid>` | startup only |
| `callgrind-idle.out.<pid>` | online, no active players |
| `callgrind-pathfinding.out.<pid>` | follow, blocked corridors, no-path targets |
| `callgrind-combat.out.<pid>` | single-target and area combat |
| `callgrind-movement.out.<pid>` | walking, teleport, floor changes, spectators |
| `callgrind-stress.out.<pid>` | many monsters, target changes and spectators |

Use `scripts/profiling/capture_callgrind.sh`. Reset Callgrind counters after
startup and before each timed workload. Generate matching perf captures with
`scripts/profiling/capture_flamegraph.sh`.

## Initial code findings

- `Creature::onCreatureMove` requests a new follow path when either follower or
  target moves. A successful follower step therefore invalidates work that can
  usually be reused.
- `Player::onCreatureMove` repeats the request already made by the base class.
- `isUpdatingPath` suppresses simultaneous requests, but an overflow rejection
  from `Dispatcher::addTask` is ignored and can leave it stuck.
- No target generation validates a queued follow request.
- Generic creatures have no failure backoff; only `Player` has a fixed
  post-failure delay.
- Reactor time budget starts after inbox draining, ready-heap draining and
  sorting. Deferred scheduled tasks are returned through the immediate inbox,
  losing reliable cancellation semantics.

These findings define hypotheses. Runtime counters and isolated before/after
captures remain required before claiming measured improvement.
