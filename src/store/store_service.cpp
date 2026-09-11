// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "store/store_service.h"

#include "account_coins.h"
#include "configmanager.h"
#include "game.h"
#include "item.h"
#include "logger.h"
#include "mounts.h"
#include "player.h"
#include "luascript.h"
#include "store/store_catalog.h"
#include "store/store_name_validator.h"
#include "store/store_repository.h"

#include <algorithm>
#include <cmath>

extern Game g_game;
extern LuaEnvironment g_luaEnvironment;

namespace {

constexpr int64_t XP_BOOST_PERCENT = 50;
constexpr int64_t XP_BOOST_DEFAULT_SECONDS = 3600;

bool playerIsInCombat(const Player& player)
{
	return player.hasCondition(CONDITION_INFIGHT);
}

bool playerIsInProtectionZone(const Player& player)
{
	const Tile* tile = player.getTile();
	return tile && tile->hasFlag(TILESTATE_PROTECTIONZONE);
}

} // namespace

StoreService& StoreService::getInstance()
{
	static StoreService instance;
	return instance;
}

StoreRateLimit& StoreService::getRateLimit(uint32_t playerId)
{
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

	// 4. Validate balance.
	if (offer->price == 0) {
		return {false, "Invalid offer price."};
	}

	const uint32_t accountId = player.getAccount();
	const uint64_t currentBalance = AccountCoins::get(accountId);
	if (currentBalance < offer->price) {
		return {false, "Not enough Tibia Coins."};
	}

	// 5. Atomically debit coins BEFORE delivery.
	if (!AccountCoins::debit(accountId, offer->price)) {
		return {false, "Not enough Tibia Coins."};
	}

	// 6. Attempt delivery.
	const std::string deliveryError = deliverOffer(player, *offer, extra);
	if (!deliveryError.empty()) {
		// Delivery failed — refund coins atomically.
		if (!AccountCoins::credit(accountId, offer->price)) {
			LOG_ERROR(fmt::format(
			    "[StoreService::purchase] CRITICAL: Refund failed! account={} player={} (guid={}) "
			    "offer={} amount={} — coins may be lost!",
			    accountId, player.getName(), player.getGUID(), offerId, offer->price));
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

	(void)StoreRepository::getInstance().addHistory(
	    accountId, player.getGUID(), offer->name,
	    -static_cast<int32_t>(offer->price), historyCount);

	// 8. Build success message.
	std::string successMessage;
	if (isXpBoostOfferType(offer->type)) {
		player.sendStats();
		successMessage = "Your XP Boost is now active.";
	} else if (offer->type == StoreOfferType::ChangeName) {
		successMessage = "Your character name has been changed. You will be disconnected in 3 seconds. "
		                 "Please log in again to use your new name.";
		// Schedule kick after name change.
		const uint32_t creatureId = player.getID();
		g_dispatcher.addTask([creatureId]() { g_game.kickPlayer(creatureId, true); });
	} else {
		successMessage = "Purchase complete: " + offer->name;
	}

	return {true, successMessage};
}

StoreResult StoreService::transferCoins(Player& player, std::string_view targetName,
                                         uint32_t amount)
{
	if (amount == 0) {
		return {false, "Invalid amount."};
	}

	const std::string trimmedTarget(targetName);
	if (trimmedTarget.empty() || trimmedTarget.size() > 50) {
		return {false, "Target player not found."};
	}

	// Cannot transfer to self.
	if (player.getName() == trimmedTarget) {
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

	// Execute transfer in a single DB transaction.
	if (!AccountCoins::transfer(sourceAccountId, targetInfo->accountId, amount)) {
		return {false, "Transfer failed, please try again."};
	}

	// Record history for both accounts.
	auto& repo = StoreRepository::getInstance();
	(void)repo.addHistory(sourceAccountId, player.getGUID(),
	                      "Coin Transfer to " + targetInfo->playerName,
	                      -static_cast<int32_t>(amount), 1, targetInfo->playerName);
	(void)repo.addHistory(targetInfo->accountId, targetInfo->playerId,
	                      "Coin Transfer from " + player.getName(),
	                      static_cast<int32_t>(amount), 1, player.getName());

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
			return deliverNameChange(player, offer, extra);
		case StoreOfferType::SexChange:
			return deliverSexChange(player, offer);
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
	if (offer.value <= 0) {
		return "Invalid premium amount.";
	}

	// addPremiumDays(days) = setPremiumTime(getPremiumEndsAt + days * 86400)
	const time_t now = time(nullptr);
	time_t currentEnd = player.getPremiumEndsAt();
	if (currentEnd < now) {
		currentEnd = now;
	}
	const time_t newEnd = currentEnd + (offer.value * 86400);
	player.setPremiumTime(newEnd);
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
			player.addOutfit(lookType, offer.addon);
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
	if (offer.value <= 0) {
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

	const int64_t duration = offer.value > 0 ? offer.value : XP_BOOST_DEFAULT_SECONDS;
	player.setXpBoostPercent(static_cast<int32_t>(XP_BOOST_PERCENT));
	player.setXpBoostTime(static_cast<uint16_t>(std::min<int64_t>(65535, duration)));
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

	auto item = Item::CreateItem(offer.itemId, offer.count);
	if (!item) {
		return "Failed to create item.";
	}

	if (g_game.internalAddItem(inbox, item.get(), INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
		return "Your store inbox is full.";
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

	for (auto& item : createdItems) {
		if (g_game.internalAddItem(inbox, item.get(), INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
			return "Your store inbox is full.";
		}
	}

	player.sendTextMessage(MESSAGE_STATUS_SMALL, "Your house item was sent to your store inbox.");
	return "";
}

std::string StoreService::deliverNameChange(Player& player, [[maybe_unused]] const StoreOffer& offer,
                                             const StorePurchaseExtra& extra)
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

std::string StoreService::deliverSexChange(Player& player, [[maybe_unused]] const StoreOffer& offer)
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
	lua_State* L = g_luaEnvironment.getLuaState();
	if (!L) {
		return "Lua environment is not available.";
	}

	lua_getglobal(L, "StoreDeliverLuaOffer");
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		// If the Lua bridge function doesn't exist, the offer type is unsupported.
		return "This offer type is not available.";
	}

	// Push arguments: player, offerType, value, displayId, extraName, extraSex
	Lua::pushUserdata<Player>(L, &player);
	Lua::setMetatable(L, -1, "Player");
	lua_pushstring(L, std::string(storeOfferTypeToString(offer.type)).c_str());
	lua_pushinteger(L, offer.value);
	lua_pushinteger(L, offer.displayId);
	lua_pushstring(L, extra.name.c_str());
	lua_pushinteger(L, extra.sex);

	// pcall with 6 args, 1 result.
	if (lua_pcall(L, 6, 1, 0) != LUA_OK) {
		const char* err = lua_tostring(L, -1);
		LOG_ERROR(fmt::format("[StoreService::deliverViaLuaCallback] Lua error: {}", err ? err : "unknown"));
		lua_pop(L, 1);
		return "Delivery failed due to an internal error.";
	}

	// Result: nil = success, string = error message.
	std::string result;
	if (lua_isstring(L, -1)) {
		result = lua_tostring(L, -1);
	}
	lua_pop(L, 1);
	return result;
}
