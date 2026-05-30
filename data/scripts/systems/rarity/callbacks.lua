-- Rarity Event Callbacks Example
-- Register custom behavior that fires when a rarity proc occurs.
-- Each callback receives the proc context and can return false to prevent
-- the default C++ behavior (e.g., skip the spell cast).
--
-- To use: copy the callback functions you need into your own script and call :register().

local rarityCallbacks = Event()

-- =============================================================================
-- onAttackProc: Fires when an on-attack spell procs (e.g. Fire Strike on hit)
-- statKey examples: "onAttackFireStrike", "onAttackIceStrike", etc.
-- combatType: COMBAT_FIREDAMAGE, COMBAT_ICEDAMAGE, COMBAT_EARTHDAMAGE,
--             COMBAT_DEATHDAMAGE, COMBAT_ENERGYDAMAGE, COMBAT_HOLYDAMAGE
-- =============================================================================
function rarityCallbacks.onAttackProc(player, target, item, statKey, combatType, damage)
	-- Example: custom effect for fire strike
	if statKey == "onAttackFireStrike" then
		target:getPosition():sendMagicEffect(CONST_ME_FIREAREA)
	end
	return true  -- true = C++ still casts the default spell
end

-- =============================================================================
-- onHitProc: Fires when an on-hit spell procs (target reflects damage back)
-- Same params as onAttackProc, but item is from any equipped slot (best wins).
-- =============================================================================
function rarityCallbacks.onHitProc(player, target, item, statKey, combatType, damage)
	return true
end

-- =============================================================================
-- onDoubleDamage: Fires when double damage procs
-- Return false to prevent the damage doubling.
-- =============================================================================
function rarityCallbacks.onDoubleDamage(player)
	-- Example: custom text message
	player:say("DOUBLE DAMAGE!", TALKTYPE_MONSTER_SAY)
	return true
end

-- =============================================================================
-- onElementalDamage: Fires when elemental/fire damage is about to be added
-- to the secondary damage slot. Return false to skip the elemental damage.
-- =============================================================================
function rarityCallbacks.onElementalDamage(player, item, fireDmg)
	return true
end

-- =============================================================================
-- onKillProc: Fires per-stat when an on-kill effect is processed.
-- statKey examples: "onKillExplosion", "onKillRegenHp", "onKillRegenMp",
--                   "onKillBuffDamage", "onKillBuffMaxHp", "onKillBuffMaxMp",
--                   "onKillBuffDuration", "onKillBuffCritChance",
--                   "onKillBuffCritAmount", "onKillBuffMaxHpPercent",
--                   "onKillBuffMaxMpPercent"
-- Return false to skip the default C++ effect for this stat.
-- =============================================================================
function rarityCallbacks.onKillProc(player, target, item, statKey, value)
	-- Example: custom heal effect instead of default
	if statKey == "onKillRegenHp" then
		player:addHealth(value * 2)   -- double the heal
		player:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
		return false  -- skip C++ default heal (already applied custom)
	end
	if statKey == "onKillExplosion" then
		target:getPosition():sendMagicEffect(CONST_ME_EXPLOSIONAREA)
	end
	return true
end

-- Uncomment to register:
-- rarityCallbacks:register()
