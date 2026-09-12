// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "store/store_service.h"

#include "account_coins.h"
#include "configmanager.h"
#include "game.h"
#include "iologindata.h"
#include "item.h"
#include "logger.h"
#include "luascript.h"
#include "mounts.h"
#include "player.h"
#include "scheduler.h"
#include "script.h"
#include "scriptmanager.h"
#include "store/store_catalog.h"
#include "store/store_name_validator.h"
#include "store/store_repository.h"
#include "tools.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

extern Game g_game;

namespace {

bool playerIsInCombat(const Player& player)
{
	return player.hasCondition(CONDITION_INFIGHT);
}

bool playerIsInProtectionZone(const Player& player)
{
	const Tile* tile = player.getTile();
	return tile && tile->hasFlag(TILESTATE_PROTECTIONZONE);
}

struct InboxItemSnapshot
{
	std::shared_ptr<Item> item;
	uint16_t count;
};

std::vector<InboxItemSnapshot> snapshotInboxItems(const StoreInbox& inbox)
{
	std::vector<InboxItemSnapshot> snapshot;
	snapshot.reserve(inbox.size());
	for (const auto& item : inbox.getItemList()) {
		if (item) {
			snapshot.push_back({item, item->getItemCount()});
		}
	}
	return snapshot;
}

void rollbackInboxDelivery(StoreInbox& inbox, const std::vector<InboxItemSnapshot>& snapshot,
	                       const Player& player)
{
	std::unordered_map<Item*, uint16_t> originalCounts;
	originalCounts.reserve(snapshot.size());
	for (const auto& entry : snapshot) {
		originalCounts.emplace(entry.item.get(), entry.count);
	}

	// internalRemoveItem mutates the inbox list, so retain shared ownership in a
	// separate vector while undoing newly inserted items and stack increments.
	std::vector<std::shared_ptr<Item>> currentItems(inbox.getItemList().begin(), inbox.getItemList().end());
	for (const auto& item : currentItems) {
		if (!item || item->isRemoved()) {
			continue;
		}

		const auto original = originalCounts.find(item.get());
		if (original == originalCounts.end()) {
			const ReturnValue ret = g_game.internalRemoveItem(item.get());
			if (ret != RETURNVALUE_NOERROR) {
				LOG_ERROR(fmt::format(
				    "[StoreService::deliverItem] CRITICAL: Failed to remove newly inserted item id={} "
				    "(ret={}) during rollback for player={}",
				    item->getID(), static_cast<int>(ret), player.getName()));
			}
			continue;
		}

		if (item->isStackable() && item->getItemCount() > original->second) {
			const uint16_t addedCount = item->getItemCount() - original->second;
			const ReturnValue ret = g_game.internalRemoveItem(item.get(), addedCount);
			if (ret != RETURNVALUE_NOERROR) {
				LOG_ERROR(fmt::format(
				    "[StoreService::deliverItem] CRITICAL: Failed to restore stack id={} by {} "
				    "(ret={}) during rollback for player={}",
				    item->getID(), addedCount, static_cast<int>(ret), player.getName()));
			}
		}
	}
}

} // namespace

StoreService& StoreService::getInstance()
{
	static StoreService instance;
	return instance;
}

StoreRateLimit& StoreService::getRateLimit(uint32_t playerId)
{
	const auto now = std::chrono::steady_clock::now();
	if (rateLimits_.size() > 64 && now - lastRateLimitCleanup_ > std::chrono::seconds(60)) {
		lastRateLimitCleanup_ = now;
		std::erase_if(rateLimits_, [now](const auto& pair) {
			const auto& limit = pair.second;
			return (now - limit.lastPurchase > std::chrono::seconds(60)) &&
			       (now - limit.lastTransfer > std::chrono::seconds(60)) &&
			       (now - limit.lastCatalog > std::chrono::seconds(60)) &&
			       (now - limit.lastHistory > std::chrono::seconds(60));
		});
	}
	return rateLimits_[playerId];
}

void StoreService::clearRateLimit(uint32_t playerId)
{
	rateLimits_.erase(playerId);
}

