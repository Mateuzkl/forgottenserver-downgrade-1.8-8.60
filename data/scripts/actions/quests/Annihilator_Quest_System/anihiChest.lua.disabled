local rewardItems = {
    [2000] = {id = 3388, count = 1},
    [2001] = {id = 3288, count = 1},
    [2002] = {id = 3319, count = 1},
    [2003] = {id = 2856, count = 1},
}

local action = Action()
function action.onUse(player, item, fromPosition, target, toPosition, isHotkey)

	local quest = player:questKV("annihilator")
	if quest:get("reward") then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "It is empty.")
		return true
	end

    local reward = rewardItems[item.uid]
    if not reward or ItemType(reward.id):getId() == 0 then
        player:getPosition():sendMagicEffect(CONST_ME_POFF)
        return true
    end

    local virtualItem = Game.createItem(reward.id, reward.count)
    if reward.id == 2856 then
        virtualItem:addItem(3213)
    end

    if player:addItemEx(virtualItem) ~= RETURNVALUE_NOERROR then
        return player:sendTextMessage(MESSAGE_INFO_DESCR, "You have found an item weighing " .. ("%.2f"):format(virtualItem:getWeight() / 100) .. " oz, it's too heavy or you do not have enough room.")
    end

	player:sendTextMessage(MESSAGE_INFO_DESCR, "You have found a " .. virtualItem:getName():lower() .. ".")
	quest:set("reward", true)
	quest:set("completedAt", os.time())
    player:getPosition():sendMagicEffect(30)
    return true
end
for uid in pairs(rewardItems) do
    action:uid(uid)
end
action:register()

local removeEvent = MoveEvent()
function removeEvent.onRemoveItem(item, tile, pos)
    return false
end

for uid in pairs(rewardItems) do
    removeEvent:uid(uid)
end
removeEvent:register()

local stepEvent = MoveEvent()
function stepEvent.onStepIn(creature, item, pos, fromPosition)
	if creature:isPlayer() then
		creature:teleportTo(fromPosition, false, CONST_ME_NONE)
		fromPosition:sendMagicEffect(CONST_ME_POFF)
	end
    return true
end

for uid in pairs(rewardItems) do
    stepEvent:uid(uid)
end
stepEvent:register()
