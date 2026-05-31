-- Rarity System Core
-- rollRarity, itemAttributes, and rollCondition functions.

-- =============================================================================
-- Roll a value in [min, max] range. Uses math.random() for integers,
-- math.random() * (max - min) for floats.
-- =============================================================================
local function rollValue(min, max)
	if min == math.floor(min) and max == math.floor(max) then
		return math.random(min, max)
	end
	return min + math.random() * (max - min)
end

-- =============================================================================
-- Roll rarity on a single item
-- @param item The Item to roll rarity on
-- @param forcedTier nil (natural roll), "rare"/"epic"/"legendary" (forced tier),
--                   true (random tier), or integer tier number
-- @param minTier minimum tier (1=rare, 2=epic, 3=legendary). 0 or nil = no minimum
-- @return tier number (1-3), or 0 if no rarity was rolled / item ineligible
-- =============================================================================
function rollRarity(item, forcedTier, minTier)
	if not item or ItemType(item:getId()):isStackable() then
		return 0
	end

	local itemType = ItemType(item:getId())
	if not itemType then
		return 0
	end

	-- Build list of eligible attributes
	local available = {}
	for attrKey, attrDef in pairs(rarityConfig.attributes) do
		if attrDef.eligible and attrDef.eligible(itemType) then
			if item:getRarityStat(attrDef.statKey) == 0 then
				available[#available + 1] = attrKey
			end
		end
	end

	if #available == 0 then
		return 0
	end

	-- Determine tier
	local tier
	if type(forcedTier) == "number" and forcedTier >= 1 and forcedTier <= 3 then
		tier = forcedTier
	elseif forcedTier == "rare" then
		tier = 1
	elseif forcedTier == "epic" then
		tier = 2
	elseif forcedTier == "legendary" then
		tier = 3
	elseif forcedTier == true then
		tier = math.random(1, 3)
	else
		-- Natural roll
		local roll = math.random(1, 10000)
		local tiers = rarityConfig.tiers
		local cumulative = 0
		for i = 3, 1, -1 do
			local tierName = ({ "rare", "epic", "legendary" })[i]
			cumulative = cumulative + tiers[tierName].chance
			if roll <= cumulative then
				tier = i
				break
			end
		end
		if not tier then
			return 0
		end
	end

	-- Apply minimum tier
	if minTier and minTier > 0 and tier < minTier then
		tier = minTier
	end

	local tierNames = { "rare", "epic", "legendary" }
	local tierName = tierNames[tier]
	local tierDef = rarityConfig.tiers[tierName]

	-- Select stats (up to 2)
	local rolledStats = {}
	local statCount = 1
	if math.random(1, 100) <= tierDef.secondStatChance then
		statCount = 2
	end

	local availCopy = {}
	for i = 1, #available do
		availCopy[i] = available[i]
	end

	for _ = 1, statCount do
		if #availCopy == 0 then break end
		local idx = math.random(1, #availCopy)
		local attrKey = availCopy[idx]
		table.remove(availCopy, idx)

		local attrDef = rarityConfig.attributes[attrKey]
		if not attrDef then break end

		local range = attrDef[tierName]
		if not range then break end

		local value = rollValue(range[1], range[2])
		rolledStats[attrKey] = value

		-- Store via custom attribute
		item:setRarityStat(attrDef.statKey, value)

		-- Write spell balancing values to item (read by C++ at combat time)
		local balanceSpell = rarityBalancing.spells[attrKey]
		if balanceSpell then
			item:setRarityStat(attrDef.statKey .. "DmgMin", balanceSpell.dmgMin)
			item:setRarityStat(attrDef.statKey .. "DmgMax", balanceSpell.dmgMax)
			item:setRarityStat("spellScaleLevel", rarityBalancing.spellScale.level)
			item:setRarityStat("spellScaleMagic", rarityBalancing.spellScale.magic)
			item:setRarityStat("spellScaleDivisor", rarityBalancing.spellScale.divisor)
		end

		-- Apply base stat modifications directly to item
		if attrDef.statKey == "attack" then
			local current = itemType:getAttack()
			item:setAttribute(ITEM_ATTRIBUTE_ATTACK, current + value)
		elseif attrDef.statKey == "defense" then
			local current = itemType:getDefense()
			item:setAttribute(ITEM_ATTRIBUTE_DEFENSE, current + value)
		elseif attrDef.statKey == "armor" then
			local current = itemType:getArmor()
			item:setAttribute(ITEM_ATTRIBUTE_ARMOR, current + value)
		end
	end

	if next(rolledStats) == nil then
		return 0
	end

	-- Set rarity tier
	item:setRarityTier(tier)

	-- Write on-kill balancing values to item (read by C++ at kill time)
	if item:getRarityStat("onKillBuffDuration") == 0 then
		item:setRarityStat("onKillBuffDuration", rarityBalancing.onKill.buffDuration)
		item:setRarityStat("onKillBuffCritChance", rarityBalancing.onKill.critChance)
		item:setRarityStat("onKillBuffCritAmount", rarityBalancing.onKill.critAmount)
		item:setRarityStat("onKillBuffMaxHpPercent", rarityBalancing.onKill.maxHpPercent)
		item:setRarityStat("onKillBuffMaxMpPercent", rarityBalancing.onKill.maxMpPercent)
	end

	-- Build description
	local descParts = {}
	local articleName = tierDef.article
	for attrKey, value in pairs(rolledStats) do
		local attrDef = rarityConfig.attributes[attrKey]
		if attrDef then
			local suffix = ""
			if attrDef.isPercent then
				suffix = "%"
			end
			local displayValue = value
			if math.floor(value) ~= value then
				displayValue = string.format("%.1f", value)
			end
			descParts[#descParts + 1] = "[" .. attrDef.name .. ": +" .. displayValue .. suffix .. "]"
		end
	end

	local desc = table.concat(descParts, "\n")
	local existingDesc = item:getAttribute(ITEM_ATTRIBUTE_DESCRIPTION)
	if existingDesc and existingDesc ~= "" then
		desc = existingDesc .. "\n" .. desc
	end
	item:setAttribute(ITEM_ATTRIBUTE_DESCRIPTION, desc)
	item:setAttribute(ITEM_ATTRIBUTE_ARTICLE, articleName)

	return tier
end

-- =============================================================================
-- Roll rarity on all items in a container (corpse/bag)
-- @return number of items that received rarity
-- =============================================================================
function rollRarityContainer(container, forcedTier, minTier)
	if not container then return 0 end

	local count = 0
	local items = container:getItems()
	if not items then return 0 end

	for _, item in ipairs(items) do
		if item:isContainer() then
			count = count + rollRarityContainer(item, forcedTier, minTier)
		else
			local tier = rollRarity(item, forcedTier, minTier)
			if tier > 0 then
				count = count + 1
			end
		end
	end

	return count
end

-- =============================================================================
-- Roll rarity on a corpse for all monsters (no monsterTiers check)
-- Called from default_onDropLoot when RARITY_SYSTEM_ENABLED is true
-- =============================================================================
function rollRarityOnCorpse(corpse, chance, minTier)
	if not RARITY_SYSTEM_ENABLED or not corpse then
		return 0
	end

	if math.random(1, 100) > chance then
		return 0
	end

	local count = rollRarityContainer(corpse, nil, minTier)

	if count > 0 and rarityConfig.popupText and rarityConfig.animations then
		local spectators = Game.getSpectators(corpse:getPosition(), false, true, 7, 7, 5, 5)
		for _, spectator in ipairs(spectators) do
			spectator:say("Rare loot!", TALKTYPE_MONSTER_SAY, false, spectator, corpse:getPosition())
		end
		corpse:getPosition():sendMagicEffect(rarityConfig.popupEffect)
	end

	return count
end

-- =============================================================================
-- Apply/remove item rarity conditions when equipping/unequipping
-- Called from onInventoryUpdate event
-- =============================================================================

-- Remove all rarity conditions for a specific slot only.
-- Does NOT affect conditions on other slots.
local function removeSlotConditions(player, slot)
	-- Stat bonuses: subId = base + slot where base = 100, 200, ..., 1100
	for base = 100, 1100, 100 do
		player:removeCondition(CONDITION_ATTRIBUTES, CONDITIONID_DEFAULT, base + slot)
	end
	-- Experience: 1500 + slot
	player:removeCondition(CONDITION_ATTRIBUTES, CONDITIONID_DEFAULT, 1500 + slot)
	-- MeleeSkills: 1200 + slot*10 + skill (SWORD=1, AXE=2, CLUB=3, FIST=4)
	for skill = 1, 4 do
		player:removeCondition(CONDITION_ATTRIBUTES, CONDITIONID_DEFAULT, 1200 + slot * 10 + skill)
	end
end

function itemAttributes(player, item, slot, equip)
	if not item or item:getRarityTier() == 0 then
		return
	end

	removeSlotConditions(player, slot)

	if not equip then
		return
	end

	-- Apply onEquip for each stored rarity stat
	for attrKey, attrDef in pairs(rarityConfig.attributes) do
		local value = item:getRarityStat(attrDef.statKey)
		if value > 0 and attrDef.onEquip then
			attrDef.onEquip(player, slot, value, true)
		end
	end

	-- Apply experience modifier condition
	local expValue = item:getRarityStat("experience")
	if expValue > 0 then
		local condition = Condition(CONDITION_ATTRIBUTES)
		condition:setParameter(CONDITION_PARAM_SUBID, 1500 + slot)
		condition:setParameter(CONDITION_PARAM_EXPERIENCE, expValue)
		condition:setParameter(CONDITION_PARAM_TICKS, -1)
		player:addCondition(condition)
	end
end

-- =============================================================================
-- Determine the monster tier for loot rarity
-- @param monsterName lowercase monster name
-- @return tierName ("boss", "miniboss") or nil for default
-- =============================================================================
function getMonsterLootTier(monsterName)
	if not monsterName then return nil end
	local name = monsterName:lower()
	for tierName, tierDef in pairs(rarityConfig.monsterTiers) do
		if tierDef.monsters then
			for _, mName in ipairs(tierDef.monsters) do
				if mName:lower() == name then
					return tierName
				end
			end
		end
	end
	return nil
end

-- =============================================================================
-- Process rarity roll on monster corpse
-- =============================================================================
function processMonsterLoot(monster, corpse)
	if not RARITY_SYSTEM_ENABLED then return end

	local monsterName = monster:getName():lower()
	local tierName = getMonsterLootTier(monsterName)

	local chance = rarityConfig.defaultMonsterChance
	local minTier = rarityConfig.defaultMinTier

	if tierName and rarityConfig.monsterTiers[tierName] then
		local mt = rarityConfig.monsterTiers[tierName]
		chance = mt.chance
		minTier = mt.minTier
	end

	if math.random(1, 100) > chance then
		return
	end

	local count = rollRarityContainer(corpse, nil, minTier)

	if count > 0 and rarityConfig.popupText and rarityConfig.animations then
		local spectators = Game.getSpectators(corpse:getPosition(), false, true, 7, 7, 5, 5)
		for _, spectator in ipairs(spectators) do
			spectator:say("Rare loot!", TALKTYPE_MONSTER_SAY, false, spectator, corpse:getPosition())
		end
		corpse:getPosition():sendMagicEffect(rarityConfig.popupEffect)
	end
end
