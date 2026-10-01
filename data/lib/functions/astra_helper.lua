AstraHelper = AstraHelper or {}

AstraHelper.OPCODES = {
	Cavebot = 210,
	CastOnFoot = 211,
	SmartFollow = 212,
	NpcConversationEnd = 213,
	BotCheckAlert = 230,
}

AstraHelper.STORAGES = {
	Cavebot = 99997,
	SmartFollow = 99998,
}

function AstraHelper.isCavebotEnabled(player)
	return player and player:getStorageValue(AstraHelper.STORAGES.Cavebot) == 1
end

function AstraHelper.isSmartFollowEnabled(player)
	return player and player:getStorageValue(AstraHelper.STORAGES.SmartFollow) == 1
end

function AstraHelper.isHelperToolEnabled(player)
	return AstraHelper.isCavebotEnabled(player) or AstraHelper.isSmartFollowEnabled(player)
end

function AstraHelper.sendBotCheckAlert(player, enabled)
	if not player or not player.sendExtendedOpcode then
		return false
	end

	return player:sendExtendedOpcode(AstraHelper.OPCODES.BotCheckAlert, enabled and "start" or "stop")
end

-- An explicit focus-release signal; do not infer farewell from localized text
-- or mistake a shop-close packet for the end of a conversation.
function AstraHelper.sendNpcConversationEnd(player, npc)
	if not player or not npc or not player:isUsingAstraClient() then
		return false
	end
	return player:sendExtendedOpcode(AstraHelper.OPCODES.NpcConversationEnd, npc:getName())
end
