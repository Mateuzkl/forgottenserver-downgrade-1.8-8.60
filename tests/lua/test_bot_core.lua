-- Pure-function coverage for BotCore. Runs under a bare Lua 5.5 interpreter
-- from the repo root: lua tests/lua/test_bot_core.lua
local H = dofile("tests/lua/harness.lua")
local BotCore = H.loadCore()

-- trim / splitList
H.eq(BotCore.trim("  hello  "), "hello", "trim strips both ends")
H.eq(BotCore.trim(nil), "", "trim tolerates nil")
H.eq(BotCore.trim(42), "42", "trim stringifies input")

local parts = BotCore.splitList(" a , b ,c ")
H.eq(#parts, 3, "splitList count")
H.eq(parts[1], "a", "splitList trims first")
H.eq(parts[2], "b", "splitList trims middle")
H.eq(parts[3], "c", "splitList trims last")
H.eq(#BotCore.splitList(""), 0, "splitList empty input")

-- parseToggle
for _, on in ipairs({ "on", "true", "1", "yes", "auto", "ON", "Yes" }) do
	H.check(BotCore.parseToggle(on), "parseToggle accepts " .. on)
end
for _, off in ipairs({ "off", "false", "0", "no", "", nil, "banana" }) do
	H.check(not BotCore.parseToggle(off), "parseToggle rejects " .. tostring(off))
end

-- parseCommand
local action, args = BotCore.parseCommand("")
H.eq(action, "", "parseCommand empty action")
H.eq(#args, 0, "parseCommand empty args")

action, args = BotCore.parseCommand("  ADD  Test Bot, auto, ek ")
H.eq(action, "add", "parseCommand lowercases action")
H.eq(#args, 3, "parseCommand arg count")
H.eq(args[1], "Test Bot", "parseCommand keeps spaces inside names")
H.eq(args[2], "auto", "parseCommand second arg")
H.eq(args[3], "ek", "parseCommand third arg")

action, args = BotCore.parseCommand("list")
H.eq(action, "list", "parseCommand action only")
H.eq(#args, 0, "parseCommand action-only args")

-- normalizeVocation
H.eq(BotCore.normalizeVocation("ek"), 4, "normalizeVocation ek")
H.eq(BotCore.normalizeVocation("ms"), 1, "normalizeVocation ms")
H.eq(BotCore.normalizeVocation("ed"), 2, "normalizeVocation ed")
H.eq(BotCore.normalizeVocation("rp"), 3, "normalizeVocation rp")
H.eq(BotCore.normalizeVocation("Sorcerer"), 1, "normalizeVocation name case-insensitive")
H.eq(BotCore.normalizeVocation("7"), 3, "normalizeVocation folds promoted numeric")
H.eq(BotCore.normalizeVocation("2"), 2, "normalizeVocation base numeric")
H.eq(BotCore.normalizeVocation(""), 4, "normalizeVocation empty defaults to knight")
H.eq(BotCore.normalizeVocation("garbage"), 4, "normalizeVocation garbage defaults to knight")
H.eq(BotCore.normalizeVocation("99"), 4, "normalizeVocation out-of-range defaults to knight")

-- vocationBase
H.eq(BotCore.vocationBase(8), 4, "vocationBase folds 8 to 4")
H.eq(BotCore.vocationBase(5), 1, "vocationBase folds 5 to 1")
H.eq(BotCore.vocationBase(3), 3, "vocationBase keeps base ids")
H.eq(BotCore.vocationBase(0), 4, "vocationBase invalid defaults to 4")
H.eq(BotCore.vocationBase(nil), 4, "vocationBase nil defaults to 4")

-- chebyshevDistance
H.eq(BotCore.chebyshevDistance({ x = 0, y = 0, z = 7 }, { x = 3, y = -2, z = 7 }), 3, "chebyshev max axis")
H.eq(BotCore.chebyshevDistance({ x = 0, y = 0, z = 7 }, { x = 1, y = 1, z = 8 }), math.huge, "z mismatch is huge")
H.eq(BotCore.chebyshevDistance(nil, { x = 0, y = 0, z = 0 }), math.huge, "nil origin is huge")
H.eq(BotCore.chebyshevDistance({ x = 5, y = 5, z = 1 }, { x = 5, y = 5, z = 1 }), 0, "same position")

-- selectTarget
local origin = { x = 10, y = 10, z = 7 }
local target, distance = BotCore.selectTarget(origin, {
	{ index = "far", position = { x = 16, y = 10, z = 7 } },
	{ index = "near", position = { x = 11, y = 11, z = 7 } },
	{ index = "wrongz", position = { x = 10, y = 10, z = 6 } }
})
H.eq(target and target.index, "near", "selectTarget picks closest")
H.eq(distance, 1, "selectTarget distance")
H.eq(BotCore.selectTarget(origin, {}), nil, "selectTarget empty candidates")
H.eq(BotCore.selectTarget(origin, { { index = 1, position = { x = 0, y = 0, z = 1 } } }), nil,
	"selectTarget all unreachable")

-- computeHeal
local cfg = { thresholdPercent = 65, restorePercent = 15, restoreMin = 25 }
H.eq(BotCore.computeHeal(100, 100, cfg), nil, "computeHeal full health")
H.eq(BotCore.computeHeal(70, 100, cfg), nil, "computeHeal above threshold")
H.eq(BotCore.computeHeal(65, 100, cfg), 25, "computeHeal restoreMin floor")
H.eq(BotCore.computeHeal(50, 1000, cfg), 150, "computeHeal restorePercent")
H.eq(BotCore.computeHeal(999, 1000, cfg), nil, "computeHeal missing clamped by threshold first")
H.eq(BotCore.computeHeal(640, 1000, cfg), 150, "computeHeal below threshold restores percent")
H.eq(BotCore.computeHeal(5, 12, cfg), 7, "computeHeal clamps to missing")
H.eq(BotCore.computeHeal(5, 0, cfg), nil, "computeHeal zero max")
H.eq(BotCore.computeHeal(5, 100, nil), nil, "computeHeal nil cfg")

-- pickWanderOffset
local rngZero = H.makeRng({ 0, 0, 2 })
local dx, dy = BotCore.pickWanderOffset(5, rngZero)
H.check(not (dx == 0 and dy == 0), "pickWanderOffset never (0,0)")
H.eq(dx, 1, "pickWanderOffset fallback east dx")
H.eq(dy, 0, "pickWanderOffset fallback east dy")

for i = 1, 50 do
	local ox, oy = BotCore.pickWanderOffset(3)
	H.check(math.abs(ox) <= 3 and math.abs(oy) <= 3, "pickWanderOffset within radius (iter " .. i .. ")")
	H.check(not (ox == 0 and oy == 0), "pickWanderOffset non-zero (iter " .. i .. ")")
end

-- equipmentFor
local knightSet = BotCore.equipmentFor(4)
H.eq(#knightSet, 6, "knight equipment count (4 common + 2 weapon/shield)")
local paladinSet = BotCore.equipmentFor(3)
H.eq(#paladinSet, 6, "paladin equipment count")
local sorcSet = BotCore.equipmentFor(1)
H.eq(#sorcSet, 5, "sorcerer equipment count")
local fallbackSet = BotCore.equipmentFor(99)
H.eq(#fallbackSet, 6, "unknown vocation falls back to knight set")
for _, entry in ipairs(knightSet) do
	H.check(type(entry.slot) == "string", "equipment slots are symbolic names")
	H.check(type(entry.itemId) == "number", "equipment ids are numbers")
end

-- phrases and delays
local phrase = BotCore.pickPhrase(H.makeRng({ 1 }))
H.eq(phrase, "hi", "pickPhrase deterministic with rng")
H.check(BotCore.nextSayDelay(H.makeRng({ 100 })) == 100, "nextSayDelay respects rng")
H.check(BotCore.firstSayDelay(H.makeRng({ 50 })) == 50, "firstSayDelay respects rng")

-- mergeDefaults
local merged = BotCore.mergeDefaults({ tickInterval = 250, heal = { enabled = false } }, BotCore.defaults)
H.eq(merged.tickInterval, 250, "mergeDefaults keeps overrides")
H.eq(merged.heal.enabled, false, "mergeDefaults keeps nested overrides")
H.eq(merged.heal.health.thresholdPercent, 65, "mergeDefaults fills nested defaults")
H.eq(merged.wanderRadius, 5, "mergeDefaults fills top-level defaults")

H.finish("test_bot_core")
