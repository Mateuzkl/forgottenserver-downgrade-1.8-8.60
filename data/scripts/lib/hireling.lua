HIRELINGS = HIRELINGS or {}
PLAYER_HIRELINGS = PLAYER_HIRELINGS or {}
HIRELING_OUTFIT_CHANGING = HIRELING_OUTFIT_CHANGING or {}

HIRELING_LAMP = 29432
HIRELING_MAX_PER_PLAYER = 10

HIRELING_SKILLS = {
	BANKER = { 1001, "banker" },
	COOKING = { 1002, "cooker" },
	STEWARD = { 1003, "steward" },
	TRADER = { 1004, "trader" },
}

HIRELING_OUTFITS = {
	BANKER = { 2001, "banker" },
	COOKING = { 2002, "cooker" },
	STEWARD = { 2003, "steward" },
	TRADER = { 2004, "trader" },
	SERVANT = { 2005, "servant" },
	HYDRA = { 2006, "hydra" },
	FERUMBRAS = { 2007, "ferumbras" },
	BONELORD = { 2008, "bonelord" },
	DRAGON = { 2009, "dragon" },
}

HIRELING_OUTFIT_ORDER = {
	"BANKER",
	"TRADER",
	"COOKING",
	"STEWARD",
	"SERVANT",
	"HYDRA",
	"FERUMBRAS",
	"BONELORD",
	"DRAGON",
}

HIRELING_SEX = {
	FEMALE = PLAYERSEX_FEMALE or 0,
	MALE = PLAYERSEX_MALE or 1,
}

HIRELING_OUTFIT_DEFAULT = { name = "Citizen", female = 1107, male = 1108 }

HIRELING_OUTFITS_TABLE = {
	BANKER = { name = "Banker Dress", female = 1109, male = 1110 },
	BONELORD = { name = "Bonelord Dress", female = 1123, male = 1124 },
	COOKING = { name = "Cook Dress", female = 1113, male = 1114 },
	DRAGON = { name = "Dragon Dress", female = 1125, male = 1126 },
	FERUMBRAS = { name = "Ferumbras Dress", female = 1131, male = 1132 },
	HYDRA = { name = "Hydra Dress", female = 1129, male = 1130 },
	SERVANT = { name = "Servant Dress", female = 1117, male = 1118 },
	STEWARD = { name = "Steward Dress", female = 1115, male = 1116 },
	TRADER = { name = "Trader Dress", female = 1111, male = 1112 },
}

HIRELING_OUTFIT_STORE_OFFERS = {
	banker = 12001,
	trader = 12002,
	cooker = 12003,
	steward = 12004,
	servant = 12005,
	hydra = 12006,
	ferumbras = 12007,
	bonelord = 12008,
	dragon = 12009,
}

HIRELING_FOODS_BOOST = {
	MAGIC = 29410,
	MELEE = 29411,
	SHIELDING = 29408,
	DISTANCE = 29409,
}

HIRELING_FOODS_IDS = {
	29412,
	29413,
	29414,
	29415,
	29416,
}

local function hirelingSystemEnabled()
	return configManager and configKeys and configKeys.HIRELING_SYSTEM_ENABLED and
		configManager.getBoolean(configKeys.HIRELING_SYSTEM_ENABLED)
end

local function astraHirelingProtocolEnabled(player)
	return hirelingSystemEnabled() and configKeys.ASTRA_HIRELING_PROTOCOL_ENABLED and
		configManager.getBoolean(configKeys.ASTRA_HIRELING_PROTOCOL_ENABLED) and
		player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function logWarning(message)
	if logger and logger.warn then
		logger.warn(message)
	else
		print(message)
	end
end

local function logError(message)
	if logger and logger.error then
		logger.error(message)
	else
		print(message)
	end
end

local function makeHirelingLampDescription(hireling)
	return "This mysterious lamp summons your very own personal hireling.\nThis item cannot be traded.\nThis magic lamp is the home of " ..
		hireling:getName() .. "."
end

local function isValidHirelingName(name)
	name = tostring(name or ""):gsub("^%s*(.-)%s*$", "%1")
	if #name < 3 or #name > 20 or name:find("  ", 1, true) then
		return false
	end

	for i = 1, #name do
		local char = name:sub(i, i)
		if not char:match("[A-Za-z ]") then
			return false
		end
	end
	return true
end

