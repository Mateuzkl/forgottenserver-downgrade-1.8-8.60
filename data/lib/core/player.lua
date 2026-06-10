local foodCondition = Condition(CONDITION_REGENERATION, CONDITIONID_DEFAULT)

function Player.feed(self, food)
	local condition = self:getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT)
	if condition then
		condition:setTicks(condition:getTicks() + (food * 1000))
	else
		local vocation = self:getVocation()
		if not vocation then return nil end

		foodCondition:setTicks(food * 1000)
		foodCondition:setParameter(CONDITION_PARAM_HEALTHGAIN, vocation:getHealthGainAmount())
		foodCondition:setParameter(CONDITION_PARAM_HEALTHTICKS, vocation:getHealthGainTicks() * 1000)
		foodCondition:setParameter(CONDITION_PARAM_MANAGAIN, vocation:getManaGainAmount())
		foodCondition:setParameter(CONDITION_PARAM_MANATICKS, vocation:getManaGainTicks() * 1000)

		self:addCondition(foodCondition)
	end
	return true
end

function Player.isSorcerer(self)
	return table.contains({ VOCATION.ID.SORCERER, VOCATION.ID.MASTER_SORCERER }, self:getVocation():getId())
end

function Player.isDruid(self)
	return table.contains({ VOCATION.ID.DRUID, VOCATION.ID.ELDER_DRUID }, self:getVocation():getId())
end

function Player.isKnight(self)
	return table.contains({ VOCATION.ID.KNIGHT, VOCATION.ID.ELITE_KNIGHT }, self:getVocation():getId())
end

function Player.isPaladin(self)
	return table.contains({ VOCATION.ID.PALADIN, VOCATION.ID.ROYAL_PALADIN }, self:getVocation():getId())
end

function Player.isMage(self)
	return table.contains({ VOCATION.ID.SORCERER, VOCATION.ID.MASTER_SORCERER, VOCATION.ID.DRUID, VOCATION.ID.ELDER_DRUID }, self:getVocation():getId())
end

function Player.isMonk(self)
	if not configManager.getBoolean(configKeys.MONK_VOCATION_ENABLED) then
		return false
	end

	return table.contains({ VOCATION.ID.MONK, VOCATION.ID.EXALTED_MONK }, self:getVocation():getId())
end

function Player.getClosestFreePosition(self, position, extended)
	if self:getGroup():getAccess() and self:getAccountType() >= ACCOUNT_TYPE_GOD then return position end
	return Creature.getClosestFreePosition(self, position, extended)
end

function Player.getDepotItems(self, depotId)
	return self:getDepotChest(depotId, true):getItemHoldingCount()
end

function Player.hasFlag(self, flag) return self:getGroup():hasFlag(flag) end

function Player.getLossPercent(self)
	local blessings = 0
	for i = 1, 8 do
		if self:hasBlessing(i) then
			blessings = blessings + 1
		end
	end
	local blessingReduction = blessings * 8
	local basePenalty = self:getDeathPenalty()
	local finalPenalty = math.max(0, basePenalty - blessingReduction)
	return finalPenalty
end

function Player.getPremiumTime(self) return math.max(0, self:getPremiumEndsAt() - os.time()) end

function Player.setPremiumTime(self, seconds)
	self:setPremiumEndsAt(os.time() + seconds)
	return true
end

function Player.addPremiumTime(self, seconds)
	self:setPremiumTime(self:getPremiumTime() + seconds)
	return true
end

function Player.removePremiumTime(self, seconds)
	local currentTime = self:getPremiumTime()
	if currentTime < seconds then return false end

	self:setPremiumTime(currentTime - seconds)
	return true
end

function Player.getPremiumDays(self) return math.floor(self:getPremiumTime() / 86400) end

function Player.addPremiumDays(self, days) return self:addPremiumTime(days * 86400) end

function Player.removePremiumDays(self, days) return self:removePremiumTime(days * 86400) end

function Player.isPremium(self)
	return self:getPremiumTime() > 0 or configManager.getBoolean(configKeys.FREE_PREMIUM) or
		       self:hasFlag(PlayerFlag_IsAlwaysPremium)
end

