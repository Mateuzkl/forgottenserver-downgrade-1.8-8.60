local talkaction = TalkAction("/testsummon")

function talkaction.onSay(player, words, param)
	if param == "" then
		player:sendCancelMessage("Usage: /testsummon name, amount")
		return false
	end

	local split = param:split(",")
	local monsterName = split[1]:trim()
	local amount = math.max(1, math.min(tonumber(split[2]) or 1, 500))

	local position = player:getPosition()
	local placed = 0

	for i = 1, amount do
		local monster = Game.createMonster(monsterName, position, true, false, CONST_ME_TELEPORT, player:getInstanceId())
		if monster then
			monster:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
			placed = placed + 1
		end
	end

	if placed > 0 then
		position:sendMagicEffect(CONST_ME_MAGIC_RED)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE,
			string.format("Summoned %d/%d %s.", placed, amount, monsterName))
	else
		player:sendCancelMessage("Could not summon " .. monsterName .. ". Check the name.")
		position:sendMagicEffect(CONST_ME_POFF)
	end

	return false
end

talkaction:separator(" ")
talkaction:access(true)
talkaction:register()