bool StoreService::isOfferAvailable(const Player& player, const StoreOffer& offer) const
{
	const bool astra = player.isAstraClient();

	// Task Board offers require corresponding systems + AstraClient.
	if (isTaskBoardOfferType(offer.type)) {
		if (!astra) {
			return false;
		}
		if (!ConfigManager::getBoolean(ConfigManager::TASK_HUNTING_SYSTEM_ENABLED)) {
			return false;
		}
		if (offer.type == StoreOfferType::BountyKillBoost &&
		    !ConfigManager::getBoolean(ConfigManager::BOUNTY_TASKS_ENABLED)) {
			return false;
		}
		if ((offer.type == StoreOfferType::WeeklyKillBoost ||
		     offer.type == StoreOfferType::WeeklyReducedItems ||
		     offer.type == StoreOfferType::WeeklyTaskExpansion) &&
		    !ConfigManager::getBoolean(ConfigManager::WEEKLY_TASKS_ENABLED)) {
			return false;
		}
	}

	// Hireling offers require AstraClient + hireling systems.
	if (isHirelingOfferType(offer.type)) {
		if (!astra ||
		    !ConfigManager::getBoolean(ConfigManager::HIRELING_SYSTEM_ENABLED) ||
		    !ConfigManager::getBoolean(ConfigManager::ASTRA_HIRELING_PROTOCOL_ENABLED)) {
			return false;
		}
	}

	// Battle Pass requires AstraClient + system enabled.
	if (offer.type == StoreOfferType::BattlePass) {
		if (!astra || !ConfigManager::getBoolean(ConfigManager::BATTLEPASS_SYSTEM_ENABLED)) {
			return false;
		}
	}

	return true;
}

