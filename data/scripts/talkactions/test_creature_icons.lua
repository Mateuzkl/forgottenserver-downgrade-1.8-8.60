-- Test Creature Icons system
-- /testcreatureicon {category} {iconId} {count}

local creatureIconTest = TalkAction("/testcreatureicon")

local iconNames = {
	[CreatureIconQuests_Hazard] = "Hazard (Rotten Charge)",
	[CreatureIconQuests_BloodDrop] = "Blood Drop",
	[CreatureIconQuests_BrownSkull] = "Brown Skull",
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
		player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "  Example: /testcreatureicon 0,23,3 # Hazard + count 3")
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
creatureIconTest:groupType("god")
creatureIconTest:register()

-- Clear all icons
local creatureIconClear = TalkAction("/clearcreatureicon")

function creatureIconClear.onSay(player, words, param)
	player:clearIcons()
	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, "All creature icons cleared.")
	return false
end

creatureIconClear:separator(" ")
creatureIconClear:groupType("god")
creatureIconClear:register()

-- Test Rotten Charge (Hazard quest icon)
local testRotten = TalkAction("/testrotten")

function testRotten.onSay(player, words, param)
	local count = tonumber(param) or 1
	-- Hazard = CreatureIconQuests_Hazard (23), Quests category (0)
	player:setIcon("rotten", 0, 23, count)
	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, string.format("Rotten Charge icon set with count=%d", count))
	return false
end

testRotten:separator(" ")
testRotten:groupType("god")
testRotten:register()

-- Test Forge (Fiendish icon)
local testForge = TalkAction("/testforge")

function testForge.onSay(player, words, param)
	local iconType = tonumber(param) or 5 -- Fiendish=5 default
	-- Fiendish = CreatureIconModifications_Fiendish (5), Modifications category (1)
	player:setIcon("forge", 1, iconType, 0)
	local name = iconType == 5 and "Fiendish" or (iconType == 4 and "Influenced" or "Unknown")
	player:sendTextMessage(MESSAGE_EVENT_ADVANCE, string.format("Forge %s icon set", name))
	return false
end

testForge:separator(" ")
testForge:groupType("god")
testForge:register()
