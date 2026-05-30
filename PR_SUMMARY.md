## Summary

Rarity System + Augment System for TFS 1.8 (protocol 8.60). C++23 engine with Lua-configurable balance. Zero hardcoded item IDs — attribute eligibility is determined by item type properties (e.g., `itemType:getArmor() > 0 or itemType:isWeapon()`).

---

## Rarity System

### C++23 Memory Safety (`src/`)

| Feature | File(s) | Purpose |
|---------|---------|---------|
| `ObserverPtr<T>` | `combat.cpp`, `game.cpp` | Non-owning pointer alias — documents lifetime semantics |
| `shared_ptr<Item>` capture | `player.h/cpp` → `getWeaponShared()`, `getInventoryItemShared()` | Guarantees item lifetime during proc; replaces raw pointer from inventory |
| `std::optional<int32_t>` | `item.h/cpp` → `getRarityTier()` | `nullopt` instead of sentinel `0` — unambiguous |
| `std::optional<double>` | `item.h/cpp` → `getRarityStat()` | `nullopt` instead of sentinel `0` — supports float values |
| `std::span<const slots_t>` | `combat.cpp` | Type-safe range iteration for slot loops |
| `std::max` + `std::pair` | `combat.cpp` | Finds best on-hit item (replaces manual loop + tracking vars) |
| `[[nodiscard]]` | `item.h` | Compiler warns if query return values are ignored |
| `Rarity::makeStatKey()` | `item.h` | Inline helper with `reserve()` — avoids allocation for stat key construction |

### Float Support (double throughout)

All numeric values accept floats — `{1.5, 3.7}` ranges in `config.lua`, `level = 2.5` in `balancing.lua`.

| Layer | Change |
|-------|--------|
| `CustomAttribute` | `getDouble()` with `int64_t` fallback for backward compat |
| All rarity procs | `int64_t` → `double` (`chance`, `fireDmg`, `totalChance`, etc.) |
| Lua bindings | `lua_pushnumber` / `getNumber<double>` |
| `core.lua` | `rollValue()` helper detects int vs float; description formats `%.1f` |

### Event Callbacks (C++ → Lua, ~4 calls/sec)

Fire **only when a proc occurs** — not every hit. Uses existing TFS `Events` system (`events.xml` + `rarity.lua` dispatcher + `event_callbacks.lua`).

| Callback | When | Parameters |
|----------|------|-----------|
| `onAttackProc` | On-attack spell procs | `player, target, item, statKey, combatType, damage` → `bool` |
| `onHitProc` | On-hit spell procs | `player, target, item, statKey, combatType, damage` → `bool` |
| `onDoubleDamage` | Double damage procs | `player` → `bool` |
| `onElementalDamage` | Elemental damage applied | `player, item, fireDmg` → `bool` |
| `onKillProc` | On-kill stat processed | `player, target, item, statKey, value` → `bool` |

Return `true` = apply default C++ behavior. Return `false` = skip default (callback did custom handling).

### Damage Amplification (specialMagicLevel)

Uses the same `specialMagicLevelSkill` mechanism as the Wheel of Destiny:

