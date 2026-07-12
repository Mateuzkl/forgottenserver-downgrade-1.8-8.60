# Death pipeline full audit

## Execution order

`Game::executeDeath` now creates one profiling `deathId`, then `Creature::onDeath` executes:

1. last-hit `onKilledCreature` callbacks;
2. damage-map/experience distribution and `onGainExperience` callbacks;
3. distinct most-damage kill callbacks;
4. forge reward;
5. corpse creation and MonsterType `onDeath`;
6. registered CreatureEvent `onDeath` callbacks;
7. `Monster::dropLoot` and `onDropLoot` events;
8. quick loot/loot corpse and optional loot highlight;
9. creature/player death finalization and removal.

Each listed stage has a `PerformanceMetric` histogram. When profiling is enabled, logs contain `ServerDeathTrace` start, stage durations, Lua packets with opcode/bytes, SQL/KV attribution and an end summary.

## Duplicate death fix

Previously every `changeHealth` call observing zero HP queued another `executeDeath`. Removal normally made later tasks no-op, but those tasks still consumed reactor capacity and could race with state changes. `tryMarkDeathScheduled()` now permits one outstanding death task. `executeDeath` also verifies that the creature is still dead before executing and clears the guard when finished.

The guard is covered by `test_creature_lifetime`.

## Server/client correlation

Profiling sends Astra extended opcode 147 to damaging players:

- `S|deathId|serverTick` before callbacks;
- `E|deathId|serverTick` after callbacks.

This opcode adds no bytes to Tibia 8.60 game packets and therefore cannot shift normal parser alignment. Astra logs packet callbacks, UI duration, widget count, refresh count, FPS and total client interval under the same id.
