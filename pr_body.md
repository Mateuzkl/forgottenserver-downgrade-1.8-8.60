## Summary

- **12 augment types** defined in `items.xml` per item type (no DB changes)
- **Automatic spell matching** via `spellNameCasting` set in `executeCastSpell` before Lua execution, cleared in `postCastSpell`
- **Cooldown reduction** from equipped augments, minimum 50% of base cooldown
- **Skill auto-detection** for `SkillDamage` type (sword/axe/club/dist/wand->ML/fist)
- **Config defaults** in `data/server_config.lua`
- **Proficiency augment stubs** in `player.h`
- **Full documentation** in `cdocs/augments.md`

### Files Changed

| File | Changes |
|---|---|
| `src/items.h` | `Augment_t` enum (12 values), `AugmentInfo` struct, `ItemType::augments`, `ITEM_PARSE_AUGMENT` |
| `src/items.cpp` | Map entry, nested XML parsing, description formatting |
| `src/item.h` | `Item::getAugments()` accessor methods |
| `src/enums.h` | `instantSpellName` in `CombatDamage` |
| `src/player.h/cpp` | `applyItemAugments()`, `calculateAugmentCooldownReduction()`, `getEquippedAugmentItems()`, proficiency stubs |
| `src/spells.cpp` | `executeCastSpell` sets `spellNameCasting`; `postCastSpell` applies cooldown reduction and clears it |
| `src/combat.cpp` | Hook `applyItemAugments()` in both `doCombat` overloads after `getCombatDamage` |
| `src/configmanager.h/cpp` | 3 config Integer enums + loading from `server_config.lua` |
| `data/server_config.lua` | `augmentIncreasedDamagePercent`, `augmentPowerfulImpactPercent`, `augmentStrongImpactPercent` |
| `cdocs/augments.md` | Complete documentation: architecture, XML format, all 12 types, formulas, examples, extension guide |