- `player:addSpecialMagicLevel(combatType, value)` — Lua binding (ported from PR #51)
- `getEffectiveMagicLevel(player, combatType)` = `baseML + specialML` — used in all spell formulas
- `getRaritySpellDamage()` uses `getEffectiveMagicLevel` per element

```lua
-- Item with fireDamage=5% equipped → addSpecialMagicLevel(FIRE, +5)
-- Fire spell cast → mlvl used in formula = baseML + 5
```

### Slot-Scoped Condition Removal

Fixed HP/MP stats being lost on equip/unequip. Before: `for subId = 100, 2000` removed ALL slots' conditions. After: `removeSlotConditions(slot)` only removes the target slot.

### Combat Pipeline

```
Combat::doTargetCombat
  ├─ processRarityOnAttack()      → weapon-based spell procs + Lua callback
  ├─ [block check + forge dodge/crit/fatal]
  ├─ processRarityOnHit()         → equipped-item spell procs (best chance wins) + Lua callback
  ├─ processRarityDoubleDamage()  → summed chance from all equipped + Lua callback
  ├─ processRarityElementalDamage() → fire/elemental secondary damage + Lua callback
  └─ Game::combatChangeHealth()
       ├─ [Lua healthChange] → resistances, life leech
       ├─ [apply damage]
       └─ processRarityOnKill()   → explosion, regen, buffs + per-stat Lua callback
```

### Lua Architecture (`data/scripts/systems/rarity/`)

| File | Purpose |
|------|---------|
| `init.lua` | Bootstrap — loads config → balancing → helpers → core (Main Interface) |
| `config.lua` | 3 tiers, 45 attributes, monster tiers, `eligible()` closures |
| `balancing.lua` | Spell damage formula params + onKill buff values — consumed by C++ |
| `core.lua` | `rollRarity()`, `itemAttributes()`, `processMonsterLoot()` |
| `combat.lua` | `healthChange`/`manaChange` — protections + life leech + damage amplification |
| `events.lua` | `onDropLoot`, `onInventoryUpdate`, `onLogin`, `onLogout` |
| `callbacks.lua` | Example event callback registration |
| `helpers.lua` | `rarityDebug()` |

### Talkactions

- `/i [rare|epic|legendary] <item> [, tier N] [, count]` — auto-creates backpack if no container space
- `/roll [rare|epic|legendary]` — applies rarity to item in front

---

## Augment System

Spell-based item augments defined in `items.xml`. Processed in `applyItemAugments(CombatDamage&)` — called from `combat.cpp` during spell execution.

### Items.xml Format

```xml
<attribute key="augments" value="1">
  <attribute key="divine caldera" value="base">
    <attribute key="value" value="800" />
  </attribute>
  <attribute key="energy wave" value="critical extra damage">
    <attribute key="value" value="800" />
  </attribute>
</attribute>
```

### 13 Augment Types

| Type | Effect | Scale |
|------|--------|-------|
| `Base` | `+value/10000` damage multiplier | e.g., `800` = +8% |
| `PowerfulImpact` | Same formula, configurable from `server_config.lua` | |
| `StrongImpact` | Same formula, configurable from `server_config.lua` | |
| `IncreasedDamage` | Same formula, configurable from `server_config.lua` | |
| `Cooldown` | Reduces spell cooldown by `value/1000` seconds | `900000` = -900s |
| `CriticalExtraDamage` | Accumulates `damage.criticalDamage += value` | `800` = +800 extra |
| `CriticalHitChance` | Roll `uniform_random(1,10000) <= value` → sets `damage.critical` | `1000` = 10% |
| `LifeLeech` | `damage.lifeLeechChance += value` | `400` = 4% of damage healed |
| `ManaLeech` | `damage.manaLeechChance += value` | `300` = 3% of damage → mana |
| `MagicLevelHealing` | `bonus = value/100 * getMagicLevel()` | bonus to heal spells |
| `MagicLevelDamage` | `bonus = value/100 * getMagicLevel()` | bonus to damage spells |
| `SkillDamage` | `bonus = value/100 * skillLevel()` | auto-detects weapon type |

### Life/Mana Leech Pipeline

```
applyItemAugments → damage.lifeLeechChance += 400
  ↓
Game::combatChangeHealth → drainHealth(realDamage=-500)
  ↓
int32_t heal = 500 * 400/10000 = 20 → attacker->gainHealth(20) ✅
```

### spellNameCasting Save/Restore

Prevents nested spells from overwriting the parent spell name:

```
executeCastSpell(A):
  save previousSpellName = "" → setCasting("A")
  → execute spell logic (may trigger spell B)
    executeCastSpell(B):
      save previousSpellName = "A" → setCasting("B")
      → B logic uses "B" for augments ✅
      restore previousSpellName = "A" ✅
  restore previousSpellName = "" ✅
```

---

## Files Changed

### C++ (22 files)

| File | Changes |
|------|---------|
| `src/configmanager.h` | + `RARITY_SYSTEM_ENABLED`, `AUGMENT_*`, `WHEEL_SYSTEM_ENABLED` |
| `src/configmanager.cpp` | Parse toggles + `std::ranges::clamp` validation |
| `src/combat.cpp` | Rarity procs (OnAttack/OnHit/DoubleDmg/EleDmg) + Augment apply + `getEffectiveMagicLevel` + `getRaritySpellDamage` |
| `src/game.cpp` | `processRarityOnKill` + Augment leech consumption |
| `src/item.h` | `namespace Rarity` (constants + `makeStatKey`) + Item methods + `getAugmentsBySpellName` |
| `src/item.cpp` | Rarity method implementations + `getDouble()` |
| `src/items.h` | `Augment_t` enum, `AugmentInfo` struct, parse/description methods |
| `src/items.cpp` | Augment XML parser + description builder |
| `src/player.h` | `getWeaponShared`, `getInventoryItemShared`, `applyItemAugments`, `spellNameCasting`, `calculateAugmentCooldownReduction` |
| `src/player.cpp` | Shared_ptr methods + augment application + 10 bug fixes |
| `src/luaplayer.cpp` | `addSpecialMagicLevel`, `getSpecialMagicLevel` bindings |
| `src/luaitem.cpp` | `hasRarity`, `get/setRarityTier/Stat`, `clearRarityStats` |
| `src/luascript.h/cpp` | Method declarations + `RARITY_SYSTEM_ENABLED` global + `CONST_SLOT_FIRST/LAST` |
| `src/events.h/cpp` | 5 rarity event callbacks (OnAttack/OnHit/Double/Ele/KillProc) |
| `src/spells.cpp` | `spellNameCasting` save/restore in executeCastSpell |
| `src/enums.h` | `instantSpellName`, `criticalDamage`, `lifeLeechChance`, `manaLeechChance` in `CombatDamage` |
| `src/observer_ptr.h` | `using ObserverPtr<T> = T*` |

### Lua (16 files)

| File | Description |
|------|-------------|
| `data/server_config.lua` | `raritySystemEnabled`, `augment*Percent` |
| `data/startup/others/functions.lua` | `dofile('data/scripts/systems/rarity/init.lua')` |
| `data/scripts/rarity_register.lua` | Loads CreatureEvent/Event files in Scripts Interface |
| `data/scripts/systems/rarity/config.lua` | Tiers + 45 attributes + monster tiers + TYPE REFERENCE |
| `data/scripts/systems/rarity/balancing.lua` | Spell damage formula params + onKill + TYPE REFERENCE |
| `data/scripts/systems/rarity/core.lua` | `rollRarity()`, `itemAttributes()`, `processMonsterLoot()` |
| `data/scripts/systems/rarity/combat.lua` | `healthChange` (protections + leech) + `damageKeys` amplification |
| `data/scripts/systems/rarity/events.lua` | `onDropLoot`, `onInventoryUpdate`, `onLogin`, `onLogout` |
| `data/scripts/systems/rarity/callbacks.lua` | Event callback registration example |
| `data/scripts/systems/rarity/helpers.lua` | `rarityDebug()` |
| `data/scripts/systems/rarity/init.lua` | Entry point, loads config modules |
| `data/events/events.xml` | Rarity callback entries |
| `data/events/scripts/rarity.lua` | TFS intermediate dispatcher for rarity callbacks |
| `data/scripts/lib/event_callbacks.lua` | `ec.onAttackProc` etc. registrations |
| `data/scripts/talkactions/god/items/create_item.lua` | `/i` with rarity + forge + auto-backpack |
| `data/scripts/talkactions/god/items/roll_item.lua` | `/roll` applies rarity to item in front |

### Documentation (2 files)

| File | Description |
|------|-------------|
| `doc/RARITY_SYSTEM.md` | Full architecture, C++23 features, callback API, types, test plan |
| `cdocs/augments.md` | Augment system reference |

---

## Testing

### Rarity
- 45 attributes with `eligible()` closures checked against item type properties
- 3 tiers with cumulative chance rolls (10000-scale)
- 6 on-attack + 6 on-hit spells with per-element damage scaling
- 7 protection types + 8 damage types + life leech
- 6 on-kill effects (explosion, regen HP/MP, 3 buffs)
- Slot-scoped condition removal verified: equip 3 items → unequip middle → only middle loses stats

### Augments
- All 13 augment types parsed from `items.xml`
- Case-insensitive spell name matching (`to_lower_copy` both sides)
- Description does not duplicate label (e.g., shows `+800` not `+800 critical extra damage CriticalExtraDamage`)
- Nested spells do not overwrite parent `spellNameCasting` (save/restore)
- `damage.instantSpellName` preferred over `spellNameCasting` in `applyItemAugments`
- Leech consumption: `lifeLeechChance` → `gainHealth()` after `drainHealth()`
- Config % validation: `std::ranges::clamp 0-100`
- Proficiency dead code removed

### Coexistence
- Rarity stats + Augments + Imbuements: 3 independent layers, no conflicts
- Rarity damage amplification + Augment damage multiplier: applied at different pipeline points