StoreResult StoreService::purchase(Player& player, uint32_t offerId,
                                   const StorePurchaseExtra& extra)
{
	const auto now = std::chrono::steady_clock::now();
	auto& rateLimit = getRateLimit(player.getID());
	if (now - rateLimit.lastPurchase < PurchaseCooldown) {
		return {false, "You are purchasing too fast. Please wait a moment."};
	}
	rateLimit.lastPurchase = now;

	// 1. Resolve immutable offer from catalog.
	auto catalog = StoreManager::getInstance().catalogSnapshot();
	if (!catalog) {
		return {false, "Store is not available."};
	}

	const StoreOffer* offer = catalog->findOffer(offerId);
	if (!offer) {
		return {false, "Offer not found."};
	}

	// 2. Validate feature gating.
	if (!isOfferAvailable(player, *offer)) {
		return {false, "This offer is not available."};
	}

	// 3. Validate offer-specific preconditions (name, etc.).
	if (offer->type == StoreOfferType::ChangeName) {
		if (extra.name.empty()) {
			return {false, "You need to choose a new character name."};
		}
	}
	if (offer->type == StoreOfferType::Hireling) {
		if (extra.name.empty()) {
			return {false, "You need to choose a hireling name."};
		}
	}

	// 4. Validate the price before touching the account.
	const auto epochSeconds = std::chrono::duration_cast<std::chrono::seconds>(
	    std::chrono::system_clock::now().time_since_epoch()).count();
	const uint32_t nowTimestamp = static_cast<uint32_t>(std::clamp<int64_t>(
	    epochSeconds, int64_t{0}, static_cast<int64_t>(std::numeric_limits<uint32_t>::max())));
	const auto dailyOffers = StoreManager::getInstance().dailyOffersSnapshot(nowTimestamp);
	const StoreDailyOffer* dailyOffer = dailyOffers.find(offerId);
	const uint32_t purchasePrice = dailyOffer ? dailyOffer->price : offer->price;
	if (purchasePrice == 0) {
		return {false, "Invalid offer price."};
	}

	const uint32_t accountId = player.getAccount();
	// 5. Atomically validate and debit in one query. A separate balance SELECT
	// would add DB traffic and could only provide a stale pre-check.
	if (!AccountCoins::debit(accountId, purchasePrice)) {
		return {false, "Not enough Tibia Coins."};
	}

	// 6. Attempt delivery.
	const std::string deliveryError = deliverOffer(player, *offer, extra);
	if (!deliveryError.empty()) {
		// Delivery failed — refund coins atomically.
		if (!AccountCoins::credit(accountId, purchasePrice)) {
			LOG_ERROR(fmt::format(
			    "[StoreService::purchase] CRITICAL: Refund failed! account={} player={} (guid={}) "
			    "offer={} amount={} — coins may be lost!",
			    accountId, player.getName(), player.getGUID(), offerId, purchasePrice));
		}
		return {false, deliveryError};
	}

	// 7. Persist history.
	uint16_t historyCount = offer->count;
	if (offer->type == StoreOfferType::Item) {
		historyCount = offer->count;
	} else if (offer->type == StoreOfferType::House) {
		historyCount = static_cast<uint16_t>(std::max<size_t>(offer->items.size(),
		                                                      static_cast<size_t>(offer->count)));
	} else if (offer->type == StoreOfferType::PreyWildcard) {
		historyCount = static_cast<uint16_t>(std::max<int64_t>(1, offer->value));
	} else {
		historyCount = 1;
	}

	if (!StoreRepository::getInstance().addHistory(
	        accountId, player.getGUID(), offer->name,
	        -static_cast<int64_t>(purchasePrice), historyCount)) {
		LOG_WARN(fmt::format(
		    "[StoreService::purchase] Failed to persist purchase history for account={} player={} offer='{}'",
		    accountId, player.getName(), offer->name));
	}

	// 8. Build success message.
	std::string successMessage;
	if (isXpBoostOfferType(offer->type)) {
		player.sendStats();
		successMessage = "Your XP Boost is now active.";
	} else if (offer->type == StoreOfferType::ChangeName) {
		successMessage = "Your character name has been changed. You will be disconnected in 3 seconds. "
		                 "Please log in again to use your new name.";
		// Schedule kick after 3 seconds, verifying player identity on execution.
		const uint32_t creatureId = player.getID();
		const uint32_t playerGuid = player.getGUID();
		g_scheduler.addEvent(3000, [creatureId, playerGuid]() {
			const auto p = g_game.getPlayerByID(creatureId);
			if (p && p->getGUID() == playerGuid) {
				g_game.kickPlayer(creatureId, true);
			}
		});
	} else {
		successMessage = "Purchase complete: " + offer->name;
	}

	return {true, successMessage};
}

StoreResult StoreService::transferCoins(Player& player, std::string_view targetName,
                                         uint32_t amount)
{
	const auto now = std::chrono::steady_clock::now();
	auto& rateLimit = getRateLimit(player.getID());
	if (now - rateLimit.lastTransfer < TransferCooldown) {
		return {false, "You are transferring coins too fast. Please wait a moment."};
	}
	rateLimit.lastTransfer = now;

	if (amount == 0) {
		return {false, "Invalid amount."};
	}

	const std::string trimmedTarget = asTrimmedString(targetName);
	if (trimmedTarget.empty() || trimmedTarget.size() > 50) {
		return {false, "Target player not found."};
	}

	// Cannot transfer to self (case-insensitive).
	if (caseInsensitiveEqual(player.getName(), trimmedTarget)) {
		return {false, "You cannot transfer coins to yourself."};
	}

	// Look up target.
	auto targetInfo = AccountCoins::findCharacterAccount(trimmedTarget);
	if (!targetInfo) {
		return {false, "Target player not found."};
	}

	const uint32_t sourceAccountId = player.getAccount();
	if (targetInfo->accountId == sourceAccountId) {
		return {false, "You cannot transfer coins to your own account."};
	}

	// Validate source balance (pre-check before the transaction).
	if (AccountCoins::get(sourceAccountId) < amount) {
		return {false, "Not enough Tibia Coins."};
	}

	// Execute transfer + history logging in a single atomic DB transaction.
	AccountCoins::TransferHistoryDetails historyDetails{
	    .sourcePlayerId = player.getGUID(),
	    .sourcePlayerName = player.getName(),
	    .destPlayerId = targetInfo->playerId,
	    .destPlayerName = targetInfo->playerName,
	};
	if (!AccountCoins::transfer(sourceAccountId, targetInfo->accountId, amount, historyDetails)) {
		return {false, "Transfer failed, please try again."};
	}

	return {true, fmt::format("You sent {} Tibia Coins to {}.", amount, targetInfo->playerName)};
}

