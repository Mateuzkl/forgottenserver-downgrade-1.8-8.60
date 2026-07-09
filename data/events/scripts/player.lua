function Player:onLook(thing, position, distance)
	local description = ""
	if hasEvent.onLook then description = Event.onLook(self, thing, position, distance, description) end

	if description ~= "" then self:sendTextMessage(MESSAGE_INFO_DESCR, description) end
end

function Player:onLookInBattleList(creature, distance)
	local description = ""
	if hasEvent.onLookInBattleList then
		description = Event.onLookInBattleList(self, creature, distance, description)
	end

	if description ~= "" then self:sendTextMessage(MESSAGE_INFO_DESCR, description) end
end

function Player:onLookInTrade(partner, item, distance)
	local description = "You see " .. item:getDescription(distance)
	if hasEvent.onLookInTrade then
		description = Event.onLookInTrade(self, partner, item, distance, description)
	end

	if description ~= "" then self:sendTextMessage(MESSAGE_INFO_DESCR, description) end
end

function Player:onLookInShop(itemType, count)
	local description = "You see "
	if hasEvent.onLookInShop then description = Event.onLookInShop(self, itemType, count, description) end

	if description ~= "" then self:sendTextMessage(MESSAGE_INFO_DESCR, description) end
end

function Player:onMoveItem(item, count, fromPosition, toPosition, fromCylinder, toCylinder)
	if hasEvent.onMoveItem then
		return Event.onMoveItem(self, item, count, fromPosition, toPosition, fromCylinder, toCylinder)
	end
	return RETURNVALUE_NOERROR
end

function Player:onItemMoved(item, count, fromPosition, toPosition, fromCylinder, toCylinder)
	if hasEvent.onItemMoved then
		Event.onItemMoved(self, item, count, fromPosition, toPosition, fromCylinder, toCylinder)
	end
end

function Player:onMoveCreature(creature, fromPosition, toPosition)
	if hasEvent.onMoveCreature then
		return Event.onMoveCreature(self, creature, fromPosition, toPosition)
	end
	return true
end

function Player:onReportRuleViolation(targetName, reportType, reportReason, comment, translation)
	if hasEvent.onReportRuleViolation then
		Event.onReportRuleViolation(self, targetName, reportType, reportReason, comment, translation)
	end
end

function Player:onReportBug(message)
	if hasEvent.onReportBug then return Event.onReportBug(self, message) end
	return true
end

function Player:onTurn(direction)
	if hasEvent.onTurn then return Event.onTurn(self, direction) end
	return true
end

function Player:onTradeRequest(target, item)
	if hasEvent.onTradeRequest then return Event.onTradeRequest(self, target, item) end
	return true
end

function Player:onTradeAccept(target, item, targetItem)
	if hasEvent.onTradeAccept then return Event.onTradeAccept(self, target, item, targetItem) end
	return true
end

function Player:onTradeCompleted(target, item, targetItem, isSuccess)
	if hasEvent.onTradeCompleted then Event.onTradeCompleted(self, target, item, targetItem, isSuccess) end
end

function Player:onGainExperience(source, exp, rawExp, sendText)
	if not hasEvent.onGainExperience then
		return exp
	end

	local result = Event.onGainExperience(self, source, exp, rawExp, sendText)
	if type(result) ~= "number" then
		return exp
	end

	if ResetBonusConfig then
		local vocation = self:getVocation()
		local vocId = vocation and vocation:getId() or 0
		local resetXpBonus = ResetBonusConfig.getTotalBonus("experience", self:getResetCount(), vocId)
		if resetXpBonus > 0 then
			result = result * (1 + resetXpBonus / 100)
		end
	end

	if StagesConfig then
		result = result * StagesConfig.getResetMultiplier(self:getResetCount())
	end

	return math.floor(result)
end

function Player:onLoseExperience(exp)
	return hasEvent.onLoseExperience and Event.onLoseExperience(self, exp) or exp
end

function Player:onGainSkillTries(skill, tries, artificial)
	if artificial then
		return hasEvent.onGainSkillTries and Event.onGainSkillTries(self, skill, tries, artificial) or
			tries
	end

	if skill == SKILL_MAGLEVEL then
		tries = tries * Game.getMagicLevelStage(self:getMagicLevel())
		return hasEvent.onGainSkillTries and math.floor(Event.onGainSkillTries(self, skill, tries, artificial)) or
			tries
	end
	tries = math.floor(tries * Game.getSkillStage(self:getSkillLevel(skill)))
	return hasEvent.onGainSkillTries and math.floor(Event.onGainSkillTries(self, skill, tries, artificial)) or
		tries
end

function Player:onNetworkMessage(recvByte, msg)
	local handler = PacketHandlers[recvByte]
	if not handler then
		--print(string.format("Player: %s sent an unknown packet header: 0x%02X with %d bytes!",
			--self:getName(), recvByte, msg:len()))
		return
	end

	handler(self, msg)
end

function Player:onUpdateStorage(key, value, oldValue, isLogin)
	if hasEvent.onUpdateStorage then Event.onUpdateStorage(self, key, value, oldValue, isLogin) end
end

function Player:onUpdateInventory(item, slot, equip)
	if hasEvent.onUpdateInventory then Event.onUpdateInventory(self, item, slot, equip) end
end

function Player:onRotateItem(item)
	if hasEvent.onRotateItem then return Event.onRotateItem(self, item) end
end

function Player:onSpellCheck(spell)
	if hasEvent.onSpellCheck then return Event.onSpellCheck(self, spell) end
	return true
end

function Player:onStepTile(fromPosition, toPosition)
    addEvent(function(playerId)
        local player = Player(playerId)
        if player and CustomMarket and CustomMarket.checkAccess then
            CustomMarket.checkAccess(player)
        end
    end, 0, self:getId())

    if hasEvent.onStepTile then
        return Event.onStepTile(self, fromPosition, toPosition)
    end
    return true
end

function Player:onFightModeChanged(stance, chase, secure)
	if hasEvent.onFightModeChanged then
		Event.onFightModeChanged(self, stance, chase, secure)
	end
end
