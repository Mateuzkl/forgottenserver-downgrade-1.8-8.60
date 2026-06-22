local CHANNEL_LOOT = 10

local function sendLootMessage(player, text)
	player:sendChannelMessage("", text, TALKTYPE_CHANNEL_O, CHANNEL_LOOT)
end

local function formatHundredthsPercent(value)
	return string.format("%.2f", (tonumber(value) or 0) / 100):gsub("0+$", ""):gsub("%.$", "")
end

local function addLootRecipient(recipients, player)
	if player then
		recipients[#recipients + 1] = player
	end
end

local function getLootRecipients(player)
	local recipients = {}
	local party = player:getParty()
	if party then
		addLootRecipient(recipients, party:getLeader())
		for _, member in ipairs(party:getMembers()) do
			addLootRecipient(recipients, member)
		end
	else
		addLootRecipient(recipients, player)
	end
	return recipients
end

local function sendUngroupedLootMessage(player, corpse, monsterName, preyLootText, bountyLootText, useColorized)
	local recipients = getLootRecipients(player)
	if #recipients == 0 then
		return
	end

	local plainText = nil
	local colorizedText = nil
	local needColorized = false

	if useColorized then
		for _, recipient in ipairs(recipients) do
			if recipient.isUsingAstraClient and recipient:isUsingAstraClient() then
				needColorized = true
				break
			end
		end
	end

	local function buildText(colorized)
		return ("Loot of %s: %s%s%s."):format(
			monsterName, corpse:getContentDescription(colorized), preyLootText, bountyLootText)
	end

	for _, recipient in ipairs(recipients) do
		local wantsColorized = needColorized and recipient.isUsingAstraClient and recipient:isUsingAstraClient()
		if wantsColorized then
			colorizedText = colorizedText or buildText(true)
			sendLootMessage(recipient, colorizedText)
		else
			plainText = plainText or buildText(false)
			sendLootMessage(recipient, plainText)
		end
	end
end

-- Applies the player's drop bonus (equipped items) to the loot chance.
-- Returns true if the item should be added, false otherwise.
local function rollWithDropBonus(lootChance, player)
	local chance = lootChance
	local rateLoot = configManager.getNumber(configKeys.RATE_LOOT)
	if rateLoot > 0 then
		chance = chance * rateLoot
	end

	if player then
		local bonus = player:getDropBonus()
		if bonus > 0 then
			-- ex: bonus=15 → chance * 1.15
			chance = math.floor(chance * (1 + bonus / 100))
		end
	end
	-- Maximum TFS chance is MAX_LOOTCHANCE (100000)
	return math.random(1, 100000) <= chance
end

local function createGuaranteedLootItem(corpse, lootItem)
	local guaranteedItem = {}
	for key, value in pairs(lootItem) do
		guaranteedItem[key] = value
	end
	guaranteedItem.chance = 100000

	local item = corpse:createLootItem(guaranteedItem)
	if not item then
		print("[Warning] DropLoot:", "Could not add loot item to corpse.")
	end
end

local event = Event()
event.onDropLoot = function(self, corpse)
	if configManager.getNumber(configKeys.RATE_LOOT) == 0 then return end

	local player = Player(corpse:getCorpseOwner())
	local mType = self:getType()
	local mTypeRaceId = mType:raceId()

	local staminaOk = true
	if player and configManager.getBoolean(configKeys.STAMINA_SYSTEM) then
		staminaOk = player:getStamina() > 840
	end

	if not player or staminaOk then
		local monsterLoot = mType:getLoot()
		local rolls = 1

		local boostedCreature = Game.getBoostedCreature()
		if boostedCreature and boostedCreature:lower() == mType:getName():lower() then
			rolls = math.max(1, math.floor(configManager.getFloat(configKeys.BOOSTED_LOOT_MULTIPLIER)))
		end

		-- Boosted Boss loot bonus
		if CustomBosstiary and CustomBosstiary.isBoostedBoss then
			if CustomBosstiary.isBoostedBoss(mTypeRaceId) then
				local boostedBossLootBonus = CustomBosstiary.getBoostedBossLootBonus()
				rolls = math.max(1, math.floor(rolls * (1 + boostedBossLootBonus / 100)))
			end
		end

		for roll = 1, rolls do
			for i = 1, #monsterLoot do
				local lootItem = monsterLoot[i]

				-- Applies the drop bonus.
				if player and lootItem.chance and lootItem.chance < 100000 then
					if rollWithDropBonus(lootItem.chance, player) then
						createGuaranteedLootItem(corpse, lootItem)
					end
				else
					-- Items with a 100% chance or no defined chance.
					local item = corpse:createLootItem(lootItem)
					if not item then
						print("[Warning] DropLoot:", "Could not add loot item to corpse.")
					end
				end
			end
		end

		local preyLootBonus = 0
		if player and PreySystem then
			local bonusType, bonusValue = PreySystem.getBonus(player, self:getName())
			if bonusType == PreySystem.BONUS_LOOT then
				preyLootBonus = bonusValue or 0
				for i = 1, #monsterLoot do
					local lootItem = monsterLoot[i]
					local chance = lootItem.chance or 100000
					if math.random(1, 100) <= bonusValue and (chance >= 100000 or rollWithDropBonus(chance, player)) then
						createGuaranteedLootItem(corpse, lootItem)
					end
				end
			end
		end

		local bountyLootBonus = 0
		if player and TaskBoard and TaskBoard.getBountyTalismanBonus then
			bountyLootBonus = TaskBoard.getBountyTalismanBonus(player, mTypeRaceId, 2)
			if bountyLootBonus > 0 and math.random(1, 10000) <= bountyLootBonus then
				for i = 1, #monsterLoot do
					local lootItem = monsterLoot[i]
					local chance = lootItem.chance or 100000
					if chance >= 100000 or rollWithDropBonus(chance, player) then
						createGuaranteedLootItem(corpse, lootItem)
					end
				end
			end
		end

		if player then
			local lootGroupingEnabled = configManager.getBoolean(configKeys.LOOT_GROUPING_ENABLED)
			if not lootGroupingEnabled then
				local preyLootText = preyLootBonus > 0 and (" (Prey Improved Loot +%d%%)"):format(preyLootBonus) or ""
				local bountyLootText = bountyLootBonus > 0 and
					(" (Bounty More Loot +%s%%)"):format(formatHundredthsPercent(bountyLootBonus)) or ""
				local useColorized = configManager.getBoolean(configKeys.COLORIZED_LOOT_VALUE)
				sendUngroupedLootMessage(player, corpse, mType:getNameDescription(), preyLootText, bountyLootText, useColorized)
			end
		end
	else
		if not configManager.getBoolean(configKeys.LOOT_GROUPING_ENABLED) then
			local text = ("Loot of %s: nothing (due to low stamina)"):format(
				             mType:getNameDescription())
			local party = player:getParty()
			if party then
				party:broadcastPartyLoot(text)
			else
				sendLootMessage(player, text)
			end
		end
	end
end
event:register()
