// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "botmanager.h"

#include "character_bazaar.h"
#include "configmanager.h"
#include "database.h"
#include "databasetasks.h"
#include "game.h"
#include "iologindata.h"
#include "logger.h"
#include "player.h"
#include "save_manager.h"
#include "tasks.h"
#include "tools.h"
#include "vocation.h"

#include <charconv>
#include <ctime>

extern Game g_game;
extern Dispatcher g_dispatcher;
extern DatabaseTasks g_databaseTasks;
extern Vocations g_vocations;

namespace tfs::bot {

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

} // namespace tfs::bot

namespace {
constexpr std::string_view BOT_ACCOUNT_NAME = "botaccount";
constexpr std::string_view LEGACY_BOT_ACCOUNT_PASSWORD_HASH = "3494b552958a9e81766feca7ffd85f800dd0bfc4";

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

std::optional<uint32_t> getAccountIdByName(std::string_view name)
{
	Database& db = Database::getInstance();
	DBResult_ptr result = db.storeQuery(fmt::format(
	    "SELECT `id` FROM `accounts` WHERE LOWER(`name`) = LOWER({:s}) LIMIT 1", db.escapeString(name)));
	if (!result) {
		return std::nullopt;
	}
	return result->getNumber<uint32_t>("id");
}

void rotateLegacyBotAccountPassword(uint32_t accountId)
{
	Database& db = Database::getInstance();
	DBResult_ptr result = db.storeQuery(
	    fmt::format("SELECT `password` FROM `accounts` WHERE `id` = {:d} LIMIT 1", accountId));
	if (!result) {
		return;
	}

	if (result->getString("password") == LEGACY_BOT_ACCOUNT_PASSWORD_HASH) {
		IOLoginData::setPassword(accountId, generateSecurePassword(32));
	}
}

std::optional<uint32_t> ensureBotAccount(std::string* message)
{
	if (auto accountId = getAccountIdByName(BOT_ACCOUNT_NAME)) {
		rotateLegacyBotAccountPassword(*accountId);
		return accountId;
	}

	uint32_t accountId = 0;
	if (IOLoginData::createAccount(std::string{BOT_ACCOUNT_NAME}, generateSecurePassword(32), accountId)) {
		return accountId;
	}

	if (auto accountIdAfterRace = getAccountIdByName(BOT_ACCOUNT_NAME)) {
		rotateLegacyBotAccountPassword(*accountIdAfterRace);
		return accountIdAfterRace;
	}

	if (message) {
		*message = "Could not create or load bot account.";
	}
	return std::nullopt;
}

uint16_t normalizeBotVocation(uint16_t vocationId)
{
	if (g_vocations.getVocation(vocationId)) {
		return vocationId;
	}
	if (g_vocations.getVocation(4)) {
		return 4;
	}
	return 0;
}

PlayerSex_t normalizeBotSex(uint16_t sex)
{
	return sex == PLAYERSEX_FEMALE ? PLAYERSEX_FEMALE : PLAYERSEX_MALE;
}
} // namespace

BotManager& BotManager::getInstance()
{
	static BotManager instance;
	return instance;
}

bool BotManager::isEnabled() const
{
	return ConfigManager::getBoolean(ConfigManager::BOT_SYSTEM_ENABLED);
}

