local CLIENT_PACKET_CHARACTER_INFO = 0xE5
local SERVER_PACKET_CHARACTER_INFO = 0xDA
local CHARACTER_INFO_ACHIEVEMENTS = 5

local function supportsAchievements(player)
	return player and player.isUsingAstraClient and player:isUsingAstraClient()
end

local function sendAchievements(player)
	local unlocked = player:getAchievements()
	table.sort(unlocked)

	local secretUnlocked = 0
	for _, achievementId in ipairs(unlocked) do
		local achievement = getAchievementInfoById(achievementId)
		if achievement and achievement.secret then
			secretUnlocked = secretUnlocked + 1
		end
	end

	local msg = NetworkMessage(player)
	msg:addByte(SERVER_PACKET_CHARACTER_INFO)
	msg:addByte(CHARACTER_INFO_ACHIEVEMENTS)
	msg:addU16(math.min(player:getAchievementPoints(), 0xFFFF))
	msg:addU16(math.min(secretUnlocked, 0xFFFF))
	msg:addU16(math.min(#unlocked, 0xFFFF))

	for index = 1, math.min(#unlocked, 0xFFFF) do
		local achievementId = unlocked[index]
		local achievement = getAchievementInfoById(achievementId)
		local unlockedAt = tonumber(player:getStorageValue(PlayerStorageKeys.achievementsBase + achievementId)) or 0

		msg:addU16(achievementId)
		msg:addU32(unlockedAt > 1 and math.min(unlockedAt, 0xFFFFFFFF) or 0)
		if achievement and achievement.secret then
			msg:addByte(1)
			msg:addString(achievement.name)
			msg:addString(achievement.description or "")
			msg:addByte(math.min(tonumber(achievement.grade) or 0, 0xFF))
		else
			msg:addByte(0)
		end
	end

	return msg:sendToPlayer(player)
end

local achievementsHandler = PacketHandler(CLIENT_PACKET_CHARACTER_INFO)
function achievementsHandler.onReceive(player, msg)
	if not supportsAchievements(player) or not NetworkGuard.cooldown(player, "character-achievements", 250) then
		return
	end

	local characterId = NetworkGuard.readU32(msg)
	local informationType = NetworkGuard.readByte(msg)
	if characterId == nil or informationType ~= CHARACTER_INFO_ACHIEVEMENTS then
		return
	end

	-- The 8.60 Astra client may only inspect its own character.
	if characterId ~= 0 and characterId ~= player:getId() and characterId ~= player:getGuid() then
		return
	end

	sendAchievements(player)
end
achievementsHandler:register()
