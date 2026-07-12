# Crystal vs TFS 8.60 death pipeline

## Scope and references

- Target: TFS downgrade 1.8/8.60, branch `perf/port-crystal-death-events-core`.
- Reference: `C:/Users/Mateus/Desktop/crystal/Crystal-Server-devv`, commit `cc09416c5ed28524677df05615d5ec570f240fb4`.
- Comparison is architectural. Protocol 8.60 packet layout and existing gameplay thresholds remain owned by this server.

## Pipeline before this port

1. `Creature::onDeath()` calculates last hit, most damage and experience.
2. `Creature::onKilledCreature()` executes every registered Lua `onKill` callback synchronously on dispatcher.
3. `CustomBestiaryKill` walks participating players, updates Lua caches and issued one SQL write per player/kill.
4. `TaskBoardKill` calls Bounty, Weekly and Hunting Task Lua modules. Periodic progress saves used synchronous `db.query()` inside kill callback.
5. Battle Pass and other extension callbacks run serially after same death.
6. Forge Lua global event built a Lua table for every online monster and filtered it. Test world contained about 175k monsters.

Result: gameplay thread owned Lua allocation, cache rebuilding and occasional database latency.

## Crystal architecture

Crystal resolves monster kill once in `Player::onKilledMonster()` and updates in-memory C++ state for Hunting Task, Bestiary, Bosstiary, Bounty and Weekly Tasks. Login loads state; player save persists it. Its Forge runtime keeps small C++ collections for forgeable, influenced and fiendish monsters. Lua `onKill` is explicitly treated as deprecated for performance-sensitive core systems.

## Adapted architecture

### Forge

- `Game` owns an O(1) monster-ID pool plus influenced/fiendish ID sets.
- `Game::updateForgeMonsters()` selects, expires and replaces Forge monsters without `Game.getMonsters()` or a full Lua table.
- `Monster::configureForge()` applies classification, level, HP, damage and expiry in C++.
- Fiendish dust/extra experience and influenced slivers execute in C++.
- Lua list getters iterate only small tracked sets.
- Old `influenced_spawn.lua` contains documentation only and registers no events.

### Bestiary and Bosstiary

- `Player` owns `raceId -> kills` in memory.
- Login loads `player_bestiary_kills` once.
- Kill increments are saturating C++ operations.
- Player save emits one batched UPSERT through existing transactional async-save path.
- Lua callback remains only as compatibility/UI layer for tracker packets, thresholds and messages. It no longer performs per-kill Bestiary SQL.

### Task Board

- Bounty, Weekly and Hunting Task caches remain Lua because their custom Astra protocol/state model differs from Crystal's current protocol.
- Periodic kill persistence now uses `db.asyncQuery()`; dispatcher no longer waits for MySQL.
- Logout and explicit state-changing actions retain synchronous persistence where caller needs success/failure.

## Preserved behavior

- Damage/last-hit calculation and existing participant-credit policy.
- Bestiary/Bosstiary thresholds, boosted increments, charm/boss point messages and tracker packets.
- Bounty, Weekly, Hunting Task and Battle Pass client payloads.
- Forge limits, 1-hour lifetime, 30% fiendish roll, influenced levels, HP/damage multipliers, dust and sliver rewards.
- Tibia 8.60/Astra creature packet order is untouched.

## Remaining Lua work

Battle Pass missions, custom tracker serialization and Task Board UI remain Lua compatibility layers. They are now memory/network work, not synchronous Bestiary/Task SQL. Further C++ migration should require a new measured hotspot and protocol-specific tests.
