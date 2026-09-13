local function activate(player, item)
	local success, message = Game.activateEchoRaid(player, item)
	if message and message ~= "" then
		player:sendTextMessage(success and MESSAGE_EVENT_ADVANCE or MESSAGE_STATUS_SMALL, message)
	end
	return true
end

local action = Action()

function action.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	return activate(player, item)
end

action:id(EchoConfig.portal.itemId)
action:register()

local stepIn = MoveEvent()

function stepIn.onStepIn(creature, item, position, fromPosition)
	local player = creature and creature:getPlayer()
	if player then
		activate(player, item)
	end
	return true
end

stepIn:type("stepin")
stepIn:id(EchoConfig.portal.itemId)
stepIn:register()
