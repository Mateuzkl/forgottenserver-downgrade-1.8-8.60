-- Rarity System Configuration
-- All tier definitions, attribute lists, monster tiers, and settings.

rarityConfig = {}

-- =============================================================================
-- Global Settings
-- =============================================================================

rarityConfig.animations = true
rarityConfig.popupText = true
rarityConfig.popupEffect = CONST_ME_STUN

-- =============================================================================
-- Rarity Tier Definitions
-- =============================================================================
-- chance: out of 10000 (e.g. 200 = 2%)
-- secondStatChance: % chance for a second stat
-- color: text color for popup (0 = default)

rarityConfig.tiers = {
	rare = {
		name = "Rare",
		article = "a rare",
		chance = 750,
		secondStatChance = 20,
		color = TEXTCOLOR_GREEN,
	},
	epic = {
		name = "Epic",
		article = "an epic",
		chance = 375,
		secondStatChance = 50,
		color = TEXTCOLOR_BLUE,
	},
	legendary = {
		name = "Legendary",
		article = "a legendary",
		chance = 200,
		secondStatChance = 100,
		color = TEXTCOLOR_ORANGE,
	},
}

-- =============================================================================
-- Monster Tier Configuration
-- =============================================================================
-- chance: % chance for any rarity roll on this monster's loot
-- minTier: 1=rare, 2=epic, 3=legendary (minimum tier guaranteed)
-- monsters: list of monster names (lowercase, exact match)

rarityConfig.monsterTiers = {
	boss = {
		chance = 50,
		minTier = 2,
		monsters = {
			"orshabaal", "morgaroth", "ferumbras", "ghazbaran",
			"zugurosh", "mahrdis", "the evil eye", "the librarian",
			"mr. punish", "horned fox", "the count", "the many",
		},
	},
	miniboss = {
		chance = 20,
		minTier = 1,
		monsters = {
			"black knight", "diseased bill", "diseased dan",
			"diseased fred", "elder wyrm", "dracola", "the masked marauder",
		},
	},
}

-- Default for unclassified monsters
rarityConfig.defaultMonsterChance = 5
rarityConfig.defaultMinTier = 1

-- =============================================================================
-- Attribute Definitions
-- =============================================================================
-- Each attribute:
--   statKey    = key stored via item:setRarityStat() (C++ matches for combat)
--   name       = display name
--   valueType  = "static" (flat number) or "percent" (adds % sign)
--   rare/epic/legendary = {min, max} roll range
--   eligible   = function(itemType) -> bool  (NO hardcoded item IDs)
--   isPercent  = true if the unit is a percentage
--   onEquip    = function(player, value)  -- optional, for condition-based stats

