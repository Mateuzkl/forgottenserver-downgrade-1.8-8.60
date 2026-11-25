local root = arg[1] or "."
local input = assert(io.open(root .. "/data/scripts/network/cyclopedia/ciclopedia.lua", "rb"))
local source = input:read("*a")
input:close()
local first = assert(source:find("local function loadKillMap(", 1, true))
local last = assert(source:find("local function loadCharmMap(", first, true))
local cache, calls, rebuilt, freed = {}, 0, 0, 0
local environment = setmetatable({
    Game = {}, killCache = cache,
    isBestiaryEnabled = function() return true end,
    rebuildEarnedPoints = function(guid, kills)
        assert(guid == 42 and kills[7] == 123)
        rebuilt = rebuilt + 1
    end,
    db = {storeQuery = function()
        calls = calls + 1
        return calls >= 3 and 1 or false
    end},
    result = {
        getDataInt = function(_, column) return column == "raceid" and 7 or 123 end,
        next = function() return false end,
        free = function() freed = freed + 1 end
    }
}, {__index = _G})
local loadKills = assert(load(source:sub(first, last - 1) .. "\nreturn loadKillMap", "kill cache", "t", environment))()
for _ = 1, 2 do
    assert(next(loadKills(42)) == nil)
    assert(cache[42] == nil and rebuilt == 0, "failed/empty query polluted kill or derived caches")
end
assert(loadKills(42)[7] == 123 and calls == 3 and freed == 1 and rebuilt == 1)
assert(loadKills(42) == cache[42] and calls == 3, "successful kills were not cached")
environment.Game.getBestiaryKills = function(guid) assert(guid == 42); return {[7] = 456} end
assert(loadKills(42)[7] == 456 and calls == 3, "native authoritative kills were bypassed")
print("PASS: Bestiary retries failed/empty SQL reads, caches successful reads and preserves native authority")
