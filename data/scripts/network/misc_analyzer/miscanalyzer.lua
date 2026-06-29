-- data/scripts/network/misc_analyzer/miscanalyzer.lua
-- Misc Analyzer - sends charm/imbuement/special skill activations to AstraClient
-- Opcodes: 0x2D (charm), 0x30 (imbuement), 0x31 (special skill)

local OPCODE_CHARM_ACTIVATED = 0x2D
local OPCODE_IMBUEMENT_ACTIVATED = 0x30
local OPCODE_SPECIAL_SKILL_ACTIVATED = 0x31

local function isAstra(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function isOtc(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local otcOrAstraOpcodes = {
	[OPCODE_CHARM_ACTIVATED] = true,
	[OPCODE_IMBUEMENT_ACTIVATED] = true,
	[OPCODE_SPECIAL_SKILL_ACTIVATED] = true,
}

local function canReceiveOpcode(player, opcode)
	if not player then
		return false
	end
	if otcOrAstraOpcodes[opcode] then
		return isAstra(player) or isOtc(player)
	end
	return isAstra(player)
end

local function sendOpcode(player, opcode)
	if not canReceiveOpcode(player, opcode) then return false end
	local out = NetworkMessage(player)
	out:addByte(opcode)
	return out
end

local function sendCharmActivated(player, charmId)
	local out = sendOpcode(player, OPCODE_CHARM_ACTIVATED)
	if not out then return false end
	out:addByte(charmId)
	return out:sendToPlayer(player)
end

local function sendImbuementActivated(player, imbuementId, amount)
	local out = sendOpcode(player, OPCODE_IMBUEMENT_ACTIVATED)
	if not out then return false end
	out:addByte(imbuementId)
	out:addU32(amount or 0)
	return out:sendToPlayer(player)
end

local function sendSpecialSkillActivated(player, skillId)
	local out = sendOpcode(player, OPCODE_SPECIAL_SKILL_ACTIVATED)
	if not out then return false end
	out:addByte(skillId)
	return out:sendToPlayer(player)
end

MiscAnalyzer = {
	sendCharm = sendCharmActivated,
	sendImbuement = sendImbuementActivated,
	sendSpecialSkill = sendSpecialSkillActivated,
}
