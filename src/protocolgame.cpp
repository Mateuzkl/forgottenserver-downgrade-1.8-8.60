// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "actions.h"
#include "astraclient.h"
#include "fonticakclient.h"
#include "ban.h"
#include "character_bazaar.h"
#include "configmanager.h"
#include "creatureevent.h"
#include "game.h"
#include "iologindata.h"
#include "save_manager.h"
#include "instance_utils.h"
#include "monster.h"
#include "monsters.h"
#include "outputmessage.h"
#include "player.h"
#include "protocolgame.h"
#include "protocollogin.h"
#include "imbuement.h"
#include "familiar.h"
#include "logger.h"
#include "scheduler.h"
#include "scriptmanager.h"
#include "spells.h"
#include "thread_pool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

uint32_t ProtocolGame::spectatorId = 1;
std::set<std::string> ProtocolGame::spectatorNames;
extern Monsters g_monsters;

namespace {

// 0x3C is reserved for native ZoneId weather on negotiated custom clients.
constexpr uint8_t ZONE_WEATHER_OPCODE = 0x3C;
constexpr uint8_t ZONE_WEATHER_PACKET_VERSION = 1;
constexpr std::array<uint8_t, 8> DLL_WEATHER_SERVER_MAGIC = {
	0xD7, 0x2B, 0x91, 0x4E, 0xC3, 0x68, 0xAF, 0x15
};
constexpr uint8_t DLL_WEATHER_PROTOCOL_VERSION = 2;
constexpr uint32_t DLL_WEATHER_BUILD_ID = 20260719;
constexpr uint32_t DLL_WEATHER_LOGIN_MAGIC = 0x4C44575A; // "ZWDL"
constexpr uint8_t DLL_WEATHER_LOGIN_VERSION = 1;
constexpr uint8_t DLL_WEATHER_LOGIN_TAG_HIGH = 0xA5;
constexpr uint8_t DLL_WEATHER_LOGIN_TAG_LOW = 0x86;

std::atomic<uint32_t> dllWeatherSequenceCounter{0x86193C01};

uint32_t nextDllWeatherSequence()
{
	uint32_t sequence = dllWeatherSequenceCounter.fetch_add(1, std::memory_order_relaxed);
	if (sequence == 0) {
		sequence = dllWeatherSequenceCounter.fetch_add(1, std::memory_order_relaxed);
	}
	return sequence;
}

std::size_t getReadableBytes(const NetworkMessage& msg)
{
	const std::size_t end = static_cast<std::size_t>(msg.getLength()) + NetworkMessage::INITIAL_BUFFER_POSITION;
	const std::size_t position = msg.getBufferPosition();
	return position <= end ? end - position : 0;
}

std::deque<std::pair<int64_t, uint32_t>> waitList; // (timeout, player guid)
std::size_t priorityCount = 0;
constexpr int64_t CAST_SWITCH_COOLDOWN_MS = 500;
constexpr uint8_t HELPER_OPCODE_CAVEBOT = 210;
constexpr uint8_t HELPER_OPCODE_CAST_ON_FOOT = 211;
constexpr uint8_t HELPER_OPCODE_SMART_FOLLOW = 212;
constexpr uint32_t STORAGE_ASTRA_HELPER_CAVEBOT = 99997;
constexpr uint32_t STORAGE_ASTRA_HELPER_SMART_FOLLOW = 99998;
constexpr auto STORE_OUTFIT_OFFERS_PATH = "data/store/gamestore.xml";
constexpr uint8_t ITEM_VALUES_OPCODE = 0xC6;
constexpr size_t ITEM_VALUE_WIRE_SIZE = sizeof(uint16_t) + sizeof(uint32_t);
constexpr size_t ITEM_VALUES_PACKET_HEADER_SIZE = sizeof(uint8_t) + sizeof(uint16_t);
constexpr size_t MAX_ITEM_VALUES_PER_PACKET =
    (NetworkMessage::MAX_PROTOCOL_BODY_LENGTH - ITEM_VALUES_PACKET_HEADER_SIZE) / ITEM_VALUE_WIRE_SIZE;

struct ItemValuesCache {
	std::vector<NetworkMessage> packets;
	bool valid = false;
};

ItemValuesCache itemValuesCache;
std::mutex itemValuesCacheMutex;

std::string anonymizeIPv4ForFile(uint32_t ip)
{
	std::string address = convertIPToString(ip);
	const size_t lastOctet = address.rfind('.');
	if (lastOctet == std::string::npos) {
		return "redacted";
	}
	address.replace(lastOctet + 1, std::string::npos, "0");
	return address;
}

void logPlayerSession(const Player& player, uint32_t ip, bool login)
{
	if (!isLoggerInitialized()) {
		return;
	}

	const auto formatMessage = [&player](std::string_view address) {
		return fmt::format("{} | Lvl:{} | Voc:{} | IP:{}", player.getName(), player.getLevel(),
		                   player.getVocation()->getVocName(), address);
	};
	const std::string consoleMessage = formatMessage(convertIPToString(ip));
	const std::string persistedMessage = formatMessage(anonymizeIPv4ForFile(ip));
	if (login) {
		g_logger().login(std::string_view{consoleMessage}, std::string_view{persistedMessage});
	} else {
		g_logger().logout(std::string_view{consoleMessage}, std::string_view{persistedMessage});
	}
}

using PlayerInventoryKey = std::pair<uint16_t, uint8_t>;
using PlayerInventoryCounts = std::map<PlayerInventoryKey, uint32_t>;

struct StoreOutfitOffer
{
	uint32_t offerId = 0;
	uint8_t addons = 3;
};

using StoreOutfitOfferMap = std::unordered_map<uint16_t, StoreOutfitOffer>;

StoreOutfitOfferMap loadStoreOutfitOffers()
{
	StoreOutfitOfferMap offers;

	pugi::xml_document doc;
	if (!doc.load_file(STORE_OUTFIT_OFFERS_PATH)) {
		return offers;
	}

	auto addLookType = [&offers](uint16_t lookType, uint32_t offerId, uint8_t addons) {
		if (lookType != 0) {
			offers[lookType] = StoreOutfitOffer{offerId, addons};
		}
	};

	for (auto categoryNode : doc.child("store").children("category")) {
		for (auto offerNode : categoryNode.children("offer")) {
			const std::string_view type = offerNode.attribute("type").as_string();
			if (type != "outfit") {
				continue;
			}

			const uint32_t offerId = offerNode.attribute("id").as_uint();
			if (offerId == 0) {
				continue;
			}

			uint32_t addonValue = offerNode.attribute("addon").as_uint(3);
			if (addonValue > 3) {
				addonValue = 3;
			}

			const auto addons = static_cast<uint8_t>(addonValue);
			const auto maleLookType = static_cast<uint16_t>(offerNode.attribute("value").as_uint(offerNode.attribute("eid").as_uint()));
			const auto femaleLookType = static_cast<uint16_t>(offerNode.attribute("femalevalue").as_uint());

			addLookType(maleLookType, offerId, addons);
			addLookType(femaleLookType, offerId, addons);
		}
	}

	return offers;
}

const StoreOutfitOfferMap& getStoreOutfitOffers()
{
	static StoreOutfitOfferMap offers;
	static std::filesystem::file_time_type lastWriteTime{};
	static bool loaded = false;

	std::error_code errorCode;
	auto currentWriteTime = std::filesystem::last_write_time(STORE_OUTFIT_OFFERS_PATH, errorCode);
	if (errorCode) {
		currentWriteTime = {};
	}

	if (!loaded || currentWriteTime != lastWriteTime) {
		offers = loadStoreOutfitOffers();
		lastWriteTime = currentWriteTime;
		loaded = true;
	}

	return offers;
}

uint32_t getPlayerInventoryItemAmount(const Item* item)
{
	if (!item) {
		return 0;
	}

	const ItemType& itemType = Item::items[item->getID()];
	if (itemType.stackable) {
		return std::max<uint32_t>(1, item->getItemCount());
	}

	return 1;
}

void addPlayerInventoryItem(PlayerInventoryCounts& counts, const Item* item)
{
	if (!item) {
		return;
	}

	const uint16_t clientId = item->getClientID();
	if (clientId == 0) {
		return;
	}

	const uint8_t tier = item->getClassification() > 0 ? item->getTier() : 0;
	const PlayerInventoryKey key{clientId, tier};
	counts[key] += getPlayerInventoryItemAmount(item);

	if (const Container* container = item->getContainer()) {
		for (const auto& child : container->getItems(false)) {
			addPlayerInventoryItem(counts, child.get());
		}
	}
}

void addPackedPlayerInventoryCount(NetworkMessage& msg, uint32_t count)
{
	if (count < 0x40) {
		msg.addByte(static_cast<uint8_t>(count));
	} else if (count < 0x4000) {
		msg.addByte(static_cast<uint8_t>((count >> 8) + 0x40));
		msg.addByte(static_cast<uint8_t>(count & 0xFF));
	} else if (count < 0x1000000) {
		msg.addByte(0x80);
		msg.addByte(static_cast<uint8_t>((count >> 16) & 0xFF));
		msg.addByte(static_cast<uint8_t>((count >> 8) & 0xFF));
		msg.addByte(static_cast<uint8_t>(count & 0xFF));
	} else {
		msg.addByte(0xC0);
		msg.addByte(static_cast<uint8_t>((count >> 24) & 0xFF));
		msg.addByte(static_cast<uint8_t>((count >> 16) & 0xFF));
		msg.addByte(static_cast<uint8_t>((count >> 8) & 0xFF));
		msg.addByte(static_cast<uint8_t>(count & 0xFF));
	}
}

uint32_t getStatPercent(uint32_t current, uint32_t maximum)
{
	if (maximum == 0) {
		return 0;
	}
	return static_cast<uint32_t>((static_cast<uint64_t>(current) * 100) / maximum);
}

bool shouldSendPercentStats(const Player* player)
{
	const auto storedValue = player->getStorageValue(STORAGE_HEALTH_DISPLAY);
	if (storedValue) {
		return storedValue.value() == 1;
	}
	return getBoolean(ConfigManager::DEFAULT_HEALTH_DISPLAY_PERCENT);
}

bool isOtclientOperatingSystem(OperatingSystem_t operatingSystem)
{
	switch (operatingSystem) {
		case CLIENTOS_OTCLIENT_LINUX:
		case CLIENTOS_OTCLIENT_WINDOWS:
		case CLIENTOS_OTCLIENT_MAC:
		case CLIENTOS_OTCLIENTV8_LINUX:
		case CLIENTOS_OTCLIENTV8_WINDOWS:
		case CLIENTOS_OTCLIENTV8_MAC:
		case CLIENTOS_OTCLIENTV8_ANDROID:
		case CLIENTOS_OTCLIENTV8_IOS:
		case CLIENTOS_OTCLIENTV8_WEB:
			return true;
		default:
			return false;
	}
}

bool looksLikeLegacyRuleViolationReport(const uint8_t* payload, std::size_t remaining)
{
	if (remaining < 6) {
		return false;
	}

	const uint16_t targetNameLength = payload[0] | (payload[1] << 8);
	if (targetNameLength == 0 || targetNameLength > 32 || static_cast<std::size_t>(targetNameLength) + 6 > remaining) {
		return false;
	}

	for (uint16_t i = 0; i < targetNameLength; ++i) {
		const uint8_t ch = payload[2 + i];
		if (ch < 32 || ch > 126) {
			return false;
		}
	}
	return true;
}

std::size_t getUnreadBytes(const NetworkMessage& msg)
{
	const std::size_t endPosition = static_cast<std::size_t>(msg.getLength()) + NetworkMessage::INITIAL_BUFFER_POSITION;
	const std::size_t currentPosition = msg.getBufferPosition();
	return currentPosition < endPosition ? endPosition - currentPosition : 0;
}

void skipUnreadBytes(NetworkMessage& msg)
{
	const auto unread = std::min<std::size_t>(getUnreadBytes(msg), std::numeric_limits<int16_t>::max());
	msg.skipBytes(static_cast<int16_t>(unread));
}

bool requireUnreadBytes(NetworkMessage& msg, std::size_t required)
{
	if (getUnreadBytes(msg) >= required) {
		return true;
	}

	skipUnreadBytes(msg);
	return false;
}

bool canUseAstraHirelingProtocol(bool isAstraClient)
{
	return isAstraClient && getBoolean(ConfigManager::HIRELING_SYSTEM_ENABLED) &&
	       getBoolean(ConfigManager::ASTRA_HIRELING_PROTOCOL_ENABLED);
}

constexpr std::size_t HIRELING_OUTFIT_REQUEST_SIZE = 9;
constexpr std::size_t HIRELING_OUTFIT_CHANGE_SIZE = 16;
constexpr uint8_t HIRELING_TARGET_TYPE = 1;

bool hasHirelingOutfitMarker(const uint8_t* payload)
{
	return payload[0] == 'H' && payload[1] == 'R' && payload[2] == 'L' && payload[3] == 'G' &&
	       payload[4] == HIRELING_TARGET_TYPE;
}

bool isHirelingOutfitRequestPacket(const NetworkMessage& msg, bool isAstraClient)
{
	if (!canUseAstraHirelingProtocol(isAstraClient) ||
	    getUnreadBytes(msg) != HIRELING_OUTFIT_REQUEST_SIZE) {
		return false;
	}

	const uint8_t* payload = msg.getBuffer() + msg.getBufferPosition();
	return hasHirelingOutfitMarker(payload);
}

bool isHirelingOutfitChangePacket(const NetworkMessage& msg, bool isAstraClient)
{
	if (!canUseAstraHirelingProtocol(isAstraClient) ||
	    getUnreadBytes(msg) != HIRELING_OUTFIT_CHANGE_SIZE) {
		return false;
	}

	const uint8_t* payload = msg.getBuffer() + msg.getBufferPosition();
	return hasHirelingOutfitMarker(payload);
}

uint8_t getRuleViolationTypeFromLegacyAction(uint8_t action)
{
	if (action == 6) {
		return REPORT_TYPE_STATEMENT;
	}

	if (action == 1 || action == 3 || action == 5) {
		return REPORT_TYPE_NAME;
	}

	return REPORT_TYPE_BOT;
}

bool isEnabledHelperBuffer(std::string_view buffer)
{
	return buffer == "1" || buffer == "true" || buffer == "on" || buffer == "enabled";
}

std::optional<uint32_t> getHelperStateStorageKey(uint8_t opcode)
{
	switch (opcode) {
		case HELPER_OPCODE_CAVEBOT:
			return STORAGE_ASTRA_HELPER_CAVEBOT;
		case HELPER_OPCODE_SMART_FOLLOW:
			return STORAGE_ASTRA_HELPER_SMART_FOLLOW;
		default:
			return std::nullopt;
	}
}

auto findClient(uint32_t guid)
{
	std::size_t slot = 1;
	for (auto it = waitList.begin(), end = waitList.end(); it != end; ++it, ++slot) {
		if (it->second == guid) {
			return std::make_pair(it, slot);
		}
	}

	return std::make_pair(waitList.end(), static_cast<std::size_t>(0));
}

constexpr int64_t getWaitTime(std::size_t slot)
{
	if (slot < 5) {
		return 5;
	} else if (slot < 10) {
		return 10;
	} else if (slot < 20) {
		return 20;
	} else if (slot < 50) {
		return 60;
	}
	return 120;
}

constexpr int64_t getTimeout(std::size_t slot)
{
	// timeout is set to 15 seconds longer than expected retry attempt
	return getWaitTime(slot) + 15;
}

std::size_t clientLogin(const Player& player)
{
	// Currentslot = position in wait list, 0 for direct access
	if (player.hasFlag(PlayerFlag_CanAlwaysLogin) || player.getAccountType() >= ACCOUNT_TYPE_GAMEMASTER) {
		return 0;
	}

	const uint32_t maxPlayers = static_cast<uint32_t>(getInteger(ConfigManager::MAX_PLAYERS));
	if (maxPlayers == 0 || (waitList.empty() && g_game.getPlayersOnline() < maxPlayers)) {
		return 0;
	}

	int64_t time = OTSYS_TIME();

	std::size_t index = 0;
	std::erase_if(waitList, [time, &index](const auto& entry) {
		const bool expired = (entry.first - time) <= 0;
		if (expired && index < priorityCount && priorityCount > 0) {
			--priorityCount;
		}
		++index;
		return expired;
	});

	std::size_t slot;
	auto it = waitList.end();
	std::tie(it, slot) = findClient(player.getGUID());
	if (it != waitList.end()) {
		// If server has capacity for this client, let him in even though his current slot might be higher than 0.
		if ((g_game.getPlayersOnline() + slot) <= maxPlayers) {
			if (slot <= priorityCount && priorityCount > 0) {
				--priorityCount;
			}
			waitList.erase(it);
			return 0;
		}

		// let them wait a bit longer
		it->first = time + (getTimeout(slot) * 1000);
		return slot;
	}

	if (player.isPremium()) {
		const std::size_t insertIndex = std::min(priorityCount, waitList.size());
		auto insertPos = waitList.begin();
		std::advance(insertPos, insertIndex);
		waitList.emplace(insertPos, time + (getTimeout(insertIndex + 1) * 1000), player.getGUID());
		++priorityCount;
		return priorityCount;
	}
	waitList.emplace_back(time + (getTimeout(waitList.size() + 1) * 1000), player.getGUID());
	return waitList.size();
}

} // namespace

ProtocolGame::~ProtocolGame()
{
#ifdef PROTOCOLGAME_BACKLOG_DIAGNOSTICS
	if (packetBacklog.peak() != 0 || packetBacklog.rejected() != 0) {
		LOG_NETWORK("[ProtocolGame] Packet backlog summary: peak={} rejected={}", packetBacklog.peak(),
		            packetBacklog.rejected());
	}
#endif
}