local function formatHirelingName(name)
	name = tostring(name or ""):gsub("^%s*(.-)%s*$", "%1"):lower()
	return (name:gsub("(%a)([%w']*)", function(first, rest)
		return first:upper() .. rest:lower()
	end))
end

local function getOwnerPlayer(ownerGuid)
	local player = Player(ownerGuid)
	if player then
		return player, false
	end

	if OfflinePlayer then
		local ok, offlinePlayer = pcall(OfflinePlayer, ownerGuid)
		if ok and offlinePlayer then
			return offlinePlayer, true
		end
	end
	return nil, false
end

local function releaseOwnerPlayer(player, isOffline)
	if isOffline and player and player.remove then
		player:remove()
	end
end

local function addHirelingLampToInbox(player, hireling)
	local inbox = player and player:getStoreInbox()
	if not inbox then
		return nil
	end

	local lamp = inbox:addItem(HIRELING_LAMP, 1, INDEX_WHEREEVER, FLAG_NOLIMIT)
	if not lamp then
		return nil
	end

	lamp:setAttribute(ITEM_ATTRIBUTE_DESCRIPTION, makeHirelingLampDescription(hireling))
	lamp:setCustomAttribute("Hireling", hireling:getId())
	return lamp
end

local function persistHirelingReturn(owner, hireling)
	local previousState = {
		active = hireling.active,
		cid = hireling.cid,
		position = {
			x = hireling.posx,
			y = hireling.posy,
			z = hireling.posz
		}
	}
	local lamp
	local committed = false
	local failureReason = "Failed to return the hireling to its lamp."

	local ok, err = pcall(function()
		lamp = addHirelingLampToInbox(owner, hireling)
		if not lamp then
			failureReason = "You don't have enough room in your store inbox."
			return
		end

		hireling.active = 0
		hireling.cid = -1
		hireling:setPosition({ x = 0, y = 0, z = 0 })

		if not hireling:save() then
			return
		end

		if not owner.save or not owner:save() then
			return
		end

		committed = true
	end)

	if committed then
		return true
	end

	local lampRollbackOk = pcall(function()
		if lamp and lamp.remove then
			lamp:remove()
		end
	end)
	local stateRollbackOk = pcall(function()
		hireling.active = previousState.active
		hireling.cid = previousState.cid
		hireling:setPosition(previousState.position)
	end)

	local hirelingRollbackOk, hirelingRollbackResult = pcall(function()
		return hireling:save()
	end)
	local ownerRollbackOk, ownerRollbackResult = pcall(function()
		return owner.save and owner:save()
	end)

	if not lampRollbackOk or not stateRollbackOk or not hirelingRollbackOk or not hirelingRollbackResult or
		not ownerRollbackOk or not ownerRollbackResult then
		logError("[Hireling] Failed to persist rollback while returning hireling id " .. tostring(hireling:getId()) .. ".")
	end
	if not ok then
		logError("[Hireling] Failed to return hireling id " .. tostring(hireling:getId()) .. ": " .. tostring(err))
	end
	return false, failureReason
end

local function ensureHirelingNpcType(npcName)
	if not createHirelingType then
		local ok, err = pcall(dofile, "data/npc/crystalserver/shops/mixed/hireling.lua")
		if not ok then
			logError("[Hireling] Failed to load Crystal hireling NPC: " .. tostring(err))
			return false
		end
	end

	if not createHirelingType then
		logError("[Hireling] createHirelingType is not available.")
		return false
	end

	local ok, err = pcall(createHirelingType, npcName)
	if not ok then
		logError("[Hireling] Failed to register NPC type " .. npcName .. ": " .. tostring(err))
		return false
	end
	return true
end

local function returnHirelingToOwnerInbox(hireling)
	if not hireling then
		return false
	end

	local owner, isOffline = getOwnerPlayer(hireling:getOwnerId())
	if not owner then
		return false
	end

	local npc = hireling.cid and Npc(hireling.cid) or nil
	local returned = persistHirelingReturn(owner, hireling)
	releaseOwnerPlayer(owner, isOffline)
	if not returned then
		return false
	end

	if npc then
		npc:remove()
	end

	return true
end

local function checkHouseAccess(hireling)
	if not hireling or hireling.active == 0 then
		return false
	end

	local tile = hireling:getPosition():getTile()
	local house = tile and tile:getHouse()
	if not house then
		return false
	end

	if house:getOwnerGuid() == hireling:getOwnerId() then
		return true
	end

	returnHirelingToOwnerInbox(hireling)
	return false