// ─── Delivery dispatch ───────────────────────────────────────────────────────

std::string StoreService::deliverOffer(Player& player, const StoreOffer& offer,
                                        const StorePurchaseExtra& extra)
{
	switch (offer.type) {
		case StoreOfferType::Premium:
			return deliverPremium(player, offer);
		case StoreOfferType::Blessing:
			return deliverBlessing(player, offer);
		case StoreOfferType::Outfit:
			return deliverOutfit(player, offer);
		case StoreOfferType::Mount:
			return deliverMount(player, offer);
		case StoreOfferType::ExpBoost:
			return deliverXpBoost(player, offer);
		case StoreOfferType::Item:
			return deliverItem(player, offer);
		case StoreOfferType::House:
			return deliverHouseItem(player, offer);
		case StoreOfferType::ChangeName:
			return deliverNameChange(player, extra);
		case StoreOfferType::SexChange:
			return deliverSexChange(player);
		case StoreOfferType::Hireling:
			return deliverHireling(player, offer, extra);
		case StoreOfferType::HirelingSkill:
			return deliverHirelingSkill(player, offer);
		case StoreOfferType::HirelingOutfit:
			return deliverHirelingOutfit(player, offer);

		// Types that must be delivered via Lua callback (subsystems only in Lua).
		case StoreOfferType::BattlePass:
		case StoreOfferType::PreyWildcard:
		case StoreOfferType::BountyKillBoost:
		case StoreOfferType::WeeklyKillBoost:
		case StoreOfferType::WeeklyReducedItems:
		case StoreOfferType::WeeklyTaskExpansion:
			return deliverViaLuaCallback(player, offer, extra);
	}

	return "Invalid offer type.";
}

std::string StoreService::deliverPremium(Player& player, const StoreOffer& offer)
{
	if (offer.value <= 0 || offer.value > 36500) {
		return "Invalid premium amount.";
	}

	// addPremiumDays(days) = setPremiumTime(getPremiumEndsAt + days * 86400)
	const time_t now = time(nullptr);
	const time_t previousEnd = player.getPremiumEndsAt();
	const int64_t currentEnd = std::max<int64_t>(static_cast<int64_t>(previousEnd), static_cast<int64_t>(now));
	constexpr int64_t maxPersistedPremiumEnd = std::numeric_limits<uint32_t>::max();
	constexpr int64_t secondsPerDay = 86400;
	if (currentEnd < 0 || currentEnd > maxPersistedPremiumEnd ||
	    offer.value > (maxPersistedPremiumEnd - currentEnd) / secondsPerDay) {
		return "Premium duration exceeds the supported date range.";
	}
	const time_t newEnd = static_cast<time_t>(currentEnd + offer.value * secondsPerDay);
	player.setPremiumTime(newEnd);
	if (!IOLoginData::updatePremiumTime(player.getAccount(), newEnd)) {
		player.setPremiumTime(previousEnd);
		return "Failed to update premium time in database.";
	}
	return "";
}

std::string StoreService::deliverBlessing(Player& player, const StoreOffer& offer)
{
	if (offer.value == -1) {
		// All regular blessings (1-5).
		bool added = false;
		for (uint8_t blessing = 1; blessing <= 5; ++blessing) {
			if (!player.hasBlessing(blessing)) {
				player.addBlessing(blessing);
				added = true;
			}
		}
		if (!added) {
			return "You already have all regular blessings.";
		}
		return "";
	}

	if (offer.value >= 1 && offer.value <= 5) {
		if (player.hasBlessing(static_cast<uint8_t>(offer.value))) {
			return "You already have this blessing.";
		}
		player.addBlessing(static_cast<uint8_t>(offer.value));
		return "";
	}

	return "Invalid blessing.";
}

