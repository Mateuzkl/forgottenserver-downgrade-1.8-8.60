// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "botmanager.h"

#include "character_bazaar.h"
#include "database.h"
#include "game.h"
#include "iologindata.h"
#include "logger.h"
#include "player.h"
#include "save_manager.h"
#include "tasks.h"
#include "tools.h"

#include <charconv>
#include <ctime>

extern Game g_game;
extern Dispatcher g_dispatcher;

namespace {
class LoginReservationGuard
{
public:
	explicit LoginReservationGuard(uint32_t guid) : guid(guid), active(g_game.reserveLogin(guid)) {}
	~LoginReservationGuard()
	{
		if (active) {
			g_game.releaseLogin(guid);
		}
	}

	LoginReservationGuard(const LoginReservationGuard&) = delete;
	LoginReservationGuard& operator=(const LoginReservationGuard&) = delete;

	bool ownsReservation() const { return active; }

private:
	uint32_t guid = 0;
	bool active = false;
};

std::optional<uint32_t> parseGuid(std::string_view text)
{
	if (text.empty()) {
		return std::nullopt;
	}

	uint32_t guid = 0;
	const char* first = text.data();
	const char* last = text.data() + text.size();
	const auto [ptr, ec] = std::from_chars(first, last, guid);
	if (ec == std::errc() && ptr == last && guid != 0) {
		return guid;
	}
	return std::nullopt;
}
} // namespace

BotManager& BotManager::getInstance()
{
	static BotManager instance;
	return instance;
}

bool BotManager::ensureTables()
{
	Database& db = Database::getInstance();
	return db.executeQuery(
	    "CREATE TABLE IF NOT EXISTS `bot_players` ("
	    "`player_id` int NOT NULL,"
	    "`enabled` tinyint NOT NULL DEFAULT '1',"
	    "`auto_spawn` tinyint NOT NULL DEFAULT '0',"
	    "`last_spawn` bigint unsigned NOT NULL DEFAULT '0',"
	    "`last_despawn` bigint unsigned NOT NULL DEFAULT '0',"
	    "`created_at` bigint unsigned NOT NULL DEFAULT '0',"
	    "`updated_at` bigint unsigned NOT NULL DEFAULT '0',"
	    "PRIMARY KEY (`player_id`),"
	    "KEY `idx_bot_players_auto_spawn` (`enabled`, `auto_spawn`),"
	    "CONSTRAINT `fk_bot_players_player` FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE"
	    ") ENGINE=InnoDB DEFAULT CHARACTER SET=utf8");
}

std::optional<uint32_t> BotManager::getGuidByName(std::string_view name) const
{
	std::string trimmed{name};
	trimString(trimmed);
	if (trimmed.empty()) {
		return std::nullopt;
	}

	if (auto guid = parseGuid(trimmed)) {
		return guid;
	}

	Database& db = Database::getInstance();
	DBResult_ptr result = db.storeQuery(fmt::format(
	    "SELECT `id` FROM `players` WHERE LOWER(`name`) = LOWER({:s}) LIMIT 1", db.escapeString(trimmed)));
	if (!result) {
		return std::nullopt;
	}
	return result->getNumber<uint32_t>("id");
}

bool BotManager::isMarkedBot(uint32_t guid, bool* enabled /* = nullptr */)
{
	if (enabled) {
		*enabled = false;
	}

	if (guid == 0 || !ensureTables()) {
		return false;
	}

	Database& db = Database::getInstance();
	DBResult_ptr result =
	    db.storeQuery(fmt::format("SELECT `enabled` FROM `bot_players` WHERE `player_id` = {:d} LIMIT 1", guid));
	if (!result) {
		return false;
	}

	const bool isEnabled = result->getNumber<uint16_t>("enabled") != 0;
	if (enabled) {
		*enabled = isEnabled;
	}
	return true;
}

