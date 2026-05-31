-- Rarity Event Registrations
-- onInventoryUpdate, onLogin hooks.
-- Note: onDropLoot rarity roll moved to eventcallbacks/monster/default_onDropLoot.lua
-- so ALL monsters are eligible (not just those in rarityConfig.monsterTiers).
--
-- === EVENT CALLBACKS (C++ → Lua, dispatched via events.xml + rarity.lua) ===
-- These fire ONLY when a proc actually occurs (~4 calls/sec even under load).
-- Register via Event() in any script file:
--
--   local cb = Event()
--   cb.onAttackProc = function(player, target, item, statKey, combatType, damage)
--       return true  -- false = skip default C++ behavior
--   end
--   cb:register()
--
-- Available callbacks:
--   onAttackProc(player, target, item, statKey, combatType, damage) → bool
--   onHitProc(player, target, item, statKey, combatType, damage) → bool
--   onDoubleDamage(player) → bool
--   onElementalDamage(player, item, fireDmg) → bool
--   onKillProc(player, target, item, statKey, value) → bool
--
-- See callbacks.lua for a complete working example.

-- =============================================================================
-- onInventoryUpdate: Apply/remove conditions when equipping/unequipping
-- =============================================================================
local rarityInventory = Event()
function rarityInventory.onInventoryUpdate(player, item, slot, equip)
	if not RARITY_SYSTEM_ENABLED then return end

	itemAttributes(player, item, slot, equip)

	-- If unequipping, re-apply conditions from remaining equipped items
	if not equip then
		for i = CONST_SLOT_FIRST, CONST_SLOT_LAST do
			local otherItem = player:getSlotItem(i)
			if otherItem and otherItem:getRarityTier() > 0 then
				itemAttributes(player, otherItem, i, true)
			end
		end
	end

	-- Recalculate damage bonuses after any equip/unequip
	applyRarityDamageBonuses(player)
end
rarityInventory:register()

-- =============================================================================
-- onLogin: Register health/mana events and re-apply equipment conditions
-- =============================================================================
local rarityLogin = CreatureEvent("rarityLogin")
function rarityLogin.onLogin(player)
	if not RARITY_SYSTEM_ENABLED then return true end

	-- Register combat events
	player:registerEvent("rarityHealthChange")
	player:registerEvent("rarityManaChange")
	player:registerEvent("rarityLogout")

	-- Apply rarity conditions from currently equipped items
	for slot = CONST_SLOT_FIRST, CONST_SLOT_LAST do
		local item = player:getSlotItem(slot)
		if item and item:getRarityTier() > 0 then
			itemAttributes(player, item, slot, true)
		end
	end

	-- Apply damage bonuses
	applyRarityDamageBonuses(player)

	-- Fix health/mana if they exceed max due to rarity bonuses
	local health = player:getHealth()
	local maxHealth = player:getMaxHealth()
	if health > maxHealth then
		player:addHealth(maxHealth - health)
	end

	local mana = player:getMana()
	local maxMana = player:getMaxMana()
	if mana > maxMana then
		player:addMana(maxMana - mana)
	end

	return true
end
rarityLogin:register()

-- =============================================================================
-- onLogout: Remove damage bonuses
-- =============================================================================
local rarityLogout = CreatureEvent("rarityLogout")
function rarityLogout.onLogout(player)
	removeRarityDamageBonuses(player)
	return true
end
rarityLogout:register()
