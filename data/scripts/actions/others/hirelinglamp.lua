local action = Action()

function action.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local spawnPosition = player:getPosition()
	local hireling_id = item:getSpecialAttribute(HIRELING_ATTRIBUTE)
	local tile = spawnPosition:getTile()
	if not tile then return false end
	local house = tile:getHouse()

	if not house then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You may use this only inside a house.")
		return false
	elseif getHirelingByPosition(spawnPosition) then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You cannot spawn another hireling here.")
		return false
	elseif house:getOwnerGuid() ~= player:getGuid() then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You cannot spawn a hireling on another's person house.")
		return false
	end

	local hireling = getHirelingById(hireling_id)
	if not hireling then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_INFO_DESCR, "This hireling no longer exists.")
		return false
	end

	hireling:setPosition(spawnPosition)
	item:remove(1)
	hireling:spawn()
	spawnPosition:sendMagicEffect(CONST_ME_TELEPORT)
	return true
end

action:id(HIRELING_LAMP_ID)
action:register()
