local proficiency = TalkAction("/proficiency")

local function reportStatus(player)
	local System = EquipmentProficiencySystem
	local itemIds = System.getEquippedItemIds(player)
	if #itemIds == 0 then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "No equipped item takes part in the proficiency system.")
		return
	end

	for _, itemId in ipairs(itemIds) do
		local itemType = ItemType(itemId)
		local name = itemType and itemType:getName() or ("item " .. itemId)

		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
			"%s (%d): level %d, %d exp.",
			name, itemId,
			System.getProficiencyLevel(player, itemId),
			System.getExperience(player, itemId)))

		local states = System.getSpellStates(player, itemId)
		if #states == 0 then
			player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "  teaches nothing to this vocation.")
		else
			for _, state in ipairs(states) do
				player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
					"  %s - %s (mastery at level %d).",
					state.spell,
					state.learned and "mastered" or "borrowed",
					state.masterLevel))
			end
		end
	end
end

function proficiency.onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	if not EquipmentProficiencySystem then
		player:sendCancelMessage("The equipment proficiency system is disabled.")
		return false
	end

	-- No argument: report what the player is currently wearing.
	if param == "" then
		reportStatus(player)
		return false
	end

	local split = param:splitTrimmed(",")
	local experience = tonumber(split[1])
	local itemId = tonumber(split[2])
	if not experience or experience <= 0 then
		player:sendCancelMessage("Usage: /proficiency [experience[, itemId]]")
		return false
	end

	if not EquipmentProficiencySystem.addExperience(player, nil, experience, itemId, false) then
		player:sendCancelMessage("Equip an item with proficiency, or provide a valid item id.")
		return false
	end

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Equipment proficiency experience added.")
	return false
end

proficiency:separator(" ")
proficiency:register()
