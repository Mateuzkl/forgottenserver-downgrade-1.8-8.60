-- data/scripts/network/boss_cooldown/bosscooldown.lua
-- Boss Cooldown Tracker - sends boss cooldown list to AstraClient (opcode 0x2C)
-- Uses KV store (player:kv() -> boss.cooldown.<raceId>)

local OPCODE_BOSS_COOLDOWN = 0x2C
local PERIODIC_REFRESH_INTERVAL = 5 * 60 * 1000
local PERIODIC_REFRESH_PLAYER_DELAY = 100

BossCooldown = BossCooldown or {}

local bossListCache
local bossByRaceIdCache
local playerCooldownKeyCache = {}

local function supportsAstraClient(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
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

local function normalizeBossOutfit(entry)
	if entry.outfit and entry.outfit.type then
		return entry.outfit
	end
	return getBossOutfit(entry.outfit and entry.outfit.lookType or 136)
end

local function buildBossCache()
	local bosses = {}
	local bossesByRaceId = {}
	if CustomBosstiary and CustomBosstiary.monstersByRaceId then
		for raceId, entry in pairs(CustomBosstiary.monstersByRaceId) do
			local boss = {
				raceId = raceId,
				key = tostring(raceId),
				name = entry.name,
				outfit = normalizeBossOutfit(entry),
			}
			bosses[#bosses + 1] = boss
			bossesByRaceId[raceId] = boss
		end
	end
	table.sort(bosses, function(a, b) return a.raceId < b.raceId end)
	bossListCache = bosses
	bossByRaceIdCache = bossesByRaceId
end

local function getBossList()
	if not bossListCache then
		buildBossCache()
	end
	return bossListCache
end

local function getBossByRaceId()
	if not bossByRaceIdCache then
		buildBossCache()
	end
	return bossByRaceIdCache
end

local function getCooldownKeys(cooldownKV)
	local ok, keys = pcall(function()
		return cooldownKV:keys()
	end)
	if ok and type(keys) == "table" then
		return keys
	end
	return nil
end

local function getPlayerCooldownKeyCache(player, cooldownKV)
	local guid = player:getGuid()
	local cachedKeys = playerCooldownKeyCache[guid]
	if cachedKeys then
		return cachedKeys
	end

	local cooldownKeys = getCooldownKeys(cooldownKV)
	if not cooldownKeys then
		return nil
	end

	cachedKeys = {}
	for _, key in ipairs(cooldownKeys) do
		local raceId = tonumber(key)
		if raceId then
			cachedKeys[tostring(raceId)] = true
		end
	end
	playerCooldownKeyCache[guid] = cachedKeys
	return cachedKeys
end

local function appendActiveBoss(activeBosses, boss, cooldownEnd, now)
	if not boss or not cooldownEnd or cooldownEnd <= now then
		return
	end

	activeBosses[#activeBosses + 1] = {
		id = boss.raceId,
		cooldown = cooldownEnd,
		name = boss.name,
		outfit = boss.outfit,
	}
end

local function getActiveBossCooldowns(player, cooldownKV)
	local now = os.time()
	local activeBosses = {}
	local cooldownKeys = getPlayerCooldownKeyCache(player, cooldownKV)

	if cooldownKeys then
		local bossesByRaceId = getBossByRaceId()
		for key in pairs(cooldownKeys) do
			local raceId = tonumber(key)
			local boss = raceId and bossesByRaceId[raceId]
			if boss then
				local cooldownEnd = cooldownKV:get(key) or 0
				if cooldownEnd > now then
					appendActiveBoss(activeBosses, boss, cooldownEnd, now)
				else
					cooldownKeys[key] = nil
				end
			end
		end
	else
		for _, boss in ipairs(getBossList()) do
			appendActiveBoss(activeBosses, boss, cooldownKV:get(boss.key) or 0, now)
		end
	end

	table.sort(activeBosses, function(a, b) return a.id < b.id end)
	return activeBosses
end

local function sendCooldowns(player)
	if not supportsAstraClient(player) then return false end

	local kv = player:kv()
	if not kv then return false end

	local cooldownKV = kv:scoped("boss.cooldown")
	local activeBosses = getActiveBossCooldowns(player, cooldownKV)

	local out = NetworkMessage(player)
	out:addByte(OPCODE_BOSS_COOLDOWN)
	local bossCount = math.min(#activeBosses, 255)
	out:addByte(bossCount)
	for i = 1, bossCount do
		local boss = activeBosses[i]
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

local function scheduleCooldownRefresh(playerId, delay)
	addEvent(function(pid)
		local player = Player(pid)
		if player then
			sendCooldowns(player)
		end
	end, delay, playerId)
end

-- Login event
local bossLogin = CreatureEvent("BossCooldownLogin")
function bossLogin.onLogin(player)
	if not supportsAstraClient(player) then return true end
	scheduleCooldownRefresh(player:getId(), 3000)
	return true
end
bossLogin:register()

local bossLogout = CreatureEvent("BossCooldownLogout")
function bossLogout.onLogout(player)
	playerCooldownKeyCache[player:getGuid()] = nil
	return true
end
bossLogout:register()

-- Lightweight safety refresh. Real updates are pushed by Player:setBossCooldown().
local bossRefresh = GlobalEvent("BossCooldownPeriodic")
function bossRefresh.onThink(interval)
	local delay = 0
	for _, player in ipairs(Game.getPlayers()) do
		if supportsAstraClient(player) then
			scheduleCooldownRefresh(player:getId(), delay)
			delay = delay + PERIODIC_REFRESH_PLAYER_DELAY
		end
	end
	return true
end
bossRefresh:interval(PERIODIC_REFRESH_INTERVAL)
bossRefresh:register()

function BossCooldown.invalidateCache()
	bossListCache = nil
	bossByRaceIdCache = nil
end

function BossCooldown.rememberKey(player, scope, cooldownEnd)
	if not player or not scope then
		return
	end

	local cachedKeys = playerCooldownKeyCache[player:getGuid()]
	if not cachedKeys then
		return
	end

	local raceId = tostring(scope):match("^boss%.cooldown%.(.+)$")
	if not raceId then
		if logger and logger.warn then
			logger.warn("[BossCooldown] Invalid cooldown scope '%s' for player %s (guid %d); invalidating cache.", tostring(scope), player:getName(), player:getGuid())
		end
		playerCooldownKeyCache[player:getGuid()] = nil
		return
	end

	if cooldownEnd and cooldownEnd > os.time() then
		cachedKeys[tostring(raceId)] = true
	else
		cachedKeys[tostring(raceId)] = nil
	end
end

BossCooldown.send = sendCooldowns
