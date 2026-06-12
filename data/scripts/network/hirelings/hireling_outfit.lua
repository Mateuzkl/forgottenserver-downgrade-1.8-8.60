local OPCODE_CONFIRM_OUTFIT_CHANGE = 0xD3

local function getOutfit(msg)
	local outfit = {}
	outfit.lookType = msg:getU16()
	outfit.lookHead = msg:getByte()
	outfit.lookBody = msg:getByte()
	outfit.lookLegs = msg:getByte()
	outfit.lookFeet = msg:getByte()
	outfit.lookAddons = msg:getByte()
	outfit.lookMount = msg:getU16()
	return outfit
end

local handler = PacketHandler(OPCODE_CONFIRM_OUTFIT_CHANGE)

function handler.onReceive(player, msg)
	if not player:isChangingHirelingOutfit() then
		return
	end

	local hireling = player:getHirelingChangingOutfit()
	if not hireling then
		player:sendCancelMessage(RETURNVALUE_NOTPOSSIBLE)
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
		getOutfit(msg)
		return
	end

	local outfit = getOutfit(msg)
	hireling:changeOutfit(outfit)
end

handler:register()