BotManager::Result BotManager::spawnByName(std::string_view name, bool broadcast /* = false */,
                                           bool requireMarked /* = true */)
{
	auto guid = getGuidByName(name);
	if (!guid) {
		return { false, fmt::format("Bot player '{}' was not found.", name), nullptr };
	}
	return spawnByGuid(*guid, broadcast, requireMarked);
}

BotManager::Result BotManager::spawnByGuid(uint32_t guid, bool broadcast /* = false */,
                                           bool requireMarked /* = true */)
{
	if (!g_dispatcher.isDispatcherThread()) {
		return { false, "Bot spawn must run on the dispatcher thread.", nullptr };
	}

	cleanupRemoved();

	if (guid == 0) {
		return { false, "Invalid bot player id.", nullptr };
	}

	bool enabled = false;
	const bool marked = isMarkedBot(guid, &enabled);
	if (requireMarked && !marked) {
		return { false, fmt::format("Player {:d} is not registered in bot_players.", guid), nullptr };
	}
	if (marked && !enabled) {
		return { false, fmt::format("Bot player {:d} is disabled.", guid), nullptr };
	}

	if (CharacterBazaar::isPlayerOnActiveAuction(guid)) {
		return { false, "This character is listed on the Character Bazaar.", nullptr };
	}

	if (auto existing = g_game.getPlayerByGUID(guid)) {
		if (existing->isBot() && isManagedBot(guid)) {
			return { true, fmt::format("Bot '{}' is already online.", existing->getName()), existing };
		}
		return { false, fmt::format("Player '{}' is already online.", existing->getName()), nullptr };
	}

	if (g_saveManager.hasFailedRecovery(guid)) {
		return { false, "This character has failed save recovery and cannot be loaded.", nullptr };
	}

	LoginReservationGuard reservation(guid);
	if (!reservation.ownsReservation()) {
		return { false, "This character is already logging in.", nullptr };
	}

	auto player = std::make_shared<Player>(nullptr);
	player->setGUID(guid);
	player->setID();
	player->setBot(true);

	if (!IOLoginData::loadPlayerById(player.get(), guid, true)) {
		return { false, fmt::format("Could not load bot player {:d}.", guid), nullptr };
	}
	if (player->getName() == "Account Manager") {
		return { false, "Account Manager cannot be spawned as a bot.", nullptr };
	}
	IOLoginData::loadPlayerWorldData(player.get());
	player->setBot(true);
	player->client->setBroadcast(broadcast);

	activeBots[guid] = player;
	const Position loginPosition = player->getLoginPosition();
	if (!g_game.placeCreature(player.get(), loginPosition)) {
		if (!g_game.placeCreature(player.get(), player->getTemplePosition(), false, true)) {
			activeBots.erase(guid);
			player->setBot(false);
			return { false, fmt::format("Could not place bot '{}' at login or temple position.", player->getName()),
				     nullptr };
		}
	}

	player->resetIdleTime();
	touchRuntime(guid, true);
	LOG_INFO(fmt::format("[BotManager] Spawned bot '{}' (guid={}, cast={})", player->getName(), guid,
	                     broadcast ? "on" : "off"));
	return { true, fmt::format("Bot '{}' is online.", player->getName()), player };
}

bool BotManager::despawnByName(std::string_view name, bool save, std::string* message /* = nullptr */)
{
	auto guid = getGuidByName(name);
	if (!guid) {
		if (message) {
			*message = fmt::format("Bot player '{}' was not found.", name);
		}
		return false;
	}
	return despawnByGuid(*guid, save, message);
}