std::string StoreService::deliverOutfit(Player& player, const StoreOffer& offer)
{
	std::vector<uint16_t> lookTypes;
	if (offer.value > 0) {
		lookTypes.push_back(static_cast<uint16_t>(offer.value));
	}
	if (offer.femaleValue > 0 && offer.femaleValue != offer.value) {
		lookTypes.push_back(static_cast<uint16_t>(offer.femaleValue));
	}

	bool added = false;
	for (uint16_t lookType : lookTypes) {
		if (lookType > 0 && !player.hasOutfit(lookType, offer.addon)) {
			// Preserve the Lua delivery behavior: unlock the base outfit first,
			// then its addons so the corresponding cosmetic notifications fire.
			player.addOutfit(lookType, 0);
			if (offer.addon > 0) {
				player.addOutfit(lookType, offer.addon);
			}
			added = true;
		}
	}

	if (!added) {
		return "You already have this outfit.";
	}
	return "";
}

std::string StoreService::deliverMount(Player& player, const StoreOffer& offer)
{
	if (offer.value <= 0 || offer.value > std::numeric_limits<uint16_t>::max()) {
		return "Failed to deliver mount.";
	}

	const uint16_t mountId = static_cast<uint16_t>(offer.value);
	const Mount* mount = g_game.mounts.getMountByID(mountId);
	if (!mount) {
		return "Failed to deliver mount.";
	}

	if (player.ownsMount(mount) || player.hasMount(mount)) {
		return "You already have this mount.";
	}

	if (!player.tameMount(mountId)) {
		return "Failed to deliver mount.";
	}
	return "";
}

std::string StoreService::deliverXpBoost(Player& player, const StoreOffer& offer)
{
	if (player.getXpBoostTime() > 0) {
		return "You already have an active XP boost.";
	}

	const int64_t configuredPercent = ConfigManager::getInteger(ConfigManager::STORE_XP_BOOST_PERCENT);
	const int64_t configuredDefaultDuration = ConfigManager::getInteger(ConfigManager::STORE_XP_BOOST_DEFAULT_DURATION);
	const int32_t percent = static_cast<int32_t>(
	    std::clamp<int64_t>(configuredPercent > 0 ? configuredPercent : 50, 1, 255));
	const int64_t fallbackDuration = configuredDefaultDuration > 0 ? configuredDefaultDuration : 3600;

	const int64_t duration = offer.value > 0 ? offer.value : fallbackDuration;
	if (duration > std::numeric_limits<uint16_t>::max()) {
		return "XP boost duration exceeds the supported limit.";
	}
	player.setXpBoostPercent(percent);
	player.setXpBoostTime(static_cast<uint16_t>(std::max<int64_t>(duration, 1)));
	return "";
}

