-- Read-only inspection of the character's inboxes, including the legacy Market
-- destination 0. Does not relocate items or issue replacement deliveries.
local marketStatus = TalkAction("!marketstatus")
function marketStatus.onSay(player, words, param)
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
	for _, id in ipairs(ids) do
		local inbox = player:getInbox(id)
		local count = inbox and inbox:getSize() or 0
		messages[#messages + 1] = depots[id] .. ": " .. count
	end
	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, table.concat(messages, "\n"))
	return true
end
marketStatus:register()
