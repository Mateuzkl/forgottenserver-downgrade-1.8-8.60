-- BotCore: pure decision logic for the managed bot system.
-- This file must stay loadable under a bare Lua 5.5 interpreter: no engine
-- globals may be read at file scope or inside these functions, so the logic
-- is unit-testable without the server (see tests/lua/).
BotCore = BotCore or {}

BotCore.defaults = {
	tickInterval = 500,
	idleMoveInterval = 850,
	combatMoveInterval = 700,
	searchRangeX = 7,
	searchRangeY = 5,
	wanderRadius = 5,
	heal = {
		enabled = true,
		health = { thresholdPercent = 65, restorePercent = 15, restoreMin = 25 },
		mana = { thresholdPercent = 50, restorePercent = 20, restoreMin = 20 }
	}
}

-- Starter gear uses symbolic slot names; the engine adapter (bot_brain.lua)
-- maps them to CONST_SLOT_* at call time.
local starterEquipment = {
	common = {
		{ slot = "backpack", itemId = 2854 },
		{ slot = "armor", itemId = 3359 },
		{ slot = "legs", itemId = 3372 },
		{ slot = "feet", itemId = 3552 }
	},
	vocations = {
		[1] = {
			{ slot = "left", itemId = 3074 }
		},
		[2] = {
			{ slot = "left", itemId = 3066 }
		},
		[3] = {
			{ slot = "left", itemId = 3277 },
			{ slot = "right", itemId = 3411 }
		},
		[4] = {
			{ slot = "left", itemId = 3271 },
			{ slot = "right", itemId = 3411 }
		}
	}
}

local idlePhrases = { "hi", "hunt?", "need cap", "refill soon", "exura" }

local vocationNames = {
	sorcerer = 1,
	druid = 2,
	paladin = 3,
	knight = 4,
	ms = 1,
	ed = 2,
	rp = 3,
	ek = 4
}

function BotCore.trim(value)
	value = tostring(value or "")
	return (value:gsub("^%s+", ""):gsub("%s+$", ""))
end

function BotCore.splitList(value, separator)
	separator = separator or ","
	local parts = {}
	for part in tostring(value or ""):gmatch("([^" .. separator .. "]+)") do
		parts[#parts + 1] = BotCore.trim(part)
	end
	return parts
end

function BotCore.parseToggle(value)
	value = tostring(value or ""):lower()
	return value == "on" or value == "true" or value == "1" or value == "yes" or value == "auto"
end

-- Splits "/bot <action> <rest>" params into a lowercased action plus the
-- comma-separated argument list.
function BotCore.parseCommand(param)
	param = BotCore.trim(param)
	if param == "" then
		return "", {}
	end

	local action, rest = param:match("^(%S+)%s*(.*)$")
	return (action or ""):lower(), BotCore.splitList(rest or "")
end

-- Accepts numeric ids (5-8 fold to their 1-4 base) and vocation names or
-- abbreviations; anything unrecognized falls back to knight (4).
function BotCore.normalizeVocation(value)
	value = tostring(value or ""):lower()
	if value == "" then
		return 4
	end

	local numeric = tonumber(value)
	if numeric then
		numeric = math.floor(numeric)
		if numeric > 4 then
			numeric = numeric - 4
		end
		if numeric >= 1 and numeric <= 4 then
			return numeric
		end
		return 4
	end

	return vocationNames[value] or 4
end

-- Folds a raw vocation id (possibly promoted, 5-8) to the 1-4 base used for
-- equipment lookup.
function BotCore.vocationBase(id)
	id = tonumber(id) or 4
	if id > 4 then
		id = id - 4
	end
	if id < 1 or id > 4 then
		return 4
	end
	return id
end

function BotCore.chebyshevDistance(a, b)
	if not a or not b or a.z ~= b.z then
		return math.huge
	end
	return math.max(math.abs(a.x - b.x), math.abs(a.y - b.y))
end

-- candidates: array of { index = <any>, position = {x, y, z} }. Returns the
-- closest record and its distance, or nil when nothing is reachable.
function BotCore.selectTarget(origin, candidates)
	local closest, closestDistance
	for _, candidate in ipairs(candidates or {}) do
		local distance = BotCore.chebyshevDistance(origin, candidate.position)
		if distance < math.huge and (not closestDistance or distance < closestDistance) then
			closest = candidate
			closestDistance = distance
		end
	end
	return closest, closestDistance
end

-- Returns the amount to restore (clamped to what is missing), or nil when the
-- current value is above the threshold. cfg = { thresholdPercent,
-- restorePercent, restoreMin }.
function BotCore.computeHeal(current, max, cfg)
	if not cfg or max <= 0 then
		return nil
	end

	local missing = max - current
	if missing <= 0 or current * 100 > max * cfg.thresholdPercent then
		return nil
	end

	local restore = math.max(cfg.restoreMin, math.floor(max * cfg.restorePercent / 100))
	return math.min(missing, restore)
end

-- Never returns (0, 0); rng is injectable for deterministic tests and must
-- follow math.random(lo, hi) semantics.
function BotCore.pickWanderOffset(radius, rng)
	rng = rng or math.random
	local dx = rng(-radius, radius)
	local dy = rng(-radius, radius)
	if dx == 0 and dy == 0 then
		local fallbacks = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } }
		local pick = fallbacks[rng(1, #fallbacks)]
		dx, dy = pick[1], pick[2]
	end
	return dx, dy
end

function BotCore.equipmentFor(vocationBase)
	local set = {}
	for _, entry in ipairs(starterEquipment.common) do
		set[#set + 1] = entry
	end
	for _, entry in ipairs(starterEquipment.vocations[vocationBase] or starterEquipment.vocations[4]) do
		set[#set + 1] = entry
	end
	return set
end

function BotCore.pickPhrase(rng)
	rng = rng or math.random
	return idlePhrases[rng(1, #idlePhrases)]
end

function BotCore.nextSayDelay(rng)
	rng = rng or math.random
	return rng(90, 240)
end

function BotCore.firstSayDelay(rng)
	rng = rng or math.random
	return rng(40, 120)
end

-- Shallow-recursive fill-in of missing keys so server owners can pre-seed
-- partial configs from any script.
function BotCore.mergeDefaults(target, defaults)
	target = target or {}
	for key, value in pairs(defaults) do
		if type(value) == "table" then
			target[key] = BotCore.mergeDefaults(target[key], value)
		elseif target[key] == nil then
			target[key] = value
		end
	end
	return target
end