std::string StoreService::deliverItem(Player& player, const StoreOffer& offer)
{
	if (offer.itemId == 0) {
		return "Invalid item.";
	}

	StoreInbox* inbox = player.getStoreInbox();
	if (!inbox) {
		return "Your store inbox is not available.";
	}

	const ItemType& itemType = Item::items[offer.itemId];
	if (itemType.id == 0 || offer.count == 0) {
		return "Invalid item.";
	}

	std::vector<std::shared_ptr<Item>> deliveryItems;
	if (itemType.stackable) {
		const uint16_t stackSize = std::max<uint16_t>(1, itemType.stackSize);
		deliveryItems.reserve((offer.count + stackSize - 1) / stackSize);
		uint32_t remaining = offer.count;
		while (remaining > 0) {
			const uint16_t stackCount = static_cast<uint16_t>(std::min<uint32_t>(remaining, stackSize));
			auto item = Item::CreateItem(offer.itemId, stackCount);
			if (!item || item->getItemCount() != stackCount) {
				return "Failed to create item.";
			}
			deliveryItems.push_back(std::move(item));
			remaining -= stackCount;
		}
	} else {
		deliveryItems.reserve(offer.count);
		for (uint16_t i = 0; i < offer.count; ++i) {
			auto item = Item::CreateItem(offer.itemId);
			if (!item) {
				return "Failed to create item.";
			}
			deliveryItems.push_back(std::move(item));
		}
	}

	if (deliveryItems.empty()) {
		return "Failed to create item.";
	}

	uint32_t maxQueryCount = 0;
	const ReturnValue capacityResult = inbox->queryMaxCount(
	    INDEX_WHEREEVER, *deliveryItems.front(), offer.count, maxQueryCount, 0);
	if (capacityResult != RETURNVALUE_NOERROR || maxQueryCount < offer.count) {
		return "Your store inbox is full.";
	}

	const auto snapshot = snapshotInboxItems(*inbox);
	for (const auto& item : deliveryItems) {
		uint32_t remainderCount = 0;
		const ReturnValue addResult = g_game.internalAddItem(
		    inbox, item.get(), INDEX_WHEREEVER, 0, false, remainderCount);
		if (addResult != RETURNVALUE_NOERROR || remainderCount != 0) {
			rollbackInboxDelivery(*inbox, snapshot, player);
			return "Your store inbox is full.";
		}
	}

	player.sendTextMessage(MESSAGE_STATUS_SMALL, "Your item was sent to your store inbox.");
	return "";
}

std::string StoreService::deliverHouseItem(Player& player, const StoreOffer& offer)
{
	StoreInbox* inbox = player.getStoreInbox();
	if (!inbox) {
		return "Your store inbox is not available.";
	}

	std::vector<uint16_t> deliveryIds = offer.items;
	if (deliveryIds.empty() && offer.itemId > 0) {
		deliveryIds.push_back(offer.itemId);
	}
	if (deliveryIds.empty()) {
		return "Invalid house item.";
	}

	// Create all decoration kit items.
	std::vector<std::shared_ptr<Item>> createdItems;
	for (uint16_t itemId : deliveryIds) {
		const ItemType& it = Item::items[itemId];
		if (it.id == 0) {
			return "Invalid house item.";
		}

		auto kit = Item::CreateItem(ITEM_DECORATION_KIT, 1);
		if (!kit) {
			return "Failed to create item.";
		}

		kit->setSpecialDescription(
		    "You bought this item in the Store.\nUnwrap it in your own house to create a <" +
		    it.name + ">.");
		kit->setIntAttr(ITEM_ATTRIBUTE_WRAPID, itemId);
		createdItems.push_back(std::move(kit));
	}

	std::vector<std::shared_ptr<Item>> insertedItems;
	for (auto& item : createdItems) {
		if (g_game.internalAddItem(inbox, item.get(), INDEX_WHEREEVER, 0) != RETURNVALUE_NOERROR) {
			for (auto& inserted : insertedItems) {
				const ReturnValue ret = g_game.internalRemoveItem(inserted.get());
				if (ret != RETURNVALUE_NOERROR) {
					LOG_ERROR(fmt::format(
					    "[StoreService::deliverHouseItem] CRITICAL: Failed to remove inserted item id={} (ret={}) during rollback for player={}",
					    inserted->getID(), static_cast<int>(ret), player.getName()));
				}
			}
			return "Your store inbox is full.";
		}
		insertedItems.push_back(item);
	}

	player.sendTextMessage(MESSAGE_STATUS_SMALL, "Your house item was sent to your store inbox.");
	return "";
}

std::string StoreService::deliverNameChange(Player& player, const StorePurchaseExtra& extra)
{
	const std::string newName = CharacterNameValidator::formatName(extra.name);
	const std::string validationError = CharacterNameValidator::validate(newName);
	if (!validationError.empty()) {
		return validationError;
	}

	if (CharacterNameValidator::nameExistsInDB(newName)) {
		return "Character name already taken.";
	}

	if (playerIsInCombat(player)) {
		return "You cannot do this during a fight.";
	}

	if (!playerIsInProtectionZone(player)) {
		return "You need to be in a protection zone.";
	}

	const std::string oldName = player.getName();
	std::string reason;
	if (!StoreRepository::getInstance().renameCharacter(player.getGUID(), oldName, newName, reason)) {
		return reason.empty() ? "Character name already taken." : reason;
	}

	StoreRepository::getInstance().recordNameChange(player.getGUID(), oldName, newName);
	return "";
}