void ProtocolGame::sendBlessingWindow()
{
	if (!player || !isAstraClient) return;

	NetworkMessage msg;
	msg.addByte(0x9B);
	msg.addByte(0x08);

	for (uint8_t i = 1; i <= 8; i++) {
		msg.add<uint16_t>(1 << i);
		msg.addByte(player->getBlessingCount(i));
		msg.addByte(0);
	}

	uint8_t blessCount = player->getBlessingReduction();
	bool isPromoted = player->isPromoted();
	uint8_t skillReduction = blessCount * 8;
	uint8_t promotionReduction = isPromoted ? 30 : 0;
	uint8_t minReduction = skillReduction + promotionReduction;
	uint8_t maxPvpReduction = 80 + (2 * blessCount) - (blessCount / 3);
	if (blessCount == 5) maxPvpReduction -= 1;
	if (isPromoted) maxPvpReduction += 6;

	msg.addByte(isPromoted ? 1 : 0);
	msg.addByte(30);
	msg.addByte(minReduction);
	msg.addByte(maxPvpReduction);
	msg.addByte(minReduction);

	bool hasSkull = player->getSkull() == SKULL_RED || player->getSkull() == SKULL_BLACK;
	auto amulet = player->getInventoryItem(CONST_SLOT_NECKLACE);
	bool usingAol = amulet && amulet->getID() == ITEM_AMULETOFLOSS;

	if (hasSkull) {
		msg.addByte(100); msg.addByte(100);
	} else if (usingAol) {
		msg.addByte(0); msg.addByte(0);
	} else {
		msg.addByte(static_cast<uint8_t>(player->getEquipmentLossPercent(true)));
		msg.addByte(static_cast<uint8_t>(player->getEquipmentLossPercent(false)));
	}
	msg.addByte(hasSkull ? 1 : 0);
	msg.addByte(usingAol ? 1 : 0);

	// Death history log
	auto& deathLog = player->getDeathLog();
	msg.addByte(static_cast<uint8_t>(deathLog.size()));
	for (const auto& entry : deathLog) {
		msg.add<uint32_t>(entry.timestamp);
		msg.addByte(entry.color);
		msg.addString(entry.message);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBlessStatus()
{
	if (!player || !isAstraClient) return;

	uint8_t totalCount = 0;
	for (uint8_t i = 2; i <= 8; i++) {
		if (player->hasBlessing(i)) totalCount++;
	}

	NetworkMessage msg;
	msg.addByte(0x9C);
	bool glow = player->getVocationId() > 0 && (totalCount >= 4 || player->getLevel() < 21);
	msg.add<uint16_t>(glow ? 1 : 0);
	msg.addByte(totalCount >= 6 ? 3 : (totalCount >= 4 ? 2 : 1));
	writeToOutputBuffer(msg);
}

void ProtocolGame::release()
{
	// dispatcher thread
	if (player) {
		if (isSpectator) {
			auto clientRef = player->client;
			if (clientRef) {
				clientRef->removeSpectator(getThis());
			}
			spectatorNames.erase(asLowerCaseString(spectator_name));
		} else {
			auto clientRef = player->client;
			if (clientRef) {
				auto clientProtocol = clientRef->protocol();
				if (clientProtocol && clientProtocol == shared_from_this()) {
					clientRef->setOwner(nullptr);
				}
			}
		}
		player.reset();
	}

	OutputMessagePool::getInstance().removeProtocolFromAutosend(shared_from_this());
	Protocol::release();
}

bool ProtocolGame::shouldSendQuickLootFlags() const
{
	return isAstraClient && getBoolean(ConfigManager::QUICK_LOOT_ENABLED);
}

bool ProtocolGame::shouldSendContainerPagination() const
{
	return isOTCv8 || isAstraClient || isMehah;
}

bool ProtocolGame::shouldPaginateContainer(const Container* container) const
{
	if (!container) {
		return false;
	}

	return container->hasPagination() || (isAstraClient && container->getRewardChest());
}

bool ProtocolGame::canSendAstraItemState() const
{
	if (!player || !player->client || !isAstraClient || isSpectator ||
	    !getBoolean(ConfigManager::ASTRA_ITEM_STATE_ENABLED)) {
		return false;
	}

	const ProtocolGame_ptr ownerProtocol = player->client->protocol();
	return ownerProtocol.get() == this;
}

bool ProtocolGame::shouldSendAstraQuiverCountU16() const
{
	return isAstraClient;
}

bool ProtocolGame::shouldSendItemTierByte() const
{
	return useItemTierByte && getBoolean(ConfigManager::ITEM_TIER_DISPLAY);
}

bool ProtocolGame::shouldSendThingUpgradeClassification() const
{
	if (!getBoolean(ConfigManager::ITEM_TIER_DISPLAY)) {
		return false;
	}

	if (isMehah) {
		return getBoolean(ConfigManager::ITEM_UPGRADE_CLASSIFICATION);
	}

	// OTCv8 Classic uses this feature as the Lua-side gate for drawing tier icons.
	// Keep the actual item wire format tied to GameItemTierByte so CIP and
	// non-tier-aware OTC clients never receive unexpected item bytes.
	return isOTCv8 && !isAstraClient && shouldSendItemTierByte();
}

bool ProtocolGame::shouldSendItemTierData() const
{
	return shouldSendItemTierByte() || shouldSendThingUpgradeClassification();
}

void ProtocolGame::login(uint32_t characterId, uint32_t accountId, OperatingSystem_t operatingSystem)
{
	if (CharacterBazaar::isPlayerOnActiveAuction(characterId)) {
		disconnectClient("This character is currently listed on the Character Bazaar and cannot enter the game until the auction finishes or is cancelled.");
		return;
	}

	// OTCv8 and Mehah features and extended opcodes
	if (isOTC) {
		// Player loading can emit status packets before finishLogin(). Astra
		// therefore needs its final wire-format features advertised up front.
		sendFeatures(isAstraClient);

		NetworkMessage opcodeMessage;
		opcodeMessage.addByte(0x32);
		opcodeMessage.addByte(0x00);
		opcodeMessage.add<uint16_t>(0x00);
		writeToOutputBuffer(opcodeMessage);
	}

	// dispatcher thread
	auto foundPlayer = g_game.getPlayerByGUID(characterId);
	std::string name;
	if (foundPlayer) {
		name = foundPlayer->getName();
	}
	if (!foundPlayer || name == "Account Manager" || getBoolean(ConfigManager::ALLOW_CLONES)) {
		player = std::make_shared<Player>(getThis());
		player->setGUID(characterId);
		player->setID();

		if (!IOLoginData::preloadPlayer(player.get())) {
			disconnectClient("Your character could not be loaded.");
			return;
		}

		name = player->getName();
		bool isAccountManager =
		    (name == "Account Manager" && ConfigManager::getBoolean(ConfigManager::ACCOUNT_MANAGER));

		if (isAccountManager && accountId != 1) {
			player->accountNumber = accountId;
		}

		if (IOBan::isPlayerNamelocked(player->getGUID()) && accountId != 1) {
			if (ConfigManager::getBoolean(ConfigManager::NAMELOCK_MANAGER)) {
				std::string originalName = player->getName();

				player->setName("Account Manager");
				player->setAccountManagerMode(ACCOUNT_MANAGER_NAMELOCK);
				player->accountNumber = accountId;
				player->setAccountManagerData(accountId);
				player->managerData.string2 = originalName;
			} else {
				disconnectClient("Your character has been namelocked.");
				return;
			}
		} else if (IOBan::isPlayerNamelocked(player->getGUID())) {
			disconnectClient("Your character has been namelocked.");
			return;
		}

		if (g_game.getGameState() == GAME_STATE_CLOSING && !player->hasFlag(PlayerFlag_CanAlwaysLogin)) {
			disconnectClient("The game is just going down.\nPlease try again later.");
			return;
		}

		if (g_game.getGameState() == GAME_STATE_CLOSED && !player->hasFlag(PlayerFlag_CanAlwaysLogin)) {
			disconnectClient("Server is currently closed.\nPlease try again later.");
			return;
		}

		if (getBoolean(ConfigManager::ONE_PLAYER_ON_ACCOUNT) && name != "Account Manager" &&
		    player->getAccountType() < ACCOUNT_TYPE_GAMEMASTER && g_game.getPlayerByAccount(player->getAccount())) {
			disconnectClient("You may only login with one character\nof your account at the same time.");
			return;
		}

		if (!player->hasFlag(PlayerFlag_CannotBeBanned)) {
			BanInfo banInfo;
			if (IOBan::isAccountBanned(accountId, banInfo)) {
				if (banInfo.reason.empty()) {
					banInfo.reason = "(none)";
				}

				if (banInfo.expiresAt > 0) {
					disconnectClient(
					    fmt::format("Your account has been banned until {:s} by {:s}.\n\nReason specified:\n{:s}",
					                formatDateShort(banInfo.expiresAt), banInfo.bannedBy, banInfo.reason));
				} else {
					disconnectClient(
					    fmt::format("Your account has been permanently banned by {:s}.\n\nReason specified:\n{:s}",
					                banInfo.bannedBy, banInfo.reason));
				}
				return;
			}
		}

		if (std::size_t currentSlot = clientLogin(*player)) {
			uint8_t retryTime = getWaitTime(currentSlot);
			auto output = OutputMessagePool::getOutputMessage();
			output->addByte(0x16);
			output->addString(
			    fmt::format("Too many players online.\nYou are at place {:d} on the waiting list.", currentSlot));
			output->addByte(retryTime);
			send(output);
			disconnect();
			return;
		}

		const uint32_t reservedGuid = player->getGUID();
		if (!g_game.reserveLogin(reservedGuid)) {
			disconnectClient("You are already logging in.");
			return;
		}

		if (g_saveManager.hasFailedRecovery(reservedGuid)) {
			g_game.releaseLogin(reservedGuid);
			disconnectClient(
			    "Your character data is in a recoverable state. Please contact an administrator to resolve this issue.");
			return;
		}

		const auto loginPlayer = player;
		g_threadPool.detach_task([self = getThis(), loginPlayer, reservedGuid, accountId, operatingSystem]() {
			// Shared atomic: guards mutual exclusion between timeout and callback
			auto completed = std::make_shared<std::atomic<bool>>(false);

			const uint32_t timeoutEventId = g_scheduler.addEvent(10000, [self, reservedGuid, completed]() {
				if (completed->exchange(true)) {
					return; // callback already handled
				}
				g_dispatcher.addTask([self, reservedGuid]() {
					g_game.releaseLogin(reservedGuid);
					if (self->player) {
						self->disconnectClient("Login timed out waiting for save to complete.");
					}
				});
			});

			g_saveManager.drainPlayerFlushAsync(reservedGuid,
				[self, reservedGuid, accountId, loginPlayer, operatingSystem, timeoutEventId, completed](bool drained) {
				g_scheduler.stopEvent(timeoutEventId);
				if (completed->exchange(true)) {
					// Timeout already handled this login
					return;
				}

				if (!drained) {
					g_dispatcher.addTask([self, reservedGuid]() {
						g_game.releaseLogin(reservedGuid);
						if (self->player) {
							self->disconnectClient(
								"Character data is still being saved. Please try again in a few seconds.");
						}
					});
					return;
				}

				g_threadPool.detach_task([self, reservedGuid, accountId, loginPlayer, operatingSystem]() {
					const bool loaded = IOLoginData::loadPlayerById(loginPlayer.get(), reservedGuid, true);
					g_dispatcher.addTask([self, reservedGuid, accountId, loaded, operatingSystem]() {
						self->finishLogin(reservedGuid, accountId, loaded, operatingSystem);
					});
				});
			});
		});
		return;
	} else {
		if (eventConnect != 0 || !getBoolean(ConfigManager::REPLACE_KICK_ON_LOGIN)) {
			// Already trying to connect
			disconnectClient("You are already logged in.");
			return;
		}

		auto clientRef = foundPlayer->client;
		if (clientRef && clientRef->protocol()) {
			clientRef->disconnectClient(
			    "You are already logged in.\nSomeone is trying to access your account?");
			clientRef->disconnect();
			clientRef->setOwner(nullptr);
			g_scheduler.addEvent(
			    createSchedulerTask(1000, ([=, thisPtr = getThis(), playerID = foundPlayer->getID()]() {
				                        thisPtr->connect(playerID, operatingSystem);
			                        })));
			return;
		} else {
			connect(foundPlayer->getID(), operatingSystem);
		}
	}
}

void ProtocolGame::finishLogin(uint32_t reservedGuid, uint32_t accountId, bool loaded, OperatingSystem_t operatingSystem)
{
	if (!player || isConnectionExpired()) {
		g_game.releaseLogin(reservedGuid);
		return;
	}

	if (!loaded) {
		g_game.releaseLogin(reservedGuid);
		disconnectClient("Your character could not be loaded.");
		return;
	}

	IOLoginData::loadPlayerWorldData(player.get());

	player->client->setOwner(getThis());
	player->setOperatingSystem(operatingSystem);
	player->client->isOTCv8 = isOTCv8;
	player->client->isMehah = isMehah;
	player->client->isOTC = isOTC;
	player->client->isAstraClient = isAstraClient;
	player->client->isFonticakClient = isFonticakClient;
	if (!g_game.placeCreature(player.get(), player->getLoginPosition())) {
		if (!g_game.placeCreature(player.get(), player->getTemplePosition(), false, true)) {
			g_game.releaseLogin(reservedGuid);
			disconnectClient("Temple position is wrong. Contact the administrator.");
			return;
		}
	}
	sendLootContainers();

	if (isOTC) {
		player->registerCreatureEvent("ExtendedOpcode");
	}

	const std::string& name = player->getName();
	if (ConfigManager::getBoolean(ConfigManager::ACCOUNT_MANAGER) && name == "Account Manager" &&
	    player->getAccountManagerMode() == ACCOUNT_MANAGER_NONE) {
		if (accountId == 1) {
			player->setAccountManagerMode(ACCOUNT_MANAGER_NEW);
			player->sendTextMessage(
			    MESSAGE_STATUS_CONSOLE_ORANGE,
			    "Account Manager: Welcome! You are now speaking with the Account Manager. To create a new account, type {account}. If you already have one and need to recover it, type {recover}. Type {cancel} anytime to restart this conversation.");
		} else {
			player->setAccountManagerMode(ACCOUNT_MANAGER_ACCOUNT);
			player->setAccountManagerData(accountId);
			player->resetTalkState(0, 0);
			player->setManagerTalkState(1, true);
			player->sendTextMessage(
			    MESSAGE_STATUS_CONSOLE_ORANGE,
			    "Account Manager: Welcome back. Type {account} to manage your account, {character} to create a new character, or {cancel} to start over.");
		}
	}

	if (player->isAccountManager()) {
		player->setMovementBlocked(true);
	}

	player->lastIP = player->getIP();
	player->lastLoginSaved = std::max<time_t>(time(nullptr), player->lastLoginSaved + 1);
	acceptPackets = true;
	logPlayerSession(*player, player->lastIP, true);
	g_game.releaseLogin(reservedGuid);
}

void ProtocolGame::spectate(const std::string& name, const std::string& password, uint32_t clientIP)
{
	//dispatcher thread
	if (isConnectionExpired()) {
		LoginAttemptLimiter::getInstance().releaseReservation(clientIP, "");
		return;
	}

	// OTC features and extended opcodes
	if (isOTC) {
		sendFeatures();
		NetworkMessage opcodeMessage;
		opcodeMessage.addByte(0x32);
		opcodeMessage.addByte(0x00);
		opcodeMessage.add<uint16_t>(0x00);
		writeToOutputBuffer(opcodeMessage);
	}

	auto foundPlayer = g_game.getPlayerByName(name);
	auto castClient = foundPlayer ? foundPlayer->client : nullptr;
	if (!foundPlayer || !castClient || !castClient->isBroadcasting()) {
		LoginAttemptLimiter::getInstance().releaseReservation(clientIP, "");
		disconnectClient("That cast is not available anymore.");
		return;
	}

	if (!castClient->password().empty() && asLowerCaseString(castClient->password()) != asLowerCaseString(password)) {
		// A cast password is a credential like any other, so a rejected guess has to
		// count. It is recorded with an empty account name, which registers against
		// the per-IP spray guard only - a cast has no account dimension, and keying
		// it by the streamer's name would let anyone lock a streamer's viewers out.
		recordRejectedCastPassword(clientIP);
		disconnectClient("Wrong password for that cast.");
		return;
	}

	if (castClient->isBanned(getIP())) {
		LoginAttemptLimiter::getInstance().releaseReservation(clientIP, "");
		disconnectClient("You are banned on this cast.");
		return;
	}
	LoginAttemptLimiter::getInstance().commitSuccess(clientIP, "");

	player = foundPlayer;
	isSpectator = true;

	do {
		spectator_name = std::string("Spectator_") + std::to_string(spectatorId);
		spectatorId += 1;
	} while (spectatorNames.contains(asLowerCaseString(spectator_name)));
	spectatorNames.insert(asLowerCaseString(spectator_name));

	sendAddCreature(player.get(), player->getPosition(), 0, CONST_ME_NONE);
	sendCastChannel();
	syncOpenContainers();

	player->client->addSpectator(getThis());
	player->resetIdleTime();
	acceptPackets = true;
	sendWelcomeMessage();

}

void ProtocolGame::recordRejectedCastPassword(uint32_t ip)
{
	LoginAttemptLimiter::getInstance().commitFailure(ip, "");
}

void ProtocolGame::connect(uint32_t playerId, OperatingSystem_t operatingSystem)
{
	eventConnect = 0;

	auto foundPlayer = g_game.getPlayerByID(playerId);
	if (!foundPlayer) {
		disconnectClient("You are already logged in.");
		return;
	}

	auto clientRef = foundPlayer->client;
	if (clientRef && clientRef->protocol()) {
		disconnectClient("You are already logged in.");
		return;
	}

	if (isConnectionExpired()) {
		// ProtocolGame::release() has been called at this point and the Connection object
		// no longer exists, so we return to prevent leakage of the Player.
		return;
	}

	player = foundPlayer;

	player->clearModalWindows();
	g_chat->removeUserFromAllChannels(*player);
	player->setOperatingSystem(operatingSystem);
	player->isConnecting = false;

	if (!player->client) {
		player->client = std::make_shared<ProtocolSpectator>(getThis());
	} else {
		player->client->setOwner(getThis());
	}
	player->client->isOTCv8 = isOTCv8;
	player->client->isMehah = isMehah;
	player->client->isOTC = isOTC;
	player->client->isAstraClient = isAstraClient;
	player->client->isFonticakClient = isFonticakClient;
	sendAddCreature(player.get(), player->getPosition(), 0);
	sendLootContainers();
	player->lastIP = player->getIP();
	player->lastLoginSaved = std::max<time_t>(time(nullptr), player->lastLoginSaved + 1);
	player->resetIdleTime();
	player->lastPing = OTSYS_TIME();
	acceptPackets = true;
	logPlayerSession(*player, player->lastIP, true);

	g_creatureEvents->playerReconnect(player.get());
}

void ProtocolGame::logout(bool displayEffect, bool forced)
{
	// dispatcher thread
	if (!player) {
		return;
	}

	if (!player->isRemoved()) {
		if (!forced) {
			if (player->getAccountType() < ACCOUNT_TYPE_GOD) {
				if (player->getTile()->hasFlag(TILESTATE_NOLOGOUT)) {
					player->sendCancelMessage(RETURNVALUE_YOUCANNOTLOGOUTHERE);
					return;
				}

				if (!player->getTile()->hasFlag(TILESTATE_PROTECTIONZONE) && player->hasCondition(CONDITION_INFIGHT)) {
					player->sendCancelMessage(RETURNVALUE_YOUMAYNOTLOGOUTDURINGAFIGHT);
					return;
				}
			}

			// scripting event - onLogout
			if (!g_creatureEvents->playerLogout(player.get())) {
				// Let the script handle the error message
				return;
			}
		}

		if (displayEffect && !player->isDead() && !player->isInGhostMode()) {
			g_game.addMagicEffect(player->getPosition(), CONST_ME_POFF, player->getInstanceID());
		}
	}

	logPlayerSession(*player, player->getIP(), false);
	player->client->clear();
	disconnect();

	g_game.removeCreature(player.get());
}

void ProtocolGame::onRecvFirstMessage(NetworkMessage& msg)
{
	if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
		disconnect();
		return;
	}

	OperatingSystem_t operatingSystem = static_cast<OperatingSystem_t>(msg.get<uint16_t>());
	clientOperatingSystem = operatingSystem;
	version = msg.get<uint16_t>();

	if (!Protocol::RSA_decrypt(msg)) {
		disconnect();
		return;
	}

	xtea::key key;
	key[0] = msg.get<uint32_t>();
	key[1] = msg.get<uint32_t>();
	key[2] = msg.get<uint32_t>();
	key[3] = msg.get<uint32_t>();
	enableXTEAEncryption();
	setXTEAKey(key);

	msg.skipBytes(1); // gamemaster flag

	auto accountName = msg.getString();
	auto characterName = msg.getString();
	auto password = msg.getString();

	uint32_t timeStamp = msg.get<uint32_t>();
	uint8_t randNumber = msg.getByte();

	if (challengeTimestamp != timeStamp || challengeRandom != randNumber) {
		disconnect();
		return;
	}

	if (operatingSystem == CLIENTOS_CUSTOM_DLL) {
		constexpr std::size_t markerSize = sizeof(uint32_t) + (sizeof(uint8_t) * 4);
		if (getReadableBytes(msg) >= markerSize) {
			const uint32_t magic = msg.get<uint32_t>();
			const uint8_t markerVersion = msg.getByte();
			const uint8_t weatherOpcode = msg.getByte();
			const uint8_t tagHigh = msg.getByte();
			const uint8_t tagLow = msg.getByte();
			supportsDllZoneWeather = magic == DLL_WEATHER_LOGIN_MAGIC &&
			                         markerVersion == DLL_WEATHER_LOGIN_VERSION &&
			                         weatherOpcode == ZONE_WEATHER_OPCODE &&
			                         tagHigh == DLL_WEATHER_LOGIN_TAG_HIGH &&
			                         tagLow == DLL_WEATHER_LOGIN_TAG_LOW;
		}

		if (!supportsDllZoneWeather) {
			disconnectClient("This custom client does not support the required Zone Weather protocol.");
			return;
		}
		dllWeatherSequence = nextDllWeatherSequence();
	}

	// OTCv8 version detection
	if (msg.getBufferPosition() < msg.getLength()) {
		uint16_t otcV8StringLength = msg.get<uint16_t>();
		if (otcV8StringLength == 5 && msg.getString(5) == "OTCv8") {
			isOTCv8 = true;
			msg.get<uint16_t>();

			while (msg.getBufferPosition() + 2 <= msg.getLength()) {
				uint16_t markerLength = msg.get<uint16_t>();
				if (markerLength == 0) {
					break;
				}

				if (markerLength > 64 || msg.getBufferPosition() + markerLength > msg.getLength()) {
					break;
				}

				const auto marker = msg.getString(markerLength);
				if (marker == "OTCv8TierByte") {
					useItemTierByte = true;
				} else if (marker == "OTCv8ZoneWeather") {
					supportsZoneWeather = true;
				} else if (marker == AstraClient::LOGIN_MARKER) {
					if (msg.getBufferPosition() + sizeof(uint32_t) > msg.getLength()) {
						break;
					}
					isAstraClient =
					    msg.get<uint32_t>() ==
					    AstraClient::generateSignature(static_cast<uint16_t>(operatingSystem), version, key,
					                                   challengeTimestamp, challengeRandom);
				} else if (marker == FonticakClient::LOGIN_MARKER) {
					if (msg.getBufferPosition() + sizeof(uint32_t) > msg.getLength()) {
						break;
					}
					isFonticakClient =
					    msg.get<uint32_t>() ==
					    FonticakClient::generateSignature(static_cast<uint16_t>(operatingSystem), version, key,
					                                   challengeTimestamp, challengeRandom);
				} else {
					break;
				}
			}
		}
	}

	// mehah detect
	if (operatingSystem == CLIENTOS_OTCLIENT_WINDOWS) {
		isMehah = true;
	}

	isOTC = isOTCv8 || isMehah || isOtclientOperatingSystem(operatingSystem);

	if (getBoolean(ConfigManager::ASTRA_CLIENT_ONLY)) {
		if (!isAstraClient) {
			LOG_WARN("[AstraClient] Client rejected: AstraClient required");
			disconnectClient(AstraClient::REQUIRED_MESSAGE);
			return;
		}
	}

	if (getBoolean(ConfigManager::FONTICAK_CLIENT_ONLY)) {
		if (!isFonticakClient) {
			LOG_WARN("[FonticakClient] Client rejected: OTC-Fonticak required");
			disconnectClient(FonticakClient::REQUIRED_MESSAGE);
			return;
		}
	}

	if (isAstraClient) {
		LOG_NETWORK("Client connected: AstraClient");
	} else if (isFonticakClient) {
		LOG_NETWORK("Client connected: FonticakClient");
	}

	if (version < CLIENT_VERSION_MIN || version > CLIENT_VERSION_MAX) {
		disconnectClient(fmt::format("Only clients with protocol {:s} allowed!", CLIENT_VERSION_STR));
		return;
	}

	if (g_game.getGameState() == GAME_STATE_STARTUP) {
		disconnectClient("Gameworld is starting up. Please wait.");
		return;
	}

	if (g_game.getGameState() == GAME_STATE_MAINTAIN) {
		disconnectClient("Gameworld is under maintenance. Please re-connect in a while.");
		return;
	}

	// Brute force protection. The login server applies this limiter on its own
	// port, but the game world validates the same account name and password
	// independently, so without the check here an attacker can skip the login
	// server entirely and guess credentials at connection speed.
	//
	// The account name is passed so failures are counted per account. A cast
	// (spectator) request carries no account name and is authenticated against the
	// cast password instead, so it only meets the per-IP spray guard - watching a
	// stream keeps working while one account on the same address is locked out.
	const uint32_t clientIP = getIP();
	if (!LoginAttemptLimiter::getInstance().reserveLogin(clientIP, accountName)) {
		disconnectClient("Too many failed login attempts. Please try again later.");
		return;
	}

	// Authenticate and resolve account/character IDs
	const auto authentication = IOLoginData::gameworldAuthentication(accountName, password, characterName);
	if (authentication.cast) {
		g_dispatcher.addTask([thisPtr = getThis(), name = std::string(characterName), pass = std::string(password),
		                      clientIP]() { thisPtr->spectate(name, pass, clientIP); });
		return;
	}
	const uint32_t accountId = authentication.accountId;
	const uint32_t characterId = authentication.characterId;

	if (authentication.status == IOLoginData::AuthenticationResult::Rejected) {
		LoginAttemptLimiter::getInstance().commitFailure(clientIP, accountName);
	} else if (authentication.status == IOLoginData::AuthenticationResult::Success) {
		LoginAttemptLimiter::getInstance().commitSuccess(clientIP, accountName);
	} else {
		LoginAttemptLimiter::getInstance().releaseReservation(clientIP, accountName);
		disconnectClient("The login service is temporarily unavailable. Please try again later.");
		return;
	}

	BanInfo banInfo;
	if (IOBan::isIpBanned(getIP(), banInfo)) {
		if (banInfo.reason.empty()) {
			banInfo.reason = "(none)";
		}

		disconnectClient(fmt::format("Your IP has been banned until {:s} by {:s}.\n\nReason specified:\n{:s}",
		                             formatDateShort(banInfo.expiresAt), banInfo.bannedBy, banInfo.reason));
		return;
	}

	if (accountId == 0) {
		disconnectClient("Account name or password is not correct.");
		return;
	}
	if (characterId != 0 && CharacterBazaar::isPlayerOnActiveAuction(characterId)) {
		disconnectClient("This character is currently listed on the Character Bazaar and cannot enter the game until the auction finishes or is cancelled.");
		return;
	}

	g_dispatcher.addTask([=, thisPtr = getThis()]() { thisPtr->login(characterId, accountId, operatingSystem); });
}

void ProtocolGame::onConnect()
{
	auto output = OutputMessagePool::getOutputMessage();
	static std::random_device rd;
	static std::ranlux24 generator(rd());
	static std::uniform_int_distribution<uint16_t> randNumber(0x00, 0xFF);

	// Skip checksum
	output->skipBytes(sizeof(uint32_t));

	// Packet length & type
	output->add<uint16_t>(0x0006);
	output->addByte(0x1F);

	// Add timestamp & random number
	challengeTimestamp = static_cast<uint32_t>(time(nullptr));
	output->add<uint32_t>(challengeTimestamp);

	challengeRandom = randNumber(generator);
	output->addByte(challengeRandom);

	// Go back and write checksum
	output->skipBytes(-12);
	output->add<uint32_t>(adlerChecksum(output->getOutputBuffer() + sizeof(uint32_t), 8));

	send(output);
}

void ProtocolGame::disconnectClient(std::string_view message) const
{
	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0x14);
	output->addString(message);
	send(output);
	disconnect();
}

