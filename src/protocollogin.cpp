// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "protocollogin.h"

#include "astraclient.h"
#include "ban.h"
#include "configmanager.h"
#include "database.h"
#include "game.h"
#include "iologindata.h"
#include "monsters.h"
#include "outputmessage.h"
#include "tasks.h"
#include "tools.h"
#include "vocation.h"

#include <ctime>
#include <fmt/format.h>
#include <limits>
#include <vector>

extern Game g_game;
extern Monsters g_monsters;
extern Vocations g_vocations;

namespace {

constexpr uint8_t ASTRA_LOGIN_BOOSTED_INFO_MARKER = 0xA1;

struct LoginBoostedEntry {
	uint32_t raceId = 0;
	std::string name;
	Outfit_t outfit;

	bool isValid() const
	{
		return raceId != 0 && !name.empty() && outfit.lookType != 0;
	}
};

LoginBoostedEntry getBoostedCreatureLoginEntry()
{
	LoginBoostedEntry entry;
	const auto& boostedCreatureName = g_game.getBoostedCreature();
	if (boostedCreatureName.empty()) {
		return entry;
	}

	const auto* monsterType = g_monsters.getMonsterType(boostedCreatureName);
	if (!monsterType) {
		return entry;
	}

	entry.raceId = monsterType->raceId;
	entry.name = monsterType->name;
	entry.outfit = monsterType->info.outfit;
	entry.outfit.lookType = AstraClient::sanitize860OutfitLookType(entry.outfit.lookType);
	return entry;
}

LoginBoostedEntry getBoostedBossLoginEntry()
{
	LoginBoostedEntry entry;
	auto& db = Database::getInstance();
	if (!db.storeQuery("SHOW TABLES LIKE 'boosted_boss'")) {
		return entry;
	}

	const std::time_t now = std::time(nullptr);
	const std::tm* localTime = std::localtime(&now);
	const uint16_t today = localTime ? static_cast<uint16_t>(localTime->tm_mday) : 0;
	const auto result = db.storeQuery(fmt::format(
	    "SELECT `boostname`, `raceid`, `looktype`, `lookhead`, `lookbody`, `looklegs`, `lookfeet`, `lookaddons` "
	    "FROM `boosted_boss` WHERE `date` = '{}' AND `raceid` <> '0' LIMIT 1",
	    today));
	if (!result) {
		return entry;
	}

	entry.raceId = result->getNumber<uint32_t>("raceid");
	entry.name = std::string{result->getString("boostname")};
	entry.outfit.lookType = AstraClient::sanitize860OutfitLookType(result->getNumber<uint16_t>("looktype"));
	entry.outfit.lookHead = result->getNumber<uint8_t>("lookhead");
	entry.outfit.lookBody = result->getNumber<uint8_t>("lookbody");
	entry.outfit.lookLegs = result->getNumber<uint8_t>("looklegs");
	entry.outfit.lookFeet = result->getNumber<uint8_t>("lookfeet");
	entry.outfit.lookAddons = result->getNumber<uint8_t>("lookaddons");

	return entry;
}

void addBoostedLoginEntry(const OutputMessage_ptr& output, const LoginBoostedEntry& entry)
{
	output->addByte(entry.isValid() ? 1 : 0);
	if (!entry.isValid()) {
		return;
	}

	output->add<uint32_t>(entry.raceId);
	output->addString(entry.name);
	output->add<uint16_t>(entry.outfit.lookType);
	output->addByte(entry.outfit.lookHead);
	output->addByte(entry.outfit.lookBody);
	output->addByte(entry.outfit.lookLegs);
	output->addByte(entry.outfit.lookFeet);
	output->addByte(entry.outfit.lookAddons);
}

void addAstraLoginBoostedInfo(const OutputMessage_ptr& output)
{
	output->addByte(ASTRA_LOGIN_BOOSTED_INFO_MARKER);
	addBoostedLoginEntry(output, getBoostedCreatureLoginEntry());
	addBoostedLoginEntry(output, getBoostedBossLoginEntry());
}

} // namespace

