local log = PacketHandler(0xF0)

function log.onReceive(player, msg)
	if not NetworkGuard.cooldown(player, "quest-log", 500) then
		return
	end
	player:sendQuestLog()
end

log:register()

local line = PacketHandler(0xF1)

function line.onReceive(player, msg)
	if not NetworkGuard.cooldown(player, "quest-line", 250) then
		return
	end

	local questId = NetworkGuard.readU16(msg)
	if not questId then
		return
	end

	local quest = Game.getQuestById(questId)
	if quest then player:sendQuestLine(quest) end
end

line:register()
