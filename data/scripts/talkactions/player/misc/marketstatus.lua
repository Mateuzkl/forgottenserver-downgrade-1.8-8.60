-- Inspect inboxes, or open the existing legacy inbox at a physical depot.
-- No items are relocated and no replacement deliveries are issued.
local marketStatus = TalkAction("!marketstatus")
function marketStatus.onSay(player, words, param)
	if (param or ""):lower():match("^%s*legacy%s*$") then
		local opened = Game.openLegacyMarketInbox(player:getGuid())
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, opened and "Your legacy Market inbox is now open." or
			"Stand next to a depot and make sure a container window is available, then use !marketstatus legacy.")
		return true
	end
	local depots = { [0] = "Legacy Market Inbox" }
	for _, town in ipairs(Game.getTowns()) do
		if town:getId() <= 99 then
			depots[town:getId()] = town:getName()
		end
	end
	local ids = {}
	for id in pairs(depots) do
		ids[#ids + 1] = id
	end
	table.sort(ids)
	local messages = { "Your inboxes (entries, including stacks and containers):" }
	messages[#messages + 1] = "Use !marketstatus legacy at a depot to open your legacy Market inbox."
	for _, id in ipairs(ids) do
		local inbox = player:getInbox(id)
		local count = inbox and inbox:getSize() or 0
		messages[#messages + 1] = depots[id] .. ": " .. count
	end
	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, table.concat(messages, "\n"))
	return true
end
marketStatus:separator(" ")
marketStatus:register()
