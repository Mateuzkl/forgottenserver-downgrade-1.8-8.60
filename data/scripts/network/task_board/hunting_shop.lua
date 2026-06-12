-- Hunting Task Shop: offer browsing, purchase validation, item/mount/outfit delivery.
-- Shop offers loaded from Lua config table.

local HuntingShop = {}

local protocol -- set by init.lua

-- Offer types
local OFFER_ITEM = 0
local OFFER_MOUNT = 1
local OFFER_OUTFIT = 2
local OFFER_ITEM_DOUBLE = 3
local OFFER_BONUS_PROMOTION = 4
local OFFER_WEEKLY_EXPANSION = 5

local function getPlayerGuid(player)
	return player:getGuid()
end

local function getItemClientId(itemId)
	itemId = tonumber(itemId) or 0
	if itemId <= 0 then
		return 0
	end

	local itemType = ItemType(itemId)
	local clientId = itemType and itemType:getClientId() or 0
	if clientId and clientId > 0 then
		return clientId
	end
	return itemId
end

local function getMountClientId(mountId)
	mountId = tonumber(mountId) or 0
	if mountId <= 0 or not Game.getMounts then
		return mountId
	end

	for _, mount in ipairs(Game.getMounts()) do
		if tonumber(mount.id) == mountId then
			return tonumber(mount.clientId) or mountId
		end
	end
	return mountId
end

-- Shop offers are loaded externally and set via setOffers()
local shopOffers = {}

function HuntingShop.setOffers(offers)
	shopOffers = offers or {}
end

function HuntingShop.setProtocol(protoModule)
	protocol = protoModule
end

-- ============================================
-- SHOP LOGIC
-- ============================================

function HuntingShop.sendShopData(player)
	local playerGuid = getPlayerGuid(player)
	local taskHuntingPoints = player:getTaskHuntingPoints()

	-- Mark offers as purchased if player already owns the item/mount/outfit
	local offersToSend = {}
	for _, offer in ipairs(shopOffers) do
		local offerCopy = {
			id = offer.id,
			name = offer.name,
			count = offer.count or 0,
			price = offer.price or 0,
			purchased = false,
			type = offer.type or OFFER_ITEM,
			itemId = offer.itemId,
			clientId = getItemClientId(offer.itemId),
			mountId = offer.mountId,
			mountClientId = getMountClientId(offer.mountId),
			outfitId = offer.outfitId,
			addons = offer.addons or 0,
		}

		-- Check if already owned (one-time purchases)
		if offer.type == OFFER_MOUNT and offer.mountId then
			offerCopy.purchased = player:hasMount(offer.mountId)
		elseif offer.type == OFFER_OUTFIT and offer.outfitId then
			offerCopy.purchased = player:hasOutfit(offer.outfitId, offer.addons or 0)
		elseif offer.type == OFFER_WEEKLY_EXPANSION then
			offerCopy.purchased = player:hasWeeklyExpansion()
		elseif offer.type == OFFER_BONUS_PROMOTION then
			offerCopy.purchased = player:isPremium() -- Or custom check
		end

		offersToSend[#offersToSend + 1] = offerCopy
	end

	return protocol.sendHuntingTaskShopData(player, offersToSend, taskHuntingPoints)
end

function HuntingShop.purchaseOffer(player, offerIndex)
	if offerIndex == nil or offerIndex < 1 or offerIndex > #shopOffers then
		return false
	end

	local offer = shopOffers[offerIndex]
	if not offer then return false end

	local taskHuntingPoints = player:getTaskHuntingPoints()

	-- Validate points
	if taskHuntingPoints < offer.price then
		return false
	end

	-- Validate not already purchased (one-time items)
	if offer.type == OFFER_MOUNT and offer.mountId and player:hasMount(offer.mountId) then
		return false
	end
	if offer.type == OFFER_OUTFIT and offer.outfitId and player:hasOutfit(offer.outfitId, offer.addons or 0) then
		return false
	end
	if offer.type == OFFER_WEEKLY_EXPANSION and player:hasWeeklyExpansion() then
		return false
	end

	-- Deduct points first
	if not player:removeTaskHuntingPoints(offer.price) then
		return false
	end

	-- Deliver reward
	local success = false

	if offer.type == OFFER_ITEM or offer.type == OFFER_ITEM_DOUBLE then
		local itemId = offer.itemId
		local count = offer.count or 1
		if offer.type == OFFER_ITEM_DOUBLE then
			count = count * 2
		end

		-- Try store inbox first, then regular inbox, then backpack
		local storeInbox = player:getStoreInbox()
		if storeInbox then
			success = storeInbox:addItem(itemId, count) ~= nil
		end
		if not success then
			local inbox = player:getInbox()
			if inbox then
				success = inbox:addItem(itemId, count) ~= nil
			end
		end
		if not success then
			success = player:addItem(itemId, count) ~= nil
		end

	elseif offer.type == OFFER_MOUNT and offer.mountId then
		success = player:addMount(offer.mountId)

	elseif offer.type == OFFER_OUTFIT and offer.outfitId then
		player:addOutfit(offer.outfitId)
		local addons = offer.addons or 0
		if addons > 0 then
			player:addOutfitAddon(offer.outfitId, addons)
		end
		success = true

	elseif offer.type == OFFER_BONUS_PROMOTION then
		-- Add 30 days to existing or current time
		local currentEndsAt = player:getPremiumEndsAt() or 0
		local baseTime = math.max(currentEndsAt, os.time())
		player:setPremiumEndsAt(baseTime + (30 * 24 * 60 * 60))
		success = true

	elseif offer.type == OFFER_WEEKLY_EXPANSION then
		player:setWeeklyExpansion(true)
		success = true
	end

	if not success then
		-- Refund points on failure
		player:addTaskHuntingPoints(offer.price)
		return false
	end

	-- Update resource balance
	protocol.sendResourceBalance(player, protocol.RESOURCE_TASK_HUNTING, player:getTaskHuntingPoints())

	-- Refresh shop data (to mark as purchased)
	HuntingShop.sendShopData(player)

	return true
end

return HuntingShop
