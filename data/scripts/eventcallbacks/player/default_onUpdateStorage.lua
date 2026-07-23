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

	if lastQuestUpdate[playerId] - now <= 0 and
		Game.isQuestStorage(key, value, oldValue) then
		lastQuestUpdate[playerId] = os.mtime() + 100
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE,
		                       "Your questlog has been updated.\nView your questlog for more information.")
	end
end

event:register()