---@param message string|number
function Player.sendCancelMessage(self, message)
	if type(message) == "number" then message = Game.getReturnMessage(message) end
	return self:sendTextMessage(MESSAGE_STATUS_SMALL, message)
end

function Player.isUsingOtClient(self)
	if self.isUsingOtc and self:isUsingOtc() then
		return true
	end

	if self.isUsingOtcV8 and self:isUsingOtcV8() then
		return true
	end

	local client = self:getClient()
	local os = client and client.os or CLIENTOS_NONE
	return os == CLIENTOS_OTCLIENT_LINUX or os == CLIENTOS_OTCLIENT_WINDOWS or os == CLIENTOS_OTCLIENT_MAC or
		       (os >= CLIENTOS_OTCLIENTV8_LINUX and os <= CLIENTOS_OTCLIENTV8_WEB)
end

function Player.sendExtendedOpcode(self, opcode, buffer)
	if not self:isUsingOtClient() then return false end
	buffer = buffer or ""
	if type(buffer) ~= "string" then
		buffer = tostring(buffer)
	end
	-- NetworkMessage::addString silently refuses strings above 8192 bytes.
	-- Avoid sending a malformed packet containing only 0x32 + opcode.
	if #buffer > 8192 then
		return false
	end

	local networkMessage<close> = NetworkMessage()
	networkMessage:addByte(0x32)
	networkMessage:addByte(opcode)
	networkMessage:addString(buffer)
	networkMessage:sendToPlayer(self)
	return true
end

function Player.sendFightMode(self)
	if not self:isUsingOtClient() then return false end

	local msg<close> = NetworkMessage()
	msg:addByte(0xA7)
	msg:addByte(self:getFightMode())
	msg:addByte(self:isChasingEnabled() and 1 or 0)
	msg:addByte(self:isSecureModeEnabled() and 1 or 0)
	msg:sendToPlayer(self)
	return true
end

-- Always pass the number through the isValidMoney function first before using the transferMoneyTo
function Player.transferMoneyTo(self, target, amount)
	if not target then return false end

	-- See if you can afford this transfer
	local balance = self:getBankBalance()
	if amount > balance then return false end

	-- See if player is online
	local targetPlayer = Player(target.guid)
	if targetPlayer then
		targetPlayer:setBankBalance(targetPlayer:getBankBalance() + amount)
	else
		db.query("UPDATE `players` SET `balance` = `balance` + " .. amount .. " WHERE `id` = '" ..
			         target.guid .. "'")
	end

	self:setBankBalance(self:getBankBalance() - amount)
	return true
end

function Player.canCarryMoney(self, amount)
	-- Anyone can carry as much imaginary money as they desire
	if amount == 0 then return true end

	-- The 3 below loops will populate these local variables
	local totalWeight = 0
	local inventorySlots = 0

	local currencyItems = Game.getCurrencyItems()
	for index = #currencyItems, 1, -1 do
		local currency = currencyItems[index]
		-- Add currency coins to totalWeight and inventorySlots
		local worth = currency:getWorth()
		local currencyCoins = math.floor(amount / worth)
		if currencyCoins > 0 then
			amount = amount - (currencyCoins * worth)
			while currencyCoins > 0 do
				local count = math.min(100, currencyCoins)
				totalWeight = totalWeight + currency:getWeight(count)
				currencyCoins = currencyCoins - count
				inventorySlots = inventorySlots + 1
			end
		end
	end

	-- If player don't have enough capacity to carry this money
	if self:getFreeCapacity() < totalWeight then return false end

	-- If player don't have enough available inventory slots to carry this money
	local backpack = self:getSlotItem(CONST_SLOT_BACKPACK) --[[@as Container]]
	if not backpack or backpack:getEmptySlots(true) < inventorySlots then return false end
	return true
end

function Player.withdrawMoney(self, amount)
	local balance = self:getBankBalance()
	if amount > balance or not self:addMoney(amount) then return false end

	self:setBankBalance(balance - amount)
	return true
end

function Player.depositMoney(self, amount)
	if not self:removeMoney(amount) then return false end

	self:setBankBalance(self:getBankBalance() + amount)
	return true
end

