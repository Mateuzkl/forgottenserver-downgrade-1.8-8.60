local config = {}

local goldConverter = Action()

function goldConverter.onUse(player, item, fromPosition, target, toPosition,
                             isHotkey)
	local coin = config[target.itemid]

	if not coin then return false end

	local charges = item:getCharges()
	local converted = false
	if coin.changeTo and target.type == 100 then
		converted = CurrencyConversion.exchangeItem(player, target, 100, coin.changeTo, 1)
	elseif coin.changeBack then
		converted = CurrencyConversion.exchangeItem(player, target, 1, coin.changeBack, 100)
	else
		return false
	end

	if not converted then return false end

	if charges <= 1 then
		item:remove(1)
	else
		item:transform(item:getId(), charges - 1)
	end
	return true
end

local currencyItems = Game.getCurrencyItems()
for index, currency in pairs(currencyItems) do
	local back, to = currencyItems[index - 1], currencyItems[index + 1]
	local currencyId = currency:getId()
	config[currencyId] = {
		changeBack = back and back:getId(),
		changeTo = to and to:getId()
	}
end

goldConverter:id(23722)
goldConverter:register()
