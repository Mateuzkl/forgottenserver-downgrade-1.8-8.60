# PR #172 surface audit (adversarial)

Independent adversarial review of the full `main...HEAD` surface inherited from
PR #172 (death pipeline, analyzers, spectators, quickloot/protocol, summons,
creature distribution): 6 specialized review lenses, every finding attacked by
independent refuters. 17 findings raised; 9 refuted; the confirmed ones are
fixed on this branch.

## Confirmed and fixed

1. **Forge in-place could resurrect monsters and erase combat damage** —
   `getRandomForgeableMonster` did not filter dead/death-scheduled monsters,
   and `configureForge`/`clearForgeStatus` set health unconditionally. A
   monster with a queued death could be converted (or a fiendish could expire)
   and come back at full health, annulling the kill: no corpse, loot or
   experience; expiry mid-combat healed the monster to full base health.
   Fixed: selection filters `isDead()`/`isDeathScheduled()`, `configureForge`
   refuses dead monsters, and `clearForgeStatus` scales the current health
   proportionally instead of restoring it (never touching dead monsters).
2. **Bestiary kills: memory became the source of truth but four consumers
   still used SQL** (stale for the whole session, refreshed only on save):
   - charm unlock gate (`BestiaryCharmSystem::getKillCount`) — now prefers the
     online player's in-memory count;
   - cyclopedia `loadKillMap` (rebuilt after every `invalidatePlayer`, e.g.
     right after a charm action) — now reads `player:getBestiaryKills()` for
     online players;
   - task hunting upgrade/display — same fix;
   - `/bestiarykills` god command wrote SQL directly (reverted by the next
     save, charm-point delta computed against stale counts) — now writes
     through `player:setBestiaryKillCount` (new binding) and reads the old
     value from memory. `Player::setBestiaryKillCount` keeps zero entries
     because the save path only upserts rows.
3. **`checkCreatures` re-arm could be dropped under reactor backpressure**,
   permanently freezing all creature AI — now detected, logged and retried
   through a direct dispatcher task.
4. **`Items::clear()` dropped the `ITEM_TYPE_NONE` sentinel** the constructor
   guarantees, so a failed `reload()` left `items[0]` dereferences on an empty
   vector — sentinel restored inside `clear()`.
5. **`KVStore::set/get` paid `fmt::format` on every operation** even with
   stats disabled — now gated on `g_stats.isEnabled()`.

## Refuted (no action, with reasoning)

- "executeDeath's `isDead()` gate annulled by server save reviving players at
  1hp": no code path mutates live health during save (`iologindata` writes
  `player->health` as-is; there is no `preSave`-style normalization in this
  fork).
- "Double cancel orphans a `cancelled` entry that assassinates a recycled id
  after uint32 wrap": the cancelled set is consumed at skip/pop time and
  post-skip cancels are rejected by `activeIdentifiers`; the interleaving
  required cannot occur.
- "Unbounded cancelInbox drain under the mutex in executeReadyTasks":
  cancellation volume is bounded by game logic; entries are 4-byte ids.

## Known follow-ups (documented, intentionally not changed here)

- Summon behavior from PR #172 (`walkToSpawn` wait-in-place for monster
  masters; master-death cleanup ordering) has theoretical edge cases flagged
  by review but changing it requires gameplay validation.
- Astra client packet semantics (single-entry `0x2C` boss delta merge,
  `0xCF` suppression) depend on client-build behavior; verify against the
  deployed Astra build before changing.
- Bestiary kills accumulated since the last save are lost on a hard crash —
  inherent to save-based persistence, same exposure as experience/skills.
- `luaAddEvent` performs `lua_getinfo` per call; the data also feeds timer
  error diagnostics, so gating it on stats alone would degrade error messages.
- Re-kill damage dealt *during* a creature's own `onDeath` callback is
  swallowed by design (the death-scheduled gate prevents re-entrant deaths);
  players are revived/teleported by `Player::death` regardless.
