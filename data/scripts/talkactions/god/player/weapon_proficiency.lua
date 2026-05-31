local proficiency = TalkAction("/proficiency")

function proficiency.onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	local split = param:splitTrimmed(",")
	local experience = tonumber(split[1])
	local itemId = tonumber(split[2])
	if not experience or experience <= 0 then
		player:sendCancelMessage("Usage: /proficiency experience[, itemId]")
		return false
	end

	if not WeaponProficiencySystem or not WeaponProficiencySystem.addExperience(player, nil, experience, itemId, false) then
		player:sendCancelMessage("Equip a weapon or provide a valid weapon item id.")
		return false
	end

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Weapon proficiency experience added.")
	return false
end

proficiency:separator(" ")
proficiency:register()
