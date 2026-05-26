WorkbenchSessions = WorkbenchSessions or {}
WorkbenchSessions.sessions = WorkbenchSessions.sessions or {}

local sessions = WorkbenchSessions.sessions

local function getWorkbenchUid(workbench)
	if not workbench then
		return nil
	end

	return workbench.uid or workbench:getUniqueId()
end

local function getWorkbenchPosition(workbench)
	if not workbench then
		return nil
	end

	return workbench:getPosition()
end

local function getWorkbenchInstanceId(workbench)
	if not workbench then
		return 0
	end

	if workbench.getInstanceId then
		return workbench:getInstanceId()
	end
	return 0
end

function WorkbenchSessions.getKey(workbench)
	local position = getWorkbenchPosition(workbench)
	if not position then
		return nil
	end

	return string.format(
		"%d:%d:%d:%d",
		getWorkbenchInstanceId(workbench),
		position.x,
		position.y,
		position.z
	)
end

function WorkbenchSessions.get(workbenchOrKey)
	local key = type(workbenchOrKey) == "string" and workbenchOrKey or WorkbenchSessions.getKey(workbenchOrKey)
	if not key then
		return nil, nil
	end

	return sessions[key], key
end

function WorkbenchSessions.reserve(workbench, ownerId)
	local key = WorkbenchSessions.getKey(workbench)
	if not key then
		return nil
	end

	local previous = sessions[key]
	if previous and previous.eventId and previous.eventId > 0 then
		stopEvent(previous.eventId)
	end

	local position = workbench:getPosition()
	sessions[key] = {
		ownerId = ownerId,
		eventId = -1,
		instanceId = getWorkbenchInstanceId(workbench),
		uid = getWorkbenchUid(workbench),
		position = {
			x = position.x,
			y = position.y,
			z = position.z
		}
	}
	return key
end

function WorkbenchSessions.setEvent(workbenchOrKey, eventId)
	local session = WorkbenchSessions.get(workbenchOrKey)
	if session then
		session.eventId = eventId or -1
	end
end

function WorkbenchSessions.clear(workbenchOrKey, stopTimer)
	local session, key = WorkbenchSessions.get(workbenchOrKey)
	if not session then
		return
	end

	if stopTimer ~= false and session.eventId and session.eventId > 0 then
		stopEvent(session.eventId)
	end
	sessions[key] = nil
end

function WorkbenchSessions.clearOwner(ownerId, stopTimers)
	for key, session in pairs(sessions) do
		if session.ownerId == ownerId then
			if stopTimers then
				if session.eventId and session.eventId > 0 then
					stopEvent(session.eventId)
				end
				sessions[key] = nil
			else
				session.ownerId = -1
			end
		end
	end
end

function WorkbenchSessions.getOwner(workbench)
	local session = WorkbenchSessions.get(workbench)
	if session and session.ownerId and session.ownerId > 0 then
		return session.ownerId
	end

	return nil
end

function WorkbenchSessions.getOwnedItemOwner(container)
	if not container then
		return nil
	end

	for i = 0, container:getSize() - 1 do
		local item = container:getItem(i)
		if item and item:hasAttribute(ITEM_ATTRIBUTE_OWNER) then
			local ownerId = tonumber(item:getAttribute(ITEM_ATTRIBUTE_OWNER)) or 0
			if ownerId > 0 then
				return ownerId, item
			end
		end
	end
	return nil
end

function WorkbenchSessions.hasOwnedItems(container)
	return WorkbenchSessions.getOwnedItemOwner(container) ~= nil
end

function WorkbenchSessions.isInUseByOther(workbench, player)
	if not workbench or not player then
		return false
	end

	local playerId = player:getId()
	local ownerId = WorkbenchSessions.getOwner(workbench)
	if ownerId and ownerId ~= playerId then
		return true
	end

	local container = Container(getWorkbenchUid(workbench))
	local itemOwnerId = WorkbenchSessions.getOwnedItemOwner(container)
	return itemOwnerId ~= nil and itemOwnerId ~= playerId
end

function WorkbenchSessions.isSameInstance(workbench, player)
	if not workbench or not player then
		return false
	end

	return getWorkbenchInstanceId(workbench) == player:getInstanceId()
end