void ProtocolGame::dispatchCancelMessage(ReturnValue message) const
{
	if (player) {
		player->sendCancelMessage(message);
	}
}

void ProtocolGame::writeToOutputBuffer(const NetworkMessage& msg)
{
	auto out = getOutputBuffer(msg.getLength());
	out->append(msg);
}

void ProtocolGame::parsePacket(NetworkMessage& msg)
{
	if (msg.getLength() == 0 || getReadableBytes(msg) == 0) {
		return;
	}

	const uint8_t opcode = msg.getBuffer()[msg.getBufferPosition()];
	auto admission = packetBacklog.tryAcquire();
	if (!admission) {
		if (admission.requestDisconnect) {
			LOG_NETWORK("[ProtocolGame] Packet backlog limit exceeded. Closing connection. pending={} limit={}",
			            admission.observedPending, packetBacklog.limit());
			g_dispatcher.addTask([thisPtr = getThis()]() { thisPtr->disconnect(); });
		}
		return;
	}

#ifdef PROTOCOLGAME_BACKLOG_DIAGNOSTICS
	if (admission.newPeak >= 16 &&
	    (admission.newPeak == packetBacklog.limit() || (admission.newPeak & (admission.newPeak - 1)) == 0)) {
		LOG_NETWORK("[ProtocolGame] New packet backlog peak: pending={} limit={}", admission.newPeak,
		            packetBacklog.limit());
	}
#endif

	// The ASIO thread only owns the incoming bytes. Protocol and player state
	// are read exclusively by the dispatcher task below.
	auto packet = tfs::net::make_network_message(msg);
	auto task = [thisPtr = getThis(), packet = std::move(packet), ticket = std::move(admission.ticket)]() mutable {
		(void)ticket;
		if (thisPtr->isConnectionExpired()) {
			return;
		}

		thisPtr->parsePacketOnDispatcher(packet);
	};

	if (tfs::net::shouldExpireQueuedGamePacket(opcode)) {
		g_dispatcher.addTask(DISPATCHER_TASK_EXPIRATION, std::move(task));
	} else {
		g_dispatcher.addTask(std::move(task));
	}
}

void ProtocolGame::parsePacketOnDispatcher(NetworkMessage_ptr& packet)
{
	assert(g_dispatcher.isDispatcherThread() && "ProtocolGame packet state must be handled by the dispatcher");
	NetworkMessage& msg = *packet;

	if (!acceptPackets || g_game.getGameState() == GAME_STATE_SHUTDOWN || msg.getLength() == 0) {
		return;
	}

	uint8_t recvbyte = msg.getByte();

	if (!player) {
		if (recvbyte == 0x0F) {
			disconnect();
		}

		return;
	}

	// a dead player can not performs actions
	if (player->isRemoved() || player->isDead()) {
		if (recvbyte == 0x0F) {
			disconnect();
			return;
		}

		if (recvbyte != 0x14) {
			return;
		}
	}

	// Spy mode: GOD can only talk, ping, and logout while spying
	if (spyActive_) {
		switch (recvbyte) {
			case 0x14: // logout
			case 0x1E: // ping
			case 0x40: // extended ping (OTC)
			case 0x96: // say (allows /unspy)
				break; // allowed — fall through to normal processing
			default:
				sendCancelWalk();
				return; // block all other actions
		}
	}

	if (isSpectator) {
		switch (recvbyte) {
			case 0x14: disconnect(); break;
			case 0x1E:
				if (clientOperatingSystem == CLIENTOS_CUSTOM_DLL) {
					parseCustomClientPing(msg);
				} else {
					g_game.playerReceivePing(player->getID());
				}
				break;
			case 0x32:
				if (isOTC) {
					parseExtendedOpcode(msg);
				}
				break; // otclient extended opcode
			case 0x40:
				if (isOTC) {
					parseNewPing(msg);
				}
				break; // GameClientExtendedPing
			case 0x6F:
			case 0x71:
				spectatorTurn(recvbyte - 0x6F);
				break;
			case 0x70: // Turn East - used for Next Cast (CTRL + RIGHT)
				if (canProcessCastSwitch()) {
					parseSwitchCast(uint8_t(1));
				}
				break;
			case 0x72: // Turn West - used for Prev Cast (CTRL + LEFT)
				if (canProcessCastSwitch()) {
					parseSwitchCast(uint8_t(0));
				}
				break;
			case 0x8C: parseLookAt(msg); break; // Look at tile/item
			case 0x96: parseSpectatorSay(msg); break;
			case 0x97:
				sendCastChannel();
				break;
			default:
				sendCancelWalk();
				break;
		}
		return;
	}

	auto handlePlayerNetworkMessage = [&](uint8_t byte) {
		g_game.parsePlayerNetworkMessage(player->getID(), byte, packet);
	};

	switch (recvbyte) {
		case 0x14:
			logout(true, false);
			break;
		case 0x1E:
			if (clientOperatingSystem == CLIENTOS_CUSTOM_DLL) {
				parseCustomClientPing(msg);
			} else {
				g_game.playerReceivePing(player->getID());
			}
			break;
		case 0x32:
			if (isOTC) {
				parseExtendedOpcode(msg);
			}
			break; // otclient extended opcode
		case 0x40:
			if (isOTC) {
				parseNewPing(msg);
			}
			break; // GameClientExtendedPing
		case 0x60:
			parseImbuementDurations(msg);
			break;
		case CharacterBazaar::CLIENT_PACKET:
			if (isAstraClient) {
				parseCharacterBazaar(msg);
			} else {
				skipUnreadBytes(msg);
			}
			break;
		case 0x64:
			parseAutoWalk(msg);
			break;
		case 0x65:
			g_game.playerMove(player->getID(), DIRECTION_NORTH);
			break;
		case 0x66:
			g_game.playerMove(player->getID(), DIRECTION_EAST);
			break;
		case 0x67:
			g_game.playerMove(player->getID(), DIRECTION_SOUTH);
			break;
		case 0x68:
			g_game.playerMove(player->getID(), DIRECTION_WEST);
			break;
		case 0x69:
			g_game.playerStopAutoWalk(player->getID());
			break;
		case 0x6A:
			g_game.playerMove(player->getID(), DIRECTION_NORTHEAST);
			break;
		case 0x6B:
			g_game.playerMove(player->getID(), DIRECTION_SOUTHEAST);
			break;
		case 0x6C:
			g_game.playerMove(player->getID(), DIRECTION_SOUTHWEST);
			break;
		case 0x6D:
			g_game.playerMove(player->getID(), DIRECTION_NORTHWEST);
			break;
		case 0x6F:
			g_game.playerTurn(player->getID(), DIRECTION_NORTH);
			break;
		case 0x70:
			g_game.playerTurn(player->getID(), DIRECTION_EAST);
			break;
		case 0x71:
			g_game.playerTurn(player->getID(), DIRECTION_SOUTH);
			break;
		case 0x72:
			g_game.playerTurn(player->getID(), DIRECTION_WEST);
			break;
		case 0x77:
			if (isAstraClient) {
				parseHotkeyEquip(msg);
			} else {
				skipUnreadBytes(msg);
			}
			break;
		case 0x78:
			parseThrow(msg);
			break;
		case 0x79:
			parseLookInShop(msg);
			break;
		case 0x7A:
			parsePlayerPurchase(msg);
			break;
		case 0x7B:
			parsePlayerSale(msg);
			break;
		case 0x7C:
			g_game.playerCloseShop(player->getID());
			break;
		case 0x7D:
			parseRequestTrade(msg);
			break;
		case 0x7E:
			parseLookInTrade(msg);
			break;
		case 0x7F:
			g_game.playerAcceptTrade(player->getID());
			break;
		case 0x80:
			g_game.playerCloseTrade(player->getID());
			break;
		case 0x82:
			parseUseItem(msg);
			break;
		case 0x83:
			parseUseItemEx(msg);
			break;
		case 0x84:
			parseUseWithCreature(msg);
			break;
		case 0x85:
			parseRotateItem(msg);
			break;
		case 0x87:
			parseCloseContainer(msg);
			break;
		case 0x88:
			parseUpArrowContainer(msg);
			break;
		case 0x89:
			parseTextWindow(msg);
			break;
		case 0x8A:
			parseHouseWindow(msg);
			break;
		case 0x8B:
			parseWrapableItem(msg);
			break;
		case 0x8C:
			parseLookAt(msg);
			break;

		case 0x8D:
			parseLookInBattleList(msg);
			break;

		case 0x8E: /* join aggression */
			break;

		case 0x8F:
			if (shouldSendQuickLootFlags()) {
				parseQuickLoot(msg);
			} else {
				skipUnreadBytes(msg);
			}
			break;

		case 0x90:
			if (shouldSendQuickLootFlags()) {
				parseLootContainer(msg);
			} else {
				skipUnreadBytes(msg);
			}
			break;

		case 0x91:
			if (shouldSendQuickLootFlags()) {
				parseQuickLootBlackWhitelist(msg);
			} else {
				skipUnreadBytes(msg);
			}
			break;

		case 0x96:
			parseSay(msg);
			break;

		case 0x97:
			g_game.playerRequestChannels(player->getID());
			break;

		case 0x98:
			parseOpenChannel(msg);
			break;

		case 0x99:
			parseCloseChannel(msg);
			break;

		case 0x9A:
			parseOpenPrivateChannel(msg);
			break;

		case 0x9E:
			g_game.playerCloseNpcChannel(player->getID());
			break;

		case 0x9F:
			if (isAstraClient) {
				parseSetMonsterPodium(msg);
			}
			break;

		case 0xA1:
			parseAttack(msg);
			break;

		case 0xA2:
			parseFollow(msg);
			break;

		case 0xA3:
			parseInviteToParty(msg);
			break;

		case 0xA4:
			parseJoinParty(msg);
			break;

		case 0xA5:
			parseRevokePartyInvite(msg);
			break;

		case 0xA6:
			parsePassPartyLeadership(msg);
			break;

		case 0xA7:
			g_game.playerLeaveParty(player->getID());
			break;

		case 0xA8:
			parseEnableSharedPartyExperience(msg);
			break;

		case 0xAA:
			g_game.playerCreatePrivateChannel(player->getID());
			break;

		case 0xAB:
			parseChannelInvite(msg);
			break;

		case 0xAC:
			parseChannelExclude(msg);
			break;

		case 0xBE:
			g_game.playerCancelAttackAndFollow(player->getID());
			break;

		case 0xCF:
			if (isAstraClient) {
				sendBlessingWindow();
			}
			break;

		case 0xC9: /* update tile */
			break;

		case 0xCA:
			parseUpdateContainer(msg);
			break;

		case 0xCB:
			if (shouldSendContainerPagination()) {
				parseBrowseField(msg);
			} else {
				skipUnreadBytes(msg);
			}
			break;

		case 0xCC:
			if (shouldSendContainerPagination()) {
				parseSeekInContainer(msg);
			} else {
				skipUnreadBytes(msg);
			}
			break;

		case 0xCD:
			if (isAstraClient) {
				parseInspectionObject(msg);
			}
			break;

		case 0xD2:
			if (isHirelingOutfitRequestPacket(msg, isAstraClient)) {
				handlePlayerNetworkMessage(recvbyte);
			} else {
				g_game.playerRequestOutfit(player->getID());
			}
			break;

		case 0xD3:
			if (isHirelingOutfitChangePacket(msg, isAstraClient)) {
				handlePlayerNetworkMessage(recvbyte);
			} else {
				parseSetOutfit(msg);
			}
			break;

		case 0xDC:
			parseAddVip(msg);
			break;

		case 0xDD:
			parseRemoveVip(msg);
			break;

		case 0xE6:
			parseBugReport(msg);
			break;

		case 0xE7: /* thank you / custom wheel gem action */
			handlePlayerNetworkMessage(recvbyte);
			break;

		case 0xF2:
			parseRuleViolationReport(msg);
			break;

		case 0xF3: /* get object info */
			break;

		case 0xF8: /* custom store transfer */
		case 0xFA: /* custom store history */
		case 0xFB: /* custom store open */
		case 0xFC: /* custom store buy */
			handlePlayerNetworkMessage(recvbyte);
			break;

		case 0xF9:
			parseModalWindowAnswer(msg);
			break;

		default:
			handlePlayerNetworkMessage(recvbyte);
			break;
	}

	if (msg.isOverrun()) {
		disconnect();
	}
}

void ProtocolGame::parseCharacterBazaar(NetworkMessage& msg)
{
	if (!player || !isAstraClient || !ConfigManager::getBoolean(ConfigManager::CHARACTER_BAZAAR_ENABLED) ||
	    !requireUnreadBytes(msg, 1)) {
		skipUnreadBytes(msg);
		return;
	}

	const uint8_t action = msg.getByte();
	if (action == CharacterBazaar::ACTION_REQUEST_REQUIREMENTS) {
		if (getUnreadBytes(msg) != 0) {
			skipUnreadBytes(msg);
			return;
		}
		CharacterBazaar::sendRequirements(player.get());
		return;
	}

	if (action != CharacterBazaar::ACTION_CREATE_AUCTION ||
	    !requireUnreadBytes(msg, sizeof(uint32_t) * 2 + sizeof(uint16_t))) {
		skipUnreadBytes(msg);
		return;
	}

	const uint32_t startPrice = msg.get<uint32_t>();
	const uint32_t duration = msg.get<uint32_t>();
	const std::string description = msg.getString();
	if (msg.isOverrun() || getUnreadBytes(msg) != 0 || description.size() > 512) {
		skipUnreadBytes(msg);
		return;
	}

	std::string result;
	const bool success = CharacterBazaar::createAuction(player.get(), startPrice, duration, description, result);
	CharacterBazaar::sendCreateResult(player.get(), success, result);
}

void ProtocolGame::GetTileDescription(const Tile* tile, NetworkMessage& msg)
{
	const uint32_t playerInstanceId = player->getInstanceID();
	const bool sendQuickLootFlags = shouldSendQuickLootFlags();
	const bool sendItemTierByte = shouldSendItemTierByte();
	const bool sendItemTierData = shouldSendItemTierData();
	const bool sendAstraItemState = canSendAstraItemState();
	const bool sendAstraQuiverCountU16 = shouldSendAstraQuiverCountU16();
	int32_t count;
	Item* ground = tile->getGround();
	if (ground) {
		msg.addItem(ground, sendItemTierData, sendItemTierByte, isOTC, sendQuickLootFlags, sendAstraItemState,
		            sendAstraQuiverCountU16);
		count = 1;
	} else {
		count = 0;
	}

	const TileItemVector* items = tile->getItemList();
	if (items) {
		for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
			if (!InstanceUtils::canSeeItemInInstance(playerInstanceId, it->get())) {
				continue;
			}
			msg.addItem(it->get(), sendItemTierData, sendItemTierByte, isOTC, sendQuickLootFlags, sendAstraItemState,
			            sendAstraQuiverCountU16);
			count++;
			if (count == 9 && tile->getPosition() == player->getPosition()) {
				break;
			} else if (count == 10) {
				return;
			}
		}
	}

	const bool isStacked = player->getPosition() == tile->getPosition();

	const CreatureVector* creatures = tile->getCreatures();
	if (creatures) {
		bool playerAdded = false;
		for (auto it = creatures->rbegin(), end = creatures->rend(); it != end; ++it) {
			const Creature* creature = it->get();

			if (!player->canSeeCreature(creature)) {
				continue;
			}

			if (!isOTC && isStacked && count == 9 && !playerAdded) {
				creature = player.get();
			}

			if (creature->getID() == player->getID()) {
				playerAdded = true;
			}

			auto [known, removedKnown] = isKnownCreature(creature->getID());
			AddCreature(msg, creature, known, removedKnown);

			if (++count == MAX_STACKPOS_THINGS) {
				if (!isOTC) return;
				break;
			}
		}
	}

	if (items && count < MAX_STACKPOS_THINGS) {
		for (auto it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end; ++it) {
			if (!InstanceUtils::canSeeItemInInstance(playerInstanceId, it->get())) {
				continue;
			}
			msg.addItem(it->get(), sendItemTierData, sendItemTierByte, isOTC, sendQuickLootFlags, sendAstraItemState,
			            sendAstraQuiverCountU16);
			if (++count == MAX_STACKPOS_THINGS) {
				return;
			}
		}
	}
}

void ProtocolGame::GetMapDescription(int32_t x, int32_t y, int32_t z, int32_t width, int32_t height,
                                     NetworkMessage& msg)
{
	int32_t skip = -1;
	int32_t startz, endz, zstep;

	if (z > 7) {
		startz = z - 2;
		endz = std::min<int32_t>(MAP_MAX_LAYERS - 1, z + 2);
		zstep = 1;
	} else {
		startz = 7;
		endz = 0;
		zstep = -1;
	}

	for (int32_t nz = startz; nz != endz + zstep; nz += zstep) {
		GetFloorDescription(msg, x, y, nz, width, height, z - nz, skip);
	}

	if (skip >= 0) {
		msg.addByte(static_cast<uint8_t>(skip));
		msg.addByte(0xFF);
	}
}

void ProtocolGame::GetFloorDescription(NetworkMessage& msg, int32_t x, int32_t y, int32_t z, int32_t width,
                                       int32_t height, int32_t offset, int32_t& skip)
{
	for (int32_t nx = 0; nx < width; nx++) {
		for (int32_t ny = 0; ny < height; ny++) {
			Tile* tile = g_game.map.getTile(static_cast<uint16_t>(x + nx + offset),
			                                static_cast<uint16_t>(y + ny + offset), static_cast<uint8_t>(z));
			if (tile) {
				if (skip >= 0) {
					msg.addByte(static_cast<uint8_t>(skip));
					msg.addByte(0xFF);
				}

				skip = 0;
				GetTileDescription(tile, msg);
			} else if (skip == 0xFE) {
				msg.addByte(0xFF);
				msg.addByte(0xFF);
				skip = -1;
			} else {
				++skip;
			}
		}
	}
}

