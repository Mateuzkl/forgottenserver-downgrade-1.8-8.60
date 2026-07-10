-- Self-sufficiency proof: every bot_*.lua lib must load under a bare Lua 5.5
-- interpreter with zero engine globals stubbed. This guarantees no file-scope
-- engine reads and no load-order dependency between the libs.
-- Run from the repo root: lua tests/lua/test_lib_load.lua
local H = dofile("tests/lua/harness.lua")

local libs = {
	{ path = "data/scripts/lib/bot_core.lua", global = "BotCore" },
	{ path = "data/scripts/lib/bot_brain.lua", global = "BotBrain" },
	{ path = "data/scripts/lib/bot_system.lua", global = "BotSystem" }
}

for _, lib in ipairs(libs) do
	local chunk, loadError = loadfile(lib.path)
	H.check(chunk ~= nil, lib.path .. " parses: " .. tostring(loadError))
	if chunk then
		local ok, runError = pcall(chunk)
		H.check(ok, lib.path .. " loads without engine globals: " .. tostring(runError))
		H.check(_G[lib.global] ~= nil, lib.path .. " defines " .. lib.global)
	end
end

-- BotBrain lifecycle guards must not touch engine APIs when the system is
-- unavailable: stop() with no pending event and activate(nil) must be safe.
H.check(BotBrain.stop ~= nil, "BotBrain.stop exists")
BotBrain.stop()
H.eq(BotBrain.running, false, "BotBrain.stop clears running")
H.eq(BotBrain.activate(nil), false, "BotBrain.activate(nil) is rejected")

-- BotSystem must degrade gracefully without the engine: Game is undefined
-- here, so availability must be false and guarded calls must return an error
-- string instead of raising.
Game = Game or {}
H.eq(BotSystem.isAvailable(), false, "BotSystem unavailable without bindings")
H.eq(BotSystem.isEnabled(), false, "BotSystem disabled without bindings")
local ok, message = BotSystem.spawn("Test")
H.eq(ok, false, "BotSystem.spawn fails gracefully without bindings")
H.check(type(message) == "string" and message:find("BotManager"), "spawn failure names the missing binding")
ok, message = BotSystem.register("Test")
H.eq(ok, false, "BotSystem.register fails gracefully without bindings")
H.eq(BotSystem.spawnAuto(), 0, "BotSystem.spawnAuto returns 0 without bindings")

H.finish("test_lib_load")