// --- Brute Force Protection ---

bool LoginAttemptLimiter::allowLogin(uint32_t ip)
{
	std::scoped_lock lock(mu);
	int64_t now = OTSYS_TIME();

	auto it = attempts.find(ip);
	if (it == attempts.end()) {
		return true;
	}

	auto& info = it->second;

	// Still blocked?
	if (info.blockUntil > now) {
		return false;
	}

	// Window expired — reset
	if (now - info.firstAttempt > WINDOW_MS) {
		attempts.erase(it);
		return true;
	}

	return true;
}

void LoginAttemptLimiter::recordFailure(uint32_t ip)
{
	std::scoped_lock lock(mu);
	int64_t now = OTSYS_TIME();

	auto& info = attempts[ip];

	// Reset window if expired
	if (info.firstAttempt == 0 || now - info.firstAttempt > WINDOW_MS) {
		info.failures = 1;
		info.firstAttempt = now;
		info.blockUntil = 0;
		return;
	}

	info.failures++;

	if (info.failures >= MAX_FAILURES) {
		info.blockUntil = now + BLOCK_TIME_MS;
		LOG_WARN(fmt::format("[Anti-BruteForce] IP {} blocked for {} minutes after {} failed login attempts.",
		                     convertIPToString(ip), BLOCK_TIME_MS / 60000, info.failures));
	}
}

void LoginAttemptLimiter::recordSuccess(uint32_t ip)
{
	std::scoped_lock lock(mu);
	attempts.erase(ip);
}

void ProtocolLogin::disconnectClient(std::string_view message)
{
	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0x0A);
	output->addString(message);
	send(output);
	disconnect();
}