std::pair<bool, uint32_t> ProtocolGame::isKnownCreature(uint32_t id)
{
	auto result = knownCreatureSet.insert(id);
	if (!result.second) {
		return {true, 0};
	}

	if (knownCreatureSet.size() > 250) {
		auto unseenIt = std::find_if(knownCreatureSet.begin(), knownCreatureSet.end(), [this](uint32_t creatureId) {
			auto creatureRef = g_game.getCreatureByIDShared(creatureId);
			Creature* creature = creatureRef.get();
			return !canSee(creature);
		});
		if (unseenIt != knownCreatureSet.end()) {
			uint32_t removedCreatureId = *unseenIt;
			knownCreatureSet.erase(unseenIt);
			return {false, removedCreatureId};
		}

		auto it = knownCreatureSet.begin();
		if (*it == id) {
			++it;
		}

		uint32_t removedId = *it;
		knownCreatureSet.erase(it);
		return {false, removedId};
	}
	return {false, 0};
}

bool ProtocolGame::canSee(const Creature* c) const
{
	if (!c || !player || c->isRemoved()) {
		return false;
	}

	if (!player->canSeeCreature(c)) {
		return false;
	}

	// Spy mode: bypass instance check (GOD sees target's instance)
	if (!spyActive_ && c != player.get() && !player->compareInstance(c->getInstanceID())) {
		return false;
	}

	return canSee(c->getPosition());
}

bool ProtocolGame::canSee(const Position& pos) const { return canSee(pos.x, pos.y, pos.z); }

bool ProtocolGame::canSee(int32_t x, int32_t y, int32_t z) const
{
	if (!player) {
		return false;
	}

	const Position& myPos = spyActive_ ? spyViewportPos_ : player->getPosition();
	if (myPos.z <= 7) {
		// we are on ground level or above (7 -> 0)
		// view is from 7 -> 0
		if (z > 7) {
			return false;
		}
	} else { // if (myPos.z >= 8) {
		// we are underground (8 -> 15)
		// view is +/- 2 from the floor we stand on
		if (std::abs(myPos.getZ() - z) > 2) {
			return false;
		}
	}

	// negative offset means that the action taken place is on a lower floor than ourself
	int32_t offsetz = myPos.getZ() - z;
	if ((x >= myPos.getX() - Map::maxClientViewportX + offsetz) &&
	    (x <= myPos.getX() + (Map::maxClientViewportX + 1) + offsetz) &&
	    (y >= myPos.getY() - Map::maxClientViewportY + offsetz) &&
	    (y <= myPos.getY() + (Map::maxClientViewportY + 1) + offsetz)) {
		return true;
	}
	return false;
}

// Parse methods
void ProtocolGame::parseChannelInvite(NetworkMessage& msg)
{
	auto name = msg.getString();
	g_game.playerChannelInvite(player->getID(), name);
}

void ProtocolGame::parseChannelExclude(NetworkMessage& msg)
{
	auto name = msg.getString();
	g_game.playerChannelExclude(player->getID(), name);
}

void ProtocolGame::parseOpenChannel(NetworkMessage& msg)
{
	uint16_t channelId = msg.get<uint16_t>();
	g_game.playerOpenChannel(player->getID(), channelId);
}

void ProtocolGame::parseCloseChannel(NetworkMessage& msg)
{
	uint16_t channelId = msg.get<uint16_t>();
	g_game.playerCloseChannel(player->getID(), channelId);
}

void ProtocolGame::parseOpenPrivateChannel(NetworkMessage& msg)
{
	auto receiver = msg.getString();
	g_game.playerOpenPrivateChannel(player->getID(), std::move(receiver));
}

void ProtocolGame::parseAutoWalk(NetworkMessage& msg)
{
	uint8_t numdirs = msg.getByte();
	if (numdirs == 0 || (msg.getBufferPosition() + numdirs) != (msg.getLength() + 8)) {
		return;
	}

	msg.skipBytes(numdirs);

	std::vector<Direction> path;
	path.reserve(numdirs);

	for (uint8_t i = 0; i < numdirs; ++i) {
		uint8_t rawdir = msg.getPreviousByte();
		switch (rawdir) {
			case 1:
				path.push_back(DIRECTION_EAST);
				break;
			case 2:
				path.push_back(DIRECTION_NORTHEAST);
				break;
			case 3:
				path.push_back(DIRECTION_NORTH);
				break;
			case 4:
				path.push_back(DIRECTION_NORTHWEST);
				break;
			case 5:
				path.push_back(DIRECTION_WEST);
				break;
			case 6:
				path.push_back(DIRECTION_SOUTHWEST);
				break;
			case 7:
				path.push_back(DIRECTION_SOUTH);
				break;
			case 8:
				path.push_back(DIRECTION_SOUTHEAST);
				break;
			default:
				break;
		}
	}

	if (path.empty()) {
		return;
	}

	g_game.playerAutoWalk(player->getID(), path);
}

