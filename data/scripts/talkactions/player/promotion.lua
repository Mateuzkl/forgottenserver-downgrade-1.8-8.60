local promotion = TalkAction("!promotion")

local config = {
	requiredLevel = 20,
	cost = 20000,
}

function promotion.onSay(player, words, param)
	if player:getVocation():getId() == VOCATION_NONE then
		player:sendCancelMessage("Sorry, you need to have a vocation to be promoted!")
		return false
	end

	local vocationPromotion = player:getVocation():getPromotion()
	if player:isPromoted() then
		player:sendCancelMessage("You are already promoted!")
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
	elseif player:getLevel() < config.requiredLevel then
		player:sendCancelMessage("Sorry, you need level " .. config.requiredLevel .. " to be promoted.")
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
	elseif not vocationPromotion then
		player:sendCancelMessage("Sorry, your vocation cannot be promoted.")
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
	elseif not player:removeMoneyBank(config.cost) then
		player:sendCancelMessage(string.format("You need at least %d gold coins to have a promotion.", config.cost))
		player:getPosition():sendMagicEffect(CONST_ME_POFF)
	else
		player:sendTextMessage(MESSAGE_LOOK, "You received a promotion!")
		player:setVocation(vocationPromotion)
		player:setStorageValue(PlayerStorageKeys.promotion, 1)
		player:sendBannerType(BANNER_TYPE_PROMOTION_GRANTED)
		player:getPosition():sendMagicEffect(CONST_ME_HOLYDAMAGE)
	end

	return false
end

promotion:register()
