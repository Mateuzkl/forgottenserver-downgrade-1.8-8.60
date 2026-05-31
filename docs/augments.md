# Item Augments

This port keeps Canary-style static item augments without rarity dependencies.
Augments are read from `data/items/items.xml` and applied only while the item is
equipped.

Enable the feature in `config.lua`:

```lua
augmentSystemEnabled = true
```

```xml
<attribute key="augments" value="1">
	<attribute key="fierce berserk" value="base damage">
		<attribute key="value" value="4"/>
	</attribute>
</attribute>
```

Supported combat effects are base damage, base healing, cooldown, life leech,
mana leech, critical extra damage, and critical hit chance. The named defaults
`increased damage`, `powerful impact`, and `strong impact` use the values from
`config.lua`.

Weapon proficiency spell augments are loaded from the official
`data/items/proficiencies.json` dataset. Only selected `SPELL_AUGMENT` perks are
applied. They are indexed by equipped weapon and spell ID, so changing weapons
cannot leave stale bonuses active.

Wheel bonuses continue to use the existing Lua condition pipeline. Their skill,
magic, and leech values accumulate with item and proficiency augments in the
classic `CombatDamage` pipeline.
