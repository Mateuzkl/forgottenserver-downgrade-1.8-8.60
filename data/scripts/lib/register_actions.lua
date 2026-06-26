local wildGrowth = {2130, 10182} -- wild growth destroyable by machete

local jungleGrass = {
	3696, 3702, 17153
}

local groundIds = {354, 355} -- pick usable ground
local sandIds = {231} -- desert sand

local holeId = {
	294, 369, 370, 385, 394, 411, 412, 413, 432, 433, 435, 8709,
	594, 595, 615, 609, 610, 615, 1156, 482, 483, 868, 874, 4824,
	7768, 433, 432, 413, 7767, 411, 370, 369, 7737, 7755, 7768, 7767,
	7515, 7516, 7517, 7518, 7519, 7520, 7521, 7522, 7762, 8144, 8690, 8709,
	12203, 12961, 17239, 19220, 23364, 43372
}

local Itemsgrinder = {
	[675] = {item_id = 30004, effect = CONST_ME_BLUE_FIREWORKS}, -- Sapphire dust
	[16122] = {item_id = 21507, effect = CONST_ME_GREENSMOKE} -- Pinch of crystal dust
}

local shovelHoles = {
	[593] = 594,
	[606] = 607,
	[608] = 609,
	[867] = 868,
	[21341] = 21342
}

local fruits = {
	3584, 3585, 3586, 3587, 3588, 3589, 3590,
	3591, 3592, 3593, 3595, 3596, 5096, 8011, 
	8012, 8013
} -- fruits to make decorated cake with knife

function destroyItem(player, target, toPosition)
	if type(target) ~= "userdata" or not target:isItem() then return false end

	if target:hasAttribute(ITEM_ATTRIBUTE_UNIQUEID) or
		target:hasAttribute(ITEM_ATTRIBUTE_ACTIONID) then return false end

	if toPosition.x == CONTAINER_POSITION then
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		return true
	end

	local destroyId = ItemType(target.itemid):getDestroyId()
	if destroyId == 0 then return false end

	if math.random(7) == 1 then
		local item = Game.createItem(destroyId, 1, toPosition)
		if item then item:decay() end

		-- Move items outside the container
		if target:isContainer() then
			for i = target:getSize() - 1, 0, -1 do
				local containerItem = target:getItem(i)
				if containerItem then containerItem:moveTo(toPosition) end
			end
		end

		target:remove(1)
	end

	toPosition:sendMagicEffect(CONST_ME_POFF)
	return true
end

function onUseMachete(player, item, fromPosition, target, toPosition, isHotkey)
	local targetId = target.itemid
	if not targetId then return true end

	if table.contains(wildGrowth, targetId) then
		toPosition:sendMagicEffect(CONST_ME_POFF)
		target:remove()
		return true
	end

	if table.contains(jungleGrass, target.itemid) then
		target:transform(target.itemid == 17153 and 17151 or target.itemid - 1)
		target:decay()
		player:addAchievementProgress("Nothing Can Stop Me", 100)
		return true
	end

	return destroyItem(player, target, toPosition)
end

function onUsePick(player, item, fromPosition, target, toPosition, isHotkey)
	if target.itemid == 10310 then -- shiny stone refining
		local chance = math.random(1, 100)
		if chance == 1 then
			player:addItem(ITEM_CRYSTAL_COIN) -- 1% chance of getting crystal coin
		elseif chance <= 6 then
			player:addItem(ITEM_GOLD_COIN) -- 5% chance of getting gold coin
		elseif chance <= 51 then
			player:addItem(ITEM_PLATINUM_COIN) -- 45% chance of getting platinum coin
		else
			player:addItem(3028) -- 49% chance of getting small diamond
		end
		player:addAchievementProgress("Petrologist", 100)
		target:getPosition():sendMagicEffect(CONST_ME_BLOCKHIT)
		target:remove(1)
		return true
	end

	local tile = Tile(toPosition)
	if not tile then return false end

	local ground = tile:getGround()
	if not ground then return false end

	if table.contains(groundIds, ground.itemid) and ground.actionid ==
		actionIds.pickHole then
		ground:transform(392)
		ground:decay()
		toPosition:sendMagicEffect(CONST_ME_POFF)

		toPosition.z = toPosition.z + 1
		tile:relocateTo(toPosition)
		return true
	end

	-- Ice fishing hole
	if ground.itemid == 7200 then
		ground:transform(7236)
		ground:decay()
		toPosition:sendMagicEffect(CONST_ME_HITAREA)
		return true
	end

	return false
end

