-- Read-only inspection of the character's inboxes, including the legacy Market
-- destination 0. Does not relocate items or issue replacement deliveries.
local marketStatus = TalkAction("!marketstatus")
function marketStatus.onSay(player, words, param)
	local depots = { [0] = "Inbox antigo do Market" }
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
	local messages = { "Inboxes do seu personagem (entradas, incluindo pilhas/containers):" }
	for _, id in ipairs(ids) do
		local inbox = player:getInbox(id)
		local count = inbox and inbox:getSize() or 0
		messages[#messages + 1] = depots[id] .. " - town/depot ID " .. id .. ": " .. count
	end
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, table.concat(messages, "\n"))
	return true
end
marketStatus:register()