std::string StoreService::deliverSexChange(Player& player)
{
	if (playerIsInCombat(player)) {
		return "You cannot do this during a fight.";
	}

	if (!playerIsInProtectionZone(player)) {
		return "You need to be in a protection zone.";
	}

	// Toggle sex.
	const auto newSex = (player.getSex() == PLAYERSEX_FEMALE) ? PLAYERSEX_MALE : PLAYERSEX_FEMALE;
	player.setSex(newSex);

	// Set default outfit for new sex.
	Outfit_t outfit = player.getCurrentOutfit();
	outfit.lookType = (newSex == PLAYERSEX_MALE) ? 128 : 136;
	player.setCurrentOutfit(outfit);
	return "";
}

std::string StoreService::deliverHireling(Player& player, const StoreOffer& offer,
                                           const StorePurchaseExtra& extra)
{
	// Hireling delivery uses Lua callback since the hireling system is Lua-based.
	return deliverViaLuaCallback(player, offer, extra);
}

std::string StoreService::deliverHirelingSkill(Player& player, const StoreOffer& offer)
{
	StorePurchaseExtra empty;
	return deliverViaLuaCallback(player, offer, empty);
}

std::string StoreService::deliverHirelingOutfit(Player& player, const StoreOffer& offer)
{
	StorePurchaseExtra empty;
	return deliverViaLuaCallback(player, offer, empty);
}

std::string StoreService::deliverViaLuaCallback(Player& player, const StoreOffer& offer,
                                                  const StorePurchaseExtra& extra)
{
	// Bridge to Lua for subsystems that only exist in Lua.
	// We call a global Lua function "StoreDeliverLuaOffer" if it exists.
	if (!g_scripts) {
		return "Lua script environment is not available.";
	}

	LuaScriptInterface& scriptInterface = g_scripts->getScriptInterface();
	lua_State* L = scriptInterface.getLuaState();
	if (!L) {
		return "Lua environment is not available.";
	}
	const int stackTop = lua_gettop(L);

	lua_getglobal(L, "StoreDeliverLuaOffer");
	if (!lua_isfunction(L, -1)) {
		lua_settop(L, stackTop);
		// If the Lua bridge function doesn't exist, the offer type is unsupported.
		return "This offer type is not available.";
	}

	if (!scriptInterface.reserveScriptEnv()) {
		lua_settop(L, stackTop);
		LOG_ERROR("[StoreService::deliverViaLuaCallback] Lua call stack overflow");
		return "Delivery failed due to an internal error.";
	}

	ScriptEnvironment* env = scriptInterface.getScriptEnv();
	env->setScriptId(EVENT_ID_USER, &scriptInterface);

	// Push arguments: player, offerType, value, displayId, extraName, extraSex
	Lua::pushUserdata<Player>(L, &player);
	Lua::setMetatable(L, -1, "Player");
	lua_pushstring(L, std::string(storeOfferTypeToString(offer.type)).c_str());
	lua_pushinteger(L, offer.value);
	lua_pushinteger(L, offer.displayId);
	lua_pushstring(L, extra.name.c_str());
	lua_pushinteger(L, extra.sex);

	// pcall with 6 args, 1 result.
	if (scriptInterface.protectedCall(L, 6, 1) != LUA_OK) {
		LuaScriptInterface::reportError("StoreDeliverLuaOffer", Lua::popString(L));
		lua_settop(L, stackTop);
		scriptInterface.resetScriptEnv();
		return "Delivery failed due to an internal error.";
	}

	// Result: nil = success, string = error message.
	std::string result;
	if (lua_isstring(L, -1)) {
		result = lua_tostring(L, -1);
	}
	lua_settop(L, stackTop);
	scriptInterface.resetScriptEnv();
	return result;
}
