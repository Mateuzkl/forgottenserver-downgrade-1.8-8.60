-- Task Board Network Module — Main Entry Point
-- Wires together protocol, bounty, weekly, shop, and resource balance modules.
-- Opcodes: 0x5F (client->server), 0x53/0xBA/0xEE (server->client via protocol module).
--
-- Uses native bytes only. No JSON, no extended opcodes.
-- Only sends modern bytes to AstraClient (IsAstraClient guard in protocol layer).

-- Config keys (registered by C++ in configKeys global table)
if not configManager or not configManager.getBoolean then
	return
end

if not configManager.getBoolean(configKeys.TASK_HUNTING_SYSTEM_ENABLED) then
	return
end

local bountyEnabled = configManager.getBoolean(configKeys.BOUNTY_TASKS_ENABLED)
local weeklyEnabled = configManager.getBoolean(configKeys.WEEKLY_TASKS_ENABLED)
local soulsealsEnabled = configManager.getBoolean(configKeys.SOULSEALS_SYSTEM_ENABLED)

-- ============================================
-- LOAD MODULES
-- ============================================

-- Protocol (always needed for resource balance at minimum)
local protocol = dofile("data/scripts/network/task_board/protocol.lua")

-- Bounty tasks
local bounty = nil
if bountyEnabled then
	bounty = dofile("data/scripts/network/task_board/bounty_tasks.lua")
	if bounty and bounty.setProtocol then
		bounty.setProtocol(protocol)
	end
end

-- Weekly tasks
local weekly = nil
if weeklyEnabled then
	weekly = dofile("data/scripts/network/task_board/weekly_tasks.lua")
	if weekly and weekly.setProtocol then
		weekly.setProtocol(protocol)
	end
end

-- Hunting shop
local shop = dofile("data/scripts/network/task_board/hunting_shop.lua")
if shop and shop.setProtocol then
	shop.setProtocol(protocol)
end

local soulseal = nil
if soulsealsEnabled then
	soulseal = dofile("data/scripts/network/task_board/soulseal_handler.lua")
	if soulseal and soulseal.setProtocol then
		soulseal.setProtocol(protocol)
	end
end

local resourceBalance = TaskBoard

-- ============================================
-- LOAD CONFIG DATA
-- ============================================

-- Delivery items for weekly tasks
if weeklyEnabled then
	local ok, deliveryItems = pcall(dofile, "data/lib/task_board/delivery_items.lua")
	if ok and weekly and weekly.setDeliveryItems then
		weekly.setDeliveryItems(deliveryItems)
	end
end

-- Shop offers
local ok, offers = pcall(dofile, "data/lib/task_board/shop_offers.lua")
if ok and shop and shop.setOffers then
	shop.setOffers(offers)
end

-- ============================================
-- PACKET HANDLER: 0x5F (Client -> Server)
-- ============================================

local OPCODE_TASK_BOARD_ACTION = 0x5F

local taskBoardActionHandler = PacketHandler(OPCODE_TASK_BOARD_ACTION)

function taskBoardActionHandler.onReceive(player, msg)
	if not player or not player.isUsingAstraClient or not player:isUsingAstraClient() then return end

	local payload = protocol.parseTaskBoardAction(msg)
	if not payload then
		return -- Invalid payload, silently ignore
	end

	local option = payload.option

	if option == 0 then -- Open Bounty
		if not bountyEnabled then return end
		if bounty then bounty.openBounty(player) end

    elseif option == 1 then -- Open Weekly
        if not weeklyEnabled then return end
        if weekly then weekly.sendWeeklyData(player) end
	elseif option == 2 then -- Change Difficulty
		if not bountyEnabled then return end
		if bounty then bounty.changeDifficulty(player, payload.difficulty) end

	elseif option == 3 then -- Reroll Tasks
		if not bountyEnabled then return end
		if bounty then bounty.rerollTasks(player) end

	elseif option == 4 then -- Claim Daily Reroll
		if not bountyEnabled then return end
		if bounty then bounty.claimDailyReroll(player) end

	elseif option == 5 then -- Select Task
		if not bountyEnabled then return end
		if bounty then bounty.selectTask(player, payload.taskIndex) end

	elseif option == 6 then -- Claim Reward
		if not bountyEnabled then return end
		if bounty then bounty.claimReward(player) end

	elseif option == 7 then -- Talisman Upgrade
		if not bountyEnabled then return end
		if bounty then bounty.talismanUpgrade(player, payload.pathIndex) end

	elseif option == 8 then -- Weekly Deliver
		if not weeklyEnabled then return end
		if weekly then weekly.deliverTask(player, payload.taskIndex) end

	elseif option == 9 then -- Weekly Select Difficulty
		if not weeklyEnabled then return end
		if weekly then weekly.selectDifficulty(player, payload.difficulty) end

	elseif option == 10 then -- Open Hunt Shop
		if shop then shop.sendShopData(player) end

	elseif option == 11 then -- Buy Shop Offer
		if shop then shop.purchaseOffer(player, payload.offerIndex) end

	elseif option == 12 then -- Unlock Preferred Slot
		if not bountyEnabled then return end
		if bounty then bounty.unlockPreferredSlot(player, payload.slot) end

	elseif option == 13 then -- Clear Preferred
		if not bountyEnabled then return end
		if bounty then bounty.clearPreferred(player, payload.slot) end

	elseif option == 14 then -- Clear Unwanted
		if not bountyEnabled then return end
		if bounty then bounty.clearUnwanted(player, payload.slot) end

	elseif option == 15 then -- Assign Preferred
		if not bountyEnabled then return end
		if bounty then bounty.assignPreferred(player, payload.slot, payload.raceId) end

	elseif option == 16 then -- Assign Unwanted
		if not bountyEnabled then return end
		if bounty then bounty.assignUnwanted(player, payload.slot, payload.raceId) end
	elseif option == 17 then -- Open SoulSeal (request creature list)
		if not soulsealsEnabled then return end
		if soulseal and soulseal.sendSoulsealsData then
			soulseal.sendSoulsealsData(player)
		end
	elseif option == 18 then -- Open Preferred List
		if not bountyEnabled then return end
		if bounty then bounty.sendBountyData(player, true) end
	end
end

taskBoardActionHandler:register()

-- ============================================
-- EXPORT MODULES
-- ============================================

-- Make modules available globally for creature events
TaskBoardProtocol = protocol
TaskBoardBountyTasks = bounty
TaskBoardWeeklyTasks = weekly
TaskBoardHuntingShop = shop
TaskBoardSoulSealHandler = soulseal
TaskBoardResourceBalance = resourceBalance

-- Also expose for other scripts
if bounty then
	_TASK_BOARD_BOUNTY_MODULE = bounty
end
if weekly then
	_TASK_BOARD_WEEKLY_MODULE = weekly
end
if shop then
	_TASK_BOARD_SHOP_MODULE = shop
end

return {
	protocol = protocol,
	bounty = bounty,
	weekly = weekly,
	shop = shop,
	resourceBalance = resourceBalance,
}
