local talkaction = TalkAction("/i")

local invalidIds = {
	1, 2, 3, 4, 5, 6, 7, 10, 11, 13, 14, 15, 19, 21, 26, 27, 28, 35, 43
}

local RARITY_NAMES = { rare = true, epic = true, legendary = true }

-- =============================================================================
-- Safely create an item in the player's inventory.
-- If the player has no container, adds a backpack first.
-- If still no space, reports error and returns nil.
-- =============================================================================
local function createItemSafe(player, itemType, count, subType)
	local function tryCreate(canDrop)
		if itemType:isStackable() then
			return player:addItem(itemType:getId(), count, canDrop, subType)
		end
		return player:addItem(itemType:getId(), count, canDrop, subType)
	end

	local result = tryCreate(true)
	if result then return result end

	-- Failed — try adding a backpack to make space
	local bp = player:addItem(1988, 1, false)  -- backpack, no dropOnMap
	if not bp then
		player:sendCancelMessage("Cannot create item — no capacity or container space.")
		return nil
	end

	result = tryCreate(false)
	if not result then
		player:sendCancelMessage("Cannot create item — no space even after adding backpack.")
		bp:remove()
		return nil
	end
	return result
end

-- =============================================================================
-- /i [rarity] <itemname|id> [, tier N] [, count]
--
-- Examples:
--   /i legendary fire axe           → legendary fire axe
--   /i rare fire axe, 3             → 3 rare fire axes
--   /i fire axe, tier 5             → fire axe forge tier 5
--   /i epic fire axe, tier 3, 2     → 2 epic fire axes, forge tier 3
--   /i fire axe, 2                  → 2 fire axes (backward compatible)
--   /i 2160, legendary              → crystal coin with legendary rarity
-- =============================================================================
function talkaction.onSay(player, words, param)
	local split = param:splitTrimmed(",")

	-- 1. Detect rarity tier in first word
	local rarityTier = nil
	local firstWord = split[1]:match("^(%S+)")
	if RARITY_SYSTEM_ENABLED and firstWord then
		local tier = firstWord:lower()
		if RARITY_NAMES[tier] then
			rarityTier = tier
			split[1] = split[1]:sub(#firstWord + 2)
			if split[1] == "" and #split >= 2 then
				table.remove(split, 1)
			end
		end
	end

	-- 2. Identify item
	local itemName = split[1]:trim()
	if itemName == "" then
		player:sendCancelMessage("Usage: /i [rare|epic|legendary] <itemname|id> [, tier N] [, count]")
		return false
	end

	local itemType = ItemType(itemName)
	if not itemType or itemType:getId() == 0 then
		itemType = ItemType(tonumber(itemName))
		if not itemType or itemType:getId() == 0 then
			player:sendCancelMessage("There is no item with that id or name.")
			return false
		end
	end

	if table.contains(invalidIds, itemType:getId()) then
		return false
	end

	-- 3. Parse forge tier and count from remaining params
	local forgeTier = 0
	local count = nil
	local subType = 1

	for i = 2, #split do
		local s = split[i]:lower():trim()
		local t = s:match("tier%s*(%d+)") or s:match("^t(%d+)")
		if t and FORGE_SYSTEM_ENABLED then
			forgeTier = tonumber(t)
		else
			local n = tonumber(s)
			if n then
				count = n
			end
		end
	end

	-- 4. Determine default count/subType
	if count then
		if itemType:isFluidContainer() then
			count = math.max(0, math.min(count, 99))
		elseif itemType:isKey() then
			subType = count
			count = 1
		else
			count = math.min(100, math.max(1, count))
		end
	else
		if not itemType:isFluidContainer() then
			count = math.max(1, itemType:getCharges())
		else
			count = 0
		end
	end

	-- 5. Create item
	local result = createItemSafe(player, itemType, count, subType)
	if not result then
		return false
	end

	-- 6. Apply rarity if specified
	if rarityTier then
		local function applyRarity(item)
			rollRarity(item, rarityTier, 0)
		end
		if type(result) == "table" then
			for _, item in ipairs(result) do
				applyRarity(item)
			end
		else
			applyRarity(result)
			if itemType:isKey() then
				result:setAttribute(ITEM_ATTRIBUTE_ACTIONID, subType)
			end
		end
	end

	-- 7. Apply forge tier if specified
	if forgeTier > 0 then
		local function applyForge(item)
			item:setAttribute(ITEM_ATTRIBUTE_TIER, forgeTier)
		end
		if type(result) == "table" then
			for _, item in ipairs(result) do
				applyForge(item)
			end
		else
			applyForge(result)
		end
	end

	-- 8. Decay non-stackable items
	if not itemType:isStackable() then
		if type(result) == "table" then
			for _, item in ipairs(result) do
				item:decay()
			end
		else
			if not rarityTier and itemType:isKey() then
				result:setAttribute(ITEM_ATTRIBUTE_ACTIONID, subType)
			end
			result:decay()
		end
	end

	player:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)

	local feedback = "Created " .. (rarityTier or "") .. " " .. (itemType:getName() or itemName)
	if forgeTier > 0 then
		feedback = feedback .. " (Tier " .. forgeTier .. ")"
	end
	if count > 1 then
		feedback = feedback .. " x" .. count
	end
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, feedback)

	return false
end

talkaction:separator(" ")
talkaction:accountType(6)
talkaction:access(true)
talkaction:register()
