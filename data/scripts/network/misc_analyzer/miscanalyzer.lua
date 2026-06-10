-- data/scripts/network/misc_analyzer/miscanalyzer.lua
-- Misc Analyzer - sends charm/imbuement/special skill activations to AstraClient
-- Opcodes: 0x2D (charm), 0x30 (imbuement), 0x31 (special skill)

local OPCODE_CHARM_ACTIVATED = 0x2D
local OPCODE_IMBUEMENT_ACTIVATED = 0x30
local OPCODE_SPECIAL_SKILL_ACTIVATED = 0x31

local function isOTC(player)
	return player and player.isUsingOtClient and player:isUsingOtClient()
end

local function sendOpcode(player, opcode)
	if not player or not isOTC(player) then return false end
	local out = NetworkMessage(player)
	out:addByte(opcode)
	return out
end

-- Charm activated (charmId: 1=Parry, 2=AdrenalineBurst, 3=Dodge, 4=LowBlow, etc.)
function sendCharmActivated(player, charmId)
	local out = sendOpcode(player, OPCODE_CHARM_ACTIVATED)
	if not out then return false end
	out:addByte(charmId)
	return out:sendToPlayer(player)
end

-- Imbuement activated (imbuementId: 1=Critical, 2=ManaLeech, 3=LifeLeech, amount=value)
function sendImbuementActivated(player, imbuementId, amount)
	local out = sendOpcode(player, OPCODE_IMBUEMENT_ACTIVATED)
	if not out then return false end
	out:addByte(imbuementId)
	out:addU32(amount or 0)
	return out:sendToPlayer(player)
end

-- Special skill activated (skillId: 0=Onslaught, 1=Ruse, 2=Momentum, 3=Transcendence)
function sendSpecialSkillActivated(player, skillId)
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
