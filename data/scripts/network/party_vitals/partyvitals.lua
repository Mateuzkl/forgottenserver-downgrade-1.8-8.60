-- Party member HP/mana sync for OTC-Fonticak (S2C opcode 0x3E)
-- Sends vitals to all OTC party members even when they are far from each other.
-- Do not use 0x34: OTC client handles that as attached spell effects.

local OPCODE_PARTY_VITALS = 0x3E
local PARTY_VITALS_PROTOCOL_VERSION = 2

PartyVitals = PartyVitals or {}

local pendingPartyBroadcast = {}
local vitalCache = {}

local function isOTC(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function getPartyRoster(anchorPlayer)
	local party = anchorPlayer:getParty()
	if not party then
		return nil
	end

	local leader = party:getLeader()
	if not leader then
		return nil
	end

	local members = {}
	members[leader:getId()] = leader
	for _, member in ipairs(party:getMembers()) do
		members[member:getId()] = member
	end
	return members
end

local function vitalPercent(current, max)
	current = math.max(tonumber(current) or 0, 0)
	max = tonumber(max) or 0
	if max <= 0 then
		return current > 0 and 100 or 0
	end
	return math.min(100, math.max(0, math.floor((current * 100) / max)))
end

local function memberVitals(player)
	return vitalPercent(player:getHealth(), player:getMaxHealth()),
		vitalPercent(player:getMana(), player:getMaxMana())
end

function PartyVitals.sendToPlayer(recipient, roster)
	if not isOTC(recipient) or not roster then
		return
	end

	local entries = {}
	for id, member in pairs(roster) do
		if member and member:isPlayer() then
			local hp, mana = memberVitals(member)
			table.insert(entries, {
				id = id,
				hp = hp,
				mana = mana,
				level = member:getLevel(),
			})
		end
	end

	if #entries == 0 then
		return
	end

	local party = recipient:getParty()
	local leader = party and party:getLeader()
	local leaderId = leader and leader:getId()

	local out = NetworkMessage(recipient)
	out:addByte(OPCODE_PARTY_VITALS)
	out:addByte(math.min(#entries, 255))
	out:addByte(PARTY_VITALS_PROTOCOL_VERSION)
	for i = 1, math.min(#entries, 255) do
		local e = entries[i]
		local member = roster[e.id]
		local pos = member and member:getPosition() or Position(0, 0, 0)
		out:addU32(e.id)
		out:addByte(e.hp)
		out:addByte(e.mana)
		out:addU16(e.level or 0)
		out:addU16(pos.x)
		out:addU16(pos.y)
		out:addByte(pos.z)
		out:addByte(leaderId and e.id == leaderId and 1 or 0)
		out:addByte(member and member:getVocation() or 0)
		out:addString(member and member:getName() or "")
	end
	out:sendToPlayer(recipient)
end

function PartyVitals.broadcastParty(anchorPlayer)
	local roster = getPartyRoster(anchorPlayer)
	if not roster then
		return
	end

	for _, recipient in pairs(roster) do
		PartyVitals.sendToPlayer(recipient, roster)
	end
end

local function queuePartyBroadcast(anchorPlayer)
	if not anchorPlayer then
		return
	end

	local party = anchorPlayer:getParty()
	if not party then
		return
	end

	local leader = party:getLeader()
	if not leader then
		return
	end

	local leaderId = leader:getId()
	if pendingPartyBroadcast[leaderId] then
		return
	end

	pendingPartyBroadcast[leaderId] = true
	addEvent(function(queuedLeaderId)
		pendingPartyBroadcast[queuedLeaderId] = nil
		local queuedLeader = Player(queuedLeaderId)
		if queuedLeader and queuedLeader:getParty() then
			PartyVitals.broadcastParty(queuedLeader)
		end
	end, 50, leaderId)
end

local function updateVitalCache(player)
	local hp, mana = memberVitals(player)
	local key = hp .. ":" .. mana
	local pid = player:getId()
	local previous = vitalCache[pid]
	vitalCache[pid] = key
	if previous == nil then
		return true
	end
	return previous ~= key
end

local partyVitalsTick = GlobalEvent("PartyVitalsTick")
function partyVitalsTick.onThink()
	for _, player in ipairs(Game.getPlayers()) do
		if not player:getParty() then
			vitalCache[player:getId()] = nil
		elseif updateVitalCache(player) then
			queuePartyBroadcast(player)
		end
	end
	return true
end
partyVitalsTick:interval(200)
partyVitalsTick:register()

-- Full party snapshot every second (keeps mana in sync even when values are stable).
local partyVitalsHeartbeat = GlobalEvent("PartyVitalsHeartbeat")
function partyVitalsHeartbeat.onThink()
	local leadersDone = {}
	for _, player in ipairs(Game.getPlayers()) do
		local party = player:getParty()
		if not party then
			goto continue
		end
		local leader = party:getLeader()
		if not leader then
			goto continue
		end
		local leaderId = leader:getId()
		if leadersDone[leaderId] then
			goto continue
		end
		leadersDone[leaderId] = true
		PartyVitals.broadcastParty(leader)
		::continue::
	end
	return true
end
partyVitalsHeartbeat:interval(1000)
partyVitalsHeartbeat:register()

local partyVitalsLogin = CreatureEvent("PartyVitalsLogin")
function partyVitalsLogin.onLogin(player)
	addEvent(function(playerId)
		local p = Player(playerId)
		if p and p:getParty() then
			PartyVitals.broadcastParty(p)
		end
	end, 500, player:getId())
	return true
end
partyVitalsLogin:type("login")
partyVitalsLogin:register()

local partyVitalsLogout = CreatureEvent("PartyVitalsLogout")
function partyVitalsLogout.onLogout(player)
	vitalCache[player:getId()] = nil
	return true
end
partyVitalsLogout:register()

local partyVitalsJoin = EventCallback
function partyVitalsJoin.onJoin(party, player)
	addEvent(function(playerId)
		local p = Player(playerId)
		if p and p:getParty() then
			PartyVitals.broadcastParty(p)
		end
	end, 100, player:getId())
	return true
end
partyVitalsJoin:register()
