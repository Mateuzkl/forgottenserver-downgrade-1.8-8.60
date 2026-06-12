local DEBUG = true

HIRELINGS = {}
PLAYER_HIRELINGS = {}
HIRELING_OUTFIT_CHANGING = {}

local function DebugPrint(str)
	if DEBUG then
		print(str)
	end
end

function hasBitSet(flag, value)
	return bit.band(value, flag) == flag
end

function setFlag(flag, value)
	return bit.bor(value, flag)
end

HIRELING_SKILLS = {
	BANKER = { "banker", 1 },
	COOKING = { "cooker", 2 },
	STEWARD = { "steward", 4 },
	TRADER = { "trader", 8 }
}

HIRELING_SEX = {
	FEMALE = 0,
	MALE = 1
}

HIRELING_OUTFIT_DEFAULT = { name = "Citizen", female = 136, male = 128 }

HIRELING_OUTFITS = {
	BANKER = 1,
	COOKING = 2,
	STEWARD = 4,
	TRADER = 8,
	SERVANT = 16,
	HYDRA = 32,
	FERUMBRAS = 64,
	BONELORD = 128,
	DRAGON = 256
}

HIRELING_OUTFITS_TABLE = {
	BANKER = { name = "Banker Dress", female = 1109, male = 1110 },
	BONELORD = { name = "Bonelord Dress", female = 1123, male = 1124 },
	COOKING = { name = "Cook Dress", female = 1113, male = 1114 },
	DRAGON = { name = "Dragon Dress", female = 1125, male = 1126 },
	FERUMBRAS = { name = "Ferumbras Dress", female = 1131, male = 1132 },
	HYDRA = { name = "Hydra Dress", female = 1129, male = 1130 },
	SERVANT = { name = "Servant Dress", female = 1117, male = 1118 },
	STEWARD = { name = "Stewart Dress", female = 1115, male = 1116 },
	TRADER = { name = "Trader Dress", female = 1111, male = 1112 }
}

HIRELING_LAMP_ID = 34875
HIRELING_ATTRIBUTE = "HIRELING_ID"

HIRELING_FOODS_BOOST = {
	MAGIC = 35174,
	MELEE = 35175,
	SHIELDING = 35172,
	DISTANCE = 35173,
}

HIRELING_FOODS = {
	35176, 35177, 35178, 35179, 35180
}

HIRELING_FOODS_IDS = HIRELING_FOODS

local function getHirelingKV(player_id)
	local player = Player(player_id)
	if player then
		return player:kv():scoped("hireling")
	end
	return nil
end

local function getHirelingKVValue(player_id, key)
	local kv = getHirelingKV(player_id)
	if kv then
		return kv:get(key) or 0
	end
	return 0
end

local function checkHouseAccess(hireling)
	if hireling.active == 0 then return false end

	local pos = hireling:getPosition()
	local tile = Tile(pos)
	if not tile then return false end
	local house = tile:getHouse()
	if not house then return false end

	if house:getOwnerGuid() == hireling:getOwnerId() then return true end

	print('>> Returning Hireling:' .. hireling:getName() .. ' to inactive (house owner changed)')
	hireling.active = 0
	hireling.cid = -1
	hireling:setPosition({ x = 0, y = 0, z = 0 })
	hireling:save()
	return false
end

local function spawnNPCs()
	print('>> Spawning Hirelings')
	for i = 1, #HIRELINGS do
		local hireling = HIRELINGS[i]
		if checkHouseAccess(hireling) then
			hireling:spawn()
		end
	end
end