void ProtocolLogin::getCharacterList(std::string_view accountName, std::string_view password, bool isAstraClient)
{
	auto connection = getConnection();
	uint32_t clientIP = connection ? connection->getIP() : 0;

	Account account;
	if (!IOLoginData::loginserverAuthentication(accountName, password, account)) {
		LoginAttemptLimiter::getInstance().recordFailure(clientIP);
		disconnectClient("Account name or password is not correct.");
		return;
	}

	LoginAttemptLimiter::getInstance().recordSuccess(clientIP);

	auto output = OutputMessagePool::getOutputMessage();

	auto motd = getString(ConfigManager::MOTD);
	if (!motd.empty()) {
		// Add MOTD
		output->addByte(0x14);
		output->addString(fmt::format("{:d}\n{:s}", g_game.getMotdNum(), motd));
	}

	struct CharacterListEntry {
		std::string name;
		uint32_t level = 0;
		uint16_t lookType = 128;
		uint8_t lookHead = 78;
		uint8_t lookBody = 69;
		uint8_t lookLegs = 58;
		uint8_t lookFeet = 76;
		uint8_t lookAddons = 0;
		std::string vocation = "None";
	};

	std::vector<CharacterListEntry> characters;
	bool hasAccountManager = ConfigManager::getBoolean(ConfigManager::ACCOUNT_MANAGER);
	bool hasNamelock = ConfigManager::getBoolean(ConfigManager::NAMELOCK_MANAGER) && IOBan::accountHasNamelockedPlayer(account.id);

	if ((hasAccountManager && account.id != 1) || hasNamelock) {
		CharacterListEntry accountManager;
		accountManager.name = "Account Manager";
		accountManager.vocation = "Account Manager";
		characters.push_back(std::move(accountManager));
	}

	Database& db = Database::getInstance();
	DBResult_ptr result = db.storeQuery(fmt::format(
	    "SELECT `name`, `level`, `vocation`, `looktype`, `lookhead`, `lookbody`, `looklegs`, `lookfeet`, `lookaddons` FROM `players` WHERE `account_id` = {:d} AND `deletion` = 0 ORDER BY `name` ASC",
	    account.id));
	if (result) {
		do {
			CharacterListEntry character;
			character.name = std::string{result->getString("name")};
			character.level = result->getNumber<uint32_t>("level");
			character.lookType = AstraClient::sanitize860OutfitLookType(result->getNumber<uint16_t>("looktype"));
			character.lookHead = result->getNumber<uint8_t>("lookhead");
			character.lookBody = result->getNumber<uint8_t>("lookbody");
			character.lookLegs = result->getNumber<uint8_t>("looklegs");
			character.lookFeet = result->getNumber<uint8_t>("lookfeet");
			character.lookAddons = result->getNumber<uint8_t>("lookaddons");

			const uint16_t vocationId = result->getNumber<uint16_t>("vocation");
			if (const auto* vocation = g_vocations.getVocation(vocationId)) {
				character.vocation = std::string{vocation->getVocName()};
			}

			characters.push_back(std::move(character));
		} while (result->next() && characters.size() < std::numeric_limits<uint8_t>::max());
	}

	auto IP = getIP(getString(ConfigManager::IP));
	auto serverName = getString(ConfigManager::SERVER_NAME);
	auto gamePort = getInteger(ConfigManager::GAME_PORT);

	uint8_t size = std::min<size_t>(std::numeric_limits<uint8_t>::max(), characters.size());

	if (isAstraClient) {
		// AstraClient extends the 8.60 list with outfit, level and vocation metadata.
		output->addByte(0x65);
		output->addByte(size);
		for (uint8_t i = 0; i < size; ++i) {
			const auto& character = characters[i];
			output->addString(character.name);
			output->addString(serverName);
			output->add<uint32_t>(IP);
			output->add<uint16_t>(gamePort);
			output->add<uint16_t>(character.lookType);
			output->addByte(character.lookHead);
			output->addByte(character.lookBody);
			output->addByte(character.lookLegs);
			output->addByte(character.lookFeet);
			output->addByte(character.lookAddons);
			output->add<uint32_t>(character.level);
			output->addString(character.vocation);
		}
	} else {
		// Standard 8.60 character list for OTCv8 Classic, Fonticak, CIP, etc.
		output->addByte(0x64);
		output->addByte(size);
		for (uint8_t i = 0; i < size; ++i) {
			const auto& character = characters[i];
			output->addString(character.name);
			output->addString(serverName);
			output->add<uint32_t>(IP);
			output->add<uint16_t>(gamePort);
		}
	}

	// Add premium days
	if (getBoolean(ConfigManager::FREE_PREMIUM)) {
		output->add<uint16_t>(0xFFFF); // client displays free premium
	} else {
		auto currentTime = time(nullptr);
		if (account.premiumEndsAt > currentTime) {
			output->add<uint16_t>(std::max<time_t>(0, account.premiumEndsAt - time(nullptr)) / 86400);
		} else {
			output->add<uint16_t>(0);
		}
	}

	if (isAstraClient) {
		addAstraLoginBoostedInfo(output);
	}

	send(output);

	disconnect();
}

void ProtocolLogin::getCastList(const std::string& password)
{
	auto casts = IOLoginData::getCastList(password);
	if (casts.empty()) {
		disconnectClient("There are no casts available at this time.");
		return;
	}

	auto output = OutputMessagePool::getOutputMessage();

	// Add MOTD
	output->addByte(0x14);
	output->addString(fmt::format("{:d}\n{:s}", normal_random(1, 255), "                    !-Welcome to Cast System-!\n\nIt will show all active casts even with password.\n\nTo enter a cast with password you just have to\nput the password in the empty space.\n\nRemember that when you open cast without\npassword you will get 10% of Exp.\n\nAlso remember that to open cast, just say !cast on."));

	// Add char list
	output->addByte(0x64);

	uint8_t limit = std::numeric_limits<uint8_t>::max();
	output->addByte(static_cast<uint8_t>(std::min<size_t>(limit, casts.size())));

	for (const auto& it : casts) {
		if (limit == 0) {
			break;
		}

		output->addString(it.first);
		output->addString(it.second);
		output->add<uint32_t>(getIP(ConfigManager::getString(ConfigManager::IP)));
		output->add<uint16_t>(ConfigManager::getInteger(ConfigManager::GAME_PORT));
		limit--;
	}

	//Add premium days
	output->add<uint16_t>(0xFFFF);

	send(output);

	disconnect();
}

