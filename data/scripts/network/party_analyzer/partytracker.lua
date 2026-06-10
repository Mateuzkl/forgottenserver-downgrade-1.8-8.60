-- data/scripts/network/party_analyzer/partytracker.lua
-- Party Hunt Analyzer - tracks per-member loot/supplies/damage/healing and sends to AstraClient

local OPCODE_PARTY_ANALYZER = 0x2B
local MSG_BLUE = MESSAGE_STATUS_CONSOLE_BLUE or MESSAGE_EVENT_ADVANCE or 19

local partySessions = {}

local function isOTC(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function getPartyLeader(player)
	local party = player:getParty()
	if not party then return nil end
	return party:getLeader()
end

local function isPartyLeader(player)
	if not player then return false end
	local party = player:getParty()
	if not party then return false end
	return party:getLeader() == player
end

local function getPartyMembers(player)
	local party = player:getParty()
	if not party then return {} end
	local members = {}
	members[party:getLeader():getId()] = party:getLeader()
	for _, m in ipairs(party:getMembers()) do
		members[m:getId()] = m
	end
	return members
end

local function getOrCreateSession(leader)
	if not leader then return nil end
	local leaderId = leader:getId()
	if not partySessions[leaderId] then
		partySessions[leaderId] = {
			startTime = os.time(),
			lootType = 0, -- 0=Market prices, 1=Leader prices
			members = {},
		}
	end
	return partySessions[leaderId]
end

local function getOrCreateMemberData(session, playerId, playerName)
	if not session.members[playerId] then
		session.members[playerId] = {
			name = playerName,
			loot = 0,
			supplies = 0,
			damage = 0,
			healing = 0,
		}
	end
	return session.members[playerId]
end

local function sendPartyAnalyzer(player)
	if not player or not isOTC(player) then return end
	local leader = getPartyLeader(player)
	if not leader then return end
	local session = getOrCreateSession(leader)
	if not session then return end

	local members = getPartyMembers(leader)

	-- Update member names and ensure all party members are tracked
	for id, m in pairs(members) do
		getOrCreateMemberData(session, id, m:getName())
	end

	local onlineMembers = {}
	for id, m in pairs(members) do
		if m:isOnline() then
			onlineMembers[id] = m
		end
	end

	local out = NetworkMessage(leader)
	out:addByte(OPCODE_PARTY_ANALYZER)
	out:addU32(session.startTime)
	out:addU32(leader:getId())
	out:addByte(session.lootType)
	out:addByte(#onlineMembers)
	for id, m in pairs(onlineMembers) do
		local data = session.members[id] or {loot=0, supplies=0, damage=0, healing=0}
		out:addU32(id)
		out:addByte(0) -- highlight flag
		out:addU64(data.loot)
		out:addU64(data.supplies)
		out:addU64(data.damage)
		out:addU64(data.healing)
	end
	out:addByte(0) -- online flag
	out:addByte(#onlineMembers)
	for id, m in pairs(onlineMembers) do
		out:addU32(id)
		out:addString(m:getName())
	end
	out:sendToPlayer(player)
end

function sendPartyAnalyzerToAll(leader)
	local members = getPartyMembers(leader)
	for _, m in pairs(members) do
		if isOTC(m) then
			sendPartyAnalyzer(m)
		end
	end
end

-- Creature event: add loot value when party member loots a corpse
local partyLootEvent = CreatureEvent("PartyAnalyzerLoot")
function partyLootEvent.onDropLoot(monster, corpse)
	local owner = Player(corpse:getCorpseOwner())
	if not owner then return end
	local leader = getPartyLeader(owner)
	if not leader then return end
	local session = getOrCreateSession(leader)
	if not session then return end
	local data = getOrCreateMemberData(session, owner:getId(), owner:getName())

	local function addContainerValue(container)
		local total = 0
		if not container then return 0 end
		for i = 0, container:getSize() - 1 do
			local item = container:getItem(i)
			if item then
				local itemType = ItemType(item:getId())
				local price = itemType and (itemType:getDefaultPrice() or itemType:getWorth()) or 0
				total = total + (price * math.max(1, item:getCount()))
			end
		end
		return total
	end

	data.loot = data.loot + addContainerValue(corpse)
	sendPartyAnalyzerToAll(leader)
end
partyLootEvent:register(HUNT_ANALYZER_DROP_TRIGGER or 100)

-- Health/mana change: track healing received
local partyHealEvent = CreatureEvent("PartyAnalyzerHeal")
function partyHealEvent.onHealthChange(creature, attacker, primaryDamage, primaryType, secondaryDamage, secondaryType)
	if not attacker or not creature:isPlayer() then return primaryDamage, primaryType, secondaryDamage, secondaryType end
	local leader = getPartyLeader(creature)
	if not leader or leader == creature then return primaryDamage, primaryType, secondaryDamage, secondaryType end
	
	local session = getOrCreateSession(leader)
	if not session then return primaryDamage, primaryType, secondaryDamage, secondaryType end

	if primaryDamage < 0 then -- healing
		local data = getOrCreateMemberData(session, creature:getId(), creature:getName())
		data.healing = data.healing + math.abs(primaryDamage)
		sendPartyAnalyzerToAll(leader)
	end
	if secondaryDamage and secondaryDamage < 0 then
		local data = getOrCreateMemberData(session, creature:getId(), creature:getName())
		data.healing = data.healing + math.abs(secondaryDamage)
		sendPartyAnalyzerToAll(leader)
	end

	return primaryDamage, primaryType, secondaryDamage, secondaryType
end
partyHealEvent:register()

-- Track damage dealt by party members using the existing impact tracker
-- We hook into the impact tracker's send function to also update party session
local originalSendImpactTracker = _G.sendImpactTracker
if originalSendImpactTracker then
	_G.sendImpactTracker = function(player, analyzerType, amount, effect, targetName)
		originalSendImpactTracker(player, analyzerType, amount, effect, targetName)

		if analyzerType == 1 then -- DAMAGE_DEALT
			local leader = getPartyLeader(player)
			if leader then
				local session = partySessions[leader:getId()]
				if session then
					local data = session.members[player:getId()]
					if data then
						data.damage = data.damage + amount
						sendPartyAnalyzerToAll(leader)
					end
				end
			end
		end
	end
end

-- Track supply usage
local originalSendSupplyTracker = _G.sendSupplyTracker
if originalSendSupplyTracker then
	_G.sendSupplyTracker = function(player, item)
		originalSendSupplyTracker(player, item)

		local leader = getPartyLeader(player)
		if leader then
			local session = partySessions[leader:getId()]
			if session then
				local data = session.members[player:getId()]
				if data then
					local itemType = ItemType(item:getId())
					local price = itemType and (itemType:getDefaultPrice() or itemType:getBuyPrice()) or 0
					data.supplies = data.supplies + price
					sendPartyAnalyzerToAll(leader)
				end
			end
		end
	end
end

-- Packet handler: client requests loot type change (0x2C = ClientPartyAnalyzerRequest)
local REQUEST_OPCODE = 0x2C
local handler = PacketHandler(REQUEST_OPCODE)
function handler.onReceive(player, msg)
	if not isOTC(player) then return true end
	local action = msg:getByte()
	if action == 0 then -- Reset session
		if isPartyLeader(player) then
			partySessions[player:getId()] = nil
			sendPartyAnalyzerToAll(player)
		end
	elseif action == 1 then -- Change loot type
		if isPartyLeader(player) then
			local session = getOrCreateSession(player)
			if session then
				session.lootType = session.lootType == 0 and 1 or 0
				sendPartyAnalyzerToAll(player)
			end
		end
	end
	return true
end
handler:register()

-- Party join/leave: send update to all members
local function onPartyChange(player)
	if not isOTC(player) then return end
	local leader = getPartyLeader(player)
	if leader then
		sendPartyAnalyzerToAll(leader)
	else
		-- Player left party, send update to remaining leader
		for leaderId, session in pairs(partySessions) do
			local leaderPlayer = Player(leaderId)
			if leaderPlayer then
				sendPartyAnalyzerToAll(leaderPlayer)
			end
		end
	end
end

local partyJoinEvent = CreatureEvent("PartyAnalyzerJoin")
function partyJoinEvent.onJoinParty(player)
	addEvent(onPartyChange, 1000, player:getId())
end
partyJoinEvent:register()

local partyLeaveEvent = CreatureEvent("PartyAnalyzerLeave")
function partyLeaveEvent.onLeaveParty(player)
	addEvent(onPartyChange, 1000, player:getId())
end
partyLeaveEvent:register()

-- Login: send current party state
local partyLoginEvent = CreatureEvent("PartyAnalyzerLogin")
function partyLoginEvent.onLogin(player)
	if not isOTC(player) then return true end
	addEvent(function(pid)
		local p = Player(pid)
		if not p then return end
		if not isOTC(p) then return end
		local leader = getPartyLeader(p)
		if leader then
			sendPartyAnalyzer(p)
		end
	end, 2000, player:getId())
	return true
end
partyLoginEvent:register()

PartyAnalyzer = {
	send = sendPartyAnalyzer,
	sendToAll = sendPartyAnalyzerToAll,
}
