# Creature event stale-callback audit

Scope: `Game::checkCreatures`/`checkCreatureWalk`/`updateCreatureWalk`/`checkCreatureAttack`,
`Creature::addEventWalk`/`stopEventWalk`/`onWalk`, `TaskReactor`, follow-path pipeline.
Reproduction scenario: `/stresssummon demon,90`.

## Verdict summary

| Hypothesis | Verdict |
| --- | --- |
| Walk callback dereferences a dangling creature pointer | Refuted — callbacks capture the creature id and revalidate (`game.cpp:5901-5909`) |
| Follow/path results applied stale | Refuted — `lifetimeToken` + `FollowPathState` generation/pending token (`game.cpp:5911-5923`, `follow_path.h`) |
| Path recomputed blindly per request | Refuted — `canReuse`/`retryAllowed`/`beginRequest` coalesce to one in-flight request with backoff (`creature.cpp:1028-1055`) |
| A cancelled walk event can still execute and fork a duplicate walk lineage | **Confirmed** — intra-cycle cancellation window in `TaskReactor` amplified by unvalidated re-arm in `Creature::onWalk` |

## Confirmed defect 1: intra-cycle cancellation window (reactor)

`TaskReactor` cancellation is only honoured while a task sits in the heap:

- `cancel()` pushes to `cancelInbox` (`reactor.cpp:141-155`).
- `drainInbox` moves `cancelInbox` into the `cancelled` set (`reactor.cpp:297-299`).
- `drainReadyTasks` drops cancelled tasks when popping the heap (`reactor.cpp:329`).
- `executeReadyTasks` **never rechecks** `cancelled` before running a task
  (`reactor.cpp:342-395`).

Game logic runs on the reactor thread itself (`Dispatcher::addTask` →
`g_reactor.send`, `tasks.cpp:72-79`). Therefore every `stopEventWalk()` issued from
inside a task of the current batch, aimed at another task **already promoted into
the same batch's `readyTasks`**, is silently ignored: the cancellation only lands
in `cancelled` on the next cycle, after the target already ran.

Under `/stresssummon demon,90` the summons are created in the same tick, so their
walk events stay phase-aligned and the ready batches are large. Any same-batch
`stopEventWalk` + `addEventWalk` pair — most commonly `setSpeed()` from
haste/paralyze conditions (`creature.h:215-219`) — hits this window.

Side effect: when a cancelled task executes anyway, its identifier is never erased
from `cancelled` (only `drainReadyTasks` erases it). The set grows monotonically,
and after `nextIdentifier` wraps uint32 a recycled identifier can be spuriously
cancelled by a stale entry.

## Confirmed defect 2: unvalidated re-arm in `Creature::onWalk`

`creature.cpp:264-267`:

```cpp
if (eventWalk != 0) {
    eventWalk = 0;
    addEventWalk();
}
```

The walk callback does not know which event id fired it, so `onWalk` cannot tell
"the tracked event fired" from "a stale, cancelled event fired". Combined with
defect 1 the lineage forks permanently:

```text
event A becomes ready (promoted into this cycle's batch)
earlier task in the batch: stopEventWalk()  -> cancel A (ineffective), eventWalk = 0
                           addEventWalk()   -> schedules B, eventWalk = B
stale A executes anyway: onWalk() sees eventWalk == B != 0
    -> eventWalk = 0; addEventWalk() schedules C          (B now untracked)
B fires later: onWalk() sees eventWalk == C != 0 -> schedules D   (C now untracked)
C fires: schedules E ...
```

One stale execution permanently doubles the creature's walk-event rate. Each
extra lineage costs a full `onWalk` → `getNextStep` → `internalMoveCreature` →
`Map::moveCreature` → 2× `getSpectators` → per-spectator `onCreatureMove` →
movement packet cascade, and the extra steps desynchronise walk cadence
(perceived stutter). This is the mechanism `WALK_STUTTER_RESULTS.md` looked for
and did not find: the guard in `addEventWalk` (`creature.cpp:344-346`) prevents
double *scheduling*, but not lineage forking after a missed cancellation.

## What is already sound (do not re-fix)

- Id-captured callbacks + `getCreatureByIDShared` + `isRemoved`/`isDead` checks.
- `updateCreatureWalk` validates `lifetimeToken` and path generation.
- `FollowPathState` (follow_path.h): snapshot reuse, single in-flight request,
  exponential backoff, generation invalidation.
- `getSpectators` cache (`map.cpp:446-551`) — though `clearSpectatorCache()` is a
  full flush on every tile add/remove (`tile.cpp:993,1232,1670`), worth metrics.

## Fixes (implemented on this branch)

1. `executeReadyTasks` drains `cancelInbox` into `cancelled` and rechecks
   before each `task.function()`, under the lock it already takes for
   `activeIdentifiers`; consumed identifiers are erased, which also stops the
   `cancelled` set leak. Covered red-to-green by
   `test_reactor_cancel_from_earlier_task_in_same_batch` and
   `test_reactor_cancel_and_replace_within_batch_keeps_single_lineage`.
2. Defense in depth: `checkCreatureWalk` rejects callbacks whose captured
   `walkGeneration` mismatches the creature's. Correctness argument: the
   generation is bumped only by `stopEventWalk`, which always cancels the
   tracked event, so a mismatch proves the callback belongs to a cancelled
   lineage — no false positives. Rejections count as `creature_walk_stale` +
   `creature_walk_rejected_generation` and are expected to stay at 0; any
   nonzero value means a new cancellation hole appeared.
