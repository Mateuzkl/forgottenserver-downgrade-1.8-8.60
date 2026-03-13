-- ============================================================
-- Monk Harmony System — Central Library
-- ============================================================
-- Harmony: points 0-5, accumulated by Builder Spells, consumed by Spender Spells
-- Virtues: modifies bonus (Harmony, Justice, Sustain)
-- Serene: state that doubles Virtue effects
--
-- C++ native methods available on Player:
--   getHarmony(), setHarmony(v), addHarmony(v), removeHarmony(v)
--   isSerene(), setSerene(bool), getSereneCooldown(), setSereneCooldown(ms)
--   getVirtue(), setVirtue(v)
--   clearSpellCooldowns()
-- ============================================================

-- === VIRTUE CONSTANTS (must match VirtueMonk_t enum in player.h) ===
VIRTUE_NONE    = 0
VIRTUE_HARMONY = 1
VIRTUE_JUSTICE = 2
VIRTUE_SUSTAIN = 3

-- === HARMONY CONFIG ===
HARMONY_MAX = 5

-- Default Harmony bonus per point
local HARMONY_BONUS_TABLE = {
	[0] = 0,
	[1] = 15,
	[2] = 30,
	[3] = 60,
	[4] = 120,
	[5] = 240,
}

-- Enhanced bonus with Virtue of Harmony active
local HARMONY_BONUS_VIRTUE_TABLE = {
	[0] = 0,
	[1] = 25,
	[2] = 55,
	[3] = 100,
	[4] = 200,
	[5] = 400,
}

-- Virtue names
local VIRTUE_NAMES = {
	[VIRTUE_NONE]    = "None",
	[VIRTUE_HARMONY] = "Virtue of Harmony",
	[VIRTUE_JUSTICE] = "Virtue of Justice",
	[VIRTUE_SUSTAIN] = "Virtue of Sustain",
}

-- ============================================================
-- getHarmonyPoints(player)
-- Returns current points clamped 0-5 (wrapper for global use)
-- ============================================================
function getHarmonyPoints(player)
	return math.max(0, math.min(player:getHarmony(), HARMONY_MAX))
end

-- ============================================================
-- addHarmonyPoint(player)
-- Adds +1 point, capped at HARMONY_MAX. Triggers notification at 5.
-- Returns true if added, false if already at max.
-- ============================================================
function addHarmonyPoint(player)
	local current = player:getHarmony()
	if current >= HARMONY_MAX then
		return false
	end

	player:addHarmony(1)
	syncHarmonyOpcode(player)

	if player:getHarmony() >= HARMONY_MAX then
		onHarmonyFull(player)
	end
	return true
end

-- ============================================================
-- spendHarmony(player)
-- Spends ALL accumulated Harmony and returns points, bonus (%).
-- ============================================================
function spendHarmony(player)
	local points = getHarmonyPoints(player)
	local bonus = getHarmonyBonus(player)
	player:setHarmony(0)
	syncHarmonyOpcode(player)
	return points, bonus
end

-- ============================================================
-- getHarmonyBonus(player)
-- Returns current bonus in %, considering Virtue and Serene.
-- ============================================================
function getHarmonyBonus(player)
	local points = getHarmonyPoints(player)
	local virtue = player:getVirtue()
	local serene = player:isSerene()

	local bonus
	if virtue == VIRTUE_HARMONY then
		bonus = HARMONY_BONUS_VIRTUE_TABLE[points] or 0
	else
		bonus = HARMONY_BONUS_TABLE[points] or 0
	end

	-- Serene doubles Virtue effects
	if serene and virtue ~= VIRTUE_NONE then
		bonus = bonus * 2
	end

	return bonus
end

-- ============================================================
-- getVirtueName(player)
-- Returns the human-readable name of the player's active virtue.
-- ============================================================
function getVirtueName(player)
	local v = player:getVirtue()
	return VIRTUE_NAMES[v] or "None"
end

-- ============================================================
-- onHarmonyFull(player)
-- Triggered when reaching 5/5 Harmony points.
-- ============================================================
function onHarmonyFull(player)
	player:sendTextMessage(MESSAGE_STATUS_CONSOLE_ORANGE,
		"[Harmony] You have reached full Harmony! (5/5) Your abilities are at peak power.")
	player:getPosition():sendMagicEffect(CONST_ME_HOLYAREA)
end

-- ============================================================
-- syncHarmonyOpcode(player)
-- Sends harmony points to the OTCv8 client.
-- ============================================================
local HARMONY_OPCODE = 130

function syncHarmonyOpcode(player)
	if player.sendExtendedOpcode then
		player:sendExtendedOpcode(HARMONY_OPCODE, tostring(player:getHarmony()))
	end
end
