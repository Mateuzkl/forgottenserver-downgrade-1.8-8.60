-- Party Hunt Analyzer packet (0x2B).
local OPCODE_PARTY_ANALYZER = 0x2B

local sessions = {}
local recipients = {}

local function supportsPartyAnalyzer(player)
	return player:isUsingOtClient()
end

local function getSession(party)
	local id = party:getAnalyzerId()
	local session = sessions[id]
	if not session then
		session = { startTime = os.time(), lootType = 0, members = {} }
		sessions[id] = session
	end
	return session
end

local function getMemberStats(session, player)
	local id = player:getId()
	local stats = session.members[id]
	if not stats then
		stats = { loot = 0, supplies = 0 }
		session.members[id] = stats
	end
	return stats
end

local function sendPartyAnalyzer(player)
	if not supportsPartyAnalyzer(player) then return end
	local id = player:getId()
	local party = player:getParty()
	local leader = party and party:getLeader()
	if not leader and not recipients[id] then return end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_PARTY_ANALYZER)

	if not leader then
		recipients[id] = nil
		out:addU32(0)
		out:addU32(0)
		out:addByte(0)
		out:addByte(0)
		out:addByte(0)
	else
		recipients[id] = true
		local session = getSession(party)
		local members = party:getMembers()
		table.insert(members, 1, leader)
		local count = math.min(#members, 255)
		out:addU32(session.startTime)
		out:addU32(leader:getId())
		out:addByte(session.lootType)
		out:addByte(count)
		for index = 1, count do
			local member = members[index]
			local memberId = member:getId()
			local data = getMemberStats(session, member)
			local damage, healing = party:getAnalyzerCombatStats(memberId)
			out:addU32(memberId)
			out:addByte(0)
			out:addU64(data.loot)
			out:addU64(data.supplies)
			out:addU64(damage)
			out:addU64(healing)
		end
		out:addByte(1)
		out:addByte(count)
		for index = 1, count do
			local member = members[index]
			out:addU32(member:getId())
			out:addString(member:getName())
		end
	end
	out:sendToPlayer(player)
end

local function sendPartyAnalyzerToAll(leader)
	local party = leader:getParty()
	if not party then return end
	sendPartyAnalyzer(leader)
	for _, member in ipairs(party:getMembers()) do
		sendPartyAnalyzer(member)
	end
end

local function trackSupply(player, item)
	local party = player:getParty()
	if not party then return end
	local itemType = ItemType(item:getId())
	if not itemType then return end
	local price = itemType:getBuyPrice()
	if price == 0 then
		price = itemType:getDefaultPrice()
	end
	local data = getMemberStats(getSession(party), player)
	data.supplies = data.supplies + price
end

local partyLootDrop = Event()
function partyLootDrop.onDropLoot(monster, corpse)
	local owner = Player(corpse:getCorpseOwner())
	local party = owner and owner:getParty()
	if not party then return end

	local value = 0
	for _, item in ipairs(corpse:getItems(true)) do
		local itemType = ItemType(item:getId())
		if itemType then
			value = value + itemType:getDefaultPrice() * math.max(1, item:getCount())
		end
	end
	local data = getMemberStats(getSession(party), owner)
	data.loot = data.loot + value
end
partyLootDrop:register(100)

local partyRefreshEvent = GlobalEvent("PartyAnalyzerPeriodic")
function partyRefreshEvent.onThink(interval)
	local activeSessions = {}
	local onlineRecipients = {}
	for _, player in ipairs(Game.getPlayers()) do
		local party = player:getParty()
		if party then
			activeSessions[party:getAnalyzerId()] = true
		end
		sendPartyAnalyzer(player)
		local playerId = player:getId()
		if recipients[playerId] then
			onlineRecipients[playerId] = true
		end
	end
	recipients = onlineRecipients
	for id in pairs(sessions) do
		if not activeSessions[id] then
			sessions[id] = nil
		end
	end
	return true
end
partyRefreshEvent:interval(2000)
partyRefreshEvent:register()

local function handlePartyAnalyzerAction(player, msg)
	if not supportsPartyAnalyzer(player) then return true end
	local party = player:getParty()
	if not party or party:getLeader() ~= player then return true end

	local action = msg:getByte()
	if action == 0 then
		local session = getSession(party)
		session.members = {}
		session.startTime = os.time()
		party:resetAnalyzerCombatStats()
	elseif action == 1 then
		local session = getSession(party)
		session.lootType = 1 - session.lootType
	else
		return true
	end
	sendPartyAnalyzerToAll(player)
	return true
end

-- Fonticak sends 0x2B. Keep the existing 0x2E action for compatible clients.
for _, opcode in ipairs({ 0x2B, 0x2E }) do
	local handler = PacketHandler(opcode)
	handler.onReceive = handlePartyAnalyzerAction
	handler:register()
end

PartyAnalyzer = {
	send = sendPartyAnalyzer,
	sendToAll = sendPartyAnalyzerToAll,
	trackSupply = trackSupply,
}
