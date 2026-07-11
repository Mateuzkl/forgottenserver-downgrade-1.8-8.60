local SERVER_PACKET_BOSS_COOLDOWN = 0x2C
local MAX_BOSSES_PER_PACKET = 0xFF

BossCooldown = BossCooldown or {}

local function supportsBossCooldown(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function clamp(value, minimum, maximum)
	value = tonumber(value) or minimum
	return math.min(math.max(value, minimum), maximum)
end

local function normalizeOutfit(outfit)
	outfit = type(outfit) == "table" and outfit or {}
	return {
		type = clamp(outfit.type or outfit.lookType, 0, 0xFFFF),
		head = clamp(outfit.head or outfit.lookHead, 0, 0xFF),
		body = clamp(outfit.body or outfit.lookBody, 0, 0xFF),
		legs = clamp(outfit.legs or outfit.lookLegs, 0, 0xFF),
		feet = clamp(outfit.feet or outfit.lookFeet, 0, 0xFF),
		addons = clamp(outfit.addons or outfit.lookAddons, 0, 0xFF),
	}
end

local function getActiveCooldowns(player)
	local active = {}
	local now = os.time()
	local kv = player:kv()
	if not kv or not CustomBosstiary or not CustomBosstiary.monstersByRaceId then
		return active
	end

	for raceId, entry in pairs(CustomBosstiary.monstersByRaceId) do
		local cooldown = tonumber(kv:get("boss.cooldown." .. raceId)) or 0
		if cooldown > now then
			active[#active + 1] = {
			raceId = raceId,
			cooldown = cooldown,
			name = entry.name or "",
			outfit = normalizeOutfit(entry.outfit),
		}
		end
	end

	table.sort(active, function(left, right)
		return left.raceId < right.raceId
	end)
	return active
end

function BossCooldown.send(player)
	if not supportsBossCooldown(player) then
		return false
	end

	local active = getActiveCooldowns(player)
	local count = math.min(#active, MAX_BOSSES_PER_PACKET)
	local msg = NetworkMessage(player)
	msg:addByte(SERVER_PACKET_BOSS_COOLDOWN)
	msg:addByte(count)
	for index = 1, count do
		local boss = active[index]
		msg:addU16(boss.raceId)
		msg:addU32(math.min(boss.cooldown, 0xFFFFFFFF))
		msg:addString(boss.name)
		msg:addU16(boss.outfit.type)
		msg:addByte(boss.outfit.head)
		msg:addByte(boss.outfit.body)
		msg:addByte(boss.outfit.legs)
		msg:addByte(boss.outfit.feet)
		msg:addByte(boss.outfit.addons)
	end
	return msg:sendToPlayer(player)
end

local bossCooldownLogin = CreatureEvent("BossCooldownLogin")
function bossCooldownLogin.onLogin(player)
	if supportsBossCooldown(player) then
		addEvent(function(playerId)
			local currentPlayer = Player(playerId)
			if currentPlayer then
				BossCooldown.send(currentPlayer)
			end
		end, 1000, player:getId())
	end
	return true
end
bossCooldownLogin:register()
