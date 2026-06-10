BossCooldown = BossCooldown or {}

local function getBosstiaryEntry(bossNameOrId)
	local raceId = tonumber(bossNameOrId)
	if raceId and raceId > 0 then
		return CustomBosstiary and CustomBosstiary.getMonster and CustomBosstiary.getMonster(raceId)
	end
	if CustomBosstiary and CustomBosstiary.monstersByName then
		return CustomBosstiary.monstersByName[tostring(bossNameOrId):lower()]
	end
	return nil
end

local function getBossRaceId(bossNameOrId)
	local entry = getBosstiaryEntry(bossNameOrId)
	return entry and entry.raceId
end

local function getKVScope(bossNameOrId)
	local raceId = tonumber(bossNameOrId)
	if not raceId or raceId <= 0 then
		raceId = getBossRaceId(bossNameOrId)
	end
	if not raceId or raceId <= 0 then
		return nil
	end
	return "boss.cooldown." .. tostring(raceId)
end

function Player:getBossCooldown(bossNameOrId)
	local scope = getKVScope(bossNameOrId)
	if not scope then
		return 0
	end
	local kv = self:kv()
	if not kv then
		return 0
	end
	return kv:get(scope) or 0
end

function Player:setBossCooldown(bossNameOrId, time)
	local scope = getKVScope(bossNameOrId)
	if not scope then
		return false
	end
	local kv = self:kv()
	if not kv then
		return false
	end
	local result = kv:set(scope, time)
	if BossCooldown and BossCooldown.send then
		BossCooldown.send(self)
	end
	return result
end

function Player:canFightBoss(bossNameOrId)
	local cooldown = self:getBossCooldown(bossNameOrId)
	if not cooldown or cooldown == false then
		return true
	end
	return cooldown <= os.time()
end
