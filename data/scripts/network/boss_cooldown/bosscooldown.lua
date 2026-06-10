-- data/scripts/network/boss_cooldown/bosscooldown.lua
-- Boss Cooldown Tracker - sends boss cooldown list to AstraClient (opcode 0x2C)
-- Requires boss kills tracked via storage values (bossName_storage pattern)

local OPCODE_BOSS_COOLDOWN = 0x2C

-- Format: bossId, storageKey, bossName, lookType
-- Add your boss entries here. storageKey is the cooldown storage for this boss.
local BOSS_LIST = {
	-- Example entries - customize for your server
	-- {id = 1, storage = 45000, name = "Orshabaal", lookType = 111},
	-- {id = 2, storage = 45001, name = "Morgaroth", lookType = 112},
	-- {id = 3, storage = 45002, name = "Ghazbaran", lookType = 113},
}

local sendCooldowns

local function isOTC(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function getBossOutfit(lookType)
	local mt = MonsterType(lookType)
	if not mt then
		return {type = lookType or 0, head = 0, body = 0, legs = 0, feet = 0, addons = 0}
	end
	local outfit = mt:getOutfit()
	if not outfit then
		return {type = lookType or 0, head = 0, body = 0, legs = 0, feet = 0, addons = 0}
	end
	return {
		type = outfit.lookType or 0,
		head = outfit.lookHead or 0,
		body = outfit.lookBody or 0,
		legs = outfit.lookLegs or 0,
		feet = outfit.lookFeet or 0,
		addons = outfit.lookAddons or 0,
	}
end

sendCooldowns = function(player)
	if not player or not isOTC(player) then return false end
	if #BOSS_LIST == 0 then return false end

	local now = os.time()
	local out = NetworkMessage(player)
	out:addByte(OPCODE_BOSS_COOLDOWN)
	out:addByte(#BOSS_LIST)
	for _, boss in ipairs(BOSS_LIST) do
		local cooldownEnd = player:getStorageValue(boss.storage)
		local cooldownStamp = (cooldownEnd and cooldownEnd > 0 and cooldownEnd > now) and cooldownEnd or 0
		local outfit = getBossOutfit(boss.lookType)
		out:addU16(boss.id)
		out:addU32(cooldownStamp)
		out:addString(boss.name)
		out:addU16(outfit.type)
		out:addByte(outfit.head)
		out:addByte(outfit.body)
		out:addByte(outfit.legs)
		out:addByte(outfit.feet)
		out:addByte(outfit.addons)
	end
	return out:sendToPlayer(player)
end

-- Login event: send cooldowns on login
local bossLoginEvent = CreatureEvent("BossCooldownLogin")
function bossLoginEvent.onLogin(player)
	if not isOTC(player) then return true end
	addEvent(function(pid)
		local p = Player(pid)
		if p then sendCooldowns(p) end
	end, 3000, player:getId())
	return true
end
bossLoginEvent:register()

-- Periodic refresh: send every 60s to online OTC players
local bossRefreshEvent = GlobalEvent("BossCooldownPeriodic")
function bossRefreshEvent.onThink(interval)
	for _, player in ipairs(Game.getPlayers()) do
		if isOTC(player) then
			sendCooldowns(player)
		end
	end
	return true
end
bossRefreshEvent:interval(60000)
bossRefreshEvent:register()

BossCooldown = {
	send = sendCooldowns,
}
