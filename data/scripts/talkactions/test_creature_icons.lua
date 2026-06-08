-- Test Creature Icons system
local creatureIconTest = TalkAction("/testcreatureicon")

local iconNames = {
	[22] = "Hazard (Rotten Charge)",
	[24] = "Blood Drop",
	[23] = "Brown Skull",
	[CreatureIconQuests_WhiteCross] = "White Cross",
	[CreatureIconQuests_RedCross] = "Red Cross",
	[CreatureIconModifications_Fiendish] = "Fiendish",
	[CreatureIconModifications_Influenced] = "Influenced",
	[CreatureIconModifications_ReducedHealth] = "Reduced Health",
	[CreatureIconModifications_HigherDamageReceived] = "Higher Damage Received",
}

function creatureIconTest.onSay(player, words, param)
	if param == "" then
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "Usage: /testcreatureicon category,iconId,count")
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "  category: 0=Quests, 1=Modifications")
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "  Example: /testcreatureicon 0,22,3 # Hazard + count 3")
		return false
	end

	local params = param:split(",")
	local category = tonumber(params[1]) or 0
	local iconId = tonumber(params[2]) or 0
	local count = tonumber(params[3]) or 0

	local iconName = iconNames[iconId] or "Unknown"
	player:setIcon("test", category, iconId, count)

	local catName = category == 0 and "Quests" or "Modifications"
	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, string.format("Set icon '%s' (%s) count=%d on yourself.",
		iconName, catName, count))
	return false
end
creatureIconTest:separator(" ")
creatureIconTest:accountType(6)
creatureIconTest:access(true)
creatureIconTest:register()

-- Clear all icons
local creatureIconClear = TalkAction("/clearcreatureicon")

function creatureIconClear.onSay(player, words, param)
	player:clearIcons()
	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "All creature icons cleared.")
	return false
end
creatureIconClear:separator(" ")
creatureIconClear:accountType(6)
creatureIconClear:access(true)
creatureIconClear:register()

-- Rotten Charge
local testRotten = TalkAction("/testrotten")

function testRotten.onSay(player, words, param)
	local count = tonumber(param) or 1
	player:setIcon("rotten", 0, 22, count)
	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, string.format("Rotten Charge icon set with count=%d", count))
	return false
end
testRotten:separator(" ")
testRotten:accountType(6)
testRotten:access(true)
testRotten:register()

-- Forge icon
local testForge = TalkAction("/testforge")

function testForge.onSay(player, words, param)
	local iconType = tonumber(param) or 5
	if iconType ~= 4 and iconType ~= 5 then
		player:sendTextMessage(MESSAGE_STATUS_WARNING, "Invalid forge icon type. Use 4 (Influenced) or 5 (Fiendish).")
		return false
	end
	player:setIcon("forge", 1, iconType, 0)
	local name = iconType == 5 and "Fiendish" or "Influenced"
	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, string.format("Forge %s icon set", name))
	return false
end
testForge:separator(" ")
testForge:accountType(6)
testForge:access(true)
testForge:register()
