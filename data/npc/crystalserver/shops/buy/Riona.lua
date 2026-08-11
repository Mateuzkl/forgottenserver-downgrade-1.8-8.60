local internalNpcName = "Riona"
local npcType = Game.createNpcType(internalNpcName)
local npcConfig = {}

npcConfig.name = internalNpcName
npcConfig.description = internalNpcName

npcConfig.health = 100
npcConfig.maxHealth = npcConfig.health
npcConfig.walkInterval = 2000
npcConfig.walkRadius = 2

npcConfig.outfit = {
	lookType = 138, lookHead = 57, lookBody = 59, lookLegs = 40, lookFeet = 76, lookAddons = 0
}

npcConfig.flags = {
	floorchange = false,
}

npcConfig.speechBubble = 2
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

npcHandler:setMessage(MESSAGE_GREET, "Hello |PLAYERNAME|. I sell backpacks and tools. Say {trade} to see my offers!")
npcHandler:setMessage(MESSAGE_FAREWELL, "Farewell, |PLAYERNAME|!")
npcHandler:setMessage(MESSAGE_WALKAWAY, "Farewell, |PLAYERNAME|!")
npcHandler:setMessage(MESSAGE_SENDTRADE, "Here is what I offer!")
npcHandler:addModule(FocusModule:new(), npcConfig.name, true, true, true)

npcConfig.shop = {
	{ id = 2854, buy = 20 },
	{ id = 3253, buy = 10000 },
	{ id = 5949, buy = 50 },
	{ id = 2869, buy = 50 },
	{ id = 8860, buy = 50 },
	{ id = 2872, buy = 50 },
	{ id = 9605, buy = 50 },
	{ id = 9601, buy = 50 },
	{ id = 7342, buy = 50 },
	{ id = 2871, buy = 50 },
	{ id = 2865, buy = 50 },
	{ id = 10327, buy = 50 },
	{ id = 9604, buy = 50 },
	{ id = 9602, buy = 50 },
	{ id = 5926, buy = 50 },
	{ id = 2866, buy = 50 },
	{ id = 5942, buy = 20000 },
	{ id = 5908, buy = 10000 },
	{ id = 2983, buy = 1000 },
	{ id = 2981, buy = 1000 },
	{ id = 2984, buy = 1000 },
	{ id = 2985, buy = 1000 },
	{ id = 3308, buy = 1000 },
	{ id = 3453, buy = 1500 },
	{ id = 3456, buy = 10 },
	{ id = 3003, buy = 50 },
	{ id = 3457, buy = 20 },
	{ id = 5710, buy = 2000 },
	{ id = 3483, buy = 100 },
	{ id = 3492, buy = 35 },
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