local WORKBENCH_ID = 27547

local action = Action()
function action.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	local container = Container(item.uid)
	if not container then return false end

	if not WorkbenchSessions.isSameInstance(item, player) then
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		return true
	end

	if WorkbenchSessions.isInUseByOther(item, player) then
		player:sendCancelMessage("This workbench is currently in use.")
		return true
	end

	for i = 0, container:getSize() - 1 do
		local subItem = container:getItem(i)
		if subItem and subItem:hasAttribute(ITEM_ATTRIBUTE_OWNER) then
			local ownerId = subItem:getAttribute(ITEM_ATTRIBUTE_OWNER)
			if player:getId() ~= ownerId then
				player:sendCancelMessage("This workbench is currently in use.")
				return true
			end
			return false
		end
	end

	return false
end
action:id(WORKBENCH_ID)
action:register()
