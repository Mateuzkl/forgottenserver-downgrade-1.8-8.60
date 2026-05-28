-- Rarity System Helpers

function rarityDebug(message)
	if RARITY_SYSTEM_ENABLED then
		print("[Rarity] " .. tostring(message))
	end
end