rarityConfig.attributes = {
	-- =========================================================================
	-- STAT BONUSES (applied on equip via conditions)
	-- =========================================================================
	["maxHp"] = {
		statKey = "maxHp",
		name = "Max HP",
		valueType = "static",
		rare = {30, 60}, epic = {80, 120}, legendary = {150, 250},
		eligible = function(itemType)
			return itemType:getArmor() > 0 or itemType:isWeapon()
		end,
		isPercent = false,
		onEquip = function(player, slot, value, equip)
			local bonus = equip and value or -value
			local condition = Condition(CONDITION_ATTRIBUTES)
			condition:setParameter(CONDITION_PARAM_SUBID, 100 + slot)
			condition:setParameter(CONDITION_PARAM_STAT_MAXHITPOINTS, bonus)
			condition:setParameter(CONDITION_PARAM_TICKS, -1)
			player:addCondition(condition)
		end,
	},
	["maxMp"] = {
		statKey = "maxMp",
		name = "Max MP",
		valueType = "static",
		rare = {30, 60}, epic = {80, 120}, legendary = {150, 250},
		eligible = function(itemType)
			return itemType:getArmor() > 0 or itemType:isWeapon() or itemType:getWeaponType() == WEAPON_SHIELD
		end,
		isPercent = false,
		onEquip = function(player, slot, value, equip)
			local bonus = equip and value or -value
			local condition = Condition(CONDITION_ATTRIBUTES)
			condition:setParameter(CONDITION_PARAM_SUBID, 200 + slot)
			condition:setParameter(CONDITION_PARAM_STAT_MAXMANAPOINTS, bonus)
			condition:setParameter(CONDITION_PARAM_TICKS, -1)
			player:addCondition(condition)
		end,
	},
	["maxHpPercent"] = {
		statKey = "maxHpPercent",
		name = "Max HP",
		valueType = "percent",
		rare = {1, 3}, epic = {4, 6}, legendary = {7, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0
		end,
		isPercent = true,
		onEquip = function(player, slot, value, equip)
			local base = player:getMaxHealth()
			local bonus = math.floor(base * (value / 100))
			if not equip then bonus = -bonus end
			local condition = Condition(CONDITION_ATTRIBUTES)
			condition:setParameter(CONDITION_PARAM_SUBID, 300 + slot)
			condition:setParameter(CONDITION_PARAM_STAT_MAXHITPOINTS, bonus)
			condition:setParameter(CONDITION_PARAM_TICKS, -1)
			player:addCondition(condition)
		end,
	},
	["maxMpPercent"] = {
		statKey = "maxMpPercent",
		name = "Max MP",
		valueType = "percent",
		rare = {1, 3}, epic = {4, 6}, legendary = {7, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0
		end,
		isPercent = true,
		onEquip = function(player, slot, value, equip)
			local base = player:getMaxMana()
			local bonus = math.floor(base * (value / 100))
			if not equip then bonus = -bonus end
			local condition = Condition(CONDITION_ATTRIBUTES)
			condition:setParameter(CONDITION_PARAM_SUBID, 400 + slot)
			condition:setParameter(CONDITION_PARAM_STAT_MAXMANAPOINTS, bonus)
			condition:setParameter(CONDITION_PARAM_TICKS, -1)
			player:addCondition(condition)
		end,
	},
	["magicLevel"] = {
		statKey = "magicLevel",
		name = "Magic Level",
		valueType = "static",
		rare = {1, 2}, epic = {3, 5}, legendary = {6, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0 or itemType:getWeaponType() == WEAPON_WAND
		end,
		isPercent = false,
		onEquip = function(player, slot, value, equip)
			local bonus = equip and value or -value
			local condition = Condition(CONDITION_ATTRIBUTES)
			condition:setParameter(CONDITION_PARAM_SUBID, 500 + slot)
			condition:setParameter(CONDITION_PARAM_STAT_MAGICPOINTS, bonus)
			condition:setParameter(CONDITION_PARAM_TICKS, -1)
			player:addCondition(condition)
		end,
	},
	["swordSkill"] = {
		statKey = "swordSkill",
		name = "Sword Skill",
		valueType = "static",
		rare = {1, 2}, epic = {3, 5}, legendary = {6, 10},
		eligible = function(itemType)
			return itemType:getWeaponType() == WEAPON_SWORD
		end,
		onEquip = function(player, slot, value, equip)
			local bonus = equip and value or -value
			local condition = Condition(CONDITION_ATTRIBUTES)
			condition:setParameter(CONDITION_PARAM_SUBID, 600 + slot)
			condition:setParameter(CONDITION_PARAM_SKILL_SWORD, bonus)
			condition:setParameter(CONDITION_PARAM_TICKS, -1)
			player:addCondition(condition)
		end,
	},
	["axeSkill"] = {
		statKey = "axeSkill",
		name = "Axe Skill",
		valueType = "static",
		rare = {1, 2}, epic = {3, 5}, legendary = {6, 10},
		eligible = function(itemType)
			return itemType:getWeaponType() == WEAPON_AXE
		end,
		onEquip = function(player, slot, value, equip)
			local bonus = equip and value or -value
			local condition = Condition(CONDITION_ATTRIBUTES)
			condition:setParameter(CONDITION_PARAM_SUBID, 700 + slot)
			condition:setParameter(CONDITION_PARAM_SKILL_AXE, bonus)
			condition:setParameter(CONDITION_PARAM_TICKS, -1)
			player:addCondition(condition)
		end,
	},
	["clubSkill"] = {
		statKey = "clubSkill",
		name = "Club Skill",
		valueType = "static",
		rare = {1, 2}, epic = {3, 5}, legendary = {6, 10},
		eligible = function(itemType)
			return itemType:getWeaponType() == WEAPON_CLUB
		end,
		onEquip = function(player, slot, value, equip)
			local bonus = equip and value or -value
			local condition = Condition(CONDITION_ATTRIBUTES)
			condition:setParameter(CONDITION_PARAM_SUBID, 800 + slot)
			condition:setParameter(CONDITION_PARAM_SKILL_CLUB, bonus)
			condition:setParameter(CONDITION_PARAM_TICKS, -1)
			player:addCondition(condition)
		end,
	},
	["fistSkill"] = {
		statKey = "fistSkill",
		name = "Fist Skill",
		valueType = "static",
		rare = {1, 2}, epic = {3, 5}, legendary = {6, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0
		end,
		onEquip = function(player, slot, value, equip)
			local bonus = equip and value or -value
			local condition = Condition(CONDITION_ATTRIBUTES)
			condition:setParameter(CONDITION_PARAM_SUBID, 900 + slot)
			condition:setParameter(CONDITION_PARAM_SKILL_FIST, bonus)
			condition:setParameter(CONDITION_PARAM_TICKS, -1)
			player:addCondition(condition)
		end,
	},
	["distanceSkill"] = {
		statKey = "distanceSkill",
		name = "Distance Skill",
		valueType = "static",
		rare = {1, 2}, epic = {3, 5}, legendary = {6, 10},
		eligible = function(itemType)
			return itemType:getWeaponType() == WEAPON_DISTANCE or itemType:getArmor() > 0
		end,
		onEquip = function(player, slot, value, equip)
			local bonus = equip and value or -value
			local condition = Condition(CONDITION_ATTRIBUTES)
			condition:setParameter(CONDITION_PARAM_SUBID, 1000 + slot)
			condition:setParameter(CONDITION_PARAM_SKILL_DISTANCE, bonus)
			condition:setParameter(CONDITION_PARAM_TICKS, -1)
			player:addCondition(condition)
		end,
	},
	["shielding"] = {
		statKey = "shielding",
		name = "Shielding",
		valueType = "static",
		rare = {1, 2}, epic = {3, 5}, legendary = {6, 10},
		eligible = function(itemType)
			return itemType:getWeaponType() == WEAPON_SHIELD or itemType:getArmor() > 0
		end,
		onEquip = function(player, slot, value, equip)
			local bonus = equip and value or -value
			local condition = Condition(CONDITION_ATTRIBUTES)
			condition:setParameter(CONDITION_PARAM_SUBID, 1100 + slot)
			condition:setParameter(CONDITION_PARAM_SKILL_SHIELD, bonus)
			condition:setParameter(CONDITION_PARAM_TICKS, -1)
			player:addCondition(condition)
		end,
	},
	["meleeSkills"] = {
		statKey = "meleeSkills",
		name = "Melee Skills",
		valueType = "static",
		rare = {1, 2}, epic = {3, 5}, legendary = {6, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0 and itemType:getSlotPosition() ~= 0
		end,
		onEquip = function(player, slot, value, equip)
			local bonus = equip and value or -value
			for _, skill in ipairs({SKILL_SWORD, SKILL_AXE, SKILL_CLUB, SKILL_FIST}) do
				local condition = Condition(CONDITION_ATTRIBUTES)
				condition:setParameter(CONDITION_PARAM_SUBID, 1200 + slot * 10 + skill)
				condition:setParameter(skill, bonus)
				condition:setParameter(CONDITION_PARAM_TICKS, -1)
				player:addCondition(condition)
			end
		end,
	},
	["experience"] = {
		statKey = "experience",
		name = "Experience",
		valueType = "percent",
		rare = {3, 5}, epic = {6, 10}, legendary = {11, 20},
		eligible = function(itemType)
			return itemType:getArmor() > 0 or itemType:isWeapon()
		end,
		isPercent = true,
	},

	-- =========================================================================
	-- ATTACK / BASE STATS (modify the item itself via setAttribute)
	-- =========================================================================
	["attack"] = {
		statKey = "attack",
		name = "Attack",
		valueType = "static",
		rare = {1, 3}, epic = {4, 6}, legendary = {7, 10},
		eligible = function(itemType)
			return itemType:isWeapon() and itemType:getAttack() > 0
		end,
	},
	["defense"] = {
		statKey = "defense",
		name = "Defense",
		valueType = "static",
		rare = {1, 2}, epic = {3, 4}, legendary = {5, 6},
		eligible = function(itemType)
			return itemType:getDefense() > 0
		end,
	},
	["armor"] = {
		statKey = "armor",
		name = "Armor",
		valueType = "static",
		rare = {1, 1}, epic = {2, 3}, legendary = {4, 5},
		eligible = function(itemType)
			return itemType:getArmor() > 0
		end,
	},

	-- =========================================================================
	-- ON ATTACK SPELLS (C++ hook in combat.cpp)
	-- =========================================================================
	["onAttackFireStrike"] = {
		statKey = "onAttackFireStrike",
		name = "Cast Fire Strike on Attack",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["onAttackIceStrike"] = {
		statKey = "onAttackIceStrike",
		name = "Cast Ice Strike on Attack",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["onAttackTerraStrike"] = {
		statKey = "onAttackTerraStrike",
		name = "Cast Terra Strike on Attack",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["onAttackDeathStrike"] = {
		statKey = "onAttackDeathStrike",
		name = "Cast Death Strike on Attack",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["onAttackEnergyStrike"] = {
		statKey = "onAttackEnergyStrike",
		name = "Cast Energy Strike on Attack",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["onAttackDivineMissile"] = {
		statKey = "onAttackDivineMissile",
		name = "Cast Divine Missile on Attack",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},

	-- =========================================================================
	-- ON HIT SPELLS (C++ hook in combat.cpp)
	-- =========================================================================
	["onHitFireStrike"] = {
		statKey = "onHitFireStrike",
		name = "Cast Fire Strike on Hit",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["onHitIceStrike"] = {
		statKey = "onHitIceStrike",
		name = "Cast Ice Strike on Hit",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["onHitTerraStrike"] = {
		statKey = "onHitTerraStrike",
		name = "Cast Terra Strike on Hit",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["onHitDeathStrike"] = {
		statKey = "onHitDeathStrike",
		name = "Cast Death Strike on Hit",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["onHitEnergyStrike"] = {
		statKey = "onHitEnergyStrike",
		name = "Cast Energy Strike on Hit",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["onHitDivineMissile"] = {
		statKey = "onHitDivineMissile",
		name = "Cast Divine Missile on Hit",
		valueType = "percent",
		rare = {5, 5}, epic = {10, 10}, legendary = {15, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},

	-- =========================================================================
	-- DAMAGE MODIFIERS (C++ hooks in combat.cpp)
	-- =========================================================================
	["doubleDamage"] = {
		statKey = "doubleDamage",
		name = "Double Damage",
		valueType = "percent",
		rare = {3, 5}, epic = {6, 8}, legendary = {9, 12},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["physicalDamage"] = {
		statKey = "physicalDamage",
		name = "Physical Damage",
		valueType = "percent",
		rare = {3, 5}, epic = {6, 10}, legendary = {11, 15},
		eligible = function(itemType)
			return itemType:isWeapon() or itemType:getWeaponType() == WEAPON_SHIELD
		end,
		isPercent = true,
	},
	["fireDamage"] = {
		statKey = "fireDamage",
		name = "Fire Damage",
		valueType = "percent",
		rare = {3, 5}, epic = {6, 10}, legendary = {11, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["iceDamage"] = {
		statKey = "iceDamage",
		name = "Ice Damage",
		valueType = "percent",
		rare = {3, 5}, epic = {6, 10}, legendary = {11, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["energyDamage"] = {
		statKey = "energyDamage",
		name = "Energy Damage",
		valueType = "percent",
		rare = {3, 5}, epic = {6, 10}, legendary = {11, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["earthDamage"] = {
		statKey = "earthDamage",
		name = "Earth Damage",
		valueType = "percent",
		rare = {3, 5}, epic = {6, 10}, legendary = {11, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["holyDamage"] = {
		statKey = "holyDamage",
		name = "Holy Damage",
		valueType = "percent",
		rare = {3, 5}, epic = {6, 10}, legendary = {11, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["deathDamage"] = {
		statKey = "deathDamage",
		name = "Death Damage",
		valueType = "percent",
		rare = {3, 5}, epic = {6, 10}, legendary = {11, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},
	["elementalDamage"] = {
		statKey = "elementalDamage",
		name = "Elemental Damage",
		valueType = "percent",
		rare = {3, 5}, epic = {6, 10}, legendary = {11, 15},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},

	-- =========================================================================
	-- PROTECTIONS (handled in Lua healthChange creaturescript)
	-- =========================================================================
	["physicalProtection"] = {
		statKey = "physicalProtection",
		name = "Physical Protection",
		valueType = "percent",
		rare = {2, 3}, epic = {4, 6}, legendary = {7, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0 or itemType:getWeaponType() == WEAPON_SHIELD
		end,
		isPercent = true,
	},
	["fireProtection"] = {
		statKey = "fireProtection",
		name = "Fire Protection",
		valueType = "percent",
		rare = {2, 3}, epic = {4, 6}, legendary = {7, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0
		end,
		isPercent = true,
	},
	["iceProtection"] = {
		statKey = "iceProtection",
		name = "Ice Protection",
		valueType = "percent",
		rare = {2, 3}, epic = {4, 6}, legendary = {7, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0
		end,
		isPercent = true,
	},
	["energyProtection"] = {
		statKey = "energyProtection",
		name = "Energy Protection",
		valueType = "percent",
		rare = {2, 3}, epic = {4, 6}, legendary = {7, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0
		end,
		isPercent = true,
	},
	["earthProtection"] = {
		statKey = "earthProtection",
		name = "Earth Protection",
		valueType = "percent",
		rare = {2, 3}, epic = {4, 6}, legendary = {7, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0
		end,
		isPercent = true,
	},
	["holyProtection"] = {
		statKey = "holyProtection",
		name = "Holy Protection",
		valueType = "percent",
		rare = {2, 3}, epic = {4, 6}, legendary = {7, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0
		end,
		isPercent = true,
	},
	["deathProtection"] = {
		statKey = "deathProtection",
		name = "Death Protection",
		valueType = "percent",
		rare = {2, 3}, epic = {4, 6}, legendary = {7, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0
		end,
		isPercent = true,
	},
	["elementalProtection"] = {
		statKey = "elementalProtection",
		name = "Elemental Protection",
		valueType = "percent",
		rare = {2, 3}, epic = {4, 6}, legendary = {7, 10},
		eligible = function(itemType)
			return itemType:getArmor() > 0
		end,
		isPercent = true,
	},

	-- =========================================================================
	-- LIFE/MANA LEECH (handled in Lua healthChange creaturescript)
	-- =========================================================================
	["lifeLeech"] = {
		statKey = "lifeLeech",
		name = "Life Leech",
		valueType = "percent",
		rare = {3, 5}, epic = {6, 10}, legendary = {11, 20},
		eligible = function(itemType)
			return itemType:isWeapon()
		end,
		isPercent = true,
	},

	-- =========================================================================
	-- ON KILL EFFECTS (C++ hook in game.cpp)
	-- =========================================================================
	["onKillExplosion"] = {
		statKey = "onKillExplosion",
		name = "Explosion on Kill",
		valueType = "percent",
		rare = {5, 5}, epic = {8, 8}, legendary = {12, 12},
		eligible = function(itemType)
			return itemType:isWeapon() or itemType:getArmor() > 0
		end,
		isPercent = true,
	},
	["onKillRegenHp"] = {
		statKey = "onKillRegenHp",
		name = "Regen Health on Kill",
		valueType = "static",
		rare = {30, 50}, epic = {60, 100}, legendary = {120, 200},
		eligible = function(itemType)
			return itemType:isWeapon() or itemType:getArmor() > 0
		end,
	},
	["onKillRegenMp"] = {
		statKey = "onKillRegenMp",
		name = "Regen Mana on Kill",
		valueType = "static",
		rare = {30, 50}, epic = {60, 100}, legendary = {120, 200},
		eligible = function(itemType)
			return itemType:isWeapon() or itemType:getArmor() > 0
		end,
	},
	["onKillBuffDamage"] = {
		statKey = "onKillBuffDamage",
		name = "Bonus Damage on Kill",
		valueType = "percent",
		rare = {5, 5}, epic = {8, 8}, legendary = {10, 10},
		eligible = function(itemType)
			return itemType:isWeapon() or itemType:getArmor() > 0
		end,
		isPercent = true,
	},
	["onKillBuffMaxHp"] = {
		statKey = "onKillBuffMaxHp",
		name = "Bonus Max HP on Kill",
		valueType = "percent",
		rare = {5, 5}, epic = {8, 8}, legendary = {10, 10},
		eligible = function(itemType)
			return itemType:isWeapon() or itemType:getArmor() > 0
		end,
		isPercent = true,
	},
	["onKillBuffMaxMp"] = {
		statKey = "onKillBuffMaxMp",
		name = "Bonus Max MP on Kill",
		valueType = "percent",
		rare = {5, 5}, epic = {8, 8}, legendary = {10, 10},
		eligible = function(itemType)
			return itemType:isWeapon() or itemType:getArmor() > 0
		end,
		isPercent = true,
	},

	-- =========================================================================
	-- ADDITIONAL LOOT (handled in Lua onDropLoot event)
	-- =========================================================================
	["additionalLoot"] = {
		statKey = "additionalLoot",
		name = "Additional Loot",
		valueType = "percent",
		rare = {3, 3}, epic = {5, 5}, legendary = {8, 8},
		eligible = function(itemType)
			return itemType:getArmor() > 0 or itemType:isWeapon()
		end,
		isPercent = true,
	},
}

-- =============================================================================
-- TYPE REFERENCE
-- =============================================================================
-- All numeric values support floats (double). Integer ranges use math.random();
-- float ranges use math.random() * (max - min) via rollValue() in core.lua.
--
-- Attribute fields:
--   statKey   : string  — key stored via item:setRarityStat(), matches C++ Rarity::
--   name      : string  — display name in item description
--   valueType : string  — "static" (flat number) | "percent" (% suffix on display)
--   rare/epic/legendary : {number, number} — min/max roll range (accepts floats)
--   eligible  : fn(itemType)→bool — which item types can roll this stat
--   isPercent : bool  — true = display "%" suffix
--   onEquip   : fn?  — optional, for condition-based stats (skills, HP, MP, etc.)
--
-- Tier fields:
--   name             : string
--   article          : string  — "a rare", "an epic", "a legendary"
--   chance           : number  — out of 10000
--   secondStatChance : number  — % chance to roll a second stat
--   color            : number  — TEXTCOLOR_* constant
--
-- Monster tier fields:
--   chance   : number    — % chance for rarity on this monster's loot
--   minTier  : number    — 1=rare, 2=epic, 3=legendary
--   monsters : {string}  — list of lowercase monster names
--
-- Examples:
--   Flat int:      rare = {30, 60}
--   Percent int:   rare = {5, 10}
--   Float:         rare = {1.5, 3.7}     (e.g. 1.5% life leech)
--   Hybrid:        epic = {80.5, 120}    (mix of float and int)