end

local function spawnHirelings()
	if not hirelingSystemEnabled() then
		return
	end

	for _, hireling in ipairs(HIRELINGS) do
		if checkHouseAccess(hireling) then
			hireling:spawn()
		end
	end
end

Hireling = Hireling or {
	id = -1,
	player_id = -1,
	name = "hireling",
	active = 0,
	sex = HIRELING_SEX.MALE,
	posx = 0,
	posy = 0,
	posz = 0,
	lookbody = 34,
	lookfeet = 116,
	lookhead = 97,
	looklegs = 3,
	looktype = 1108,
	cid = -1,
}

function Hireling:new(o)
	o = o or {}
	setmetatable(o, self)
	self.__index = self
	return o
end

function Hireling:getOwnerId()
	return self.player_id
end

function Hireling:getId()
	return self.id
end

function Hireling:getName()
	return self.name
end

function Hireling:canTalkTo(player)
	if not player or self.active ~= 1 then
		return false
	end

	if getDistanceBetween(player:getPosition(), self:getPosition()) > 3 then
		return false
	end

	local playerTile = player:getPosition():getTile()
	local playerHouse = playerTile and playerTile:getHouse()
	local hirelingTile = self:getPosition():getTile()
	local hirelingHouse = hirelingTile and hirelingTile:getHouse()
	return playerHouse and hirelingHouse and playerHouse:getId() == hirelingHouse:getId()
end

function Hireling:getPosition()
	return Position(self.posx, self.posy, self.posz)
end

function Hireling:setPosition(pos)
	self.posx = pos.x
	self.posy = pos.y
	self.posz = pos.z
end

function Hireling:getOutfit()
	return {
		lookType = self.looktype,
		lookHead = self.lookhead,
		lookBody = self.lookbody,
		lookLegs = self.looklegs,
		lookFeet = self.lookfeet,
		lookAddons = 0,
		lookMount = 0,
	}
end

function Hireling:getAvailableOutfits(includeLocked)
	local player = Player(self:getOwnerId())
	local sex = self.sex == HIRELING_SEX.FEMALE and "female" or "male"
	local outfitsAvailable = {
		{ name = HIRELING_OUTFIT_DEFAULT.name, lookType = HIRELING_OUTFIT_DEFAULT[sex], storeOffer = 0 },
	}

	if not player then
		return outfitsAvailable
	end

	for _, key in ipairs(HIRELING_OUTFIT_ORDER) do
		local outfitInfo = HIRELING_OUTFITS[key]
		local outfitName = outfitInfo and outfitInfo[2]
		local outfitTable = HIRELING_OUTFITS_TABLE[key]
		if outfitName and outfitTable then
			local haveOutfit = player:kv():scoped("hireling-outfits"):get(outfitName) == true
			if haveOutfit or includeLocked then
				outfitsAvailable[#outfitsAvailable + 1] = {
					name = outfitTable.name,
					lookType = outfitTable[sex],
					storeOffer = haveOutfit and 0 or (HIRELING_OUTFIT_STORE_OFFERS[outfitName] or 0),
				}
			end
		end
	end
	return outfitsAvailable
end

function Hireling:requestOutfitChange()
	local player = Player(self:getOwnerId())
	if not player then
		return false
	end

	HIRELING_OUTFIT_CHANGING[self:getOwnerId()] = self:getId()
	return player:sendHirelingOutfitWindow(self)
end

function Hireling:hasOutfit(lookType)
	for _, outfit in ipairs(self:getAvailableOutfits(false)) do
		if outfit.lookType == lookType then
			return true
		end
	end
	return false
end

function Hireling:setOutfit(outfit)
	self.looktype = outfit.lookType
	self.lookhead = outfit.lookHead
	self.lookbody = outfit.lookBody
	self.looklegs = outfit.lookLegs
	self.lookfeet = outfit.lookFeet
end

function Hireling:changeOutfit(player, outfit)
	HIRELING_OUTFIT_CHANGING[self:getOwnerId()] = nil
	if not player or self:getOwnerId() ~= player:getGuid() or not self:canTalkTo(player) then
		return false
	end

	if not self:hasOutfit(outfit.lookType) then
		return false
	end

	local creature = Creature(self.cid)
	if not creature then
		return false
	end

	creature:setOutfit(outfit)
	self:setOutfit(outfit)
	self:save()
	return true
