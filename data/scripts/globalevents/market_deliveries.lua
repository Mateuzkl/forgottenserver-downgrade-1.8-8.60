-- Independent of Market sessions and script reloads. Pending rights survive DB
-- failures, logout and restarts; C++ defers players with an async save in flight.
local deliveries = GlobalEvent("MarketDeliveries")
function deliveries.onThink(interval)
	Game.processMarketDeliveries()
	return true
end
deliveries:interval(5000)
deliveries:register()