Hireling = {
	id = -1,
	player_id = -1,
	name = 'hireling',
	skills = 0,
	active = 0,
	sex = 0,
	posx = 0,
	posy = 0,
	posz = 0,
	lookbody = 34,
	lookfeet = 116,
	lookhead = 97,
	looklegs = 3,
	looktype = 0,
	cid = -1
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
	if not player then return false end
	local tile = Tile(player:getPosition())
	if not tile then return false end
	local house = tile:getHouse()
	if not house then return false end
	local hirelingTile = Tile(self:getPosition())
	if not hirelingTile then return false end
	local hirelingHouse = hirelingTile:getHouse()
	if not hirelingHouse then return false end
	return house:getId() == hirelingHouse:getId()
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
		lookAddons = 0,
		lookMount = 0,
		lookLegs = self.looklegs,
		lookBody = self.lookbody,
		lookFeet = self.lookfeet
	}
end

function Hireling:getAvailableOutfits()
	local flags = getHirelingKVValue(self:getOwnerId(), "outfits")
	local sex = (self.sex == HIRELING_SEX.FEMALE) and 'female' or 'male'
	local outfits = {}
	table.insert(outfits, { name = HIRELING_OUTFIT_DEFAULT.name, lookType = HIRELING_OUTFIT_DEFAULT[sex] })
	if flags > 0 then
		for key, value in pairs(HIRELING_OUTFITS) do
			if hasBitSet(value, flags) then
				table.insert(outfits, {
					name = HIRELING_OUTFITS_TABLE[key].name,
					lookType = HIRELING_OUTFITS_TABLE[key][sex]
				})
			end
		end
	end
	return outfits
end

function Hireling:requestOutfitChange()
	local player = Player(self:getOwnerId())
	if not player then return end
	HIRELING_OUTFIT_CHANGING[self:getOwnerId()] = self:getId()
	self:sendHirelingOutfitWindow(player)
end

function Hireling:hasOutfit(lookType)
	local outfits = self:getAvailableOutfits()
	for _, outfit in ipairs(outfits) do
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

function Hireling:changeOutfit(outfit)
	HIRELING_OUTFIT_CHANGING[self:getOwnerId()] = nil
	if not self:hasOutfit(outfit.lookType) then return end
	local npc = Npc(self.cid)
	if npc then
		npc:setOutfit(outfit)
	end
	self:setOutfit(outfit)
end

local SKILL_FLAG_MAP = {}
for _, v in pairs(HIRELING_SKILLS) do
	SKILL_FLAG_MAP[v[1]] = v[2]
end

local function resolveSkillFlag(SKILL)
	if type(SKILL) == "table" then
		return SKILL[2]
	elseif type(SKILL) == "string" then
		return SKILL_FLAG_MAP[SKILL] or 0
	else
		return SKILL
	end
end

function Hireling:hasSkill(SKILL)
	local flag = resolveSkillFlag(SKILL)
	local skills = getHirelingKVValue(self:getOwnerId(), "skills")
	if skills <= 0 then
		return false
	else
		return hasBitSet(flag, skills)
	end
end

function Hireling:setCreature(cid)
	self.cid = cid
end

function Hireling:save()
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
	db.query(sql)
end

function Hireling:spawn()
	self.active = 1
	local pos = self:getPosition()
	local npc = Game.createNpc("Hireling", pos)
	if not npc then
		DebugPrint('Error spawning Hireling: ' .. self:getName())
		return
	end
	npc:setOutfit(self:getOutfit())
	npc:setSpeechBubble(0)
	self:setCreature(npc:getId())
	pos:sendMagicEffect(CONST_ME_TELEPORT)
end