end

function Hireling:hasSkill(skillName)
	local player, isOffline = getOwnerPlayer(self:getOwnerId())
	local hasSkill = false
	if player then
		hasSkill = player:kv():scoped("hireling-skills"):get(skillName) == true
	end
	releaseOwnerPlayer(player, isOffline)
	return hasSkill
end

function Hireling:setCreature(creature)
	if type(creature) == "number" then
		self.cid = creature
	elseif creature and creature.getId then
		self.cid = creature:getId()
	end
end

function Hireling:save()
	if self.id <= 0 then
		return false
	end

	local sql = "UPDATE `player_hirelings` SET"
	sql = sql .. " `name`=" .. db.escapeString(self.name)
	sql = sql .. ", `active`=" .. tostring(self.active)
	sql = sql .. ", `sex`=" .. tostring(self.sex)
	sql = sql .. ", `posx`=" .. tostring(self.posx)
	sql = sql .. ", `posy`=" .. tostring(self.posy)
	sql = sql .. ", `posz`=" .. tostring(self.posz)
	sql = sql .. ", `lookbody`=" .. tostring(self.lookbody)
	sql = sql .. ", `lookfeet`=" .. tostring(self.lookfeet)
	sql = sql .. ", `lookhead`=" .. tostring(self.lookhead)
	sql = sql .. ", `looklegs`=" .. tostring(self.looklegs)
	sql = sql .. ", `looktype`=" .. tostring(self.looktype)
	sql = sql .. " WHERE `id`=" .. tostring(self.id)
	return db.query(sql)
end

function Hireling:spawn()
	if not hirelingSystemEnabled() then
		return false
	end

	local npcName = "Hireling " .. self:getName()
	if not ensureHirelingNpcType(npcName) then
		return false
	end

	self.active = 1
	local npc = Game.createNpc(npcName, self:getPosition(), false, true, CONST_ME_NONE)
	if not npc then
		self.active = 0
		return false
	end

	npc:setOutfit(self:getOutfit())
	npc:setSpeechBubble(SPEECHBUBBLE_HIRELING or 7)
	npc:getPosition():sendMagicEffect(CONST_ME_TELEPORT)
	self:setCreature(npc)
	self:save()
	return true
end

function Hireling:returnToLamp(playerId)
	if self.active ~= 1 or self._returning then
		return false
	end

	local player = Player(playerId)
	if not player or self:getOwnerId() ~= playerId then
		if player then
			player:getPosition():sendMagicEffect(CONST_ME_POFF)
			player:sendTextMessage(MESSAGE_FAILURE, "You are not the master of this hireling.")
		end
		return false
	end

	self._returning = true
	local hirelingId = self:getId()
	local npcId = self.cid
	addEvent(function(ownerGuid, delayedHirelingId, delayedNpcId)
		local owner = Player(ownerGuid)
		local hireling = getHirelingById(delayedHirelingId)
		if not owner or not hireling then
			if hireling then
				hireling._returning = nil
			end
			return
		end

		local npc = Npc(delayedNpcId)
		local returned, failureReason = persistHirelingReturn(owner, hireling)
		if not returned then
			owner:getPosition():sendMagicEffect(CONST_ME_POFF)
			owner:sendTextMessage(MESSAGE_FAILURE, failureReason)
			hireling._returning = nil
			return
		end

		if npc then
			npc:say("As you wish!", TALKTYPE_PRIVATE_NP, false, owner, npc:getPosition())
			npc:getPosition():sendMagicEffect(CONST_ME_PURPLESMOKE)
			npc:remove()
		end

		hireling._returning = nil
	end, 1000, player:getGuid(), hirelingId, npcId)
	return true
end

function SaveHirelings()
	if not HIRELINGS then
		return true
	end

	for _, hireling in ipairs(HIRELINGS) do
		if not hireling:save() then
			logWarning("[Hireling] Failed to save hireling id " .. tostring(hireling:getId()))
		end
	end
	return true
end

function getHirelingById(id)
	id = tonumber(id)
	if not id then
		return nil
	end

	for _, hireling in ipairs(HIRELINGS) do
		if hireling:getId() == id then
			return hireling
		end
	end
	return nil
end

