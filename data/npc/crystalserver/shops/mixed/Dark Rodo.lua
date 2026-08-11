local internalNpcName = "Dark Rodo"
local npcType = Game.createNpcType(internalNpcName)
local npcConfig = {}

npcConfig.name = internalNpcName
npcConfig.description = internalNpcName

npcConfig.health = 100
npcConfig.maxHealth = npcConfig.health
npcConfig.walkInterval = 2000
npcConfig.walkRadius = 2

npcConfig.outfit = {
	lookType = 133, lookHead = 0, lookBody = 86, lookLegs = 0, lookFeet = 38, lookAddons = 1
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

npcHandler:setMessage(MESSAGE_GREET, "Hello |PLAYERNAME|. I sell runes, potions, wands and rods. Say {trade}.")
npcHandler:setMessage(MESSAGE_FAREWELL, "Farewell, |PLAYERNAME|!")
npcHandler:setMessage(MESSAGE_WALKAWAY, "Farewell, |PLAYERNAME|!")
npcHandler:setMessage(MESSAGE_SENDTRADE, "Here is what I offer!")
npcHandler:addModule(FocusModule:new(), npcConfig.name, true, true, true)

npcConfig.shop = {
	{ id = 3059, buy = 150 },
	{ id = 3047, buy = 400 },
	{ id = 7876, buy = 20 },
	{ id = 266, buy = 45 },
	{ id = 268, buy = 50 },
	{ id = 236, buy = 100 },
	{ id = 237, buy = 80 },
	{ id = 239, buy = 190 },
	{ id = 238, buy = 120 },
	{ id = 7642, buy = 190 },
	{ id = 7643, buy = 310 },
	{ id = 7644, buy = 50 },
	{ id = 3203, buy = 375 },
	{ id = 3153, buy = 250 },
	{ id = 3161, buy = 250 },
	{ id = 3147, buy = 250 },
	{ id = 3178, buy = 210 },
	{ id = 3177, buy = 80 },
	{ id = 3148, buy = 45 },
	{ id = 3197, buy = 80 },
	{ id = 3149, buy = 250 },
	{ id = 3164, buy = 250 },
	{ id = 3166, buy = 250 },
	{ id = 3200, buy = 250 },
	{ id = 3192, buy = 250 },
	{ id = 3188, buy = 250 },
	{ id = 3190, buy = 250 },
	{ id = 3189, buy = 250 },
	{ id = 3191, buy = 180 },
	{ id = 3198, buy = 120 },
	{ id = 3182, buy = 250 },
	{ id = 3158, buy = 250 },
	{ id = 3152, buy = 95 },
	{ id = 3174, buy = 40 },
	{ id = 3180, buy = 350 },
	{ id = 3165, buy = 700 },
	{ id = 3173, buy = 250 },
	{ id = 3172, buy = 250 },
	{ id = 3176, buy = 250 },
	{ id = 3195, buy = 250 },
	{ id = 3179, buy = 250 },
	{ id = 3175, buy = 250 },
	{ id = 3155, buy = 350 },
	{ id = 3202, buy = 250 },
	{ id = 3160, buy = 175 },
	{ id = 3156, buy = 250 },
	{ id = 3074, buy = 500, sell = 250 },
	{ id = 3075, buy = 1000, sell = 500 },
	{ id = 3072, buy = 5000, sell = 2500 },
	{ id = 8093, buy = 7500, sell = 3750 },
	{ id = 3073, buy = 10000, sell = 5000 },
	{ id = 3071, buy = 15000, sell = 7500 },
	{ id = 8092, buy = 18000, sell = 9000 },
	{ id = 8094, buy = 22000, sell = 11000 },
	{ id = 3066, buy = 500, sell = 250 },
	{ id = 3070, buy = 1000, sell = 500 },
	{ id = 3069, buy = 5000, sell = 2500 },
	{ id = 8083, buy = 7500, sell = 3750 },
	{ id = 3065, buy = 10000, sell = 5000 },
	{ id = 3067, buy = 15000, sell = 7500 },
	{ id = 8084, buy = 18000, sell = 9000 },
	{ id = 8082, buy = 22000, sell = 11000 },
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