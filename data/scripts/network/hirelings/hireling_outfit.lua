local OPCODE_REQUEST_OUTFIT = 0xD2
local OPCODE_CHANGE_OUTFIT = 0xD3
local HIRELING_MARKER = { 0x48, 0x52, 0x4C, 0x47 }
local HIRELING_TARGET_TYPE = 1

local function hirelingProtocolEnabled(player)
	return configManager.getBoolean(configKeys.HIRELING_SYSTEM_ENABLED) and
		configManager.getBoolean(configKeys.ASTRA_HIRELING_PROTOCOL_ENABLED) and
		player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function readHirelingOutfit(msg)
	local outfit = {}
	outfit.lookType = NetworkGuard.readU16(msg)
	outfit.lookHead = NetworkGuard.readByte(msg)
	outfit.lookBody = NetworkGuard.readByte(msg)
	outfit.lookLegs = NetworkGuard.readByte(msg)
	outfit.lookFeet = NetworkGuard.readByte(msg)
	outfit.lookAddons = NetworkGuard.readByte(msg)
	outfit.lookMount = 0

	if not outfit.lookType or not outfit.lookHead or not outfit.lookBody or not outfit.lookLegs or not outfit.lookFeet or not outfit.lookAddons then
		return nil
	end
	return outfit
end

local function readHirelingHeader(msg)
	for _, expected in ipairs(HIRELING_MARKER) do
		if NetworkGuard.readByte(msg) ~= expected then
			return false
		end
	end
	return NetworkGuard.readByte(msg) == HIRELING_TARGET_TYPE
end

local requestHandler = PacketHandler(OPCODE_REQUEST_OUTFIT)

function requestHandler.onReceive(player, msg)
	if not hirelingProtocolEnabled(player) or not NetworkGuard.cooldown(player, "hireling-outfit-request", 250) then
		return
	end

	if not readHirelingHeader(msg) then
		return
	end

	local hirelingCid = NetworkGuard.readU32(msg)
	if not hirelingCid then
		return
	end

	local hireling = getHirelingByCid(hirelingCid)
	if not hireling or not hireling:canTalkTo(player) then
		player:sendOutfitWindow()
		return
	end

	if hireling:getOwnerId() ~= player:getGuid() then
		player:sendCancelMessage("You are not the master of this hireling.")
		return
	end

	HIRELING_OUTFIT_CHANGING[player:getGuid()] = hireling:getId()
	player:sendHirelingOutfitWindow(hireling)
end

requestHandler:register()

local changeHandler = PacketHandler(OPCODE_CHANGE_OUTFIT)

function changeHandler.onReceive(player, msg)
	if not hirelingProtocolEnabled(player) or not NetworkGuard.cooldown(player, "hireling-outfit-change", 250) then
		return
	end

	if not readHirelingHeader(msg) then
		return
	end

	if not player:isChangingHirelingOutfit() then
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		return
	end

	local outfit = readHirelingOutfit(msg)
	local hirelingCid = NetworkGuard.readU32(msg)
	if not outfit or not hirelingCid then
		return
	end

	local requestedHireling = getHirelingByCid(hirelingCid)
	local changingHireling = player:getHirelingChangingOutfit()
	if not requestedHireling or not changingHireling or requestedHireling:getId() ~= changingHireling:getId() then
		HIRELING_OUTFIT_CHANGING[player:getGuid()] = nil
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		return
	end

	if not changingHireling:changeOutfit(player, outfit) then
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
	end
end

changeHandler:register()
