local loot = ChatChannel(10, "Loot")
loot:public(true)

function loot.onSpeak(player, type, message)
	-- read-only channel for automated loot notifications
	return false
end

loot:register()
