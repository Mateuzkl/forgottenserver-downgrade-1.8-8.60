local action = Action()

local berserk = Condition(CONDITION_ATTRIBUTES)
berserk:setParameter(CONDITION_PARAM_TICKS, 10 * 60 * 1000)
berserk:setParameter(CONDITION_PARAM_SKILL_MELEE, 5)
berserk:setParameter(CONDITION_PARAM_SKILL_SHIELD, -10)
berserk:setParameter(CONDITION_PARAM_BUFF_SPELL, true)

local mastermind = Condition(CONDITION_ATTRIBUTES)
mastermind:setParameter(CONDITION_PARAM_TICKS, 10 * 60 * 1000)
mastermind:setParameter(CONDITION_PARAM_STAT_MAGICPOINTS, 3)
mastermind:setParameter(CONDITION_PARAM_BUFF_SPELL, true)

local bullseye = Condition(CONDITION_ATTRIBUTES)
bullseye:setParameter(CONDITION_PARAM_TICKS, 10 * 60 * 1000)
bullseye:setParameter(CONDITION_PARAM_SKILL_DISTANCE, 5)
bullseye:setParameter(CONDITION_PARAM_SKILL_SHIELD, -10)
bullseye:setParameter(CONDITION_PARAM_BUFF_SPELL, true)

local potions = {
	[6558] = { -- concentrated demonic blood
		transform = {236, 237},
		effect = CONST_ME_DRAWBLOOD
	},
	[7439] = { -- berserk potion
		condition = berserk,
		vocations = {4, 8},
		effect = CONST_ME_MAGIC_RED,
		description = "Only knights may drink this potion.",
		text = "You feel stronger."
	},
	[7440] = { -- mastermind potion
		condition = mastermind,
		vocations = {1, 2, 5, 6},
		effect = CONST_ME_MAGIC_BLUE,
		description = "Only sorcerers and druids may drink this potion.",
		text = "You feel smarter."
	},
	[7443] = { -- bullseye potion
		condition = bullseye,
		vocations = {3, 7},
		effect = CONST_ME_MAGIC_GREEN,
		description = "Only paladins may drink this potion.",
		text = "You feel more accurate."
	},
	[236] = { -- strong health potion
		health = {250, 350},
		vocations = {3, 4, 7, 8, 9, 10},
		level = 50,
		flask = 283,
		description = "Only knights, monks and paladins of level 50 or above may drink this fluid."
	},
	[237] = { -- strong mana potion
		mana = {115, 185},
		vocations = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10},
		level = 50,
		flask = 283,
		description = "Only sorcerers, druids, knights, monks and paladins of level 50 or above may drink this fluid."
	},
	[238] = { -- great mana potion
		mana = {150, 250},
		vocations = {1, 2, 3, 5, 6, 7, 9, 10},
		level = 80,
		flask = 284,
		description = "Only druids, sorcerers, monks and paladins of level 80 or above may drink this fluid."
	},
	[23373] = { -- ultimate mana potion
		mana = {450, 550},
		vocations = {1, 2, 5, 6},
		level = 130,
		flask = 284,
		description = "Only druids and sorcerers of level 130 or above may drink this fluid."
	},
	[239] = { -- great health potion
		health = {430, 575},
		vocations = {4, 8, 9, 10},
		level = 80,
		flask = 284,
		description = "Only knights and monks of level 80 or above may drink this fluid."
	},
	[266] = { -- health potion
		health = {125, 175},
		flask = 285
	},
	[268] = { -- mana potion
		mana = {75, 125},
		flask = 285
	},
	[7642] = { -- great spirit potion
		health = {250, 350},
		mana = {100, 200},
		vocations = {3, 7, 9, 10},
		level = 80,
		flask = 284,
		description = "Only paladins and monks of level 80 or above may drink this fluid."
	},
	[23374] = { -- ultimate spirit potion
		health = {450, 550},
		mana = {200, 250},
		vocations = {3, 7, 9, 10},
		level = 130,
		flask = 284,
		description = "Only paladins and monks of level 130 or above may drink this fluid."
	},
	[7643] = { -- ultimate health potion
		health = {650, 850},
		vocations = {4, 8, 9, 10},
		level = 130,
		flask = 284,
		description = "Only knights and monks of level 130 or above may drink this fluid."
	},
	[23375] = { -- supreme health potion
		health = {900, 1050},
		vocations = {4, 8, 9, 10},
		level = 200,
		flask = 284,
		description = "Only knights and monks of level 200 or above may drink this fluid."
	},
	[7644] = { -- antidote potion
		antidote = true,
		flask = 285
	},
	[7876] = { -- small health potion
		health = {60, 90},
		flask = 285
	}
}

function action.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if type(target) == "userdata" and not target:isPlayer() then return false end

	local potion = potions[item:getId()]
	if potion.level and player:getLevel() < potion.level or potion.vocations and
		not table.contains(potion.vocations, player:getVocation():getId()) then
		player:say(potion.description, TALKTYPE_MONSTER_SAY)
		return true
	end

	if potion.condition then
		player:addCondition(potion.condition)
		player:say(potion.text, TALKTYPE_MONSTER_SAY)
		player:getPosition():sendMagicEffect(potion.effect)
	elseif potion.transform then
		local reward = potion.transform[math.random(#potion.transform)]
		if fromPosition.x == CONTAINER_POSITION then
			local targetContainer = Container(item:getParent().uid)
			targetContainer:addItem(reward, 1)
		else
			Game.createItem(reward, 1, fromPosition)
		end
		item:getPosition():sendMagicEffect(potion.effect)
		item:remove(1)
		return true
	else
		if potion.health then
			doTargetCombat(0, target, COMBAT_HEALING, potion.health[1], potion.health[2])
		end

		if potion.mana then
			doTargetCombat(0, target, COMBAT_MANADRAIN, potion.mana[1], potion.mana[2])
		end

		if potion.antidote then target:removeCondition(CONDITION_POISON) end

		player:addAchievementProgress("Potion Addict", 100000)
		target:say("Aaaah...", TALKTYPE_MONSTER_SAY)
		target:getPosition():sendMagicEffect(CONST_ME_MAGIC_BLUE)
	end

	if not configManager.getBoolean(configKeys.REMOVE_POTION_CHARGES) then
		return true
	end

	item:remove(1)
	return true
end

action:id(6558, 7439, 7440, 7443, 236, 237, 238, 239, 266, 268, 7642, 7643, 7644, 7876, 23373, 23374, 23375)
action:register()
