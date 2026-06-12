local action = Action()

local drunk = Condition(CONDITION_DRUNK)
drunk:setParameter(CONDITION_PARAM_TICKS, 60000)

local poison = Condition(CONDITION_POISON)
poison:setParameter(CONDITION_PARAM_DELAYED, true)
poison:setParameter(CONDITION_PARAM_MINVALUE, -50)
poison:setParameter(CONDITION_PARAM_MAXVALUE, -120)
poison:setParameter(CONDITION_PARAM_STARTVALUE, -5)
poison:setParameter(CONDITION_PARAM_TICKINTERVAL, 4000)
poison:setParameter(CONDITION_PARAM_FORCEUPDATE, true)

local fluidMessage = {
	[FLUID_BEER] = "Aah...",
	[FLUID_SLIME] = "Urgh!",
	[FLUID_LEMONADE] = "Mmmh.",
	[FLUID_MANA] = "Aaaah...",
	[FLUID_LIFE] = "Aaaah...",
	[FLUID_OIL] = "Urgh!",
	[FLUID_URINE] = "Urgh!",
	[FLUID_WINE] = "Aah...",
	[FLUID_MUD] = "Urgh!",
	[FLUID_RUM] = "Aah...",
	[FLUID_MEAD] = "Aaaah..."
}

local distillery = {[5512] = 5468, [5513] = 5469}

function action.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local targetItemType = ItemType(target.itemid)
	if targetItemType and targetItemType:isFluidContainer() then
		if target.type == FLUID_NONE and item.type ~= FLUID_NONE then
			target:transform(target:getId(), item.type)
			item:transform(item:getId(), FLUID_NONE)
			return true
		elseif target.type ~= FLUID_NONE and item.type == FLUID_NONE then
			item:transform(item:getId(), target.type)
			target:transform(target:getId(), FLUID_NONE)
			return true
		end
	end

	if target.itemid == 1 then
		if item.type == FLUID_NONE then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "It is empty.")
		elseif target.uid == player.uid then
			if table.contains({FLUID_BEER, FLUID_WINE, FLUID_MEAD}, item.type) then
				player:addCondition(drunk)
			elseif item.type == FLUID_SLIME then
				player:addCondition(poison)
			elseif item.type == FLUID_MANA then
				player:addMana(math.random(50, 150))
				fromPosition:sendMagicEffect(CONST_ME_MAGIC_BLUE, player:getInstanceId())
			elseif item.type == FLUID_LIFE then
				player:addHealth(60)
				fromPosition:sendMagicEffect(CONST_ME_MAGIC_BLUE, player:getInstanceId())
			end
			player:say(fluidMessage[item.type] or "Gulp.", TALKTYPE_MONSTER_SAY)
			item:transform(item:getId(), FLUID_NONE)
		else
			Game.createItem(2886, item.type, toPosition):decay()
			item:transform(item:getId(), FLUID_NONE)
		end
	else
		local fluidSource = targetItemType and targetItemType:getFluidSource() or
			                    FLUID_NONE
		if fluidSource ~= FLUID_NONE then
			item:transform(item:getId(), fluidSource)
		elseif table.contains(distillery, target.itemid) then
			local tmp = distillery[target.itemid]
			if tmp then
				item:transform(item:getId(), FLUID_NONE)
			else
				player:sendCancelMessage(
					"You have to process the bunch into the distillery to get rum.")
			end
		elseif item.type == FLUID_NONE then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "It is empty.")
		else
			if toPosition.x == CONTAINER_POSITION then
				toPosition = player:getPosition()
			end
			Game.createItem(2886, item.type, toPosition):decay()
			item:transform(item:getId(), FLUID_NONE)
		end
	end
	return true
end

action:id(2524, 2873, 2874, 2875, 2876, 2877, 2879, 2880, 2881, 2882, 2885, 2893, 2901, 2902, 2903, 2904, 3477, 3478, 3479, 3480, 5552)
action:register()