void ProtocolGame::parseSetOutfit(NetworkMessage& msg)
{
	if (player->isAccountManager()) {
		dispatchCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Outfit_t newOutfit;
	newOutfit.lookType = msg.get<uint16_t>();
	newOutfit.lookHead = msg.getByte();
	newOutfit.lookBody = msg.getByte();
	newOutfit.lookLegs = msg.getByte();
	newOutfit.lookFeet = msg.getByte();
	newOutfit.lookAddons = msg.getByte();
	if (isOTC || getVersion() != 861) {
		newOutfit.lookMount = msg.get<uint16_t>();
		if (newOutfit.lookMount != 0 && !player->isMounted()) {
			const Mount* mount = g_game.mounts.getMountByClientID(newOutfit.lookMount);
			if (mount && mount->id == player->getCurrentMount()) {
				newOutfit.lookMount = 0;
			}
		}
	} else {
		newOutfit.lookMount = 0;
	}
	if (isAstraClient) {
		const uint16_t requestedFamiliar = msg.get<uint16_t>();
		const auto familiar = Familiar::getFamiliarInfo(player.get());
		newOutfit.lookFamiliar =
		    familiar && requestedFamiliar == familiar->lookType ? requestedFamiliar : 0;
	}
	g_game.playerChangeOutfit(player->getID(), newOutfit);
}

void ProtocolGame::parseInspectionObject(NetworkMessage& msg)
{
	if (!requireUnreadBytes(msg, 1)) {
		return;
	}

	const uint8_t inspectionType = msg.getByte();
	if (inspectionType == INSPECT_NORMALOBJECT) {
		if (!requireUnreadBytes(msg, 5)) {
			return;
		}

		const Position position = msg.getPosition();
		g_game.playerInspectItem(player->getID(), position);
		return;
	}

	if (inspectionType != INSPECT_NPCTRADE && inspectionType != INSPECT_CYCLOPEDIA &&
	    inspectionType != INSPECT_PROFICIENCY) {
		return;
	}

	if (!requireUnreadBytes(msg, 3)) {
		return;
	}

	const uint16_t itemId = msg.get<uint16_t>();
	const uint8_t itemCount = msg.getByte();
	g_game.playerInspectItem(player->getID(), itemId, itemCount, inspectionType);
}

void ProtocolGame::parseSetMonsterPodium(NetworkMessage& msg)
{
	if (!requireUnreadBytes(msg, 15)) {
		return;
	}

	const uint32_t raceId = msg.get<uint32_t>();
	const Position position = msg.getPosition();
	const uint16_t itemId = msg.get<uint16_t>();
	const uint8_t stackPos = msg.getByte();
	const uint8_t direction = msg.getByte();
	const bool podiumVisible = msg.getByte() != 0;
	const bool creatureVisible = msg.getByte() != 0;

	g_game.playerSetMonsterPodium(player->getID(), raceId, position, stackPos, itemId, direction, podiumVisible,
	                              creatureVisible);
}

void ProtocolGame::parseUseItem(NetworkMessage& msg)
{
	if (player->isAccountManager()) {
		dispatchCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Position pos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	uint8_t index = msg.getByte();
	g_game.playerUseItem(player->getID(), pos, stackpos, index, spriteId);
}

void ProtocolGame::parseBrowseField(NetworkMessage& msg)
{
	if (player->isAccountManager() || !requireUnreadBytes(msg, 5)) {
		return;
	}

	const Position pos = msg.getPosition();
	g_game.playerBrowseField(player->getID(), pos);
}

void ProtocolGame::parseSeekInContainer(NetworkMessage& msg)
{
	if (player->isAccountManager() || !requireUnreadBytes(msg, 3)) {
		return;
	}

	const uint8_t containerId = msg.getByte();
	const uint16_t index = msg.get<uint16_t>();
	g_game.playerSeekInContainer(player->getID(), containerId, index);
}

void ProtocolGame::parseHotkeyEquip(NetworkMessage& msg)
{
	const std::size_t packetSize = getUnreadBytes(msg);
	if (!player || !isAstraClient || isSpectator || player->isAccountManager()) {
		skipUnreadBytes(msg);
		return;
	}

	if (packetSize != 2 && packetSize != 3) {
		skipUnreadBytes(msg);
		return;
	}

	uint16_t itemId = msg.get<uint16_t>();
	if (itemId == 0 || itemId >= Item::items.size() || Item::items[itemId].id == 0) {
		skipUnreadBytes(msg);
		return;
	}

	uint8_t tier = 0;
	bool hasTier = getBoolean(ConfigManager::ITEM_TIER_DISPLAY) &&
	               (useItemTierByte || (getBoolean(ConfigManager::ITEM_UPGRADE_CLASSIFICATION) &&
	                                   Item::items[itemId].classification > 0));
	if (packetSize != (hasTier ? 3 : 2)) {
		skipUnreadBytes(msg);
		return;
	}

	if (hasTier) {
		tier = msg.getByte();
	}

	g_game.playerEquipItem(player->getID(), itemId, hasTier, tier);
}

void ProtocolGame::parseUseItemEx(NetworkMessage& msg)
{
	if (player->isAccountManager()) {
		dispatchCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Position fromPos = msg.getPosition();
	uint16_t fromSpriteId = msg.get<uint16_t>();
	uint8_t fromStackPos = msg.getByte();
	Position toPos = msg.getPosition();
	uint16_t toSpriteId = msg.get<uint16_t>();
	uint8_t toStackPos = msg.getByte();
	g_game.playerUseItemEx(player->getID(), fromPos, fromStackPos, fromSpriteId, toPos, toStackPos, toSpriteId);
}

void ProtocolGame::parseUseWithCreature(NetworkMessage& msg)
{
	if (player->isAccountManager()) {
		dispatchCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Position fromPos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t fromStackPos = msg.getByte();
	uint32_t creatureId = msg.get<uint32_t>();
	g_game.playerUseWithCreature(player->getID(), fromPos, fromStackPos, creatureId, spriteId);
}

void ProtocolGame::parseCloseContainer(NetworkMessage& msg)
{
	uint8_t cid = msg.getByte();
	g_game.playerCloseContainer(player->getID(), cid);
}

void ProtocolGame::parseUpArrowContainer(NetworkMessage& msg)
{
	uint8_t cid = msg.getByte();
	g_game.playerMoveUpContainer(player->getID(), cid);
}

void ProtocolGame::parseUpdateContainer(NetworkMessage& msg)
{
	uint8_t cid = msg.getByte();
	g_game.playerUpdateContainer(player->getID(), cid);
}

void ProtocolGame::parseQuickLoot(NetworkMessage& msg)
{
	if (!player || !shouldSendQuickLootFlags()) {
		skipUnreadBytes(msg);
		return;
	}

	if (!requireUnreadBytes(msg, 6)) {
		return;
	}

	const uint8_t variant = msg.getByte();
	Position pos = msg.getPosition();

	if (variant == 2) {
		g_game.playerLootNearby(player->getID());
		skipUnreadBytes(msg);
		return;
	}

	if (!requireUnreadBytes(msg, 3)) {
		return;
	}

	uint16_t itemId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	const bool lootAllCorpses = variant == 1;
	g_game.playerQuickLoot(player->getID(), pos, itemId, stackpos, lootAllCorpses);
}

void ProtocolGame::parseLootContainer(NetworkMessage& msg)
{
	if (!player || !shouldSendQuickLootFlags()) {
		skipUnreadBytes(msg);
		return;
	}

	if (!requireUnreadBytes(msg, 1)) {
		return;
	}

	uint8_t action = msg.getByte();
	switch (action) {
		case 0:
		case 4: {
			if (!requireUnreadBytes(msg, 9)) {
				return;
			}

			auto category = static_cast<ObjectCategory_t>(msg.getByte());
			Position pos = msg.getPosition();
			uint16_t itemId = msg.get<uint16_t>();
			uint8_t stackpos = msg.getByte();
			const bool isLootContainer = action == 0;
			g_game.playerSetManagedLootContainer(player->getID(), category, pos, itemId, stackpos, isLootContainer);
			break;
		}
		case 1:
		case 5: {
			if (!requireUnreadBytes(msg, 1)) {
				return;
			}

			auto category = static_cast<ObjectCategory_t>(msg.getByte());
			const bool isLootContainer = action == 1;
			g_game.playerClearManagedLootContainer(player->getID(), category, isLootContainer);
			break;
		}
		case 2:
		case 6: {
			if (!requireUnreadBytes(msg, 1)) {
				return;
			}

			auto category = static_cast<ObjectCategory_t>(msg.getByte());
			const bool isLootContainer = action == 2;
			g_game.playerOpenManagedLootContainer(player->getID(), category, isLootContainer);
			break;
		}
		case 3: {
			if (!requireUnreadBytes(msg, 1)) {
				return;
			}

			bool useMainAsFallback = msg.getByte() == 1;
			g_game.playerSetQuickLootFallback(player->getID(), useMainAsFallback);
			break;
		}
		default:
			skipUnreadBytes(msg);
			break;
	}
}

void ProtocolGame::parseQuickLootBlackWhitelist(NetworkMessage& msg)
{
	if (!player || !shouldSendQuickLootFlags()) {
		skipUnreadBytes(msg);
		return;
	}

	if (!requireUnreadBytes(msg, 3)) {
		return;
	}

	const uint8_t filterByte = msg.getByte();
	if (filterByte != QUICKLOOTFILTER_SKIPPEDLOOT && filterByte != QUICKLOOTFILTER_ACCEPTEDLOOT) {
		skipUnreadBytes(msg);
		return;
	}

	auto filter = static_cast<QuickLootFilter_t>(filterByte);
	const uint16_t size = msg.get<uint16_t>();
	if (size > 4096 || getUnreadBytes(msg) < static_cast<std::size_t>(size) * sizeof(uint16_t)) {
		skipUnreadBytes(msg);
		return;
	}

	std::vector<uint16_t> listedItems;
	listedItems.reserve(size);

	for (uint16_t i = 0; i < size; ++i) {
		listedItems.push_back(msg.get<uint16_t>());
	}

	g_game.playerQuickLootBlackWhitelist(player->getID(), filter, std::move(listedItems));
}

void ProtocolGame::parseThrow(NetworkMessage& msg)
{
	if (player->isAccountManager()) {
		dispatchCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Position fromPos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t fromStackpos = msg.getByte();
	Position toPos = msg.getPosition();
	uint8_t count = msg.getByte();

	if (toPos != fromPos) {
		g_game.playerMoveThing(player->getID(), fromPos, spriteId, fromStackpos, toPos, count);
	}
}

void ProtocolGame::parseLookAt(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	msg.skipBytes(2); // spriteId
	uint8_t stackpos = msg.getByte();

	if (!player) {
		return;
	}

	if (isSpectator && pos.x != 0xFFFF && !canSee(pos)) {
		return;
	}

	g_game.playerLookAt(player->getID(), pos, stackpos);
}

void ProtocolGame::parseLookInBattleList(NetworkMessage& msg)
{
	uint32_t creatureId = msg.get<uint32_t>();
	g_game.playerLookInBattleList(player->getID(), creatureId);
}

void ProtocolGame::parseSay(NetworkMessage& msg)
{
	std::string receiver;
	uint16_t channelId;

	SpeakClasses type = static_cast<SpeakClasses>(msg.getByte());
	switch (type) {
		case TALKTYPE_PRIVATE:
		case TALKTYPE_PRIVATE_RED:
			receiver = msg.getString();
			channelId = 0;
			break;

		case TALKTYPE_CHANNEL_Y:
		case TALKTYPE_CHANNEL_R1:
		case TALKTYPE_CHANNEL_R2:
			channelId = msg.get<uint16_t>();
			break;

		default:
			channelId = 0;
			break;
	}

	auto text = msg.getString();
	const bool forceCastOnFoot = consumeHelperCastOnFoot();
	if (text.length() > 255) {
		return;
	}

	if (player->isAccountManager()) {
		player->manageAccount(text);
		return;
	}

	g_game.playerSay(player->getID(), channelId, type, receiver, text, forceCastOnFoot);
}

void ProtocolGame::parseAttack(NetworkMessage& msg)
{
	if (player->isAccountManager()) {
		dispatchCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	uint32_t creatureId = msg.get<uint32_t>();
	if (isMehah) {
		msg.get<uint32_t>(); // attack sequence
	}
	g_game.playerSetAttackedCreature(player->getID(), creatureId);
}

void ProtocolGame::parseFollow(NetworkMessage& msg)
{
	uint32_t creatureId = msg.get<uint32_t>();
	if (isMehah) {
		msg.get<uint32_t>(); // follow sequence
	}
	g_game.playerFollowCreature(player->getID(), creatureId);
}

void ProtocolGame::parseTextWindow(NetworkMessage& msg)
{
	uint32_t windowTextID = msg.get<uint32_t>();
	auto newText = msg.getString();
	g_game.playerWriteItem(player->getID(), windowTextID, newText);
}

void ProtocolGame::parseHouseWindow(NetworkMessage& msg)
{
	uint8_t doorId = msg.getByte();
	uint32_t id = msg.get<uint32_t>();
	auto text = msg.getString();
	g_game.playerUpdateHouseWindow(player->getID(), doorId, id, text);
}

void ProtocolGame::parseLookInShop(NetworkMessage& msg)
{
	uint16_t id = msg.get<uint16_t>();
	uint8_t count = msg.getByte();
	g_game.playerLookInShop(player->getID(), id, count);
}

void ProtocolGame::parsePlayerPurchase(NetworkMessage& msg)
{
	uint16_t id = msg.get<uint16_t>();
	uint8_t count = msg.getByte();
	uint8_t amount = msg.getByte();
	bool ignoreCap = msg.getByte() != 0;
	bool inBackpacks = msg.getByte() != 0;
	g_game.playerPurchaseItem(player->getID(), id, count, amount, ignoreCap, inBackpacks);
}

void ProtocolGame::parsePlayerSale(NetworkMessage& msg)
{
	uint16_t id = msg.get<uint16_t>();
	uint8_t count = msg.getByte();
	uint8_t amount = msg.getByte();
	bool ignoreEquipped = msg.getByte() != 0;
	g_game.playerSellItem(player->getID(), id, count, amount, ignoreEquipped);
}

void ProtocolGame::parseRequestTrade(NetworkMessage& msg)
{
	if (player->isAccountManager()) {
		dispatchCancelMessage(RETURNVALUE_NOTPOSSIBLE);
		return;
	}

	Position pos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	uint32_t playerId = msg.get<uint32_t>();
	g_game.playerRequestTrade(player->getID(), pos, stackpos, playerId, spriteId);
}

void ProtocolGame::parseLookInTrade(NetworkMessage& msg)
{
	bool counterOffer = (msg.getByte() == 0x01);
	uint8_t index = msg.getByte();
	g_game.playerLookInTrade(player->getID(), counterOffer, index);
}

void ProtocolGame::parseAddVip(NetworkMessage& msg)
{
	auto name = msg.getString();
	g_game.playerRequestAddVip(player->getID(), name);
}

void ProtocolGame::parseRemoveVip(NetworkMessage& msg)
{
	uint32_t guid = msg.get<uint32_t>();
	g_game.playerRequestRemoveVip(player->getID(), guid);
}

void ProtocolGame::parseRotateItem(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	g_game.playerRotateItem(player->getID(), pos, stackpos, spriteId);
}

void ProtocolGame::parseWrapableItem(NetworkMessage& msg)
{
	if (!isOTC && !isOTCv8 && !isAstraClient) {
		skipUnreadBytes(msg);
		return;
	}

	Position pos = msg.getPosition();
	uint16_t spriteId = msg.get<uint16_t>();
	uint8_t stackpos = msg.getByte();
	g_game.playerWrapableItem(player->getID(), pos, stackpos, spriteId);
}

void ProtocolGame::parseRuleViolationReport(NetworkMessage& msg)
{
	uint8_t reportType;
	uint8_t reportReason;
	std::string targetName;
	std::string comment;
	std::string translation;

	if (looksLikeLegacyRuleViolationReport(msg.getRemainingBuffer(), msg.getRemainingBufferLength())) {
		targetName = msg.getString();
		reportReason = msg.getByte();
		reportType = getRuleViolationTypeFromLegacyAction(msg.getByte());
		comment = msg.getString();
		translation = msg.getString();
		if (msg.getRemainingBufferLength() >= sizeof(uint16_t)) {
			msg.get<uint16_t>(); // legacy statement id
		}
		if (msg.getRemainingBufferLength() >= 1) {
			msg.getByte(); // legacy IP banishment flag
		}
	} else {
		reportType = msg.getByte();
		reportReason = msg.getByte();
		targetName = msg.getString();
		comment = msg.getString();
		if (reportType == REPORT_TYPE_NAME) {
			translation = msg.getString();
		} else if (reportType == REPORT_TYPE_STATEMENT) {
			translation = msg.getString();
			msg.get<uint32_t>(); // statement id, used to get whatever player have said, we don't log that.
		}
	}

	g_game.playerReportRuleViolation(player->getID(), targetName, reportType, reportReason, comment, translation);
}

void ProtocolGame::parseBugReport(NetworkMessage& msg)
{
	auto message = msg.getString();
	g_game.playerReportBug(player->getID(), message);
}

void ProtocolGame::parseInviteToParty(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	g_game.playerInviteToParty(player->getID(), targetId);
}

void ProtocolGame::parseJoinParty(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	g_game.playerJoinParty(player->getID(), targetId);
}

void ProtocolGame::parseRevokePartyInvite(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	g_game.playerRevokePartyInvitation(player->getID(), targetId);
}

void ProtocolGame::parsePassPartyLeadership(NetworkMessage& msg)
{
	uint32_t targetId = msg.get<uint32_t>();
	g_game.playerPassPartyLeadership(player->getID(), targetId);
}

void ProtocolGame::parseEnableSharedPartyExperience(NetworkMessage& msg)
{
	bool sharedExpActive = msg.getByte() == 1;
	g_game.playerEnableSharedPartyExperience(player->getID(), sharedExpActive);
}

void ProtocolGame::parseModalWindowAnswer(NetworkMessage& msg)
{
	if (!isOTC) {
		return;
	}

	uint32_t id = msg.get<uint32_t>();
	uint8_t button = msg.getByte();
	uint8_t choice = msg.getByte();
	g_game.playerAnswerModalWindow(player->getID(), id, button, choice);
}

// Send methods
void ProtocolGame::sendOpenPrivateChannel(std::string_view receiver)
{
	NetworkMessage msg;
	msg.addByte(0xAD);
	msg.addString(receiver);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureOutfit(const Creature* creature, const Outfit_t& outfit)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x8E);
	msg.add<uint32_t>(creature->getID());
	AddOutfit(msg, outfit);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureEmblem(const Creature* creature)
{
	if (!canSee(creature)) {
		return;
	}

	if (isAstraClient) {
		if (const Monster* monster = creature->getMonster(); monster && monster->isFamiliar()) {
			return;
		}
	}

	// Remove creature from client and re-add to update
	Position pos = creature->getPosition();
	int32_t stackpos = creature->getTile()->getClientIndexOfCreature(player.get(), creature);
	sendRemoveTileThing(pos, stackpos);
	NetworkMessage msg;
	msg.addByte(0x6A);
	msg.addPosition(pos);
	msg.addByte(stackpos);
	AddCreature(msg, creature, false, creature->getID());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureLight(const Creature* creature)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	AddCreatureLight(msg, creature);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendWorldLight(LightInfo lightInfo)
{
	NetworkMessage msg;
	AddWorldLight(msg, lightInfo);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCharmActivated(uint8_t charmId)
{
	if (!isAstraClient) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x2D);
	msg.addByte(charmId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendKillTrackerUpdate(const std::shared_ptr<Container>& corpse, std::string_view monsterName,
                                         const Outfit_t& monsterOutfit)
{
	if (!corpse || (!isAstraClient && !isOTC && !isOTCv8 && !isMehah)) {
		return;
	}

	constexpr size_t MAX_ITEMS_PER_CONTAINER = 255;
	constexpr uint8_t MAX_CONTAINER_DEPTH = 4;

	NetworkMessage msg;
	msg.addByte(0xD1);
	msg.addString(monsterName);
	const bool hasCreatureOutfit = monsterOutfit.lookType != 0;
	msg.add<uint16_t>(hasCreatureOutfit ? monsterOutfit.lookType : 21);
	msg.addByte(hasCreatureOutfit ? monsterOutfit.lookHead : 0);
	msg.addByte(hasCreatureOutfit ? monsterOutfit.lookBody : 0);
	msg.addByte(hasCreatureOutfit ? monsterOutfit.lookLegs : 0);
	msg.addByte(hasCreatureOutfit ? monsterOutfit.lookFeet : 0);
	msg.addByte(hasCreatureOutfit ? monsterOutfit.lookAddons : 0);

	const auto addContainerItems = [&](const auto& self, const std::shared_ptr<Container>& container,
	                                   uint8_t depth) -> void {
		if (!container || depth > MAX_CONTAINER_DEPTH) {
			msg.addByte(0);
			return;
		}

		const auto& items = container->getItemList();
		const size_t itemCount = std::min(items.size(), MAX_ITEMS_PER_CONTAINER);
		msg.addByte(static_cast<uint8_t>(itemCount));

		for (size_t index = 0; index < itemCount; ++index) {
			const auto& item = items[index];
			if (!item) {
				msg.add<uint16_t>(0);
				msg.addByte(0);
				msg.add<uint16_t>(0);
				msg.addString("");
				continue;
			}

			const ItemType& itemType = Item::items[item->getID()];
			msg.add<uint16_t>(item->getClientID());

			if (auto childContainer = std::dynamic_pointer_cast<Container>(item)) {
				self(self, childContainer, static_cast<uint8_t>(depth + 1));
				continue;
			}

			msg.addByte(static_cast<uint8_t>(std::min<uint32_t>(item->getItemCount(), UINT8_MAX)));
			const uint32_t price = itemType.sellPrice > 0 ? itemType.sellPrice : item->getWorth();
			msg.add<uint16_t>(static_cast<uint16_t>(std::min<uint32_t>(price, UINT16_MAX)));
			msg.addString(item->getName());
		}
	};

	addContainerItems(addContainerItems, corpse, 1);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendItemValues()
{
	if (!isAstraClient) {
		return;
	}

	std::vector<NetworkMessage> packets;
	for (;;) {
		{
			std::lock_guard<std::mutex> lock(itemValuesCacheMutex);
			if (itemValuesCache.valid) {
				packets = itemValuesCache.packets;
				break;
			}
		}

		rebuildItemValuesCache();
	}

	for (const auto& packet : packets) {
		writeToOutputBuffer(packet);
	}
}

void ProtocolGame::rebuildItemValuesCache()
{
	std::lock_guard<std::mutex> lock(itemValuesCacheMutex);

	std::vector<std::pair<uint16_t, uint32_t>> entries;
	entries.reserve(Item::items.size());
	for (size_t id = 0, size = Item::items.size(); id < size; ++id) {
		const ItemType& itemType = Item::items.getItemType(id);
		const uint64_t value = itemType.sellPrice > 0 ? itemType.sellPrice :
		                       (itemType.buyPrice > 0 ? itemType.buyPrice : itemType.worth);
		if (itemType.id != 0 && value > 0) {
			entries.emplace_back(
			    itemType.id,
			    static_cast<uint32_t>(std::min<uint64_t>(value, std::numeric_limits<uint32_t>::max())));
		}
	}

	std::vector<NetworkMessage> packets;
	packets.reserve((entries.size() + MAX_ITEM_VALUES_PER_PACKET - 1) / MAX_ITEM_VALUES_PER_PACKET);
	for (size_t offset = 0; offset < entries.size(); offset += MAX_ITEM_VALUES_PER_PACKET) {
		const size_t chunkSize = std::min(MAX_ITEM_VALUES_PER_PACKET, entries.size() - offset);
		NetworkMessage& msg = packets.emplace_back();
		msg.addByte(ITEM_VALUES_OPCODE);
		msg.add<uint16_t>(static_cast<uint16_t>(chunkSize));
		for (size_t index = offset; index < offset + chunkSize; ++index) {
			msg.add<uint16_t>(entries[index].first);
			msg.add<uint32_t>(entries[index].second);
		}
	}

	itemValuesCache.packets = std::move(packets);
	itemValuesCache.valid = true;
}

void ProtocolGame::invalidateItemValuesCache()
{
	std::lock_guard<std::mutex> lock(itemValuesCacheMutex);
	itemValuesCache.valid = false;
}

void ProtocolGame::sendImpactTracker(uint8_t analyzerType, uint32_t amount, CombatType_t combatType,
                                     std::string_view targetName)
{
	if (!isAstraClient || amount == 0) {
		return;
	}

	uint8_t effect = 0;
	switch (combatType) {
		case COMBAT_FIREDAMAGE: effect = 1; break;
		case COMBAT_EARTHDAMAGE: effect = 2; break;
		case COMBAT_ENERGYDAMAGE: effect = 3; break;
		case COMBAT_ICEDAMAGE: effect = 4; break;
		case COMBAT_HOLYDAMAGE: effect = 5; break;
		case COMBAT_DEATHDAMAGE: effect = 6; break;
		case COMBAT_HEALING: effect = 7; break;
		case COMBAT_DROWNDAMAGE: effect = 8; break;
		case COMBAT_LIFEDRAIN: effect = 9; break;
		case COMBAT_MANADRAIN: effect = 10; break;
		default: break;
	}

	NetworkMessage msg;
	msg.addByte(0xCC);
	msg.addByte(analyzerType);
	msg.add<uint32_t>(amount);
	if (analyzerType == 1) {
		msg.addByte(effect);
	} else if (analyzerType == 2) {
		msg.addByte(effect);
		msg.addString(targetName.empty() ? "Environment" : targetName);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureWalkthrough(const Creature* creature, bool walkthrough)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x92);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(walkthrough ? 0x00 : 0x01);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureShield(const Creature* creature)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x91);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(player->getPartyShield(creature->getPlayer()));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSkull(const Creature* creature)
{
	// Allow influenced/fiendish monsters to show skull regardless of world type
	bool isForgeMonster = false;
	if (const Monster* monster = creature->getMonster()) {
		isForgeMonster = monster->isInfluenced() || monster->isFiendish();
	}

	if (!isForgeMonster && g_game.getWorldType() != WORLD_TYPE_PVP) {
		return;
	}

	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x90);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(player->getSkullClient(creature));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSquare(const Creature* creature, SquareColor_t color)
{
	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x86);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(color);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTutorial(uint8_t tutorialId)
{
	NetworkMessage msg;
	msg.addByte(0xDC);
	msg.addByte(tutorialId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddMarker(const Position& pos, uint8_t markType, std::string_view desc)
{
	NetworkMessage msg;
	msg.addByte(0xDD);
	msg.addPosition(pos);
	msg.addByte(markType);
	msg.addString(desc);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendReLoginWindow()
{
	NetworkMessage msg;
	msg.addByte(0x28);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendStats()
{
	NetworkMessage msg;
	AddPlayerStats(msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendBasicData()
{
	if (!player || !isAstraClient) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x9F);

	// premium
	msg.addByte(player->isPremium() ? 0x01 : 0x00);

	// vocation
	msg.addByte(static_cast<uint8_t>(player->getVocationId()));

	// prey - OTC client expects 1 byte for prey status when GamePrey feature is enabled
	msg.addByte(0x00);

	std::vector<uint16_t> knownSpells;
	if (g_spells) {
		for (const auto& entry : g_spells->getInstantSpells()) {
			const auto& spell = entry.second;
			if (spell.getId() != 0 && spell.canCast(player.get())) {
				knownSpells.push_back(spell.getId());
			}
		}
	}

	std::ranges::sort(knownSpells);
	knownSpells.erase(std::unique(knownSpells.begin(), knownSpells.end()), knownSpells.end());

	msg.add<uint16_t>(static_cast<uint16_t>(knownSpells.size()));
	for (uint16_t spellId : knownSpells) {
		msg.add<uint16_t>(spellId);
	}
	msg.addByte(player->getVocation()->getMagicShield());

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextMessage(const TextMessage& message)
{
	NetworkMessage msg;
	msg.addByte(0xB4);
	msg.addByte(message.type);
	msg.addString(message.text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextMessage(MessageClasses mclass, const std::string& message)
{
	NetworkMessage msg;
	msg.addByte(0xB4);
	msg.addByte(mclass);
	msg.addString(message);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendClosePrivate(uint16_t channelId)
{
	NetworkMessage msg;
	msg.addByte(0xB3);
	msg.add<uint16_t>(channelId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatePrivateChannel(uint16_t channelId, std::string_view channelName)
{
	NetworkMessage msg;
	msg.addByte(0xB2);
	msg.add<uint16_t>(channelId);
	msg.addString(channelName);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannelsDialog()
{
	if (!player) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xAB);

	const ChannelList& list = g_chat->getChannelList(*player);
	if (player->client->isBroadcasting()) {
		msg.addByte(list.size() + 1);
		msg.add<uint16_t>(CHANNEL_CAST);
		msg.addString("Cast Channel");
		for (const ChatChannel* channel : list) {
			msg.add<uint16_t>(channel->getId());
			msg.addString(channel->getName());
		}
	} else {
		msg.addByte(list.size());
		for (const ChatChannel* channel : list) {
			msg.add<uint16_t>(channel->getId());
			msg.addString(channel->getName());
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannel(uint16_t channelId, std::string_view channelName)
{
	NetworkMessage msg;
	msg.addByte(0xAC);
	msg.add<uint16_t>(channelId);
	msg.addString(channelName);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChannelMessage(std::string_view author, std::string_view text, SpeakClasses type,
                                      uint16_t channel)
{
	if (!player) {
		return;
	}

	ChatChannel* varChannel = g_chat->getChannelById(channel);
	std::string messageText(text);

	bool isLootChannel = (channel == 10);
	bool isPlayerInChannel = (varChannel && varChannel->getUsers().contains(player->getID()));

	if (isLootChannel && !isPlayerInChannel) {
		player->sendTextMessage(MESSAGE_INFO_DESCR, messageText);
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xAA);
	msg.add<uint32_t>(0x00);
	msg.addString(author);
	msg.add<uint16_t>(0x00);
	msg.addByte(type);
	msg.add<uint16_t>(channel);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendIcons(uint16_t icons)
{
	NetworkMessage msg;
	msg.addByte(0xA2);
	msg.add<uint16_t>(icons);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendIcons(uint64_t icons, IconBakragore_t bakragoreIcon)
{
	if (!player || !supportsAstraCreatureIcons()) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xA2);
	msg.add<uint64_t>(icons);
	msg.addByte(static_cast<uint8_t>(bakragoreIcon));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureIcon(const Creature* creature)
{
	if (!creature || !player || !supportsCreatureIcons()) {
		return;
	}

	if (!canSee(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x8B);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(14);
	AddCreatureIcon(msg, creature);
	writeToOutputBuffer(msg);
}

void ProtocolGame::AddCreatureIcon(NetworkMessage& msg, const Creature* creature)
{
	if (!creature) {
		return;
	}

	const auto& icons = creature->getIcons();
	const size_t count = std::min<size_t>(icons.size(), 3);
	msg.addByte(static_cast<uint8_t>(count));
	for (size_t i = 0; i < count; ++i) {
		const auto& icon = icons[i];
		msg.addByte(icon.serialize());
		msg.addByte(static_cast<uint8_t>(icon.category));
		msg.add<uint16_t>(icon.count);
	}
}

void ProtocolGame::sendContainer(uint8_t cid, const Container* container, bool hasParent, uint16_t firstIndex)
{
	NetworkMessage msg;
	msg.addByte(0x6E);

	msg.addByte(cid);

	const bool sendQuickLootFlags = shouldSendQuickLootFlags();
	const bool sendItemTierByte = shouldSendItemTierByte();
	const bool sendItemTierData = shouldSendItemTierData();
	const bool sendAstraItemState = canSendAstraItemState();
	const bool sendAstraQuiverCountU16 = shouldSendAstraQuiverCountU16();
	const bool sendContainerPagination = shouldSendContainerPagination();
	const bool paginateContainer = shouldPaginateContainer(container);
	if (container->getID() == ITEM_BROWSEFIELD) {
		msg.addItem(ITEM_BAG, 1, sendItemTierData, sendItemTierByte, sendQuickLootFlags, sendAstraItemState,
		            sendAstraQuiverCountU16);
		msg.addString("Browse Field");
	} else {
		msg.addItem(container, sendItemTierData, sendItemTierByte, isOTC, sendQuickLootFlags, sendAstraItemState,
		            sendAstraQuiverCountU16);
		msg.addString(container->getName());
	}

	msg.addByte(static_cast<uint8_t>(container->capacity()));

	msg.addByte(hasParent ? 0x01 : 0x00);

	const uint32_t containerSize = container->size();
	if (sendContainerPagination) {
		msg.addByte(0x01); // drag and drop
		msg.addByte(paginateContainer ? 0x01 : 0x00);
		msg.add<uint16_t>(static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, containerSize)));
		msg.add<uint16_t>(firstIndex);
	}

	const uint32_t maxItemsToSend = paginateContainer ? container->capacity() : 0xFF;
	const uint32_t itemCount = firstIndex >= containerSize ? 0 :
	                           std::min<uint32_t>(maxItemsToSend, containerSize - firstIndex);
	msg.addByte(static_cast<uint8_t>(std::min<uint32_t>(0xFF, itemCount)));

	const ItemDeque& itemList = container->getItemList();
	if (itemCount > 0) {
		uint32_t i = 0;
		for (ItemDeque::const_iterator cit = itemList.begin() + firstIndex, end = itemList.end();
		     i < itemCount && cit != end; ++cit, ++i) {
			msg.addItem(cit->get(), sendItemTierData, sendItemTierByte, isOTC, sendQuickLootFlags, sendAstraItemState,
			            sendAstraQuiverCountU16);
		}
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendLootContainers()
{
	if (!player || !shouldSendQuickLootFlags()) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xC0);

	player->ensureQuickLootStateLoaded();
	msg.addByte(player->getQuickLootFallbackToMainContainer() ? 1 : 0);

	const auto& containers = player->getManagedLootContainers();
	uint8_t lootContainerCount = 0;
	uint8_t obtainContainerCount = 0;
	for (const auto& [category, managedContainer] : containers) {
		if (managedContainer.loot != 0) {
			++lootContainerCount;
		}
		if (managedContainer.obtain != 0) {
			++obtainContainerCount;
		}
	}

	msg.addByte(lootContainerCount);
	for (const auto& [category, managedContainer] : containers) {
		if (managedContainer.loot == 0) {
			continue;
		}

		msg.addByte(static_cast<uint8_t>(category));
		msg.add<uint16_t>(managedContainer.loot);
	}

	msg.addByte(obtainContainerCount);
	for (const auto& [category, managedContainer] : containers) {
		if (managedContainer.obtain == 0) {
			continue;
		}

		msg.addByte(static_cast<uint8_t>(category));
		msg.add<uint16_t>(managedContainer.obtain);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendShop(const ShopInfoList& itemList)
{
	NetworkMessage msg;
	msg.addByte(0x7A);

	uint16_t itemsToSend = std::min<size_t>(itemList.size(), std::numeric_limits<uint16_t>::max());
	msg.addByte(itemsToSend);

	uint16_t i = 0;
	for (auto it = itemList.begin(); i < itemsToSend; ++it, ++i) {
		AddShopItem(msg, *it);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseShop()
{
	NetworkMessage msg;
	msg.addByte(0x7C);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSaleItemList(const std::list<ShopInfo>& shop)
{
	NetworkMessage msg;
	msg.addByte(0x7B);

	auto shopOwnerPtr = player->shopOwner.lock();
	uint16_t moneyType = shopOwnerPtr ? shopOwnerPtr->getMoneyType() : 0;
	uint64_t money = 0;

	if (moneyType == 0) {
		money = player->getMoney();
		if (getBoolean(ConfigManager::NPCS_USING_BANK_MONEY)) {
			money += player->getBankBalance();
		}
	} else {
		money = player->getItemTypeCount(moneyType);
	}

	if (isOTC) {
		msg.add<uint64_t>(money);
	} else {
		msg.add<uint32_t>(money);
	}

	std::unordered_map<uint16_t, uint32_t> saleMap;

	if (shop.size() <= 5) {
		// For very small shops it's not worth it to create the complete map
		for (const ShopInfo& shopInfo : shop) {
			if (shopInfo.sellPrice == 0) {
				continue;
			}

			int8_t subtype = -1;

			const ItemType& itemType = Item::items[shopInfo.itemId];
			if (itemType.hasSubType() && !itemType.stackable) {
				subtype = (shopInfo.subType == 0 ? -1 : shopInfo.subType);
			}

			uint32_t count = player->getItemTypeCount(shopInfo.itemId, subtype);
			if (count > 0) {
				saleMap[shopInfo.itemId] = count;
			}
		}
	} else {
		// Large shop, it's better to get a cached map of all item counts and use it
		// We need a temporary map since the finished map should only contain items
		// available in the shop
		std::unordered_map<uint32_t, uint32_t> tempSaleMap;
		player->getAllItemTypeCount(tempSaleMap);

		// We must still check manually for the special items that require subtype matches
		// (That is, fluids such as potions etc., actually these items are very few since
		// health potions now use their own ID)
		for (const ShopInfo& shopInfo : shop) {
			if (shopInfo.sellPrice == 0) {
				continue;
			}

			int8_t subtype = -1;

			const ItemType& itemType = Item::items[shopInfo.itemId];
			if (itemType.hasSubType() && !itemType.stackable) {
				subtype = (shopInfo.subType == 0 ? -1 : shopInfo.subType);
			}

			if (subtype != -1) {
				uint32_t count;
				if (itemType.isFluidContainer() || itemType.isSplash()) {
					count = player->getItemTypeCount(shopInfo.itemId, subtype); // This shop item requires extra checks
				} else {
					count = subtype;
				}

				if (count > 0) {
					saleMap[shopInfo.itemId] = count;
				}
			} else {
				auto findIt = tempSaleMap.find(shopInfo.itemId);
				if (findIt != tempSaleMap.end() && findIt->second > 0) {
					saleMap[shopInfo.itemId] = findIt->second;
				}
			}
		}
	}

	uint8_t itemsToSend = std::min<size_t>(saleMap.size(), std::numeric_limits<uint8_t>::max());
	msg.addByte(itemsToSend);

	uint8_t i = 0;
	for (auto it = saleMap.begin(); i < itemsToSend; ++it, ++i) {
		msg.addItemId(it->first);
		msg.addByte(static_cast<uint8_t>(std::min<uint32_t>(it->second, std::numeric_limits<uint8_t>::max())));
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTradeItemRequest(std::string_view traderName, const Item* item, bool ack)
{
	NetworkMessage msg;

	if (ack) {
		msg.addByte(0x7D);
	} else {
		msg.addByte(0x7E);
	}

	msg.addString(traderName);
	const bool sendQuickLootFlags = shouldSendQuickLootFlags();
	const bool sendItemTierByte = shouldSendItemTierByte();
	const bool sendItemTierData = shouldSendItemTierData();

	if (const Container* tradeContainer = item->getContainer()) {
		std::list<const Container*> listContainer{tradeContainer};
		std::list<const Item*> itemList{tradeContainer};
		while (!listContainer.empty()) {
			const Container* container = listContainer.front();
			listContainer.pop_front();

			for (const auto& containerItem : container->getItemList()) {
				Container* tmpContainer = containerItem->getContainer();
				if (tmpContainer) {
					listContainer.push_back(tmpContainer);
				}
				itemList.push_back(containerItem.get());
			}
		}

		msg.addByte(itemList.size());
		const bool sendAstraItemState = canSendAstraItemState();
		const bool sendAstraQuiverCountU16 = shouldSendAstraQuiverCountU16();
		for (const Item* listItem : itemList) {
			msg.addItem(listItem, sendItemTierData, sendItemTierByte, isOTC, sendQuickLootFlags, sendAstraItemState,
			            sendAstraQuiverCountU16);
		}
	} else {
		msg.addByte(0x01);
		msg.addItem(item, sendItemTierData, sendItemTierByte, isOTC, sendQuickLootFlags, canSendAstraItemState(),
		            shouldSendAstraQuiverCountU16());
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseTrade()
{
	NetworkMessage msg;
	msg.addByte(0x7F);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCloseContainer(uint8_t cid)
{
	NetworkMessage msg;
	msg.addByte(0x6F);
	msg.addByte(cid);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureTurn(const Creature* creature, uint32_t stackpos)
{
	if (stackpos >= MAX_STACKPOS_THINGS || !canSee(creature)) {
		return;
	}

	uint8_t dir = static_cast<uint8_t>(creature->getDirection());
	if (dir > 3) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(creature->getPosition());
	msg.addByte(static_cast<uint8_t>(stackpos));

	msg.add<uint16_t>(0x63);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(dir);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureSay(const Creature* creature, SpeakClasses type, std::string_view text,
                                   const Position* pos /* = nullptr*/)
{
	if (!creature) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xAA);
	msg.add<uint32_t>(0x00);

	msg.addString(creature->getName());

	// Add level only for players
	if (const Player* speaker = creature->getPlayer()) {
		if (!speaker->isAccessPlayer()) {
			msg.add<uint16_t>(static_cast<uint16_t>(speaker->getLevel()));
		} else {
			msg.add<uint16_t>(0x00);
		}
	} else {
		msg.add<uint16_t>(0x00);
	}

	msg.addByte(type);
	if (pos) {
		msg.addPosition(*pos);
	} else {
		msg.addPosition(creature->getPosition());
	}

	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendToChannel(const Creature* creature, SpeakClasses type, std::string_view text, uint16_t channelId)
{
	if (!creature) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xAA);
	msg.add<uint32_t>(0x00);

	if (type == TALKTYPE_CHANNEL_R2) {
		msg.addString("");
		type = TALKTYPE_CHANNEL_R1;
	} else {
		msg.addString(creature->getName());
		// Add level only for players
		if (const Player* speaker = creature->getPlayer()) {
			if (!speaker->isAccessPlayer()) {
				msg.add<uint16_t>(static_cast<uint16_t>(speaker->getLevel()));
			} else {
				msg.add<uint16_t>(0x00);
			}
		} else {
			msg.add<uint16_t>(0x00);
		}
	}

	msg.addByte(type);
	msg.add<uint16_t>(channelId);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPrivateMessage(const Player* speaker, SpeakClasses type, std::string_view text)
{
	if (!speaker) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xAA);
	static uint32_t statementId = 0;
	msg.add<uint32_t>(++statementId);
	msg.addString(speaker->getName());
	if (!speaker->isAccessPlayer()) {
		msg.add<uint16_t>(static_cast<uint16_t>(speaker->getLevel()));
	} else {
		msg.add<uint16_t>(0x00);
	}
	msg.addByte(type);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCancelTarget()
{
	NetworkMessage msg;
	msg.addByte(0xA3);
	msg.add<uint32_t>(0x00);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendChangeSpeed(const Creature* creature, uint32_t speed)
{
	NetworkMessage msg;
	msg.addByte(0x8F);
	msg.add<uint32_t>(creature->getID());
	msg.add<uint16_t>(static_cast<uint16_t>(speed));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCancelWalk()
{
	NetworkMessage msg;
	msg.addByte(0xB5);
	msg.addByte(player->getDirection());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSkills()
{
	NetworkMessage msg;
	AddPlayerSkills(msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendPing()
{
	if (clientOperatingSystem == CLIENTOS_CUSTOM_DLL) {
		if (++customPingSequence == 0) {
			++customPingSequence;
		}
		sendCustomClientPing(customPingSequence);
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x1E);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCustomClientPing(uint32_t pingId)
{
	NetworkMessage msg;
	msg.addByte(0x1E);
	msg.add<uint32_t>(pingId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendDistanceShoot(const Position& from, const Position& to, uint16_t type)
{
	NetworkMessage msg;
	msg.addByte(0x85);
	msg.addPosition(from);
	msg.addPosition(to);
	msg.add<uint16_t>(type);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMagicEffect(const Position& pos, uint16_t type)
{
	if (!canSee(pos)) {
		return;
	}

	Tile* tile = g_game.map.getTile(pos);
	if (!tile || !tile->getGround()) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x83);
	msg.addPosition(pos);
	msg.add<uint16_t>(type);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendCreatureHealth(const Creature* creature)
{
	NetworkMessage msg;
	msg.addByte(0x8C);
	msg.add<uint32_t>(creature->getID());

	if (creature->isHealthHidden()) {
		msg.addByte(0x00);
	} else {
		msg.addByte(std::ceil(
		    (static_cast<double>(creature->getHealth()) / std::max<int32_t>(creature->getMaxHealth(), 1)) * 100));
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendFYIBox(std::string_view message)
{
	NetworkMessage msg;
	msg.addByte(0x15);
	msg.addString(message);
	writeToOutputBuffer(msg);
}

// tile
void ProtocolGame::sendMapDescription(const Position& pos)
{
	NetworkMessage msg;
	msg.addByte(0x64);
	msg.addPosition(spyActive_ ? spyViewportPos_ : player->getPosition());
	GetMapDescription(pos.x - Map::maxClientViewportX, pos.y - Map::maxClientViewportY, pos.z,
	                  (Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2, msg);
	writeToOutputBuffer(msg);
}

void ProtocolGame::refreshWorldView()
{
	knownCreatureSet.clear();
	sendMapDescription(player->getPosition());
	sendZoneWeather(player->getPosition(), true);
}

void ProtocolGame::sendZoneWeather(const Position& position, bool force)
{
	const bool weatherEnabled = zoneWeatherFeatureEnabled || supportsDllZoneWeather;
	if (!player || !supportsNativeZoneWeather() || !weatherEnabled) {
		return;
	}

	WeatherState state = ZoneWeather::getState(position);
	if (state.type > WeatherType::Sand) {
		state = {};
	}
	state.intensity = std::min<uint8_t>(state.intensity, 100);
	if (state.type == WeatherType::None) {
		state.intensity = 0;
		state.windX = 0;
		state.windY = 0;
	}

	// Reuse the outgoing transition only for unmapped areas without an explicit transition.
	if (state.type == WeatherType::None && state.transitionMs == 0 && lastZoneWeather) {
		state.transitionMs = lastZoneWeather->transitionMs;
	}

	const bool sameClientPayload = lastZoneWeather &&
	                               lastZoneWeather->type == state.type &&
	                               lastZoneWeather->intensity == state.intensity &&
	                               lastZoneWeather->windX == state.windX &&
	                               lastZoneWeather->windY == state.windY &&
	                               lastZoneWeather->transitionMs == state.transitionMs;
	if (!force && sameClientPayload) {
		return;
	}
	lastZoneWeather = state;

	NetworkMessage msg;
	msg.addByte(ZONE_WEATHER_OPCODE);
	if (clientOperatingSystem == CLIENTOS_CUSTOM_DLL) {
		for (const uint8_t value : DLL_WEATHER_SERVER_MAGIC) {
			msg.addByte(value);
		}
		msg.addByte(DLL_WEATHER_PROTOCOL_VERSION);
		msg.add<uint32_t>(DLL_WEATHER_BUILD_ID);
		msg.add<uint32_t>(dllWeatherSequence);
	} else {
		msg.addByte(ZONE_WEATHER_PACKET_VERSION);
	}
	msg.addByte(static_cast<uint8_t>(state.type));
	msg.addByte(state.intensity);
	msg.addByte(static_cast<uint8_t>(state.windX));
	msg.addByte(static_cast<uint8_t>(state.windY));
	msg.add<uint16_t>(state.transitionMs);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddTileItem(const Position& pos, uint32_t stackpos, const Item* item)
{
	if (stackpos >= MAX_STACKPOS_THINGS || !canSee(pos)) {
		return;
	}

	if (!InstanceUtils::canSeeItemInInstance(player->getInstanceID(), item)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6A);
	msg.addPosition(pos);
	msg.addByte(static_cast<uint8_t>(stackpos));
	msg.addItem(item, shouldSendItemTierData(), shouldSendItemTierByte(), isOTC, shouldSendQuickLootFlags(),
	            canSendAstraItemState(), shouldSendAstraQuiverCountU16());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateTileItem(const Position& pos, uint32_t stackpos, const Item* item)
{
	if (stackpos >= MAX_STACKPOS_THINGS || !canSee(pos)) {
		return;
	}

	if (!InstanceUtils::canSeeItemInInstance(player->getInstanceID(), item)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(pos);
	msg.addByte(static_cast<uint8_t>(stackpos));
	msg.addItem(item, shouldSendItemTierData(), shouldSendItemTierByte(), isOTC, shouldSendQuickLootFlags(),
	            canSendAstraItemState(), shouldSendAstraQuiverCountU16());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRemoveTileThing(const Position& pos, uint32_t stackpos)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	RemoveTileThing(msg, pos, stackpos);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateTileCreature(const Position& pos, uint32_t stackpos, const Creature* creature)
{
	if (stackpos >= MAX_STACKPOS_THINGS || !canSee(pos)) {
		return;
	}

	if (creature != player.get() && !player->canSeeCreature(creature)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(pos);
	msg.addByte(static_cast<uint8_t>(stackpos));

	auto [known, removedKnown] = isKnownCreature(creature->getID());
	AddCreature(msg, creature, known, removedKnown);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateTile(const Tile* tile, const Position& pos)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x69);
	msg.addPosition(pos);

	if (tile) {
		GetTileDescription(tile, msg);
		msg.addByte(0x00);
		msg.addByte(0xFF);
	} else {
		msg.addByte(0x01);
		msg.addByte(0xFF);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendFightModes()
{
	NetworkMessage msg;
	msg.addByte(0xA7);
	msg.addByte(player->fightMode);
	msg.addByte(player->chaseMode);
	msg.addByte(player->secureMode);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddCreature(const Creature* creature, const Position& pos, int32_t stackpos,
                                   MagicEffectClasses magicEffect /*= CONST_ME_NONE*/)
{
	if (!canSee(pos)) {
		return;
	}

	if (creature != player.get() && !player->canSeeCreature(creature)) {
		return;
	}

	if (creature != player.get()) {
		if (stackpos != -1 && stackpos < MAX_STACKPOS_THINGS) {
			NetworkMessage msg;
			msg.addByte(0x6A);
			msg.addPosition(pos);
			msg.addByte(static_cast<uint8_t>(stackpos));

			auto [known, removedKnown] = isKnownCreature(creature->getID());
			AddCreature(msg, creature, known, removedKnown);
			writeToOutputBuffer(msg);
		}

		if (magicEffect != CONST_ME_NONE) {
			sendMagicEffect(pos, magicEffect);
		}
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x0A);

	msg.add<uint32_t>(player->getID());
	msg.add<uint16_t>(0x32); // beat duration (50)

	// can report bugs?
	if (player->getAccountType() >= ACCOUNT_TYPE_TUTOR) {
		msg.addByte(0x01);
	} else {
		msg.addByte(0x00);
	}

	writeToOutputBuffer(msg);

	sendMapDescription(pos);
	sendZoneWeather(pos, true);

	if (magicEffect != CONST_ME_NONE) {
		sendMagicEffect(pos, magicEffect);
	}

	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		sendInventoryItem(static_cast<slots_t>(i), player->getInventoryItem(static_cast<slots_t>(i)));
	}

	if (isOTC) {
		sendInventoryItem(CONST_SLOT_STORE_INBOX, player->getStoreInbox());
	}

	sendPlayerInventory();

	sendStats();
	sendSkills();

	if (isAstraClient) {
		sendBasicData();
	}

	if (isAstraClient && (player->getVocationId() == 9 || player->getVocationId() == 10)) {
		player->sendMonkData();
	}

	if (isAstraClient) {
		sendBlessStatus();
	}

	sendWorldLight(g_game.getWorldLightInfo());
	sendCreatureLight(creature);

	const std::forward_list<VIPEntry>& vipEntries = IOLoginData::getVIPEntries(player->getAccount());
	for (const VIPEntry& entry : vipEntries) {
		auto vipPlayer = g_game.getPlayerByGUID(entry.guid);

		sendVIP(entry.guid, entry.name,
		        static_cast<VipStatus_t>((vipPlayer && (!vipPlayer->isInGhostMode() || player->isAccessPlayer()))));
	}

	player->sendIcons();
}

void ProtocolGame::sendMoveCreature(const Creature* creature, const Position& newPos, int32_t newStackPos,
                                    const Position& oldPos, int32_t oldStackPos, bool teleport)
{
	if (spyActive_ && creature->getID() == spyTargetCreatureId_) {
		spyViewportPos_ = newPos;

		if (teleport || oldStackPos >= MAX_STACKPOS_THINGS) {
			sendRemoveTileThing(oldPos, oldStackPos);
			sendMapDescription(newPos);
			return;
		}

		NetworkMessage msg;
		if (oldPos.z == 7 && newPos.z >= 8) {
			RemoveTileThing(msg, oldPos, oldStackPos);
		} else {
			msg.addByte(0x6D);
			msg.addPosition(oldPos);
			msg.addByte(static_cast<uint8_t>(oldStackPos));
			msg.addPosition(newPos);
		}

		if (newPos.z > oldPos.z) {
			MoveDownCreature(msg, creature, newPos, oldPos);
		} else if (newPos.z < oldPos.z) {
			MoveUpCreature(msg, creature, newPos, oldPos);
		}

		if (oldPos.y > newPos.y) {
			msg.addByte(0x65);
			GetMapDescription(oldPos.x - Map::maxClientViewportX, newPos.y - Map::maxClientViewportY, newPos.z,
			                  (Map::maxClientViewportX * 2) + 2, 1, msg);
		} else if (oldPos.y < newPos.y) {
			msg.addByte(0x67);
			GetMapDescription(oldPos.x - Map::maxClientViewportX, newPos.y + (Map::maxClientViewportY + 1),
			                  newPos.z, (Map::maxClientViewportX * 2) + 2, 1, msg);
		}
		if (oldPos.x < newPos.x) {
			msg.addByte(0x66);
			GetMapDescription(newPos.x + (Map::maxClientViewportX + 1), newPos.y - Map::maxClientViewportY,
			                  newPos.z, 1, (Map::maxClientViewportY * 2) + 2, msg);
		} else if (oldPos.x > newPos.x) {
			msg.addByte(0x68);
			GetMapDescription(newPos.x - Map::maxClientViewportX, newPos.y - Map::maxClientViewportY, newPos.z,
			                  1, (Map::maxClientViewportY * 2) + 2, msg);
		}
		writeToOutputBuffer(msg);
		return;
	}

	if (creature != player.get() && !player->canSeeCreature(creature)) {
		if (oldStackPos != -1 && canSee(oldPos)) {
			sendRemoveTileThing(oldPos, oldStackPos);
		}
		return;
	}

	if (creature == player.get()) {
		if (teleport || oldStackPos >= MAX_STACKPOS_THINGS) {
			sendRemoveTileThing(oldPos, oldStackPos);
			sendMapDescription(newPos);
		} else {
			NetworkMessage msg;
			if (oldPos.z == 7 && newPos.z >= 8) {
				RemoveTileThing(msg, oldPos, oldStackPos);
			} else {
				msg.addByte(0x6D);
				msg.addPosition(oldPos);
				msg.addByte(static_cast<uint8_t>(oldStackPos));
				msg.addPosition(newPos);
			}

			if (newPos.z > oldPos.z) {
				MoveDownCreature(msg, creature, newPos, oldPos);
			} else if (newPos.z < oldPos.z) {
				MoveUpCreature(msg, creature, newPos, oldPos);
			}

			if (!isOTC && newStackPos >= MAX_STACKPOS_THINGS) {
				msg.addByte(0x64);
				msg.addPosition(player->getPosition());
				GetMapDescription(newPos.x - Map::maxClientViewportX, newPos.y - Map::maxClientViewportY, newPos.z,
				                  (Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2, msg);
			} else {
				if (oldPos.y > newPos.y) {
					msg.addByte(0x65);
					GetMapDescription(oldPos.x - Map::maxClientViewportX, newPos.y - Map::maxClientViewportY, newPos.z,
					                  (Map::maxClientViewportX * 2) + 2, 1, msg);
				} else if (oldPos.y < newPos.y) {
					msg.addByte(0x67);
					GetMapDescription(oldPos.x - Map::maxClientViewportX, newPos.y + (Map::maxClientViewportY + 1),
					                  newPos.z, (Map::maxClientViewportX * 2) + 2, 1, msg);
				}
				if (oldPos.x < newPos.x) {
					msg.addByte(0x66);
					GetMapDescription(newPos.x + (Map::maxClientViewportX + 1), newPos.y - Map::maxClientViewportY,
					                  newPos.z, 1, (Map::maxClientViewportY * 2) + 2, msg);
				} else if (oldPos.x > newPos.x) {
					msg.addByte(0x68);
					GetMapDescription(newPos.x - Map::maxClientViewportX, newPos.y - Map::maxClientViewportY, newPos.z,
					                  1, (Map::maxClientViewportY * 2) + 2, msg);
				}
			}
			writeToOutputBuffer(msg);
		}
		sendZoneWeather(newPos);
	} else if (canSee(oldPos) && canSee(creature->getPosition())) {
		if (teleport || (oldPos.z == 7 && newPos.z >= 8) || oldStackPos >= MAX_STACKPOS_THINGS) {
			sendRemoveTileThing(oldPos, oldStackPos);
			sendAddCreature(creature, newPos, newStackPos);
		} else {
			NetworkMessage msg;
			msg.addByte(0x6D);
			msg.addPosition(oldPos);
			msg.addByte(static_cast<uint8_t>(oldStackPos));
			msg.addPosition(creature->getPosition());
			writeToOutputBuffer(msg);
		}
	} else if (canSee(oldPos)) {
		sendRemoveTileThing(oldPos, oldStackPos);
	} else if (canSee(creature->getPosition())) {
		sendAddCreature(creature, newPos, newStackPos);
	}
}

void ProtocolGame::sendInventoryItem(slots_t slot, const Item* item)
{
	NetworkMessage msg;
	if (item) {
		msg.addByte(0x78);
		msg.addByte(slot);
		msg.addItem(item, shouldSendItemTierData(), shouldSendItemTierByte(), isOTC, shouldSendQuickLootFlags(),
		            canSendAstraItemState(), shouldSendAstraQuiverCountU16());
	} else {
		msg.addByte(0x79);
		msg.addByte(slot);
	}
	writeToOutputBuffer(msg);

	if (imbuementTrackerOpen) {
		sendImbuementDurations(slot, item);
	}
}

void ProtocolGame::sendPlayerInventory()
{
	if (!canSendAstraItemState()) {
		return;
	}

	PlayerInventoryCounts counts;
	for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
		addPlayerInventoryItem(counts, player->getInventoryItem(static_cast<slots_t>(slot)));
	}

	NetworkMessage msg;
	msg.addByte(0xF5);
	constexpr std::size_t astraInventorySlotMarkers = 11;
	const std::size_t inventoryEntries = counts.size() + astraInventorySlotMarkers;
	if (inventoryEntries > std::numeric_limits<uint16_t>::max()) {
		LOG_WARN("[AstraInventory] Inventory snapshot truncated for player {}", player->getName());
	}

	const std::size_t totalItems = std::min<std::size_t>(inventoryEntries, std::numeric_limits<uint16_t>::max());
	msg.add<uint16_t>(static_cast<uint16_t>(totalItems));

	std::size_t written = 0;
	for (uint16_t slotMarker = 1; slotMarker <= astraInventorySlotMarkers && written < totalItems; ++slotMarker) {
		msg.add<uint16_t>(slotMarker);
		msg.addByte(0);
		addPackedPlayerInventoryCount(msg, 1);
		++written;
	}

	for (const auto& [key, amount] : counts) {
		if (written >= totalItems) {
			break;
		}

		msg.add<uint16_t>(key.first);
		msg.addByte(key.second);
		addPackedPlayerInventoryCount(msg, amount);
		++written;
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendModalWindow(const ModalWindow& modalWindow)
{
	if (!isOTC) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xFA);

	msg.add<uint32_t>(modalWindow.id);
	msg.addString(modalWindow.title);
	msg.addString(modalWindow.message);

	msg.addByte(modalWindow.buttons.size());
	for (const auto& it : modalWindow.buttons) {
		msg.addString(it.first);
		msg.addByte(it.second);
	}

	msg.addByte(modalWindow.choices.size());
	for (const auto& it : modalWindow.choices) {
		msg.addString(it.first);
		msg.addByte(it.second);
	}

	msg.addByte(modalWindow.defaultEscapeButton);
	msg.addByte(modalWindow.defaultEnterButton);
	msg.addByte(modalWindow.priority ? 0x01 : 0x00);

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAddContainerItem(uint8_t cid, const Item* item)
{
	sendAddContainerItem(cid, 0, item);
}

void ProtocolGame::sendAddContainerItem(uint8_t cid, uint16_t slot, const Item* item)
{
	NetworkMessage msg;
	msg.addByte(0x70);
	msg.addByte(cid);
	if (shouldSendContainerPagination()) {
		msg.add<uint16_t>(slot);
	}
	msg.addItem(item, shouldSendItemTierData(), shouldSendItemTierByte(), isOTC, shouldSendQuickLootFlags(),
	            canSendAstraItemState(), shouldSendAstraQuiverCountU16());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdateContainerItem(uint8_t cid, uint16_t slot, const Item* item)
{
	NetworkMessage msg;
	msg.addByte(0x71);
	msg.addByte(cid);
	if (shouldSendContainerPagination()) {
		msg.add<uint16_t>(slot);
	} else {
		msg.addByte(slot);
	}
	msg.addItem(item, shouldSendItemTierData(), shouldSendItemTierByte(), isOTC, shouldSendQuickLootFlags(),
	            canSendAstraItemState(), shouldSendAstraQuiverCountU16());
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendRemoveContainerItem(uint8_t cid, uint16_t slot)
{
	sendRemoveContainerItem(cid, slot, nullptr);
}

void ProtocolGame::sendRemoveContainerItem(uint8_t cid, uint16_t slot, const Item* lastItem)
{
	NetworkMessage msg;
	msg.addByte(0x72);
	msg.addByte(cid);
	if (shouldSendContainerPagination()) {
		msg.add<uint16_t>(slot);
		if (lastItem) {
			msg.addItem(lastItem, shouldSendItemTierData(), shouldSendItemTierByte(), isOTC, shouldSendQuickLootFlags(),
			            canSendAstraItemState(), shouldSendAstraQuiverCountU16());
		} else {
			msg.add<uint16_t>(0x00);
		}
	} else {
		msg.addByte(slot);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextWindow(uint32_t windowTextId, Item* item, uint16_t maxlen, bool canWrite)
{
	NetworkMessage msg;
	msg.addByte(0x96);
	msg.add<uint32_t>(windowTextId);
	msg.addItemId(item->getID());

	if (canWrite) {
		msg.add<uint16_t>(maxlen);
		msg.addString(item->getText());
	} else {
		auto text = item->getText();
		msg.add<uint16_t>(text.size());
		msg.addString(text);
	}

	auto writer = item->getWriter();
	if (!writer.empty()) {
		msg.addString(writer);
	} else {
		msg.add<uint16_t>(0x00);
	}

	time_t writtenDate = item->getDate();
	if (writtenDate != 0) {
		msg.addString(formatDateShort(writtenDate));
	} else {
		msg.add<uint16_t>(0x00);
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendTextWindow(uint32_t windowTextId, uint16_t itemId, std::string_view text)
{
	NetworkMessage msg;
	msg.addByte(0x96);
	msg.add<uint32_t>(windowTextId);
	msg.addItemId(itemId);
	msg.add<uint16_t>(text.size());
	msg.addString(text);
	msg.add<uint16_t>(0x00);
	msg.add<uint16_t>(0x00);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendHouseWindow(uint32_t windowTextId, std::string_view text)
{
	NetworkMessage msg;
	msg.addByte(0x97);
	msg.addByte(0x00);
	msg.add<uint32_t>(windowTextId);
	msg.addString(text);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendOutfitWindow()
{
	const auto& outfits = Outfits::getInstance().getOutfits(player->getSex());
	if (outfits.empty()) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xC8);

	const bool monkVocationEnabled = ConfigManager::getBoolean(ConfigManager::MONK_VOCATION_ENABLED);
	const bool isAstra860 = isAstraClient && getVersion() == 860;
	auto isHiddenOutfit = [monkVocationEnabled](const Outfit* outfit) {
		return outfit && !monkVocationEnabled && outfit->name == "Monk";
	};
	auto firstVisibleOutfit = [&outfits, &isHiddenOutfit]() -> const Outfit* {
		for (const Outfit* outfit : outfits) {
			if (!isHiddenOutfit(outfit)) {
				return outfit;
			}
		}
		return nullptr;
	};

	Outfit_t currentOutfit = player->getDefaultOutfit();
	const Outfit* currentOutfitType = Outfits::getInstance().getOutfitByLookType(currentOutfit.lookType);
	if (currentOutfit.lookType == 0 || isHiddenOutfit(currentOutfitType)) {
		const Outfit* visibleOutfit = firstVisibleOutfit();
		if (!visibleOutfit) {
			return;
		}
		currentOutfit = {};
		currentOutfit.lookType = visibleOutfit->lookType;
	}

	Mount* currentMount = g_game.mounts.getMountByID(player->getCurrentMount());
	if (currentMount) {
		currentOutfit.lookMount = currentMount->clientId;
	}

	AddOutfit(msg, currentOutfit);
	if (isAstraClient) {
		const auto familiar = Familiar::getFamiliarInfo(player.get());
		if (!familiar || currentOutfit.lookFamiliar != familiar->lookType) {
			currentOutfit.lookFamiliar = 0;
		}
		msg.add<uint16_t>(currentOutfit.lookFamiliar);
	}

	std::vector<ProtocolOutfit> protocolOutfits;
	if (player->isAccessPlayer()) {
		protocolOutfits.emplace_back("Gamemaster", 75, 0);
	}

	const auto& storeOutfitOffers = getStoreOutfitOffers();
	size_t maxProtocolOutfits = static_cast<size_t>(getInteger(ConfigManager::MAX_PROTOCOL_OUTFITS));
	if (isOTC) {
		maxProtocolOutfits = std::min<size_t>(maxProtocolOutfits, std::numeric_limits<uint8_t>::max());
	} else {
		maxProtocolOutfits = std::min<size_t>(maxProtocolOutfits, std::numeric_limits<uint16_t>::max());
	}

	for (const Outfit* outfit : outfits) {
		if (!outfit || isHiddenOutfit(outfit)) {
			continue;
		}

		uint8_t addons = 0;
		uint8_t mode = 0;
		uint32_t storeOfferId = 0;
		if (player->getOutfitAddons(*outfit, addons)) {
			// available outfit
		} else if (isAstra860) {
			const auto offerIt = storeOutfitOffers.find(outfit->lookType);
			if (offerIt == storeOutfitOffers.end()) {
				continue;
			}

			mode = 1;
			addons = offerIt->second.addons;
			storeOfferId = offerIt->second.offerId;
		} else {
			continue;
		}

		protocolOutfits.emplace_back(outfit->name, outfit->lookType, addons, mode, storeOfferId);
		if (protocolOutfits.size() >= maxProtocolOutfits) {
			break;
		}
	}

	if (isOTC || isAstra860) {
		msg.addByte(static_cast<uint8_t>(protocolOutfits.size()));
	} else {
		msg.add<uint16_t>(static_cast<uint16_t>(protocolOutfits.size()));
	}

	for (const ProtocolOutfit& outfit : protocolOutfits) {
		msg.add<uint16_t>(outfit.lookType);
		msg.addString(outfit.name);
		msg.addByte(outfit.addons);
		if (isAstra860) {
			msg.addByte(outfit.mode);
			if (outfit.mode == 1) {
				msg.add<uint32_t>(outfit.storeOfferId);
			}
		}
	}

	if (isOTC || isAstra860 || getVersion() != 861) {
		std::vector<const Mount*> mounts;
		for (const auto& [id, mount] : g_game.mounts.getMounts()) {
			if (player->hasMount(&mount)) {
				mounts.push_back(&mount);
			}
		}

		msg.addByte(static_cast<uint8_t>(mounts.size()));
		for (const Mount* mount : mounts) {
			msg.add<uint16_t>(mount->clientId);
			msg.addString(mount->name);
		}
	}

	if (isAstraClient) {
		const auto familiar = Familiar::getFamiliarInfo(player.get());
		msg.add<uint16_t>(familiar ? 1 : 0);
		if (familiar) {
			msg.add<uint16_t>(familiar->lookType);
			msg.addString(familiar->name);
			msg.addByte(0);
		}
	}

	writeToOutputBuffer(msg);
}

void ProtocolGame::sendItemInspection(std::shared_ptr<Item> item, uint16_t itemId, uint8_t itemCount, uint8_t inspectionType)
{
	if (!isAstraClient) {
		return;
	}

	if (item) {
		itemId = item->getID();
	}

	if (itemId >= Item::items.size()) {
		return;
	}

	const ItemType& itemType = Item::items[itemId];
	if (itemType.id == 0) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x76);
	msg.addByte(0);
	if (inspectionType == INSPECT_CYCLOPEDIA) {
		msg.addByte(1);
	} else if (inspectionType == INSPECT_PROFICIENCY) {
		msg.addByte(2);
	} else {
		msg.addByte(0);
	}
	msg.add<uint32_t>(player->getID());
	msg.addByte(1);
	msg.addString(item ? item->getName() : std::string_view(itemType.name));
	if (item) {
		msg.addItem(item.get(), shouldSendItemTierData(), shouldSendItemTierByte(), isOTC, shouldSendQuickLootFlags(),
		            canSendAstraItemState(), shouldSendAstraQuiverCountU16());
	} else {
		msg.addItem(itemId, itemCount, shouldSendItemTierData(), shouldSendItemTierByte(),
		            shouldSendQuickLootFlags(), canSendAstraItemState(), shouldSendAstraQuiverCountU16());
	}
	msg.addByte(0);

	std::vector<std::pair<std::string, std::string>> descriptions;
	if (!itemType.description.empty()) {
		descriptions.emplace_back("Description", itemType.description);
	}
	if (item) {
		const std::string weight = item->getWeightDescription();
		if (!weight.empty()) {
			descriptions.emplace_back("Weight", weight);
		}
	}

	msg.addByte(static_cast<uint8_t>(descriptions.size()));
	for (const auto& [detail, description] : descriptions) {
		msg.addString(detail);
		msg.addString(description);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendMonsterPodiumWindow(const Item* podium, const Position& position, uint16_t itemId,
                                            uint8_t stackPos)
{
	if (!isAstraClient || !podium) {
		return;
	}

	auto getAttribute = [podium](std::string_view key, int64_t defaultValue = 0) {
		const auto* attribute = podium->getCustomAttribute(std::string(key));
		if (!attribute) {
			return defaultValue;
		}
		if (const auto* value = std::get_if<int64_t>(&attribute->value)) {
			return *value;
		}
		if (const auto* value = std::get_if<bool>(&attribute->value)) {
			return static_cast<int64_t>(*value);
		}
		return defaultValue;
	};

	Outfit_t currentOutfit;
	currentOutfit.lookType = static_cast<uint16_t>(getAttribute("LookType"));
	currentOutfit.lookTypeEx = static_cast<uint16_t>(getAttribute("LookTypeEx"));
	currentOutfit.lookHead = static_cast<uint8_t>(getAttribute("LookHead"));
	currentOutfit.lookBody = static_cast<uint8_t>(getAttribute("LookBody"));
	currentOutfit.lookLegs = static_cast<uint8_t>(getAttribute("LookLegs"));
	currentOutfit.lookFeet = static_cast<uint8_t>(getAttribute("LookFeet"));
	currentOutfit.lookAddons = static_cast<uint8_t>(getAttribute("LookAddons"));

	const uint16_t currentRaceId = static_cast<uint16_t>(getAttribute("PodiumMonsterRaceId"));
	const bool bossPodium = itemId == 38707;

	std::map<uint16_t, const MonsterType*> availableMonsters;
	for (const auto& [_, monsterType] : g_monsters.monsters) {
		if (monsterType && monsterType->raceId > 0 && monsterType->raceId <= std::numeric_limits<uint16_t>::max()) {
			availableMonsters.insert_or_assign(static_cast<uint16_t>(monsterType->raceId), monsterType.get());
		}
	}

	NetworkMessage msg;
	msg.addByte(0xC2);
	msg.add<uint16_t>(currentOutfit.lookType);
	if (currentOutfit.lookType != 0) {
		msg.addByte(currentOutfit.lookHead);
		msg.addByte(currentOutfit.lookBody);
		msg.addByte(currentOutfit.lookLegs);
		msg.addByte(currentOutfit.lookFeet);
		msg.addByte(currentOutfit.lookAddons);
	} else {
		msg.add<uint16_t>(currentOutfit.lookTypeEx);
	}
	msg.add<uint16_t>(0);
	msg.addByte(bossPodium);
	const size_t monsterCount = std::min<size_t>(availableMonsters.size(), std::numeric_limits<uint16_t>::max());
	msg.add<uint16_t>(static_cast<uint16_t>(monsterCount));
	size_t sentMonsters = 0;
	for (const auto& [raceId, monsterType] : availableMonsters) {
		if (sentMonsters++ >= monsterCount) {
			break;
		}
		msg.add<uint16_t>(raceId);
		if (bossPodium) {
			msg.addString(monsterType->name);
			const Outfit_t& outfit = monsterType->info.outfit;
			msg.add<uint16_t>(outfit.lookType);
			if (outfit.lookType != 0) {
				msg.addByte(outfit.lookHead);
				msg.addByte(outfit.lookBody);
				msg.addByte(outfit.lookLegs);
				msg.addByte(outfit.lookFeet);
				msg.addByte(outfit.lookAddons);
			} else {
				msg.add<uint16_t>(outfit.lookTypeEx);
			}
		}
	}
	msg.addPosition(position);
	msg.add<uint16_t>(itemId);
	msg.addByte(stackPos);
	msg.addByte(static_cast<uint8_t>(getAttribute("LookDirection", DIRECTION_SOUTH)));
	msg.addByte(static_cast<uint8_t>(getAttribute("PodiumVisible", 1) != 0));
	msg.addByte(static_cast<uint8_t>(getAttribute("MonsterVisible", currentRaceId != 0) != 0));
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUpdatedVIPStatus(uint32_t guid, VipStatus_t newStatus)
{
	NetworkMessage msg;
	msg.addByte(newStatus == VIPSTATUS_ONLINE ? 0xD3 : 0xD4);
	msg.add<uint32_t>(guid);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendVIP(uint32_t guid, std::string_view name, VipStatus_t status)
{
	NetworkMessage msg;
	msg.addByte(0xD2);
	msg.add<uint32_t>(guid);
	msg.addString(name);
	msg.addByte(status);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendAnimatedText(std::string_view message, const Position& pos, TextColor_t color)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x84);
	msg.addPosition(pos);
	msg.addByte(color);
	msg.addString(message);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSpellCooldown(uint16_t spellId, uint32_t time)
{
	if (!isOTC || spellId > std::numeric_limits<uint8_t>::max()) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xA4);
	msg.addByte(static_cast<uint8_t>(spellId));
	msg.add<uint32_t>(time);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendSpellGroupCooldown(SpellGroup_t groupId, uint32_t time)
{
	if (!isOTC) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xA5);
	msg.addByte(groupId);
	msg.add<uint32_t>(time);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendUseItemCooldown(uint32_t time)
{
	if (!isOTC) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0xA6);
	msg.add<uint32_t>(time);
	writeToOutputBuffer(msg);
}

////////////// Add common messages
void ProtocolGame::AddCreature(NetworkMessage& msg, const Creature* creature, bool known, uint32_t remove)
{
	const Player* otherPlayer = creature->getPlayer();
	if (known) {
		msg.add<uint16_t>(0x62);
		msg.add<uint32_t>(creature->getID());
	} else {
		msg.add<uint16_t>(0x61);
		msg.add<uint32_t>(remove);
		msg.add<uint32_t>(creature->getID());
		if (creature->getMonster() && creature->getMonster()->getLevel() > 0) {
			msg.addString(creature->getName() + " [" + std::to_string(creature->getMonster()->getLevel()) + "]");
		} else {
			msg.addString(creature->getName());
		}
	}

	if (creature->isHealthHidden()) {
		msg.addByte(0x00);
	} else {
		msg.addByte(static_cast<uint8_t>(std::ceil(
		    (static_cast<double>(creature->getHealth()) / std::max<int32_t>(creature->getMaxHealth(), 1)) * 100)));
	}

	uint8_t direction = static_cast<uint8_t>(creature->getDirection());
	if (direction > 3) {
			direction = DIRECTION_SOUTH;
	}
	msg.addByte(direction);

	if (!creature->isInGhostMode() && !creature->isInvisible()) {
		AddOutfit(msg, creature->getCurrentOutfit());
	} else {
		static Outfit_t outfit;
		AddOutfit(msg, outfit);
	}

	LightInfo lightInfo = creature->getCreatureLight();
	if (getBoolean(ConfigManager::DEFAULT_WORLD_LIGHT)) {
		msg.addByte(player->isAccessPlayer() ? 0xFF : lightInfo.level);
	} else {
		msg.addByte(lightInfo.level);
	}
	msg.addByte(lightInfo.color);

	msg.add<uint16_t>(static_cast<uint16_t>(creature->getStepSpeed()));

	if (supportsCreatureIcons()) {
		AddCreatureIcon(msg, creature);
	}

	msg.addByte(player->getSkullClient(creature));
	msg.addByte(player->getPartyShield(otherPlayer));

	if (!known) {
		auto addCreatureEmblem = [this, &msg, creature](GuildEmblems_t emblem) {
			if (!isAstraClient) {
				if (emblem == GUILDEMBLEM_MEMBER) {
					emblem = GUILDEMBLEM_ALLY;
				} else if (emblem == GUILDEMBLEM_OTHER) {
					emblem = GUILDEMBLEM_NEUTRAL;
				}
			} else if (const Monster* monster = creature->getMonster(); monster && monster->isFamiliar()) {
				msg.addByte(GUILDEMBLEM_NONE);
				return;
			}
			msg.addByte(emblem);
		};

		if (otherPlayer) {
			addCreatureEmblem(player->getGuildEmblem(otherPlayer, isAstraClient));
		} else {
			if (creature->isSummon()) {
				auto master = creature->getMaster();
				if (master) {
					Player* masterPlayer = master->getPlayer();
					if (masterPlayer) {
						if (player.get() == masterPlayer) {
							addCreatureEmblem(GUILDEMBLEM_ALLY);
						} else {
							addCreatureEmblem(GUILDEMBLEM_ENEMY);
						}
					} else {
						addCreatureEmblem(creature->getEmblem());
					}
				} else {
					addCreatureEmblem(creature->getEmblem());
				}
			} else {
				addCreatureEmblem(creature->getEmblem());
			}
		}
	}

	if (isOTC) {
		if (const auto npc = creature->getNpc()) {
			msg.addByte(npc->getSpeechBubble());
		} else {
			msg.addByte(SPEECHBUBBLE_NONE);
		}
	}

	msg.addByte(player->canWalkthroughEx(creature) ? 0x00 : 0x01);
}

void ProtocolGame::AddPlayerStats(NetworkMessage& msg)
{
	msg.addByte(0xA0);

	uint32_t health = player->getHealth();
	uint32_t maxHealth = player->getMaxHealth();
	uint32_t mana = player->getMana();
	uint32_t maxMana = player->getMaxMana();

	if (shouldSendPercentStats(player.get())) {
		health = getStatPercent(health, maxHealth);
		maxHealth = 100;
		mana = getStatPercent(mana, maxMana);
		maxMana = 100;
	}

	msg.add<uint32_t>(health);
	msg.add<uint32_t>(maxHealth);

	msg.add<uint32_t>(player->hasFlag(PlayerFlag_HasInfiniteCapacity) ? 1000000 : player->getFreeCapacity());

	msg.add<uint32_t>(std::min<uint32_t>(player->getExperience(), std::numeric_limits<int32_t>::max()));

	msg.add<uint16_t>(static_cast<uint16_t>(player->getLevel()));
	msg.addByte(player->getLevelPercent());

	if (isAstraClient) {
		msg.add<uint16_t>(player->getBaseXpGain());
		msg.add<uint16_t>(0); // voucher XP boost
		msg.add<uint16_t>(player->getDisplayGrindingXpBoost());
		msg.add<uint16_t>(player->getDisplayXpBoostPercent());
		msg.add<uint16_t>(player->getStaminaXpBoost());
	}

	msg.add<uint32_t>(mana);
	msg.add<uint32_t>(maxMana);

	msg.addByte(static_cast<uint8_t>(std::min<uint32_t>(player->getMagicLevel(), std::numeric_limits<uint8_t>::max())));
	if (isOTC) {
		msg.addByte(
		    static_cast<uint8_t>(std::min<uint32_t>(player->getBaseMagicLevel(), std::numeric_limits<uint8_t>::max())));
	}
	msg.addByte(player->getMagicLevelPercent());

	msg.addByte(player->getSoul());

	msg.add<uint16_t>(player->getStaminaMinutes());

	if (isOTC) {
		msg.add<uint16_t>(player->getBaseSpeed() / 2);
		msg.add<uint16_t>(player->getOfflineTrainingTime() / 60 / 1000);
		if (isAstraClient) {
			msg.add<uint16_t>(player->getXpBoostTime());
			// 0x00 means boost is active and cannot be bought; 0x01 means the client may buy one.
			msg.addByte(player->getXpBoostTime() > 0 ? 0x00 : 0x01);
		}
	}

	/*msg.add<uint16_t>(player->getBaseSpeed() / 2);

	Condition* condition = player->getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT);
	msg.add<uint16_t>(condition ? condition->getTicks() / 1000 : 0x00);

	msg.add<uint16_t>(player->getOfflineTrainingTime() / 60 / 1000);

	msg.add<uint16_t>(0); // xp boost time (seconds)
	msg.addByte(0); // enables exp boost in the store
	*/
}

void ProtocolGame::AddPlayerSkills(NetworkMessage& msg)
{
	msg.addByte(0xA1);

	if (!isOTC) {
		for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
			msg.addByte(
			    std::min<uint8_t>(static_cast<uint8_t>(player->getSkillLevel(i)), std::numeric_limits<uint8_t>::max()));
			msg.addByte(player->getSkillPercent(i));
		}
	} else {
		for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
			msg.add<uint16_t>(std::min<uint16_t>(player->getSkillLevel(i), std::numeric_limits<uint16_t>::max()));
			msg.add<uint16_t>(player->getBaseSkill(i));
			msg.addByte(player->getSkillPercent(i));
		}

		for (uint8_t i = SPECIALSKILL_FIRST; i <= SPECIALSKILL_LAST; ++i) {
			msg.add<uint16_t>(std::min<uint16_t>(player->getSpecialSkill(i), 10000));
			msg.add<uint16_t>(0);
		}
	}
}

void ProtocolGame::AddOutfit(NetworkMessage& msg, const Outfit_t& outfit)
{
	msg.add<uint16_t>(outfit.lookType);

	if (outfit.lookType != 0) {
		msg.addByte(outfit.lookHead);
		msg.addByte(outfit.lookBody);
		msg.addByte(outfit.lookLegs);
		msg.addByte(outfit.lookFeet);
		msg.addByte(outfit.lookAddons);
	} else {
		msg.addItemId(outfit.lookTypeEx);
	}

	if (isOTC || getVersion() != 861) {
		msg.add<uint16_t>(outfit.lookMount);
	}
}

void ProtocolGame::AddWorldLight(NetworkMessage& msg, LightInfo lightInfo)
{
	msg.addByte(0x82);
	if (getBoolean(ConfigManager::DEFAULT_WORLD_LIGHT)) {
		msg.addByte(player->isAccessPlayer() ? 0xFF : lightInfo.level);
	} else {
		msg.addByte(lightInfo.level);
	}
	msg.addByte(lightInfo.color);
}

void ProtocolGame::AddCreatureLight(NetworkMessage& msg, const Creature* creature)
{
	LightInfo lightInfo = creature->getCreatureLight();

	msg.addByte(0x8D);
	msg.add<uint32_t>(creature->getID());
	if (getBoolean(ConfigManager::DEFAULT_WORLD_LIGHT)) {
		msg.addByte(player->isAccessPlayer() ? 0xFF : lightInfo.level);
	} else {
		msg.addByte(lightInfo.level);
	}
	msg.addByte(lightInfo.color);
}

// tile
void ProtocolGame::RemoveTileThing(NetworkMessage& msg, const Position& pos, uint32_t stackpos)
{
	if (stackpos >= MAX_STACKPOS_THINGS) {
		return;
	}

	msg.addByte(0x6C);
	msg.addPosition(pos);
	msg.addByte(static_cast<uint8_t>(stackpos));
}

void ProtocolGame::MoveUpCreature(NetworkMessage& msg, const Creature* creature, const Position& newPos,
                                  const Position& oldPos)
{
	if (creature != player.get() && !(spyActive_ && creature->getID() == spyTargetCreatureId_)) {
		return;
	}

	if (!creature || !creature->getTile()) {
		return;
	}

	// floor change up
	msg.addByte(0xBE);

	// going to surface
	if (newPos.z == 7) {
		int32_t skip = -1;

		// floor 7 and 6 already set
		for (int i = 5; i >= 0; --i) {
			GetFloorDescription(msg, oldPos.x - Map::maxClientViewportX, oldPos.y - Map::maxClientViewportY, i,
			                    (Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2, 8 - i, skip);
		}
		if (skip >= 0) {
			msg.addByte(static_cast<uint8_t>(skip));
			msg.addByte(0xFF);
		}
	}
	// underground, going one floor up (still underground)
	else if (newPos.z > 7) {
		int32_t skip = -1;
		GetFloorDescription(msg, oldPos.x - Map::maxClientViewportX, oldPos.y - Map::maxClientViewportY,
		                    oldPos.getZ() - 3, (Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2, 3,
		                    skip);

		if (skip >= 0) {
			msg.addByte(static_cast<uint8_t>(skip));
			msg.addByte(0xFF);
		}
	}

	// moving up a floor up makes us out of sync
	// west
	msg.addByte(0x68);
	GetMapDescription(oldPos.x - Map::maxClientViewportX, oldPos.y - (Map::maxClientViewportY - 1), newPos.z, 1,
	                  (Map::maxClientViewportY * 2) + 2, msg);

	// north
	msg.addByte(0x65);
	GetMapDescription(oldPos.x - Map::maxClientViewportX, oldPos.y - Map::maxClientViewportY, newPos.z,
	                  (Map::maxClientViewportX * 2) + 2, 1, msg);
}

void ProtocolGame::MoveDownCreature(NetworkMessage& msg, const Creature* creature, const Position& newPos,
                                    const Position& oldPos)
{
	if (creature != player.get() && !(spyActive_ && creature->getID() == spyTargetCreatureId_)) {
		return;
	}

	if (!creature || !creature->getTile()) {
		return;
	}

	// floor change down
	msg.addByte(0xBF);

	// going from surface to underground
	if (newPos.z == 8) {
		int32_t skip = -1;

		for (int i = 0; i < 3; ++i) {
			GetFloorDescription(msg, oldPos.x - Map::maxClientViewportX, oldPos.y - Map::maxClientViewportY,
			                    newPos.z + i, (Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2,
			                    -i - 1, skip);
		}
		if (skip >= 0) {
			msg.addByte(static_cast<uint8_t>(skip));
			msg.addByte(0xFF);
		}
	}
	// going further down
	else if (newPos.z > oldPos.z && newPos.z > 8 && newPos.z < 14) {
		int32_t skip = -1;
		GetFloorDescription(msg, oldPos.x - Map::maxClientViewportX, oldPos.y - Map::maxClientViewportY, newPos.z + 2,
		                    (Map::maxClientViewportX * 2) + 2, (Map::maxClientViewportY * 2) + 2, -3, skip);

		if (skip >= 0) {
			msg.addByte(static_cast<uint8_t>(skip));
			msg.addByte(0xFF);
		}
	}

	// moving down a floor makes us out of sync
	// east
	msg.addByte(0x66);
	GetMapDescription(oldPos.x + (Map::maxClientViewportX + 1), oldPos.y - (Map::maxClientViewportY + 1), newPos.z, 1,
	                  (Map::maxClientViewportY * 2) + 2, msg);

	// south
	msg.addByte(0x67);
	GetMapDescription(oldPos.x - Map::maxClientViewportX, oldPos.y + (Map::maxClientViewportY + 1), newPos.z,
	                  (Map::maxClientViewportX * 2) + 2, 1, msg);
}

void ProtocolGame::AddShopItem(NetworkMessage& msg, const ShopInfo& item)
{
	const ItemType& it = Item::items[item.itemId];
	msg.add<uint16_t>(it.id);

	if (it.isSplash() || it.isFluidContainer()) {
		msg.addByte(serverFluidToClient(static_cast<uint8_t>(item.subType)));
	} else {
		msg.addByte(0x00);
	}

	msg.addString(item.realName);
	msg.add<uint32_t>(it.weight);
	msg.add<uint32_t>(static_cast<uint32_t>(std::max<int64_t>(item.buyPrice, 0)));
	msg.add<uint32_t>(static_cast<uint32_t>(std::max<int64_t>(item.sellPrice, 0)));
}

void ProtocolGame::parseExtendedOpcode(NetworkMessage& msg)
{
	uint8_t opcode = msg.getByte();
	auto buffer = msg.getString();

	if (opcode == HELPER_OPCODE_CAST_ON_FOOT) {
		helperCastOnFootNextSay = buffer.empty() || isEnabledHelperBuffer(buffer);
		return;
	}

	const auto helperStateStorageKey = getHelperStateStorageKey(opcode);

	// process additional opcodes via lua script event
	if (helperStateStorageKey) {
		player->setStorageValue(*helperStateStorageKey,
		                        std::optional<int64_t>{isEnabledHelperBuffer(buffer) ? 1 : 0});
	}
	g_game.parsePlayerExtendedOpcode(player->getID(), opcode, buffer);
}

void ProtocolGame::sendNewPing(uint32_t pingId)
{
	// if (!isOTCv8) return;

	NetworkMessage msg;
	msg.addByte(0x40);
	msg.add<uint32_t>(pingId);
	writeToOutputBuffer(msg);
}

void ProtocolGame::sendExtendedOpcode(uint8_t opcode, std::string_view data)
{
	if (!isOTCv8 && !isOTC && !isAstraClient) {
		return;
	}

	if (data.size() > 65535) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x32);
	msg.addByte(opcode);
	msg.addString(data);
	writeToOutputBuffer(msg);
}

void ProtocolGame::parseNewPing(NetworkMessage& msg)
{
	uint32_t pingId = msg.get<uint32_t>();
	if (g_game.getGameState() == GAME_STATE_NORMAL && player) {
		sendNewPing(pingId);
	}
}

void ProtocolGame::parseCustomClientPing(NetworkMessage& msg)
{
	const uint32_t pingId = msg.get<uint32_t>();
	if (g_game.getGameState() == GAME_STATE_NORMAL && player) {
		g_game.playerReceivePing(player->getID());
		sendCustomClientPing(pingId);
	}
}

// OTCv8 and Mehah
void ProtocolGame::sendFeatures(bool advertiseAstraItemState)
{
	zoneWeatherFeatureEnabled = false;

	if (isMehah && !isOTCv8) {
		std::unordered_map<GameFeature, bool> features;
		features[GameFeature::ContainerPagination] = true;
		features[GameFeature::BrowseField] = true;
		features[GameFeature::ThingUpgradeClassification] = shouldSendThingUpgradeClassification();

		NetworkMessage msg;
		msg.addByte(0x43);
		msg.add<uint16_t>(features.size());
		for (auto& feature : features) {
			msg.addByte(static_cast<uint8_t>(feature.first));
			msg.addByte(feature.second ? 1 : 0);
		}
		writeToOutputBuffer(msg);
		return;
	}

	if (!isOTCv8 && !isAstraClient) return;

	std::unordered_map<GameFeature, bool> features;
	features[GameFeature::ExtendedOpcode] = true;
	features[GameFeature::SkillsBase] = true;
	features[GameFeature::PlayerMounts] = true;
	features[GameFeature::MagicEffectU16] = true;
	features[GameFeature::OfflineTrainingTime] = true;
	features[GameFeature::DoubleSkills] = true;
	features[GameFeature::BaseSkillU16] = true;
	features[GameFeature::AdditionalSkills] = true;
	features[GameFeature::ExtendedClientPing] = true;
	features[GameFeature::CreatureIcons] = true;
	features[GameFeature::ContainerPagination] = true;
	features[GameFeature::BrowseField] = true;
	if (isAstraClient) {
		features[GameFeature::ExperienceBonus] = true;
		features[GameFeature::PlayerFamiliars] = true;
		features[GameFeature::AstraCreatureIcons] = true;
		features[GameFeature::AstraQuiverCountU16] = true;
		features[GameFeature::AstraOutfitStoreMode] = true;
	}
	if (supportsNativeZoneWeather()) {
		features[GameFeature::ZoneWeather] = true;
		zoneWeatherFeatureEnabled = true;
	}
	if (advertiseAstraItemState && isAstraClient && getBoolean(ConfigManager::ASTRA_ITEM_STATE_ENABLED)) {
		features[GameFeature::DisplayItemDuration] = true;
		features[GameFeature::DisplayItemCharges] = true;
		features[GameFeature::PackedPlayerInventory] = true;
		features[GameFeature::AstraItemMetadata] = true;
	}
	features[GameFeature::QuickLootFlags] = shouldSendQuickLootFlags();
	features[GameFeature::ThingUpgradeClassification] = shouldSendThingUpgradeClassification();
	features[GameFeature::ItemTierByte] = shouldSendItemTierByte();

	if (features.empty()) return;

	NetworkMessage msg;
	msg.addByte(0x43);
	msg.add<uint16_t>(features.size());
	for (auto& feature : features) {
		msg.addByte(static_cast<uint8_t>(feature.first));
		msg.addByte(feature.second ? 1 : 0);
	}
	writeToOutputBuffer(msg);
}

void ProtocolGame::spectatorTurn(uint8_t direction)
{
	std::vector<std::string> candidates;
	candidates.reserve(32);

	for (const auto& player : g_game.getPlayers()) {
		if (player->isRemoved() || !player->client->protocol())
			continue;

		if (!player->client->isBroadcasting())
			continue;

		if (!player->client->password().empty())
			continue;

		if (player->client->isBanned(getIP()))
			continue;

		candidates.push_back(player->getName());
	}

	int index = 0;
	std::ranges::sort(candidates);
	for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
		if (candidates[i] == player->getName()) {
			index = i;
			break;
		}
	}

	if (candidates.size() < 2) {
		return;
	}

	int dir = 0;
	if (direction == 0 || direction == 1) {
		dir = 1;
	}
	if (direction == 2 || direction == 3) {
		dir = -1;
	}
	if (index == 0 && dir == -1) {
		dir = 0;
	}

	auto _player = g_game.getPlayerByName(candidates[(index + dir) % candidates.size()]);
	if (!_player || player.get() == _player.get()) {
		return;
	}

	const auto& openedContainers = player->getOpenContainers();
	for (const auto& it : openedContainers) {
		sendCloseContainer(it.first);
	}

	player->client->removeSpectator(getThis());
	player = _player;

	knownCreatureSet.clear();
	sendAddCreature(player.get(), player->getPosition(), 0, CONST_ME_NONE);
	sendCastChannel();
	syncOpenContainers();

	player->client->addSpectator(getThis());
}

void ProtocolGame::parseSpectatorSay(NetworkMessage& msg)
{
	std::string receiver;
	uint16_t channelId;

	SpeakClasses type = static_cast<SpeakClasses>(msg.getByte());
	switch (type) {
		case TALKTYPE_PRIVATE:
		case TALKTYPE_PRIVATE_RED:
			receiver = msg.getString();
			channelId = 0;
			break;

		case TALKTYPE_CHANNEL_Y:
		case TALKTYPE_CHANNEL_R1:
		case TALKTYPE_CHANNEL_R2:
			channelId = msg.get<uint16_t>();
			break;

		default:
			channelId = 0;
			break;
	}

	const std::string text(msg.getString());
	if (text.length() > 255) {
		return;
	}

	spectatorSay(text, channelId);
}

void ProtocolGame::spectatorSay(const std::string text, uint16_t channelId)
{
	if (channelId != CHANNEL_CAST || !player || !player->client) {
		return;
	}

	player->client->spectatorSay(getThis(), text);
}

void ProtocolGame::sendCastChannel()
{
	sendChannel(CHANNEL_CAST, "Cast Channel");
}

bool ProtocolGame::canProcessCastSwitch()
{
	const int64_t now = OTSYS_TIME();
	if (now < nextCastSwitchTime) {
		if (now >= nextCastSwitchCooldownMessageTime) {
			sendTextMessage(MESSAGE_STATUS_SMALL, "You are switching casts too fast. Please wait before switching again.");
			nextCastSwitchCooldownMessageTime = nextCastSwitchTime;
		}
		return false;
	}

	nextCastSwitchTime = now + CAST_SWITCH_COOLDOWN_MS;
	nextCastSwitchCooldownMessageTime = 0;
	return true;
}

bool ProtocolGame::shouldResyncCastChannelOnSwitch() const
{
	if (isOTC) {
		return false;
	}

	return !isOtclientOperatingSystem(clientOperatingSystem);
}

void ProtocolGame::syncOpenContainers()
{
	const auto& openContainers = player->getOpenContainers();
	for (const auto& it : openContainers) {
		auto openContainer = it.second;
		auto container = openContainer.container.lock();
		if (!container) {
			continue;
		}
		bool hasParent = (dynamic_cast<const Container*>(container->getParent()) != nullptr);
		sendContainer(it.first, container.get(), hasParent, openContainer.index);
	}
}

void ProtocolGame::sendWelcomeMessage()
{
	std::string message = "Welcome to the Live Cast System!\n\n"
		"Do you know you can use CTRL + ARROWS to switch casts?\n\n"
		"Voce sabia que pode usar CTRL + SETAS para alternar casts?\n\n"
		"Type /commands in the cast channel to see available commands.";
	TextMessage textMessage(MESSAGE_EVENT_ADVANCE, message);
	sendTextMessage(textMessage);
}

void ProtocolGame::parseSwitchCast(uint8_t direction)
{
	if (!player || !player->client) {
		return;
	}

	std::vector<Player*> casters = g_game.getLiveCasters("");
	if (casters.empty()) {
		sendTextMessage(MESSAGE_STATUS_SMALL, "No live casts available.");
		return;
	}

	auto it = std::find(casters.begin(), casters.end(), player.get());
	if (it == casters.end()) {
		if (!casters.empty()) {
			Player* newCaster = casters[0];
			if (newCaster && newCaster != player.get()) {
				player->client->removeSpectator(getThis());
				player->client->sendCastMessage(spectator_name, spectator_name + " has left the cast.", TALKTYPE_CHANNEL_O);
					knownCreatureSet.clear();
				player = g_game.getCreatureSharedRef<Player>(newCaster);
				player->client->addSpectator(getThis());
				sendAddCreature(player.get(), player->getPosition(), 0, CONST_ME_NONE);
				syncOpenContainers();
				if (shouldResyncCastChannelOnSwitch()) {
					sendCastChannel();
				}
				player->client->sendCastMessage(spectator_name, spectator_name + " has joined the cast.", TALKTYPE_CHANNEL_O);
				sendMagicEffect(player->getPosition(), CONST_ME_TELEPORT);
			}
		}
		return;
	}

	size_t currentIndex = std::distance(casters.begin(), it);
	size_t newIndex;
	if (direction == 1) {
		newIndex = (currentIndex + 1) % casters.size();
	} else {
		newIndex = (currentIndex == 0) ? casters.size() - 1 : currentIndex - 1;
	}

	if (newIndex == currentIndex) {
		sendTextMessage(MESSAGE_STATUS_SMALL, "No other casts available.");
		return;
	}

	Player* newCaster = casters[newIndex];
	if (!newCaster || newCaster == player.get()) {
		return;
	}

	player->client->removeSpectator(getThis());
	player->client->sendCastMessage(spectator_name, spectator_name + " has left the cast.", TALKTYPE_CHANNEL_O);
	knownCreatureSet.clear();
	player = g_game.getCreatureSharedRef<Player>(newCaster);
	player->client->addSpectator(getThis());
	sendAddCreature(player.get(), player->getPosition(), 0, CONST_ME_NONE);
	syncOpenContainers();
	if (shouldResyncCastChannelOnSwitch()) {
		sendCastChannel();
	}
	player->client->sendCastMessage(spectator_name, spectator_name + " has joined the cast.", TALKTYPE_CHANNEL_O);
	sendMagicEffect(player->getPosition(), CONST_ME_TELEPORT);

	std::stringstream ss;
	ss << "Switched to cast: " << player->getName();
	sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, ss.str());
}

void ProtocolGame::parseImbuementDurations(NetworkMessage& msg)
{
	const bool open = msg.getByte() != 0;
	imbuementTrackerOpen = open;
	if (open) {
		sendImbuementDurations();
	}
}

void ProtocolGame::sendImbuementDurations(slots_t updatedSlot, const Item* updatedItem)
{
	if (!player || !imbuementTrackerOpen) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x5D); // GameServerImbuementDurations = 93

	const slots_t slots[] = {
		CONST_SLOT_HEAD,
		CONST_SLOT_BACKPACK,
		CONST_SLOT_ARMOR,
		CONST_SLOT_RIGHT,
		CONST_SLOT_LEFT,
		CONST_SLOT_FEET
	};

	std::vector<std::pair<slots_t, const Item*>> trackedItems;
	for (slots_t slot : slots) {
		const Item* item = (slot == updatedSlot) ? updatedItem : player->getInventoryItem(slot);
		if (item && item->getImbuementSlots() > 0) {
			trackedItems.push_back({slot, item});
		}
	}

	msg.addByte(static_cast<uint8_t>(trackedItems.size()));

	for (const auto& p : trackedItems) {
		slots_t slot = p.first;
		const Item* item = p.second;

		msg.addByte(static_cast<uint8_t>(slot));
		msg.addItem(item, shouldSendItemTierData(), shouldSendItemTierByte(), isOTC, shouldSendQuickLootFlags(),
		            canSendAstraItemState(), shouldSendAstraQuiverCountU16());

		uint16_t totalSlots = item->getImbuementSlots();
		msg.addByte(static_cast<uint8_t>(totalSlots));

		const auto& imbuements = const_cast<Item*>(item)->getImbuements();
		for (uint16_t slotIndex = 0; slotIndex < totalSlots; ++slotIndex) {
			if (slotIndex < imbuements.size()) {
				const auto& imb = imbuements[slotIndex];
				msg.addByte(1); // slotImbued = true

				// Find definition
				const ImbuementDefinition* def = nullptr;
				for (const auto& d : Imbuements::getInstance().getDefinitions()) {
					if (d.imbuementType == imb->imbuetype && d.baseId == imb->baseId) {
						def = &d;
						break;
					}
				}

				if (def) {
					msg.addString(def->name);
					msg.add<uint16_t>(def->iconId);
				} else {
					msg.addString("Imbuement");
					msg.add<uint16_t>(0);
				}

				msg.add<uint32_t>(imb->duration);
				msg.addByte(1); // state: decaying
			} else {
				msg.addByte(0); // slotImbued = false
			}
		}
	}

	writeToOutputBuffer(msg);
}
