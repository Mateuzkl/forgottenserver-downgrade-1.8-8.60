# Full stutter log analysis

## Scope and evidence

This audit covers all seven `data/logs/server_20260712_*.log` sessions and every file under `data/logs/stats`. OTS aggregate files contain only the date, so exact event time correlation uses the server logs. Startup and shutdown samples are kept separate from gameplay. All existing Callgrind captures are zero-byte files and therefore provide no valid evidence.

The statistics implementation is based on OTS Statistics by kondra (otclient@otclient.ovh).

## Proven hotspots

| Area | Evidence | Conclusion |
|---|---|---|
| Walk/move | At 10:31:18 `checkCreatureWalk` max was 705.467 ms, `internalMoveCreature` 705.112 ms and `Map::moveCreature` 705.107 ms in the same window. | The long walk stall propagated from movement; it was not a path-search stall. |
| Spectators | Normal p95 was commonly 16–32 us, but isolated maxima reached 50.811 ms and shutdown reached 105.810 ms. | Spectator lookup has a heavy tail and must be correlated with move/packet callbacks. |
| Pathfinding | In the 705 ms window `Map::getPathMatching` max was 7.404 ms; many later windows stayed below 0.1 ms. | Pathfinding is not the cause of the largest observed stall. |
| Creature checks | Runtime windows usually had 48–50 bucket calls per 5 seconds; maxima reached about 60.95 ms. | The scheduler cadence is expected; work inside a bucket can still spike. |
| Lua timers | Final-session timers reached 38, 39, 42, 46, 48, 60, 63, 80 and 87 ms as `(Unknown scriptfile)`. | Missing attribution was a blocker; it is now fixed at schedule time. |
| Death scripts | Older runs show `custom_bestiary.lua` up to 49 ms, Battle Pass 26 ms, task-board callbacks 93 ms and `Monster@onDropLoot` 9–17 ms. | Per-kill work was spread across several callbacks, not one function. |
| Party analyzer | One older aggregate callback reached 521 ms. The script sent a complete party snapshot on every corpse and heal component. | Full refresh bursts were a confirmed design problem. |
| KV/SQL | A cold boss cooldown KV read took 17 ms. Older per-kill bestiary upserts took 5–48 ms. | KV cache misses are synchronous; KV sets are memory-only until persistence. |
| Reactor | Stress backlog reached roughly 907 with 802 deferred tasks; normal final-session queue was around 50 with zero drops. | Stress and runtime must not be combined; queue pressure can amplify a slow callback. |

## Changes made from the evidence

- Lua timers retain file, function, definition line, timer id and requested delay. Dispatcher and Lua slow logs now show the same identity.
- Lua, synchronous SQL and KV measurements inherit the active `deathId`.
- Death stages have separate histograms and emit p50/p95/p99 every five seconds when `performanceMetricsEnabled` is enabled.
- Repeated damage after zero health can no longer enqueue duplicate death tasks.
- Boss cooldown sends one changed entry instead of scanning all boss KV keys on every update.
- Party updates are coalesced for 100 ms rather than sending a full party packet for every heal/loot callback.
- Astra uses the loot already present in `0xD1`; the server no longer sends redundant per-item `0xCF` packets to Astra.

## Remaining measurement rule

No post-change gameplay percentile is claimed in this document. A Release build passed, but final before/after p95 and p99 require a new live kill/walk capture with the commands in `FINAL_STUTTER_RESULTS.md`.