function onUseRope(player, item, fromPosition, target, toPosition, isHotkey)
	if toPosition.x == CONTAINER_POSITION then
		return false
	end

	local tile = Tile(toPosition)
	if not tile then return false end

	local ground = tile:getGround()

	if tile:isRopeSpot() or tile:getItemById(14435) then
		tile = Tile(toPosition:moveUpstairs())
		if not tile then return false end

		if tile:hasFlag(TILESTATE_PROTECTIONZONE) and player:isPzLocked() then
			player:sendCancelMessage(RETURNVALUE_PLAYERISPZLOCKED)
			return true
		end

		player:teleportTo(toPosition, false, CONST_ME_NONE)
		return true
	end

	if table.contains(holeId, target.itemid) then
		toPosition.z = toPosition.z + 1
		tile = Tile(toPosition)
		if not tile then return false end

		local thing = tile:getTopVisibleThing()
		if not thing then return true end

		if thing:isCreature() and (thing:isPlayer() or thing:isMonster()) then
			if Tile(toPosition:moveUpstairs()):queryAdd(thing) ~= RETURNVALUE_NOERROR then
				return false
			end

			return thing:teleportTo(toPosition, false, CONST_ME_NONE)
		elseif thing:isItem() and thing:getType():isMovable() then
			return thing:moveTo(toPosition:moveUpstairs())
		end

		return true
	end

	return false
end

function onUseShovel(player, item, fromPosition, target, toPosition, isHotkey)
	local tile = Tile(toPosition)
	if not tile then return false end

	local ground = tile:getGround()
	if not ground then return false end

	local groundId = ground:getId()
	local openHoleId = shovelHoles[groundId]
	if openHoleId then
		ground:transform(openHoleId)
		ground:decay()
		toPosition.z = toPosition.z + 1
		tile:relocateTo(toPosition)
		player:addAchievementProgress("The Undertaker", 500)
	elseif target and target:isItem() and shovelHoles[target.itemid] then
		target:transform(shovelHoles[target.itemid])
		target:decay()
		player:addAchievementProgress("The Undertaker", 500)
	elseif table.contains(sandIds, groundId) then
		local randomValue = math.random(1, 100)
		if target.actionid == actionIds.sandHole and randomValue <= 20 then
			ground:transform(615)
			ground:decay()
		elseif randomValue == 1 then
			Game.createItem(2159, 1, toPosition)
			player:addAchievementProgress("Gold Digger", 100)
		elseif randomValue > 95 then
			Game.createMonster("Scarab", toPosition)
		end
		toPosition:sendMagicEffect(CONST_ME_POFF)
	else
		return false
	end

	return true
end

function onUseScythe(player, item, fromPosition, target, toPosition, isHotkey)
	if not table.contains({3453, 9596}, item.itemid) then return false end

	if target.itemid == 3653 then -- wheat
		target:transform(3651)
		target:decay()
		Game.createItem(3605, 1, toPosition) -- bunch of wheat
		player:addAchievementProgress("Happy Farmer", 200)
		return true
	end
	if target.itemid == 5464 then -- burning sugar cane
		target:transform(5463)
		target:decay()
		Game.createItem(5466, 1, toPosition) -- bunch of sugar cane
		player:addAchievementProgress("Natural Sweetener", 50)
		return true
	end
	return destroyItem(player, target, toPosition)
end

function onUseCrowbar(player, item, fromPosition, target, toPosition, isHotkey)
	if not table.contains({3304, 9598}, item.itemid) then return false end

	return destroyItem(player, target, toPosition)
end

function onUseKitchenKnife(player, item, fromPosition, target, toPosition,
                           isHotkey)
	if not table.contains({3469, 9594, 9598}, item.itemid) then return false end

	if table.contains(fruits, target.itemid) and player:removeItem(6277, 1) then
		target:remove(1)
		player:addItem(6278, 1)
		player:getPosition():sendMagicEffect(CONST_ME_MAGIC_GREEN)
		return true
	end

	return false
end

function onGrindItem(player, item, fromPosition, target, toPosition)
	if not(target.itemid == 21573) then
		return false
	end

	for index, value in pairs(Itemsgrinder) do
		if item.itemid == index then
			local topParent = item:getTopParent()
			if topParent.isItem and (not topParent:isItem() or topParent.itemid ~= 470) then
				local parent = item:getParent()
				if not parent:isTile() and (parent:addItem(value.item_id, 1) or topParent:addItem(value.item_id, 1)) then
					item:remove(1)
					player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You grind a " .. ItemType(index):getName() .. " into fine, " .. ItemType(value.item_id):getName() .. ".")
					doSendMagicEffect(target:getPosition(), value.effect)
					return true
				else
					Game.createItem(value.item_id, 1, item:getPosition())
				end
			else
				Game.createItem(value.item_id, 1, item:getPosition())
			end
			player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "You grind a " .. ItemType(index):getName() .. " into fine, " .. ItemType(value.item_id):getName() .. ".")
			item:remove(1)
			doSendMagicEffect(target:getPosition(), value.effect)
			return
		end
	end
end
