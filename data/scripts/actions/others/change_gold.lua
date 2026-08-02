local config = {}

local changeGold = Action()

function changeGold.onUse(player, item, fromPosition, target, toPosition,
                          isHotkey)
	local coin = config[item:getId()]
	local converted, reason
	if coin.changeTo and item.type == 100 then
		converted, reason = CurrencyConversion.exchangeItem(player, item, 100, coin.changeTo, 1)
	elseif coin.changeBack then
		converted, reason = CurrencyConversion.exchangeItem(player, item, 1, coin.changeBack, 100)
	else
		return false
	end

	if not converted then
		player:sendCancelMessage(CurrencyConversion.getExchangeFailureMessage(reason))
		return false
	end
	return true
end

local currencyItems = Game.getCurrencyItems()
if #currencyItems > 0 then
	for index, currency in pairs(currencyItems) do
		local back, to = currencyItems[index - 1], currencyItems[index + 1]
		local currencyId = currency:getId()
		config[currencyId] = {
			changeBack = back and back:getId(),
			changeTo = to and to:getId()
		}
		changeGold:id(currencyId)
	end
	changeGold:register()
end
