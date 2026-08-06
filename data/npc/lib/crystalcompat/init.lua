-- Crystal Compat initialization
if _G.CrystalCompatLoaded then return end
_G.CrystalCompatLoaded = true

MESSAGE_GREET = MESSAGE_GREET or 1
MESSAGE_FAREWELL = MESSAGE_FAREWELL or 2
MESSAGE_WALKAWAY = MESSAGE_WALKAWAY or 16

if VOCATION and VOCATION.ID and not VOCATION.BASE_ID then
	VOCATION.BASE_ID = {
		SORCERER = VOCATION.ID.SORCERER,
		DRUID = VOCATION.ID.DRUID,
		PALADIN = VOCATION.ID.PALADIN,
		KNIGHT = VOCATION.ID.KNIGHT,
		MONK = VOCATION.ID.MONK,
	}
end

if Vocation and not Vocation.getBaseId then
	function Vocation.getBaseId(self)
		local base = self.getBase and self:getBase() or self
		return base and base:getId() or 0
	end
end

local function crystalStorageHash(path, base)
	local hash = base
	for i = 1, #path do
		hash = (hash * 131 + string.byte(path, i)) % 1000000000
	end
	return base + hash
end

local function crystalStorageKey(value)
	if type(value) == "table" then
		local key = rawget(value, "__crystalStorageKey")
		if key then
			return key
		end
	end
	return value
end

local function crystalStorageValue(value, defaultValue)
	if value ~= nil then
		return value
	end
	if defaultValue ~= nil then
		return defaultValue
	end
	return -1
end

local crystalStorageNodeMeta = {}
crystalStorageNodeMeta.__index = function(self, key)
	local path = rawget(self, "__crystalStoragePath") .. "." .. tostring(key)
	local base = rawget(self, "__crystalStorageBase")
	local node = {
		__crystalStorageBase = base,
		__crystalStorageKey = crystalStorageHash(path, base),
		__crystalStoragePath = path,
	}
	setmetatable(node, crystalStorageNodeMeta)
	rawset(self, key, node)
	return node
end
crystalStorageNodeMeta.__tostring = function(self)
	return tostring(rawget(self, "__crystalStorageKey"))
end

local function installCrystalStorageRoot(root, rootName, base)
	root = root or {}
	local oldMeta = getmetatable(root)
	local oldIndex = oldMeta and oldMeta.__index

	setmetatable(root, {
		__index = function(self, key)
			local oldValue
			if type(oldIndex) == "function" then
				oldValue = oldIndex(self, key)
			elseif type(oldIndex) == "table" then
				oldValue = oldIndex[key]
			end
			if oldValue ~= nil then
				return oldValue
			end

			local path = rootName .. "." .. tostring(key)
			local node = {
				__crystalStorageBase = base,
				__crystalStorageKey = crystalStorageHash(path, base),
				__crystalStoragePath = path,
			}
			setmetatable(node, crystalStorageNodeMeta)
			rawset(self, key, node)
			return node
		end,
	})

	return root
end

Storage = installCrystalStorageRoot(Storage, "Storage", 500000000)
storage = storage or Storage
GlobalStorage = installCrystalStorageRoot(GlobalStorage, "GlobalStorage", 1000000000)

if PlayerStorageKeys and PlayerStorageKeys.firstRod and not rawget(Storage, "FirstMageWeapon") then
	Storage.FirstMageWeapon = PlayerStorageKeys.firstRod
end

if Player and not Player.__crystalStorageCompat then
	Player.__crystalStorageCompat = true

	local originalGetStorageValue = Player.getStorageValue
	if originalGetStorageValue then
		function Player:getStorageValue(key, defaultValue)
			return crystalStorageValue(originalGetStorageValue(self, crystalStorageKey(key), defaultValue), defaultValue)
		end
	end

	local originalSetStorageValue = Player.setStorageValue
	if originalSetStorageValue then
		function Player:setStorageValue(key, value)
			return originalSetStorageValue(self, crystalStorageKey(key), value)
		end
	end

	local originalRemoveStorageValue = Player.removeStorageValue
	if originalRemoveStorageValue then
		function Player:removeStorageValue(key)
			return originalRemoveStorageValue(self, crystalStorageKey(key))
		end
	end
