-- data/scripts/network/boss_cooldown/bosscooldown.lua
-- Boss Cooldown Tracker - sends boss cooldown list to AstraClient (opcode 0x2C)
-- Uses KV store (player:kv() -> boss.cooldown.<raceId>)

local OPCODE_BOSS_COOLDOWN = 0x2C

local function isOTC(player)
	return player and player:isUsingOtClient()
end

local function getBossOutfit(lookType)
	local mt = MonsterType(lookType)
	if mt then
		local outfit = mt:getOutfit()
		if outfit then
			return {
				type = outfit.lookType or lookType,
				head = outfit.lookHead or 0,
				body = outfit.lookBody or 0,
				legs = outfit.lookLegs or 0,
				feet = outfit.lookFeet or 0,
				addons = outfit.lookAddons or 0,
			}
		end
	end
	return {type = lookType, head = 0, body = 0, legs = 0, feet = 0, addons = 0}
end

local function getBossList()
	local bosses = {}
	if CustomBosstiary and CustomBosstiary.monstersByRaceId then
		for raceId, entry in pairs(CustomBosstiary.monstersByRaceId) do
			bosses[#bosses + 1] = {
				raceId = raceId,
				name = entry.name,
				outfit = entry.outfit or {},
			}
		end
	end
	table.sort(bosses, function(a, b) return a.raceId < b.raceId end)
	return bosses
end

local function sendCooldowns(player)
	if not player or not isOTC(player) then return false end

	local kv = player:kv()
	if not kv then return false end

	local now = os.time()
	local cooldownKV = kv:scoped("boss.cooldown")
	local activeBosses = {}
	local bosses = getBossList()

	for _, boss in ipairs(bosses) do
		local key = tostring(boss.raceId)
		local cooldownEnd = cooldownKV:get(key) or 0
		if cooldownEnd > now then
			local outfit
			if boss.outfit and boss.outfit.type then
				outfit = boss.outfit
			else
				outfit = getBossOutfit(boss.outfit and boss.outfit.lookType or 136)
			end
			activeBosses[#activeBosses + 1] = {
				id = boss.raceId,
				cooldown = cooldownEnd,
				name = boss.name,
				outfit = outfit,
			}
		end
	end

	local out = NetworkMessage(player)
	out:addByte(OPCODE_BOSS_COOLDOWN)
	out:addByte(math.min(#activeBosses, 255))
	for _, boss in ipairs(activeBosses) do
		out:addU16(boss.id)
		out:addU32(boss.cooldown)
		out:addString(boss.name)
		out:addU16(boss.outfit.type)
		out:addByte(boss.outfit.head)
		out:addByte(boss.outfit.body)
		out:addByte(boss.outfit.legs)
		out:addByte(boss.outfit.feet)
		out:addByte(boss.outfit.addons)
	end
	return out:sendToPlayer(player)
end

-- Login event
local bossLogin = CreatureEvent("BossCooldownLogin")
function bossLogin.onLogin(player)
	if not isOTC(player) then return true end
	addEvent(function(pid)
		local p = Player(pid)
		if p then sendCooldowns(p) end
	end, 3000, player:getId())
	return true
end
bossLogin:register()

-- Periodic refresh every 30s
local bossRefresh = GlobalEvent("BossCooldownPeriodic")
function bossRefresh.onThink(interval)
	for _, player in ipairs(Game.getPlayers()) do
		sendCooldowns(player)
	end
	return true
end
bossRefresh:interval(30000)
bossRefresh:register()

BossCooldown = BossCooldown or {}
BossCooldown.send = sendCooldowns