void ProtocolLogin::onRecvFirstMessage(NetworkMessage& msg)
{
	if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
		disconnect();
		return;
	}

	uint16_t operatingSystem = msg.get<uint16_t>();

	uint16_t version = msg.get<uint16_t>();
	msg.skipBytes(12);
	/*
	 * Skipped bytes:
	 * 4 bytes: protocolVersion
	 * 12 bytes: dat, spr, pic signatures (4 bytes each)
	 * 1 byte: 0
	 */

	if (version <= 760) {
		disconnectClient(fmt::format("Only clients with protocol {:s} allowed!", CLIENT_VERSION_STR));
		return;
	}

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
	setXTEAKey(std::move(key));

	if (version < CLIENT_VERSION_MIN || version > CLIENT_VERSION_MAX) {
		disconnectClient(fmt::format("Only clients with protocol {:s} allowed!", CLIENT_VERSION_STR));
		return;
	}

	if (g_game.getGameState() == GAME_STATE_STARTUP) {
		disconnectClient("Gameworld is starting up. Please wait.");
		return;
	}

	if (g_game.getGameState() == GAME_STATE_MAINTAIN) {
		disconnectClient("Gameworld is under maintenance.\nPlease re-connect in a while.");
		return;
	}

	BanInfo banInfo;
	auto connection = getConnection();
	if (!connection) {
		return;
	}

	if (IOBan::isIpBanned(connection->getIP(), banInfo)) {
		if (banInfo.reason.empty()) {
			banInfo.reason = "(none)";
		}

		disconnectClient(fmt::format("Your IP has been banned until {:s} by {:s}.\n\nReason specified:\n{:s}",
		                             formatDateShort(banInfo.expiresAt), banInfo.bannedBy, banInfo.reason));
		return;
	}

	auto accountName = msg.getString();

	// Read and validate password from the message
	auto password = msg.getString();

	// Always detect AstraClient, regardless of astraClientOnly setting.
	// This allows sending the correct packet format (0x65 vs 0x64) to each client.
	if (msg.getBufferPosition() + 2 <= msg.getLength()) {
		uint16_t markerLength = msg.get<uint16_t>();
		if (markerLength > 0 && markerLength <= 64 && msg.getBufferPosition() + markerLength <= msg.getLength()) {
			const auto marker = msg.getString(markerLength);
			if (marker == AstraClient::LOGIN_MARKER && msg.getBufferPosition() + sizeof(uint32_t) <= msg.getLength()) {
				isAstraClient_ =
				    msg.get<uint32_t>() == AstraClient::generateSignature(operatingSystem, version, key);
			}
		}
	}

	// When astraClientOnly is true, reject any client that is not AstraClient.
	if (getBoolean(ConfigManager::ASTRA_CLIENT_ONLY) && !isAstraClient_) {
		LOG_INFO("[AstraClient] Client rejected: AstraClient required");
		disconnectClient(AstraClient::REQUIRED_MESSAGE);
		return;
	}

	if (isAstraClient_) {
		LOG_INFO("[AstraClient] Client accepted");
	}

	// Brute force check before dispatching login task
	uint32_t clientIP = connection ? connection->getIP() : 0;
	if (!LoginAttemptLimiter::getInstance().allowLogin(clientIP)) {
		disconnectClient("Too many failed login attempts. Please wait 5 minutes.");
		return;
	}

	g_dispatcher.addTask([=, thisPtr = std::static_pointer_cast<ProtocolLogin>(shared_from_this()),
	                      accountName = std::string{accountName},
	                      password = std::string{password}]() {
		if (accountName.empty()) {
			thisPtr->getCastList(password);
		} else {
			thisPtr->getCharacterList(accountName, password, thisPtr->isAstraClient_);
		}
	});
}