bool BotManager::ensureTables()
{
	if (tablesReady) {
		return true;
	}

	Database& db = Database::getInstance();
	const bool ok = db.executeQuery(
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
	if (ok) {
		tablesReady = true;
	}
	return ok;
}

std::optional<uint32_t> BotManager::getGuidByName(std::string_view name) const
{
	std::string trimmed{name};
	trimString(trimmed);
	if (trimmed.empty()) {
		return std::nullopt;
	}

	if (auto guid = tfs::bot::parseGuid(trimmed)) {
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

BotManager::RegistrationResult BotManager::registerByName(std::string_view rawName, bool autoSpawn /* = false */,
                                                          bool createIfMissing /* = true */,
                                                          uint16_t vocationId /* = 4 */, uint16_t sex /* = 1 */)
{
	if (!isEnabled()) {
		return { false, false, 0, "", "Bot system is disabled in config.lua (botSystemEnabled = false)." };
	}

	if (!ensureTables()) {
		return { false, false, 0, "", "Could not create bot tables." };
	}

	std::string name{rawName};
	trimString(name);
	if (!validateAndFormatPlayerName(name)) {
		return { false, false, 0, name, "Invalid player name." };
	}

	auto guid = getGuidByName(name);
	bool created = false;
	if (!guid) {
		if (!createIfMissing) {
			return { false, false, 0, name, fmt::format("Player '{}' was not found.", name) };
		}

		std::string accountMessage;
		auto accountId = ensureBotAccount(&accountMessage);
		if (!accountId) {
			return { false, false, 0, name, accountMessage };
		}

		const uint16_t normalizedVocationId = normalizeBotVocation(vocationId);
		if (!IOLoginData::createPlayer(*accountId, name, normalizedVocationId, normalizeBotSex(sex))) {
			return { false, false, 0, name, fmt::format("Could not create bot player '{}'.", name) };
		}

		guid = getGuidByName(name);
		if (!guid) {
			return { false, true, 0, name, fmt::format("Bot player '{}' was created but could not be loaded.", name) };
		}
		created = true;
	}

	Database& db = Database::getInstance();
	const std::time_t now = std::time(nullptr);
	const bool registered = db.executeQuery(fmt::format(
	    "INSERT INTO `bot_players` (`player_id`, `enabled`, `auto_spawn`, `created_at`, `updated_at`) "
	    "VALUES ({:d}, 1, {:d}, {:d}, {:d}) "
	    "ON DUPLICATE KEY UPDATE `enabled` = 1, `auto_spawn` = VALUES(`auto_spawn`), "
	    "`updated_at` = VALUES(`updated_at`)",
	    *guid, autoSpawn ? 1 : 0, static_cast<uint64_t>(now), static_cast<uint64_t>(now)));
	if (!registered) {
		return { false, created, *guid, name, fmt::format("Could not register bot '{}'.", name) };
	}

	return { true, created, *guid, name,
		     fmt::format("Bot '{}' {}registered{}.", name, created ? "created and " : "",
		                 autoSpawn ? " with auto-spawn" : "") };
}

std::optional<BotManager::MarkedState> BotManager::isMarkedBot(uint32_t guid)
{
	if (guid == 0) {
		return MarkedState{};
	}

	if (!ensureTables()) {
		return std::nullopt;
	}

	// COUNT(*) always yields exactly one row, so a null result unambiguously
	// means a database error rather than "no such registration" (storeQuery
	// returns null both on error and on an empty result set).
	Database& db = Database::getInstance();
	DBResult_ptr result = db.storeQuery(fmt::format(
	    "SELECT COUNT(*) AS `c`, COALESCE(MAX(`enabled`), 0) AS `e` FROM `bot_players` WHERE `player_id` = {:d}",
	    guid));
	if (!result) {
		return std::nullopt;
	}

	MarkedState state;
	state.marked = result->getNumber<uint64_t>("c") > 0;
	state.enabled = state.marked && result->getNumber<uint16_t>("e") != 0;
	return state;
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
	if (!isEnabled()) {
		return { false, "Bot system is disabled in config.lua (botSystemEnabled = false).", nullptr };
	}

	if (!g_dispatcher.isDispatcherThread()) {
		return { false, "Bot spawn must run on the dispatcher thread.", nullptr };
	}

	sweepRemoved();

	if (guid == 0) {
		return { false, "Invalid bot player id.", nullptr };
	}

	const auto marked = isMarkedBot(guid);
	if (!marked) {
		return { false, "Bot registry is unavailable (database error).", nullptr };
	}
	if (requireMarked && !marked->marked) {
		return { false, fmt::format("Player {:d} is not registered in bot_players.", guid), nullptr };
	}
	if (marked->marked && !marked->enabled) {
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

	// The local shared_ptr keeps the player alive through placement; the
	// registry entry (and the tile) take over ownership only on success, so
	// the failure path needs no rollback.
	const Position loginPosition = player->getLoginPosition();
	if (!g_game.placeCreature(player.get(), loginPosition)) {
		if (!g_game.placeCreature(player.get(), player->getTemplePosition(), false, true)) {
			return { false, fmt::format("Could not place bot '{}' at login or temple position.", player->getName()),
				     nullptr };
		}
	}

	player->client->setBroadcast(broadcast);
	registry.insert(guid, player);
	player->resetIdleTime();
	touchRuntimeAsync(guid, true);
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

	sweepRemoved();

	// Keep the bot alive through removal; forget() is triggered exactly once
	// by Player::onRemoveCreature and is the single bookkeeping sink.
	std::shared_ptr<Player> player = registry.find(guid);
	if (!player) {
		if (message) {
			*message = fmt::format("Bot {:d} is not online.", guid);
		}
		return false;
	}

	const std::string name = player->getName();
	if (!save) {
		player->setSaveFlag(false);
	}
	player->client->clear();
	g_game.removeCreature(player.get(), true);
	if (registry.erase(guid)) {
		// removeCreature runs onRemoveCreature synchronously, so this only
		// fires if the removal path skipped forget(); keep bookkeeping tight.
		touchRuntimeAsync(guid, false);
	}

	if (message) {
		*message = fmt::format("Bot '{}' is offline.", name);
	}
	LOG_INFO(fmt::format("[BotManager] Despawned bot '{}' (guid={}, save={})", name, guid, save ? "yes" : "no"));
	return true;
}

size_t BotManager::despawnAll(bool save)
{
	size_t count = 0;
	for (uint32_t guid : registry.guids()) {
		if (despawnByGuid(guid, save)) {
			++count;
		}
	}
	sweepRemoved();
	return count;
}

bool BotManager::setBroadcast(uint32_t guid, bool enabled, std::string* message /* = nullptr */)
{
	if (!isEnabled()) {
		if (message) {
			*message = "Bot system is disabled in config.lua (botSystemEnabled = false).";
		}
		return false;
	}

	sweepRemoved();

	auto player = registry.find(guid);
	if (!player) {
		if (message) {
			*message = fmt::format("Bot {:d} is not online.", guid);
		}
		return false;
	}

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

bool BotManager::isManagedBot(uint32_t guid) const { return registry.contains(guid); }

std::vector<std::shared_ptr<Player>> BotManager::getActiveBots() const { return registry.snapshot(); }

void BotManager::forget(uint32_t guid)
{
	if (registry.erase(guid)) {
		touchRuntimeAsync(guid, false);
	}
}

void BotManager::sweepRemoved()
{
	for (uint32_t guid : registry.sweepRemoved()) {
		touchRuntimeAsync(guid, false);
	}
}

void BotManager::touchRuntimeAsync(uint32_t guid, bool spawned)
{
	if (guid == 0 || !ensureTables()) {
		return;
	}

	// Telemetry only; nothing reads these back in-process. DatabaseTasks is a
	// single FIFO worker and its shutdown() flushes the queue, so ordering and
	// shutdown persistence hold.
	const std::time_t now = std::time(nullptr);
	const char* column = spawned ? "last_spawn" : "last_despawn";
	g_databaseTasks.addTask(
	    fmt::format("UPDATE `bot_players` SET `{:s}` = {:d}, `updated_at` = {:d} WHERE `player_id` = {:d}", column,
	                static_cast<uint64_t>(now), static_cast<uint64_t>(now), guid));
}
