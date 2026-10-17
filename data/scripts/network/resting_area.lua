local OPCODE_RESTING_AREA_STATE = 0xA9

local STREAK = {
	HP = 2,
	MP = 3,
	STAMINA = 4,
	DOUBLE_HP = 5,
	DOUBLE_MP = 6,
	SOUL = 7,
}

local FREE_MAX_STREAK = 3
local THINK_INTERVAL_MS = 1000
local SOUL_REGEN_INTERVAL_MS = 15 * 60 * 1000

RestingAreaSystem = RestingAreaSystem or {}

local regenTicks = {}

local function getStorageNumber(player, key, default)
	local value = tonumber(player:getStorageValue(key)) or -1
	if value < 0 then
		return default or 0
	end
	return value
end

local function getEffectiveStreak(player)
	local streak = getStorageNumber(player, PlayerStorageKeys.dailyRewardStreak, 0)
	if not player:isPremium() then
		return math.min(streak, FREE_MAX_STREAK)
	end
	return streak
end

local function buildRestingMessage(streak)
	if streak < STREAK.HP then
		return "Resting Area (no active bonus)"
	end

	local parts = {}
	if streak < STREAK.DOUBLE_HP then
		parts[#parts + 1] = "Hit Points Regeneration"
	else
		parts[#parts + 1] = "Double Hit Points Regeneration"
	end

	if streak >= STREAK.MP then
		if streak < STREAK.DOUBLE_MP then
			parts[#parts + 1] = "Mana Points Regeneration"
		else
			parts[#parts + 1] = "Double Mana Points Regeneration"
		end
	end

	if streak >= STREAK.STAMINA then
		parts[#parts + 1] = "Stamina Points Regeneration"
	end

	if streak >= STREAK.SOUL then
		parts[#parts + 1] = "Soul Points Regeneration"
	end

	return "Active Resting Area Bonuses: " .. table.concat(parts, ", ") .. "."
end

local function clearRegenTicks(playerId)
	regenTicks[playerId] = nil
end

local function getRegenRates(player, streak)
	local vocation = player:getVocation()
	if not vocation then
		return nil
	end

	local rates = {
		healthGain = 0,
		healthTicks = math.max(1, vocation:getHealthGainTicks()) * 1000,
		manaGain = 0,
		manaTicks = math.max(1, vocation:getManaGainTicks()) * 1000,
	}

	if streak >= STREAK.HP then
		rates.healthGain = vocation:getHealthGainAmount()
		if streak >= STREAK.DOUBLE_HP then
			rates.healthGain = rates.healthGain * 2
		end
	end

	if streak >= STREAK.MP then
		rates.manaGain = vocation:getManaGainAmount()
		if streak >= STREAK.DOUBLE_MP then
			rates.manaGain = rates.manaGain * 2
		end
	end

	return rates
end

local function processRestingRegeneration(player, interval)
	local playerId = player:getId()
	if not RestingAreaSystem.isRestingArea(player:getPosition()) then
		clearRegenTicks(playerId)
		return
	end

	local streak = getEffectiveStreak(player)
	if streak < STREAK.HP then
		clearRegenTicks(playerId)
		return
	end

	local rates = getRegenRates(player, streak)
	if not rates then
		return
	end

	local state = regenTicks[playerId] or { health = 0, mana = 0, soul = 0 }
	regenTicks[playerId] = state
	state.health = state.health + interval
	state.mana = state.mana + interval
	state.soul = state.soul + interval

	if rates.healthGain > 0 and state.health >= rates.healthTicks then
		state.health = state.health - rates.healthTicks
		if player:getHealth() < player:getMaxHealth() then
			player:addHealth(rates.healthGain)
		end
	end

	if rates.manaGain > 0 and state.mana >= rates.manaTicks then
		state.mana = state.mana - rates.manaTicks
		if player:getMana() < player:getMaxMana() then
			player:addMana(rates.manaGain)
		end
	end

	if streak >= STREAK.SOUL and player:isPremium() and state.soul >= SOUL_REGEN_INTERVAL_MS then
		state.soul = state.soul - SOUL_REGEN_INTERVAL_MS
		if player:getSoul() < player:getMaxSoul() then
			player:addSoul(1)
		end
	end
end

function RestingAreaSystem.isRestingArea(position)
	local tile = Tile(position)
	if not tile then
		return false
	end
	return tile:hasFlag(TILESTATE_PROTECTIONZONE)
end

function RestingAreaSystem.sendRestingStatus(player, inRestingArea)
	if not player or not player:isUsingOtClient() then
		return false
	end

	local protection = inRestingArea and 1 or 0
	local streak = getEffectiveStreak(player)
	local activeBonus = streak >= STREAK.HP and 1 or 0

	local out = NetworkMessage(player)
	out:addByte(OPCODE_RESTING_AREA_STATE)
	out:addByte(protection)
	out:addByte(activeBonus)
	if protection == 1 then
		out:addString(buildRestingMessage(streak))
	else
		out:addString("")
	end
	return out:sendToPlayer(player)
end

function RestingAreaSystem.getEffectiveStreak(player)
	if not player then
		return 0
	end
	return getEffectiveStreak(player)
end

function RestingAreaSystem.getMessage(player)
	if not player then
		return ""
	end
	return buildRestingMessage(getEffectiveStreak(player))
end

function RestingAreaSystem.refresh(player)
	if not player then
		return false
	end
	return RestingAreaSystem.sendRestingStatus(player, RestingAreaSystem.isRestingArea(player:getPosition()))
end

local restingAreaLogin = CreatureEvent("RestingAreaLogin")
function restingAreaLogin.onLogin(player)
	addEvent(function(playerId)
		local onlinePlayer = Player(playerId)
		if onlinePlayer then
			RestingAreaSystem.refresh(onlinePlayer)
		end
	end, 500, player:getId())
	return true
end
restingAreaLogin:register()

local restingAreaLogout = CreatureEvent("RestingAreaLogout")
function restingAreaLogout.onLogout(player)
	clearRegenTicks(player:getId())
	return true
end
restingAreaLogout:register()

local restingAreaRegeneration = GlobalEvent("RestingAreaRegeneration")
function restingAreaRegeneration.onThink(interval)
	for _, player in ipairs(Game.getPlayers()) do
		processRestingRegeneration(player, interval)
	end
	return true
end
restingAreaRegeneration:interval(THINK_INTERVAL_MS)
restingAreaRegeneration:register()