function Player.removeTotalMoney(self, amount)
	local moneyCount = self:getMoney()
	local bankCount = self:getBankBalance()
	if amount <= moneyCount then
		self:removeMoney(amount)
		return true
	elseif amount <= (moneyCount + bankCount) then
		if moneyCount ~= 0 then
			self:removeMoney(moneyCount)
			local remains = amount - moneyCount
			self:setBankBalance(bankCount - remains)
			self:sendTextMessage(MESSAGE_INFO_DESCR,
			                     ("Paid %d from inventory and %d gold from bank account. Your account balance is now %d gold."):format(
				                     moneyCount, amount - moneyCount, self:getBankBalance()))
			return true
		end
			self:setBankBalance(bankCount - amount)
			self:sendTextMessage(MESSAGE_INFO_DESCR,
			                     ("Paid %d gold from bank account. Your account balance is now %d gold."):format(
				                     amount, self:getBankBalance()))
			return true
		end
	return false
end

function Player.removeMoneyBank(self, amount)
	return self:removeTotalMoney(amount)
end

function Player.addLevel(self, amount, round)
	round = round or false
	local level, amount = self:getLevel(), amount or 1
	if amount > 0 then
		return self:addExperience(Game.getExperienceForLevel(level + amount) -
			                          (round and self:getExperience() or Game.getExperienceForLevel(level)))
	end
		return self:removeExperience(
			       ((round and self:getExperience() or Game.getExperienceForLevel(level)) -
				       Game.getExperienceForLevel(level + amount)))
end

function Player.addMagicLevel(self, value)
	local currentMagLevel = self:getBaseMagicLevel()
	local sum = 0

	if value > 0 then
		while value > 0 do
			sum = sum + self:getVocation():getRequiredManaSpent(currentMagLevel + value)
			value = value - 1
		end

		return self:addManaSpent(sum - self:getManaSpent())
	else
		value = math.min(currentMagLevel, math.abs(value))
		while value > 0 do
			sum = sum + self:getVocation():getRequiredManaSpent(currentMagLevel - value + 1)
			value = value - 1
		end

		return self:removeManaSpent(sum + self:getManaSpent())
	end
end

function Player.addSkillLevel(self, skillId, value)
	local currentSkillLevel = self:getSkillLevel(skillId)
	local sum = 0

	if value > 0 then
		while value > 0 do
			sum = sum + self:getVocation():getRequiredSkillTries(skillId, currentSkillLevel + value)
			value = value - 1
		end

		return self:addSkillTries(skillId, sum - self:getSkillTries(skillId))
	else
		value = math.min(currentSkillLevel, math.abs(value))
		while value > 0 do
			sum = sum + self:getVocation():getRequiredSkillTries(skillId, currentSkillLevel - value + 1)
			value = value - 1
		end

		return self:removeSkillTries(skillId, sum + self:getSkillTries(skillId), true)
	end
end

function Player.addSkill(self, skillId, value, round)
	if skillId == SKILL_LEVEL then
		return self:addLevel(value, round or false)
	elseif skillId == SKILL_MAGLEVEL then
		return self:addMagicLevel(value)
	end
	return self:addSkillLevel(skillId, value)
end

function Player.getWeaponType(self)
	local weapon = self:getSlotItem(CONST_SLOT_LEFT)
	if weapon then return weapon:getType():getWeaponType() end
	return WEAPON_NONE
end

function Player.isPromoted(self)
	local vocation = self:getVocation()
	if not vocation then
		return false
	end

	local demotion = vocation:getDemotion()
	if not demotion then
		return false
	end

	return vocation:getId() ~= demotion:getId()
end

function Player.setAccountStorageValue(self, key, value)
	return Game.setAccountStorageValue(self:getAccountId(), key, value)
end

function Player.getAccountStorageValue(self, key)
	return Game.getAccountStorageValue(self:getAccountId(), key)
end

function Player.addTibiaCoins(self, tibiaCoins)
	return self:setTibiaCoins(self:getTibiaCoins() + tibiaCoins)
end

function Player.removeTibiaCoins(self, removeCoins)
	local tibiaCoins = self:getTibiaCoins()
	if tibiaCoins < removeCoins then return false end
	return self:setTibiaCoins(tibiaCoins - removeCoins)
