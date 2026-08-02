local config = {}

local changeGold = Action()

function changeGold.onUse(player, item, fromPosition, target, toPosition,
                          isHotkey)
	local coin = config[item:getId()]
	if coin.changeTo and item.type == 100 then
		local converted = CurrencyConversion.exchangeItem(player, item, 100, coin.changeTo, 1)
		return converted
	elseif coin.changeBack then
		local converted = CurrencyConversion.exchangeItem(player, item, 1, coin.changeBack, 100)
		return converted
	else
		return false
	end
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
