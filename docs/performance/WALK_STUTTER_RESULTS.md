# Walk stutter results

## Baseline

The strongest correlated sample is `server_20260712_102812.log` at 10:31:18:

- `Game::checkCreatureWalk`: 31 calls, max 705.467 ms, p95 bucket 33.554 ms.
- `Game::internalMoveCreature`: 20 calls, max 705.112 ms, p95 bucket 33.554 ms.
- `Map::moveCreature`: 20 calls, max 705.107 ms, p95 bucket 33.554 ms.
- `Map::getPathMatching`: not responsible for the matching 705 ms duration.

The histogram uses power-of-two upper bounds. A displayed p99 of 1.073 s is a bucket boundary, not a measured 1.073 s call; the measured maximum is authoritative.

## Duplicate audit

`Creature::addEventWalk` already rejects a second live `eventWalk`, `onWalk` clears the id before scheduling the next step, and `stopEventWalk` cancels and clears it. No duplicate walk event was proven. Roughly 100 `checkCreatures` calls per 10-second OTS interval are expected from ten buckets advanced every 100 ms.

The remaining walk investigation is therefore movement/spectator/packet work, not removal of a valid creature cadence. Existing generation/lifetime checks protect asynchronous follow-path results.

## Reproduction

Run a dense monster movement scenario for at least 60 seconds with `performanceMetricsEnabled = true`, then compare matching timestamps for `checkCreatureWalk`, `internalMoveCreature`, `Map::moveCreature`, `Map::getSpectators`, reactor backlog and Astra FPS. Do not mix shutdown output into runtime percentiles.
