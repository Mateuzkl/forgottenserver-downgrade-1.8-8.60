local talkaction = TalkAction("/summon")

function talkaction.onSay(player, words, param)
	local position = player:getPosition()
	local monster = Game.createMonster(param, position, false, false, CONST_ME_TELEPORT, player:getInstanceId())
	if monster then
		player:addSummon(monster)
		position:sendMagicEffect(CONST_ME_MAGIC_RED)
	else
		player:sendCancelMessage("There is not enough room.")
		position:sendMagicEffect(CONST_ME_POFF)
	end
	return false
end

talkaction:separator(" ")
talkaction:access(true)
talkaction:register()
