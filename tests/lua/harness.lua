-- Minimal test harness for the standalone (engine-free) Lua tests.
-- Usage: dofile("tests/lua/harness.lua") from the repo root.
local H = {}

H.failures = 0
H.checks = 0

function H.check(condition, message)
	H.checks = H.checks + 1
	if not condition then
		H.failures = H.failures + 1
		io.stderr:write("[FAIL] " .. (message or "check failed") .. "\n")
	end
end

function H.eq(actual, expected, message)
	H.check(actual == expected,
		(message or "eq") .. ": expected " .. tostring(expected) .. ", got " .. tostring(actual))
end

function H.finish(name)
	if H.failures > 0 then
		io.stderr:write(string.format("%s: %d/%d check(s) failed\n", name, H.failures, H.checks))
		os.exit(1)
	end
	print(string.format("%s: %d check(s) passed", name, H.checks))
end

-- Deterministic rng following math.random(lo, hi) semantics: consumes the
-- given sequence of values and clamps them into the requested range.
function H.makeRng(sequence)
	local index = 0
	return function(lo, hi)
		index = index + 1
		local value = sequence[((index - 1) % #sequence) + 1]
		if value < lo then
			return lo
		end
		if value > hi then
			return hi
		end
		return value
	end
end

function H.loadCore()
	dofile("data/scripts/lib/bot_core.lua")
	assert(BotCore, "bot_core.lua must define the BotCore global")
	return BotCore
end

return H