end

if Game and not Game.__crystalStorageCompat then
	Game.__crystalStorageCompat = true

	local originalGetStorageValue = Game.getStorageValue
	if originalGetStorageValue then
		function Game.getStorageValue(key, defaultValue)
			return crystalStorageValue(originalGetStorageValue(crystalStorageKey(key), defaultValue), defaultValue)
		end
	end

	local originalSetStorageValue = Game.setStorageValue
	if originalSetStorageValue then
		function Game.setStorageValue(key, value)
			return originalSetStorageValue(crystalStorageKey(key), value)
		end
	end
end

local originalGetGlobalStorageValue = getGlobalStorageValue
if originalGetGlobalStorageValue and not _G.__crystalGetGlobalStorageCompat then
	_G.__crystalGetGlobalStorageCompat = true
	function getGlobalStorageValue(key, defaultValue)
		return crystalStorageValue(originalGetGlobalStorageValue(crystalStorageKey(key), defaultValue), defaultValue)
	end
	getStorage = getGlobalStorageValue
end

local originalSetGlobalStorageValue = setGlobalStorageValue
if originalSetGlobalStorageValue and not _G.__crystalSetGlobalStorageCompat then
	_G.__crystalSetGlobalStorageCompat = true
	function setGlobalStorageValue(key, value)
		return originalSetGlobalStorageValue(crystalStorageKey(key), value)
	end
	doSetStorage = setGlobalStorageValue
end

if not _G.__crystalDofileCompat then
	_G.__crystalDofileCompat = true
	_G.__crystalOriginalDofile = dofile

	local function crystalFileExists(path)
		if not io or not io.open then
			return false
		end

		local file = io.open(path, "r")
		if file then
			file:close()
			return true
		end
		return false
	end

	local function crystalResolveNpcFile(path)
		if type(path) ~= "string" then
			return path
		end

		local prefix = "data/npc/"
		if path:sub(1, #prefix) ~= prefix or crystalFileExists(path) then
			return path
		end

		local relativePath = path:sub(#prefix + 1)
		local currentRoot = rawget(_G, "__npcCurrentScriptRoot")
		if currentRoot then
			local rooted = currentRoot .. "/" .. relativePath
			if crystalFileExists(rooted) then
				return rooted
			end
		end

		local scriptFiles = rawget(_G, "__npcScriptFilesByName")
		local indexedByPath = scriptFiles and scriptFiles[relativePath] or nil
		if indexedByPath and crystalFileExists(indexedByPath) then
			return indexedByPath
		end

		local filename = path:match("([^/\\]+)$")
		local currentDir = rawget(_G, "__npcCurrentScriptDir")
		if filename and currentDir then
			local sibling = currentDir .. "/" .. filename
			if crystalFileExists(sibling) then
				return sibling
			end
		end

		local indexedFile = filename and scriptFiles and scriptFiles[filename] or nil
		if indexedFile and crystalFileExists(indexedFile) then
			return indexedFile
		end

		return path
	end

	function dofile(path)
		return _G.__crystalOriginalDofile(crystalResolveNpcFile(path))
	end
end

dofile('data/npc/lib/crystalcompat/keywordhandler.lua')
dofile('data/npc/lib/crystalcompat/npchandler.lua')
dofile('data/npc/lib/crystalcompat/stdmodule.lua')
dofile('data/npc/lib/crystalcompat/focusmodule.lua')
dofile('data/npc/lib/crystalcompat/npctype.lua')
dofile('data/npc/lib/crystalcompat/bank.lua')

if KeywordHandler and NpcHandler then
	if not _G.OriginalKeywordHandlerNew then _G.OriginalKeywordHandlerNew = KeywordHandler.new end
	if not _G.OriginalNpcHandlerNew then _G.OriginalNpcHandlerNew = NpcHandler.new end

	function KeywordHandler:new(...)
		return CrystalKeywordHandler:new(...)
	end

	function NpcHandler:new(...)
		return CrystalNpcHandler:new(...)
	end
end
