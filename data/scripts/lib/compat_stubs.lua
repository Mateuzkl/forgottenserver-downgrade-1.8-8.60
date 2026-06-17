-- Compatibility stubs for crystalserver systems used by quest scripts

BossLever = setmetatable({}, {
	__call = function(self, config)
		local bossName = config.boss and config.boss.name or "unknown"
		return setmetatable({ name = bossName:lower(), config = config }, { __index = BossLever })
	end,
	__index = function(self, key)
		return function(...)
			local args = {...}
			if key == "aid" then local a = Action(); a:aid(args[1]); a:register()
			elseif key == "uid" then local a = Action(); a:uid(args[1]); a:register()
			end
			return self
		end
	end
})

SimpleTeleport = function(config)
	local tp = MoveEvent(); tp:type("stepin"); tp:position(config.position or config.fromPos); tp:register(); return tp
end

if not Set then
	local SM = { union = function(s,o) for k,v in pairs(o or {}) do s[k]=v end; return s end }
	SM.__index = SM
	Set = setmetatable({}, { __call = function(_,t,opts)
		local s = {}
		if t then for _,v in ipairs(t) do s[opts and opts.insensitive and type(v)=="string" and v:lower() or v] = true end end
		return setmetatable(s, SM)
	end})
	Set.new = function(t) return Set(t) end
end

if not toKey then function toKey(s) return s:gsub("%s+","_"):lower() end end
if not ParseDuration then function ParseDuration(d) return tonumber(d) or 0 end end

Hazard = { new = function(c) return setmetatable({config=c}, {__index=Hazard}) end }
Encounter = setmetatable({}, { __call = function(...) return setmetatable({}, {__index=Encounter}) end })
EventCallback = setmetatable({}, { __call = function(...) return setmetatable({}, {__index=EventCallback}) end })

Zone = {
	getByName = function() return nil end,
	new = function(c) return setmetatable({}, {__index=Zone}) end,
	addRemoveMonsters = function() end,
	setSpawnPosition = function() end,
}

if not string.removeAllSpaces then function string.removeAllSpaces(s) return s:gsub("%s+","") end end

SoulWarQuest = {}
sixthSealTable = {}
if not getTibiaTimerDayOrNight then function getTibiaTimerDayOrNight() return 1 end end

if not configManager then configManager = { getNumber=function() return 0 end, getString=function() return "" end, getBoolean=function() return false end } end
if not logger then logger = { info=function() end, warn=function() end, error=function() end, debug=function() end } end

print(">> [CompatStubs] Loaded crystalserver compat stubs.")

-- Make Zone callable (scripts use Zone(name) or Zone.new(config))
Zone = setmetatable({
	getByName = function(name) return nil end,
	new = function(config) return setmetatable({}, {__index=Zone}) end,
	addRemoveMonsters = function() end,
	setSpawnPosition = function() end,
}, {
	__call = function(self, ...) return setmetatable({}, {__index=Zone}) end,
	__index = Zone,
})

-- Make EventCallback callable
EventCallback = setmetatable({}, {
	__call = function(...) return setmetatable({}, {__index=EventCallback}) end,
	__index = EventCallback,
})

-- tasks stub (killing in the name of quest)
if not tasks then
	tasks = {
		GrizzlyAdams = {},
		DanielSteelsoul = {},
		Budrik = {},
		RaymondStriker = {},
	}
end

print(">> [CompatStubs] Loaded additional stubs (Zone, EventCallback, tasks)")

-- Rarity stub
Rarity = { roll = function() return { rarity = 0, name = "Common" } end }

-- Titles stub
Titles = {}

-- getNpcCid stub (crystalserver NPC function)
function getNpcCid() return 0 end

-- loadLuaMapAction/loadLuaMapSign stubs (startup table functions, available in scripts context)
if not loadLuaMapAction then function loadLuaMapAction() end end
if not loadLuaMapSign then function loadLuaMapSign() end end

print(">> [CompatStubs] Final stubs loaded (Rarity, Titles, getNpcCid, loadLuaMap*)")
if not loadLuaMapBookDocument then function loadLuaMapBookDocument() end end
if not loadLuaMapUnique then function loadLuaMapUnique() end end

-- CreateMapItem stub
function CreateMapItem() end

-- Comprehensive Zone stub with all methods quests might call
Zone = setmetatable({
	getByName = function() return nil end,
	new = function(c) return setmetatable({}, {__index=Zone}) end,
	addRemoveMonsters = function() end,
	setSpawnPosition = function() end,
	contains = function() return false end,
	addArea = function() end,
	iter = function() return function() end end,
	groupType = function() return 0 end,
	countPlayers = function() return 0 end,
	blockFamiliars = function() end,
	addSpawnMonsters = function() end,
}, {
	__call = function(...) return setmetatable({}, {__index=Zone}) end,
	__index = Zone,
})

-- EventCallback - make properly callable
EventCallback = setmetatable({}, {
	__call = function(...) return setmetatable({}, {__index=EventCallback}) end,
	__index = EventCallback,
})

print(">> [CompatStubs] Zone methods + CreateMapItem loaded")
function updateKeysStorage() end
