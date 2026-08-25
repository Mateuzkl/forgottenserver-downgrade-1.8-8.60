local lastQuestUpdate = {}

local event = Event()

event.onUpdateStorage = function(creature, key, value, oldValue)
	local player = creature:getPlayer()
	if not player then
		player = Player(creature:getId())
	end

	if not player then
		return
	end

	local playerId = player:getId()
	local now = os.mtime()
	if not lastQuestUpdate[playerId] then lastQuestUpdate[playerId] = now end

	local isQuestUpdate, questName, isCompleted = Game.isQuestStorage(key, value, oldValue, player)
	if lastQuestUpdate[playerId] - now <= 0 and isQuestUpdate then
		lastQuestUpdate[playerId] = os.mtime() + 100
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE,
		                       "Your questlog has been updated.\nView your questlog for more information.")
		if questName and player.isUsingAstraClient and player:isUsingAstraClient() then
			local message<close> = NetworkMessage(player)
			message:addByte(0x75)
			message:addByte(8)
			message:addString(questName)
			message:addByte(isCompleted and 1 or 0)
			message:sendToPlayer(player)
		end
	end
end

event:register()
