local internalNpcName = "Mad"
local npcType = Game.createNpcType(internalNpcName)
local npcConfig = {}

npcConfig.name = internalNpcName
npcConfig.description = internalNpcName

npcConfig.health = 100
npcConfig.maxHealth = npcConfig.health
npcConfig.walkInterval = 2000
npcConfig.walkRadius = 2

npcConfig.outfit = {
	lookType = 129, lookHead = 75, lookBody = 115, lookLegs = 125, lookFeet = 85, lookAddons = 3
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

npcHandler:setMessage(MESSAGE_GREET, "Hello |PLAYERNAME|. I buy and sell equipment. Say {trade}!")
npcHandler:setMessage(MESSAGE_FAREWELL, "Farewell, |PLAYERNAME|!")
npcHandler:setMessage(MESSAGE_WALKAWAY, "Farewell, |PLAYERNAME|!")
npcHandler:setMessage(MESSAGE_SENDTRADE, "Here is what I offer!")
npcHandler:addModule(FocusModule:new(), npcConfig.name, true, true, true)

npcConfig.shop = {
	{ id = 5460, buy = 10000 },
	{ id = 3394, sell = 2500 },
	{ id = 3437, sell = 4000 },
	{ id = 3266, sell = 100 },
	{ id = 3305, sell = 60 },
	{ id = 3418, sell = 1500 },
	{ id = 3423, sell = 150000 },
	{ id = 3567, sell = 15000 },
	{ id = 3079, sell = 40000 },
	{ id = 3359, sell = 200 },
	{ id = 3295, sell = 6000 },
	{ id = 3301, sell = 70 },
	{ id = 3358, sell = 100 },
	{ id = 3352, sell = 35 },
	{ id = 3311, sell = 200 },
	{ id = 3381, sell = 20000 },
	{ id = 3385, sell = 5000 },
	{ id = 3382, sell = 15000 },
	{ id = 3419, sell = 5000 },
	{ id = 3391, sell = 9000 },
	{ id = 3388, sell = 90000 },
	{ id = 3420, sell = 40000 },
	{ id = 3356, sell = 4000 },
	{ id = 3275, sell = 200 },
	{ id = 3322, sell = 2000 },
	{ id = 3302, sell = 10000 },
	{ id = 3386, sell = 60000 },
	{ id = 7402, sell = 20000 },
	{ id = 7419, sell = 30000 },
	{ id = 3425, sell = 100 },
	{ id = 3320, sell = 10000 },
	{ id = 3280, sell = 3000 },
	{ id = 3281, sell = 10000 },
	{ id = 3360, sell = 30000 },
	{ id = 3555, sell = 200000 },
	{ id = 3364, sell = 80000 },
	{ id = 3422, sell = 100000 },
	{ id = 3315, sell = 7500 },
	{ id = 3415, sell = 200 },
	{ id = 3269, sell = 200 },
	{ id = 3276, sell = 20 },
	{ id = 3284, sell = 4000 },
	{ id = 3353, sell = 30 },
	{ id = 3370, sell = 5000 },
	{ id = 3318, sell = 2000 },
	{ id = 3371, sell = 6000 },
	{ id = 3286, sell = 30 },
	{ id = 3278, sell = 150000 },
	{ id = 3366, sell = 200000 },
	{ id = 3288, sell = 120000 },
	{ id = 3414, sell = 60000 },
	{ id = 3436, sell = 8000 },
	{ id = 3574, sell = 500 },
	{ id = 8063, sell = 30000 },
	{ id = 3357, sell = 400 },
	{ id = 3557, sell = 500 },
	{ id = 3392, sell = 40000 },
	{ id = 3297, sell = 1500 },
	{ id = 3294, sell = 30 },
	{ id = 5741, sell = 35000 },
	{ id = 3324, sell = 6000 },
	{ id = 8061, sell = 60000 },
	{ id = 3271, sell = 800 },
	{ id = 3554, sell = 40000 },
	{ id = 3319, sell = 90000 },
	{ id = 3264, sell = 25 },
	{ id = 3309, sell = 90000 },
	{ id = 3428, sell = 4000 },
	{ id = 3265, sell = 400 },
	{ id = 3434, sell = 25000 },
	{ id = 3279, sell = 6000 },
	{ id = 3296, sell = 100000 },
	{ id = 3369, sell = 6000 },
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