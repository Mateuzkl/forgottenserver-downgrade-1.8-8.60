-- GM access to the skill tree without a client UI.
--
--   /skilltree                     report budget, spent points and allocation
--   /skilltree <nodeId>, <level>   set one node's level (level 0 removes it)
--   /skilltree reset               wipe points and allocation
--
-- The whole allocation is revalidated on every change, exactly like the network
-- path, so this cannot produce a state the client could not also reach.

local skilltree = TalkAction("/skilltree")

local function reportStatus(player)
	local System = SkillTreeSystem
	local nodes = System.getNodes(player)

	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
		"Skill tree: %d of %d points spent (highest level reached: %d).",
		System.getSpentPoints(player), System.getPointBudget(player), player:getHighestLevel()))

	local ids = {}
	for id in pairs(nodes) do
		ids[#ids + 1] = id
	end
	table.sort(ids)

	if #ids == 0 then
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "  nothing allocated.")
		return
	end

	for _, id in ipairs(ids) do
		local nextCost = System.getNextLevelCost(id, nodes[id])
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, string.format(
			"  %s: level %d%s",
			id, nodes[id],
			nextCost and string.format(" (next level costs %d).", nextCost) or " (max)."))
	end
end

function skilltree.onSay(player, words, param)
	if not player:getGroup():getAccess() then
		return true
	end

	if not SkillTreeSystem then
		player:sendCancelMessage("The skill tree system is disabled.")
		return false
	end

	if param == "" then
		reportStatus(player)
		return false
	end

	if param:lower() == "reset" then
		SkillTreeSystem.clearProfile(player)
		player:sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, "Skill tree cleared.")
		return false
	end

	local split = param:splitTrimmed(",")
	local nodeId = split[1]
	local level = tonumber(split[2])
	if not nodeId or not level or level < 0 then
		player:sendCancelMessage("Usage: /skilltree [nodeId, level | reset]")
		return false
	end

	local nodes = SkillTreeSystem.getNodes(player)
	if level == 0 then
		nodes[nodeId] = nil
	else
		nodes[nodeId] = math.floor(level)
	end

	local ok, reason = SkillTreeSystem.setNodes(player, nodes)
	if not ok then
		player:sendCancelMessage(reason)
		return false
	end

	reportStatus(player)
	return false
end

skilltree:separator(" ")
skilltree:register()
