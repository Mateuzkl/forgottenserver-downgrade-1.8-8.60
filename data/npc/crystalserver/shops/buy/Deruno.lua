local internalNpcName = "Deruno"
local npcType = Game.createNpcType(internalNpcName)
local npcConfig = {}

npcConfig.name = internalNpcName
npcConfig.description = internalNpcName

npcConfig.health = 100
npcConfig.maxHealth = npcConfig.health
npcConfig.walkInterval = 2000
npcConfig.walkRadius = 2

npcConfig.outfit = {
	lookType = 132, lookHead = 20, lookBody = 39, lookLegs = 45, lookFeet = 7, lookAddons = 0
}

npcConfig.flags = {
	floorchange = false,
}

local keywordHandler = KeywordHandler:new()
local npcHandler = NpcHandler:new(keywordHandler)

npcType.onThink = function(npc, interval)
	npcHandler:onThink(npc, interval)
end

npcType.onAppear = function(npc, creature)
	npcHandler:onAppear(npc, creature)
end

npcType.onDisappear = function(npc, creature)
	npcHandler:onDisappear(npc, creature)
end

npcType.onMove = function(npc, creature, fromPosition, toPosition)
	npcHandler:onMove(npc, creature, fromPosition, toPosition)
end

npcType.onSay = function(npc, creature, type, message)
	npcHandler:onSay(npc, creature, type, message)
end

npcType.onCloseChannel = function(npc, creature)
	npcHandler:onCloseChannel(npc, creature)
end

keywordHandler:addKeyword({ "offer", "wares" }, StdModule.say, { npcHandler = npcHandler, text = "Ask me for {trade} and I'll show you my offers." })

npcHandler:setMessage(MESSAGE_GREET, "Hello |PLAYERNAME|. Say {trade} to see my offers.")
npcHandler:setMessage(MESSAGE_FAREWELL, "Farewell, |PLAYERNAME|!")
npcHandler:setMessage(MESSAGE_WALKAWAY, "Farewell, |PLAYERNAME|!")
npcHandler:setMessage(MESSAGE_SENDTRADE, "Here is what I offer!")
npcHandler:addModule(FocusModule:new(), npcConfig.name, true, true, true)

npcConfig.shop = {
	{ id = 3503, buy = 15 },
	{ id = 3507, buy = 5 },
	{ id = 3505, buy = 10 },
}

npcType.onBuyItem = function(npc, player, itemId, subType, amount, ignore, inBackpacks, totalCost)
	npc:sellItem(player, itemId, amount, subType, 0, ignore, inBackpacks)
end

npcType.onSellItem = function(npc, player, itemId, subtype, amount, ignore, name, totalCost)
	player:sendTextMessage(MESSAGE_INFO_DESCR, string.format("Sold %ix %s for %i gold.", amount, name, totalCost))
end

npcType.onCheckItem = function(npc, player, clientId, subType)
end

npcType:register(npcConfig)