end

function Player.setExhaustion(self, key, milliseconds)
	return self:setStorageValue(key, os.mtime() + milliseconds)
end

function Player.getExhaustion(self, key)
	local milliseconds = self:getStorageValue(key)
	if not milliseconds then return 0 end
	return math.max(0, os.mtime() - milliseconds)
end

function Player.hasExhaustion(self, key) return self:getExhaustion(key) > 0 end

function Player.questKV(self, questName)
	return self:kv():scoped("quests"):scoped(questName)
end

---@param type ExperienceRateType
---@param value integer
function Player:addExperienceRate(type, value)
	return self:setExperienceRate(type, self:getExperienceRate(type) + value)
end

do
	if not nextUseStaminaTime then nextUseStaminaTime = {} end

	local function useStamina(player)
		local staminaMinutes = player:getStamina()
		if staminaMinutes == 0 then return end

		local playerId = player:getId()
		if not nextUseStaminaTime[playerId] then nextUseStaminaTime[playerId] = 0 end

		local currentTime = os.time()
		local timePassed = currentTime - nextUseStaminaTime[playerId]
		if timePassed <= 0 then return end

		if timePassed > 60 then
			if staminaMinutes > 2 then
				staminaMinutes = staminaMinutes - 2
			else
				staminaMinutes = 0
			end
			nextUseStaminaTime[playerId] = currentTime + 120
		else
			staminaMinutes = staminaMinutes - 1
			nextUseStaminaTime[playerId] = currentTime + 60
		end
		player:setStamina(math.floor(staminaMinutes))
	end

	function Player:updateStamina()
		if not configManager.getBoolean(configKeys.STAMINA_SYSTEM) then return false end

		useStamina(self)

		local staminaMinutes = self:getStamina()
		if staminaMinutes > 2400 and self:isPremium() then
			self:setExperienceRate(ExperienceRateType.STAMINA, 150)
		elseif staminaMinutes <= 840 then
			self:setExperienceRate(ExperienceRateType.STAMINA, 100)
		end
		return true
	end

	-- Guild Balance Functions
	function Player.depositGuildMoney(self, amount)
		local guild = self:getGuild()
		if not guild then
			return false, "You are not in a guild."
		end

		if not self:removeMoney(amount) then
			return false, "You don't have enough money."
		end

		guild:setBankBalance(guild:getBankBalance() + amount)
		return true, "Successfully deposited " .. amount .. " gold to guild bank."
	end

	function Player.withdrawGuildMoney(self, amount)
		local guild = self:getGuild()
		if not guild then
			return false, "You are not in a guild."
		end

		-- Check if player has permission (Leader or Vice-Leader)
		if self:getGuid() ~= guild:getOwnerGUID() and self:getGuildLevel() ~= 2 then
			return false, "Only guild leaders and vice-leaders can withdraw money."
		end

		if amount > guild:getBankBalance() then
			return false, "Guild doesn't have enough money."
		end

		if not self:addMoney(amount) then
			return false, "You can't carry that much money."
		end

		guild:setBankBalance(guild:getBankBalance() - amount)
		return true, "Successfully withdrew " .. amount .. " gold from guild bank."
	end

	function Player.transferGuildMoneyToPlayer(self, targetPlayer, amount)
		local guild = self:getGuild()
		if not guild then
			return false, "You are not in a guild."
		end

		-- Check if player has permission (Leader or Vice-Leader)
		if self:getGuid() ~= guild:getOwnerGUID() and self:getGuildLevel() ~= 2 then
			return false, "Only guild leaders and vice-leaders can transfer money."
		end

		if amount > guild:getBankBalance() then
			return false, "Guild doesn't have enough money."
		end

		local target = Player(targetPlayer)
		if not target then
			return false, "Target player not found."
		end

		guild:setBankBalance(guild:getBankBalance() - amount)
		target:setBankBalance(target:getBankBalance() + amount)
		return true, "Successfully transferred " .. amount .. " gold to " .. target:getName() .. "."
	end

	function Player.getGuildBalance(self)
		local guild = self:getGuild()
		if not guild then
			return 0
		end
		return guild:getBankBalance()
	end
end
