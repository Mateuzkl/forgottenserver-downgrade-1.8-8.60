local hirelingLamp = Action()

local function hirelingSystemEnabled()
	return configManager and configKeys and configKeys.HIRELING_SYSTEM_ENABLED and
		configManager.getBoolean(configKeys.HIRELING_SYSTEM_ENABLED)
end

function hirelingLamp.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if not hirelingSystemEnabled() then
		player:sendCancelMessage("The hireling system is disabled.")
		return true
	end

	local spawnPosition = player:getPosition()
	local tile = spawnPosition:getTile()
	local house = tile and tile:getHouse()
	local hirelingId = item:getCustomAttribute("Hireling")

	if not hirelingId then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_FAILURE, "This hireling lamp is not linked to a hireling.")
		return true
	end

	if not house then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_FAILURE, "You may use this only inside a house.")
		return true
	end

	if house:getDoorIdByPosition(spawnPosition) then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_FAILURE, "You cannot spawn a hireling on the door.")
		return true
	end

	if getHirelingByPosition(spawnPosition) then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_FAILURE, "You cannot spawn another hireling here.")
		return true
	end

	if house:getOwnerGuid() ~= player:getGuid() then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_FAILURE, "You cannot spawn a hireling in another person's house.")
		return true
	end

	local hireling = getHirelingById(hirelingId)
	if not hireling or hireling:getOwnerId() ~= player:getGuid() then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "There was an error creating the hireling and the lamp has been deleted. Please contact the server administrator.")
		if logger and logger.warn then
			logger.warn("[HirelingLamp] Invalid hireling lamp used by " .. player:getName() .. " with id " .. tostring(hirelingId))
		end
		item:remove(1)
		return true
	end

	hireling:setPosition(spawnPosition)
	if not hireling:spawn() then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_FAILURE, "The hireling could not be summoned.")
		return true
	end

	item:remove(1)
	return true
end

hirelingLamp:id(HIRELING_LAMP)
hirelingLamp:register()