function Hireling:sendHirelingOutfitWindow(player)
	if not player then return end
	local out = NetworkMessage(player)
	out:addByte(0xC8)

	local outfit = self:getOutfit()
	out:addU16(outfit.lookType)
	out:addByte(outfit.lookHead)
	out:addByte(outfit.lookBody)
	out:addByte(outfit.lookLegs)
	out:addByte(outfit.lookFeet)
	out:addByte(outfit.lookAddons)
	out:addU16(outfit.lookMount)

	local availableOutfits = self:getAvailableOutfits()
	out:addU16(#availableOutfits)
	for _, o in ipairs(availableOutfits) do
		out:addU16(o.lookType)
		out:addString(o.name)
		out:addByte(0x00)
	end

	out:addU16(0x00)
	out:sendToPlayer(player)
end

function Hireling:returnToLamp(player_id)
	local creature = Creature(self.cid)
	local player = Player(player_id)
	if not player then return end
	local lampType = ItemType(HIRELING_LAMP_ID)

	if self:getOwnerId() ~= player_id then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You are not the master of this hireling.")
		return
	end

	if player:getFreeCapacity() < lampType:getWeight(1) then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You do not have enough capacity.")
		return
	end

	local inbox = player:getStoreInbox()
	if not inbox then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You don't have enough room in your inbox.")
		return
	end

	local lamp = inbox:addItem(HIRELING_LAMP_ID, 1)
	if not lamp then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendTextMessage(MESSAGE_INFO_DESCR, "You don't have enough room in your inbox.")
		return
	end

	if creature then
		creature:getPosition():sendMagicEffect(CONST_ME_PURPLESMOKE)
		creature:remove()
	end

	lamp:setAttribute(ITEM_ATTRIBUTE_DESCRIPTION, "This mysterious lamp summons your very own personal hireling.\nThis item cannot be traded.\nThis magic lamp is the home of " .. self:getName() .. ".")
	lamp:setSpecialAttribute(HIRELING_ATTRIBUTE, self:getId())
	self.active = 0
	self.cid = -1
	self:setPosition({ x = 0, y = 0, z = 0 })
end

function SaveHirelings()
	for _, hireling in ipairs(HIRELINGS) do
		hireling:save()
	end
end

function getHirelingById(id)
	for i = 1, #HIRELINGS do
		if HIRELINGS[i]:getId() == id then
			return HIRELINGS[i]
		end
	end
	return nil
end

function getHirelingByPosition(position)
	for i = 1, #HIRELINGS do
		local h = HIRELINGS[i]
		if h.posx == position.x and h.posy == position.y and h.posz == position.z then
			return h
		end
	end
	return nil
end

function HirelingsInit()
	local rows = db.storeQuery("SELECT * FROM `player_hirelings`")
	if rows then
		repeat
			local player_id = result.getNumber(rows, "player_id")
			if not PLAYER_HIRELINGS[player_id] then
				PLAYER_HIRELINGS[player_id] = {}
			end
			local hireling = Hireling:new()
			hireling.id = result.getNumber(rows, "id")
			hireling.player_id = player_id
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
			table.insert(PLAYER_HIRELINGS[player_id], hireling)
			table.insert(HIRELINGS, hireling)
		until not result.next(rows)
		result.free(rows)
		spawnNPCs()
	end
end

function PersistHireling(hireling)
	db.query(string.format("INSERT INTO `player_hirelings` (`player_id`,`name`,`active`,`sex`,`posx`,`posy`,`posz`,`lookbody`,`lookfeet`,`lookhead`,`looklegs`,`looktype`) VALUES (%d, %s, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)",
		hireling.player_id, db.escapeString(hireling.name), hireling.active, hireling.sex, hireling.posx, hireling.posy, hireling.posz, hireling.lookbody, hireling.lookfeet, hireling.lookhead, hireling.looklegs, hireling.looktype))

	local hirelings = PLAYER_HIRELINGS[hireling.player_id] or {}
	local ids = ""
	for i = 1, #hirelings do
		if i > 1 then
			ids = ids .. "','"
		end
		ids = ids .. tostring(hirelings[i].id)
	end
	local query = string.format("SELECT `id` FROM `player_hirelings` WHERE `player_id`= %d and `id` NOT IN ('%s')", hireling.player_id, ids)
	local resultId = db.storeQuery(query)
	if resultId then
		local id = result.getNumber(resultId, 'id')
		hireling.id = id
		result.free(resultId)
		return true
	else
		return false
	end
end

function Player:getHirelings()
	return PLAYER_HIRELINGS[self:getGuid()] or {}
end

function Player:getHirelingsCount()
	local hirelings = self:getHirelings()
	return #hirelings
end

function Player:addNewHireling(name, sex)
	local hireling = Hireling:new()
	hireling.name = name
	hireling.player_id = self:getGuid()
	if sex == HIRELING_SEX.FEMALE then
		hireling.looktype = 136
		hireling.sex = HIRELING_SEX.FEMALE
	else
		hireling.looktype = 128
		hireling.sex = HIRELING_SEX.MALE
	end

	local lampType = ItemType(HIRELING_LAMP_ID)
	if self:getFreeCapacity() < lampType:getWeight(1) then
		self:getPosition():sendMagicEffect(CONST_ME_POFF)
		self:sendTextMessage(MESSAGE_INFO_DESCR, "You do not have enough capacity.")
		return false
	end

	local inbox = self:getStoreInbox()
	if not inbox then
		self:getPosition():sendMagicEffect(CONST_ME_POFF)
		self:sendTextMessage(MESSAGE_INFO_DESCR, "You don't have enough room in your inbox.")
		return false
	end

	local saved = PersistHireling(hireling)
	if not saved then
		DebugPrint('Error saving Hireling:' .. name .. ' - player:' .. self:getName())
		return false
	end

	if not PLAYER_HIRELINGS[self:getGuid()] then
		PLAYER_HIRELINGS[self:getGuid()] = {}
	end
	table.insert(PLAYER_HIRELINGS[self:getGuid()], hireling)
	table.insert(HIRELINGS, hireling)
	local lamp = inbox:addItem(HIRELING_LAMP_ID, 1)
	lamp:setAttribute(ITEM_ATTRIBUTE_DESCRIPTION, "This mysterious lamp summons your very own personal hireling.\nThis item cannot be traded.\nThis magic lamp is the home of " .. hireling:getName() .. ".")
	lamp:setSpecialAttribute(HIRELING_ATTRIBUTE, hireling:getId())
	hireling.active = 0
	return hireling
end

function Player:isChangingHirelingOutfit()
	return HIRELING_OUTFIT_CHANGING[self:getGuid()] and HIRELING_OUTFIT_CHANGING[self:getGuid()] > 0 or false
end

function Player:getHirelingChangingOutfit()
	local id = HIRELING_OUTFIT_CHANGING[self:getGuid()]
	if not id then return nil end
	return getHirelingById(id)
end

function Player:hasHirelings()
	return PLAYER_HIRELINGS[self:getGuid()] and #PLAYER_HIRELINGS[self:getGuid()] > 0 or false
end

function Player:findHirelingLamp(hirelingId)
	local inbox = self:getStoreInbox()
	if not inbox then return nil end
	for i = 0, inbox:getSize() - 1 do
		local item = inbox:getItem(i)
		if item and item:getId() == HIRELING_LAMP_ID and item:getSpecialAttribute(HIRELING_ATTRIBUTE) == hirelingId then
			return item
		end
	end
	return nil
end

function Player:hasHirelingSkill(SKILL)
	local kv = self:kv():scoped("hireling")
	local skills = kv:get("skills") or 0
	if skills <= 0 then
		return false
	else
		return hasBitSet(SKILL[2], skills)
	end
end

function Player:enableHirelingSkill(SKILL)
	local kv = self:kv():scoped("hireling")
	local skills = kv:get("skills") or 0
	if skills < 0 then skills = 0 end
	skills = setFlag(SKILL[2], skills)
	kv:set("skills", skills)
end

function Player:hasHirelingOutfit(OUTFIT)
	local kv = self:kv():scoped("hireling")
	local outfits = kv:get("outfits") or 0
	if outfits <= 0 then
		return false
	else
		return hasBitSet(OUTFIT, outfits)
	end
end

function Player:enableHirelingOutfit(OUTFIT)
	local kv = self:kv():scoped("hireling")
	local outfits = kv:get("outfits") or 0
	if outfits < 0 then outfits = 0 end
	outfits = setFlag(OUTFIT, outfits)
	kv:set("outfits", outfits)
end