function getHirelingByPosition(position)
	if not position then
		return nil
	end

	for _, hireling in ipairs(HIRELINGS) do
		if hireling.active == 1 and hireling.posx == position.x and hireling.posy == position.y and hireling.posz == position.z then
			return hireling
		end
	end
	return nil
end

function getHirelingByCid(cid)
	cid = tonumber(cid)
	if not cid then
		return nil
	end

	for _, hireling in ipairs(HIRELINGS) do
		if hireling.cid == cid then
			return hireling
		end
	end
	return nil
end

function GetHirelingSkillNameById(id)
	id = tonumber(id)
	for _, skill in pairs(HIRELING_SKILLS) do
		if skill[1] == id then
			return skill[2]
		end
	end
	return nil
end

function GetHirelingOutfitNameById(id)
	id = tonumber(id)
	for _, outfit in pairs(HIRELING_OUTFITS) do
		if outfit[1] == id then
			return outfit[2]
		end
	end
	return nil
end

function HirelingsInit()
	if not hirelingSystemEnabled() then
		return true
	end

	if db.tableExists and not db.tableExists("player_hirelings") then
		logWarning("[Hireling] player_hirelings table is missing.")
		return false
	end

	HIRELINGS = {}
	PLAYER_HIRELINGS = {}
	HIRELING_OUTFIT_CHANGING = {}

	local rows = db.storeQuery("SELECT * FROM `player_hirelings`")
	if rows then
		repeat
			local playerId = result.getNumber(rows, "player_id")
			PLAYER_HIRELINGS[playerId] = PLAYER_HIRELINGS[playerId] or {}

			local hireling = Hireling:new()
			hireling.id = result.getNumber(rows, "id")
			hireling.player_id = playerId
			hireling.name = result.getString(rows, "name")
			hireling.active = result.getNumber(rows, "active")
			hireling.sex = result.getNumber(rows, "sex")
			hireling.posx = result.getNumber(rows, "posx")
			hireling.posy = result.getNumber(rows, "posy")
			hireling.posz = result.getNumber(rows, "posz")
			hireling.lookbody = result.getNumber(rows, "lookbody")
			hireling.lookfeet = result.getNumber(rows, "lookfeet")
			hireling.lookhead = result.getNumber(rows, "lookhead")
			hireling.looklegs = result.getNumber(rows, "looklegs")
			hireling.looktype = result.getNumber(rows, "looktype")
			hireling.cid = -1

			table.insert(PLAYER_HIRELINGS[playerId], hireling)
			table.insert(HIRELINGS, hireling)
		until not result.next(rows)
		result.free(rows)
	end

	spawnHirelings()
	return true
end

function PersistHireling(hireling)
	local query = string.format(
		"INSERT INTO `player_hirelings` (`player_id`,`name`,`active`,`sex`,`posx`,`posy`,`posz`,`lookbody`,`lookfeet`,`lookhead`,`looklegs`,`looktype`) VALUES (%d, %s, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)",
		hireling.player_id,
		db.escapeString(hireling.name),
		hireling.active,
		hireling.sex,
		hireling.posx,
		hireling.posy,
		hireling.posz,
		hireling.lookbody,
		hireling.lookfeet,
		hireling.lookhead,
		hireling.looklegs,
		hireling.looktype
	)

	if not db.query(query) then
		return false
	end

	hireling.id = db.lastInsertId()
	return hireling.id and hireling.id > 0
end

function Player:getHirelings()
	return PLAYER_HIRELINGS[self:getGuid()] or {}
end

function Player:getHirelingsCount()
	return #self:getHirelings()
end

function Player:hasHirelings()
	return self:getHirelingsCount() > 0
end

function Player:addNewHireling(name, sex)
	if not hirelingSystemEnabled() then
		return false, "Hireling system is disabled."
	end

	name = formatHirelingName(name)
	if not isValidHirelingName(name) then
		return false, "You cannot use this hireling name."
	end

	if self:getHirelingsCount() >= HIRELING_MAX_PER_PLAYER then
		return false, "You already own the maximum amount of hirelings."
	end

	local hireling = Hireling:new()
	hireling.name = name
	hireling.player_id = self:getGuid()
	hireling.sex = tonumber(sex) == HIRELING_SEX.FEMALE and HIRELING_SEX.FEMALE or HIRELING_SEX.MALE
	hireling.looktype = hireling.sex == HIRELING_SEX.FEMALE and HIRELING_OUTFIT_DEFAULT.female or HIRELING_OUTFIT_DEFAULT.male

	if not PersistHireling(hireling) then
		return false, "Failed to save hireling."
	end

	local lamp = addHirelingLampToInbox(self, hireling)
	if not lamp then
		db.query("DELETE FROM `player_hirelings` WHERE `id`=" .. hireling:getId())
		return false, "Your store inbox is not available."
	end

	PLAYER_HIRELINGS[self:getGuid()] = PLAYER_HIRELINGS[self:getGuid()] or {}
	table.insert(PLAYER_HIRELINGS[self:getGuid()], hireling)
	table.insert(HIRELINGS, hireling)
	return hireling