bool BotManager::despawnByGuid(uint32_t guid, bool save, std::string* message /* = nullptr */)
{
	if (!g_dispatcher.isDispatcherThread()) {
		if (message) {
			*message = "Bot despawn must run on the dispatcher thread.";
		}
		return false;
	}

	cleanupRemoved();

	auto it = activeBots.find(guid);
	if (it == activeBots.end()) {
		if (message) {
			*message = fmt::format("Bot {:d} is not online.", guid);
		}
		return false;
	}

	std::shared_ptr<Player> player = it->second;
	const std::string name = player ? player->getName() : std::to_string(guid);
	if (player && !player->isRemoved()) {
		const bool oldSaveFlag = player->getSaveFlag();
		if (!save) {
			player->setSaveFlag(false);
		}
		player->client->clear();
		g_game.removeCreature(player.get(), true);
		player->setSaveFlag(oldSaveFlag);
	}

	activeBots.erase(guid);
	touchRuntime(guid, false);
	if (message) {
		*message = fmt::format("Bot '{}' is offline.", name);
	}
	LOG_INFO(fmt::format("[BotManager] Despawned bot '{}' (guid={}, save={})", name, guid, save ? "yes" : "no"));
	return true;
}

size_t BotManager::despawnAll(bool save)
{
	std::vector<uint32_t> guids;
	guids.reserve(activeBots.size());
	for (const auto& [guid, player] : activeBots) {
		if (player && !player->isRemoved()) {
			guids.push_back(guid);
		}
	}

	size_t count = 0;
	for (uint32_t guid : guids) {
		if (despawnByGuid(guid, save)) {
			++count;
		}
	}
	cleanupRemoved();
	return count;
}

bool BotManager::setBroadcast(uint32_t guid, bool enabled, std::string* message /* = nullptr */)
{
	cleanupRemoved();

	auto it = activeBots.find(guid);
	if (it == activeBots.end() || !it->second || it->second->isRemoved()) {
		if (message) {
			*message = fmt::format("Bot {:d} is not online.", guid);
		}
		return false;
	}

	auto& player = it->second;
	player->client->setBroadcast(enabled);
	IOLoginData::updateOnlineStatus(guid, true, player->client->isBroadcasting(), player->client->password(),
	                                player->client->description(), player->client->spectatorList().size());
	if (message) {
		*message = fmt::format("Bot '{}' cast is {}.", player->getName(), enabled ? "on" : "off");
	}
	return true;
}

bool BotManager::setBroadcast(std::string_view name, bool enabled, std::string* message /* = nullptr */)
{
	auto guid = getGuidByName(name);
	if (!guid) {
		if (message) {
			*message = fmt::format("Bot player '{}' was not found.", name);
		}
		return false;
	}
	return setBroadcast(*guid, enabled, message);
}

bool BotManager::isManagedBot(uint32_t guid) const
{
	auto it = activeBots.find(guid);
	return it != activeBots.end() && it->second && !it->second->isRemoved();
}

std::vector<std::shared_ptr<Player>> BotManager::getActiveBots() const
{
	std::vector<std::shared_ptr<Player>> bots;
	bots.reserve(activeBots.size());
	for (const auto& [guid, player] : activeBots) {
		if (player && !player->isRemoved()) {
			bots.push_back(player);
		}
	}
	return bots;
}

void BotManager::forget(uint32_t guid)
{
	auto it = activeBots.find(guid);
	if (it != activeBots.end()) {
		activeBots.erase(it);
		touchRuntime(guid, false);
	}
}

void BotManager::cleanupRemoved()
{
	for (auto it = activeBots.begin(); it != activeBots.end();) {
		if (!it->second || it->second->isRemoved()) {
			const uint32_t guid = it->first;
			it = activeBots.erase(it);
			touchRuntime(guid, false);
		} else {
			++it;
		}
	}
}

void BotManager::touchRuntime(uint32_t guid, bool spawned)
{
	if (guid == 0 || !ensureTables()) {
		return;
	}

	const std::time_t now = std::time(nullptr);
	const char* column = spawned ? "last_spawn" : "last_despawn";
	Database& db = Database::getInstance();
	db.executeQuery(fmt::format("UPDATE `bot_players` SET `{:s}` = {:d}, `updated_at` = {:d} WHERE `player_id` = {:d}",
	                            column, static_cast<uint64_t>(now), static_cast<uint64_t>(now), guid));
}