end

function Player:isChangingHirelingOutfit()
	return (HIRELING_OUTFIT_CHANGING[self:getGuid()] or 0) > 0
end

function Player:getHirelingChangingOutfit()
	return getHirelingById(HIRELING_OUTFIT_CHANGING[self:getGuid()])
end

function Player:sendHirelingOutfitWindow(hireling)
	if not astraHirelingProtocolEnabled(self) or not hireling then
		return false
	end

	local outfit = hireling:getOutfit()
	local msg = NetworkMessage()
	msg:addByte(0xC8)
	msg:addU16(outfit.lookType)
	if outfit.lookType == 0 then
		msg:addU16(outfit.lookTypeEx or 0)
	else
		msg:addByte(outfit.lookHead)
		msg:addByte(outfit.lookBody)
		msg:addByte(outfit.lookLegs)
		msg:addByte(outfit.lookFeet)
		msg:addByte(outfit.lookAddons)
	end
	msg:addU16(outfit.lookMount or 0)

	local availableOutfits = hireling:getAvailableOutfits(true)
	msg:addByte(math.min(#availableOutfits, 255))
	local storeOffers = {}
	for i = 1, math.min(#availableOutfits, 255) do
		local outfitData = availableOutfits[i]
		msg:addU16(outfitData.lookType)
		msg:addString(outfitData.name)
		msg:addByte(0)
		if outfitData.storeOffer and outfitData.storeOffer > 0 then
			storeOffers[#storeOffers + 1] = { outfitData.lookType, outfitData.storeOffer }
		end
	end

	msg:addByte(0) -- mount count

	msg:addByte(0x48) -- H
	msg:addByte(0x52) -- R
	msg:addByte(0x4C) -- L
	msg:addByte(0x47) -- G
	msg:addU32(hireling.cid > 0 and hireling.cid or 0)
	msg:addByte(hireling.sex)
	msg:addU16(math.min(#storeOffers, 255))
	for i = 1, math.min(#storeOffers, 255) do
		msg:addU16(storeOffers[i][1])
		msg:addU32(storeOffers[i][2])
	end
	msg:addByte(0) -- try-on pair count

	return msg:sendToPlayer(self)
end

function Player:findHirelingLamp(hirelingId)
	local inbox = self:getStoreInbox()
	if not inbox then
		return nil
	end

	for i = 0, inbox:getSize() - 1 do
		local item = inbox:getItem(i)
		if item and item:getId() == HIRELING_LAMP and item:getCustomAttribute("Hireling") == hirelingId then
			return item
		end
	end
	return nil
end

function Player:hasHirelingSkill(skillName)
	return self:kv():scoped("hireling-skills"):get(skillName) == true
end

function Player:enableHirelingSkill(skillName)
	local skillScoped = self:kv():scoped("hireling-skills")
	if skillScoped:get(skillName) then
		return false
	end
	skillScoped:set(skillName, true)
	return true
end

function Player:hasHirelingOutfit(outfitName)
	return self:kv():scoped("hireling-outfits"):get(outfitName) == true
end

function Player:enableHirelingOutfit(outfitName)
	local outfitScoped = self:kv():scoped("hireling-outfits")
	if outfitScoped:get(outfitName) then
		return false
	end
	outfitScoped:set(outfitName, true)
	return true
end

function Player:clearAllHirelingStats()
	local skillsScoped = self:kv():scoped("hireling-skills")
	for _, skill in pairs(HIRELING_SKILLS) do
		skillsScoped:set(skill[2], false)
	end

	local outfitsScoped = self:kv():scoped("hireling-outfits")
	for _, outfit in pairs(HIRELING_OUTFITS) do
		outfitsScoped:set(outfit[2], false)
	end
end
