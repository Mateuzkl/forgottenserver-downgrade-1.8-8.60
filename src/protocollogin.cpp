// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "protocollogin.h"

#include "astraclient.h"
#include "fonticakclient.h"
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
constexpr uint8_t ASTRA_LOGIN_CAST_LIST_MARKER = 0xA2;
constexpr uint8_t ASTRA_LOGIN_CAST_LIST_VERSION = 1;
constexpr size_t ASTRA_LOGIN_CAST_LIST_LIMIT = std::numeric_limits<uint8_t>::max();
constexpr std::string_view ASTRA_LOGIN_CAST_LIST_REQUEST = "__astra_casts_v1__";

struct LoginCastEntry {
	std::string name;
	uint16_t viewers = 0;
	bool hasPassword = false;
};

struct LoginBoostedEntry {
	uint32_t raceId = 0;
	std::string name;
	Outfit_t outfit;

	bool isValid() const
	{
		return raceId != 0 && !name.empty() && outfit.lookType != 0;
	}
};

std::string getBoostedBossDateKey()
{
	const std::time_t now = std::time(nullptr);
	const std::tm* localTime = std::localtime(&now);
	if (!localTime) {
		return {};
	}

	return fmt::format("{:04}-{:02}-{:02}", localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday);
}

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
	return entry;
}

LoginBoostedEntry getBoostedBossLoginEntry()
{
	LoginBoostedEntry entry;
	auto& db = Database::getInstance();
	if (!db.storeQuery("SHOW TABLES LIKE 'boosted_boss'")) {
		return entry;
	}

	const std::string today = getBoostedBossDateKey();
	if (today.empty()) {
		return entry;
	}

	const auto result = db.storeQuery(fmt::format(
	    "SELECT `boostname`, `raceid`, `looktype`, `lookhead`, `lookbody`, `looklegs`, `lookfeet`, `lookaddons` "
	    "FROM `boosted_boss` WHERE `date` = {:s} AND `raceid` <> '0' LIMIT 1",
	    db.escapeString(today)));
	if (!result) {
		return entry;
	}

	entry.raceId = result->getNumber<uint32_t>("raceid");
	entry.name = std::string{result->getString("boostname")};
	entry.outfit.lookType = result->getNumber<uint16_t>("looktype");
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

namespace {

std::string normalizeAccountName(std::string_view accountName)
{
	std::string normalized = asTrimmedString(accountName);
	toLowerCaseString(normalized);
	return normalized;
}

} // namespace

uint32_t LoginAttemptLimiter::accountFailureThreshold()
{
	const int64_t configured = ConfigManager::getInteger(ConfigManager::BRUTEFORCE_ACCOUNT_FAILURES);
	return configured > 0 ? static_cast<uint32_t>(configured) : DEFAULT_ACCOUNT_FAILURES;
}

uint32_t LoginAttemptLimiter::ipFailureThreshold()
{
	const int64_t configured = ConfigManager::getInteger(ConfigManager::BRUTEFORCE_IP_FAILURES);
	return configured > 0 ? static_cast<uint32_t>(configured) : DEFAULT_IP_FAILURES;
}

int64_t LoginAttemptLimiter::blockDurationMs()
{
	const int64_t configured = ConfigManager::getInteger(ConfigManager::BRUTEFORCE_BLOCK_SECONDS);
	return (configured > 0 ? configured : DEFAULT_BLOCK_SECONDS) * 1000;
}

void LoginAttemptLimiter::cleanup(int64_t now)
{
	if (lastCleanup != 0 && now - lastCleanup < WINDOW_MS) {
		return;
	}
	lastCleanup = now;

	const auto expired = [now](const auto& pair) {
		const AttemptInfo& info = pair.second;
		return info.pending == 0 && info.blockUntil <= now && now - info.firstAttempt > WINDOW_MS;
	};

	std::erase_if(accountAttempts, expired);
	std::erase_if(ipAttempts, expired);
}

bool LoginAttemptLimiter::isBlocked(AttemptInfo& info, int64_t now)
{
	if (info.blockUntil > now) {
		return true;
	}

	// Block expired, or the counting window elapsed: start counting again.
	if (info.blockUntil != 0 || now - info.firstAttempt > WINDOW_MS) {
		const uint32_t pending = info.pending;
		info = AttemptInfo{};
		info.pending = pending;
	}
	return false;
}

bool LoginAttemptLimiter::hasCapacity(AttemptInfo& info, int64_t now, uint32_t threshold)
{
	return !isBlocked(info, now) && info.failures + info.pending < threshold;
}

void LoginAttemptLimiter::registerFailure(AttemptInfo& info, int64_t now, uint32_t threshold,
                                         int64_t blockDuration, int64_t& blockedAt)
{
	if (info.firstAttempt == 0 || now - info.firstAttempt > WINDOW_MS) {
		info.failures = 1;
		info.firstAttempt = now;
		info.blockUntil = 0;
	} else {
		++info.failures;
	}

	if (info.failures >= threshold) {
		info.blockUntil = now + blockDuration;
		blockedAt = info.failures;
	}
}

bool LoginAttemptLimiter::allowLogin(uint32_t ip, std::string_view accountName)
{
	std::scoped_lock lock(mu);
	const int64_t now = OTSYS_TIME();
	cleanup(now);

	if (auto ipIt = ipAttempts.find(ip);
	    ipIt != ipAttempts.end() && !hasCapacity(ipIt->second, now, ipFailureThreshold())) {
		return false;
	}

	// No account dimension (for example a cast list request): the per-IP guard above
	// is the only thing that applies.
	const std::string account = normalizeAccountName(accountName);
	if (account.empty()) {
		return true;
	}

	auto it = accountAttempts.find(AccountKey{ip, account});
	return it == accountAttempts.end() || hasCapacity(it->second, now, accountFailureThreshold());
}

bool LoginAttemptLimiter::reserveLogin(uint32_t ip, std::string_view accountName)
{
	std::scoped_lock lock(mu);
	const int64_t now = OTSYS_TIME();
	cleanup(now);

	AttemptInfo& ipInfo = ipAttempts[ip];
	if (!hasCapacity(ipInfo, now, ipFailureThreshold())) {
		return false;
	}

	const std::string account = normalizeAccountName(accountName);
	AttemptInfo* accountInfo = nullptr;
	if (!account.empty()) {
		accountInfo = &accountAttempts[AccountKey{ip, account}];
		if (!hasCapacity(*accountInfo, now, accountFailureThreshold())) {
			return false;
		}
	}

	++ipInfo.pending;
	if (accountInfo) {
		++accountInfo->pending;
	}
	return true;
}

void LoginAttemptLimiter::releaseReservationLocked(uint32_t ip, const std::string& account)
{
	if (auto ipIt = ipAttempts.find(ip); ipIt != ipAttempts.end() && ipIt->second.pending > 0) {
		--ipIt->second.pending;
	}

	if (!account.empty()) {
		auto accountIt = accountAttempts.find(AccountKey{ip, account});
		if (accountIt != accountAttempts.end() && accountIt->second.pending > 0) {
			--accountIt->second.pending;
		}
	}
}

void LoginAttemptLimiter::recordFailureLocked(uint32_t ip, const std::string& account, int64_t now)
{
	const int64_t blockDuration = blockDurationMs();

	int64_t blockedAfter = 0;
	registerFailure(ipAttempts[ip], now, ipFailureThreshold(), blockDuration, blockedAfter);
	if (blockedAfter != 0) {
		LOG_WARN(fmt::format("[Anti-BruteForce] IP {} blocked for {} seconds after {} failed login attempts across "
		                     "multiple accounts.",
		                     convertIPToString(ip), blockDuration / 1000, blockedAfter));
	}

	if (account.empty()) {
		return;
	}

	blockedAfter = 0;
	registerFailure(accountAttempts[AccountKey{ip, account}], now, accountFailureThreshold(), blockDuration,
	                blockedAfter);
	if (blockedAfter != 0) {
		LOG_WARN(fmt::format("[Anti-BruteForce] IP {} blocked for {} seconds after {} failed login attempts on one "
		                     "account.",
		                     convertIPToString(ip), blockDuration / 1000, blockedAfter));
	}
}

void LoginAttemptLimiter::recordSuccessLocked(uint32_t ip, const std::string& account)
{
	// Deliberately clears only this account's history. Erasing everything for the
	// IP let anyone holding one valid account reset the counter at will: guess four
	// times against a victim, log into your own account, repeat forever. The per-IP
	// spray counter is likewise left alone and expires on its own window.
	if (account.empty()) {
		return;
	}

	auto it = accountAttempts.find(AccountKey{ip, account});
	if (it == accountAttempts.end()) {
		return;
	}

	const uint32_t pending = it->second.pending;
	if (pending == 0) {
		accountAttempts.erase(it);
	} else {
		it->second = AttemptInfo{};
		it->second.pending = pending;
	}
}

void LoginAttemptLimiter::commitFailure(uint32_t ip, std::string_view accountName)
{
	const std::string account = normalizeAccountName(accountName);
	std::scoped_lock lock(mu);
	const int64_t now = OTSYS_TIME();
	cleanup(now);
	releaseReservationLocked(ip, account);
	recordFailureLocked(ip, account, now);
}

void LoginAttemptLimiter::commitSuccess(uint32_t ip, std::string_view accountName)
{
	const std::string account = normalizeAccountName(accountName);
	std::scoped_lock lock(mu);
	cleanup(OTSYS_TIME());
	releaseReservationLocked(ip, account);
	recordSuccessLocked(ip, account);
}

void LoginAttemptLimiter::releaseReservation(uint32_t ip, std::string_view accountName)
{
	const std::string account = normalizeAccountName(accountName);
	std::scoped_lock lock(mu);
	cleanup(OTSYS_TIME());
	releaseReservationLocked(ip, account);
}

void LoginAttemptLimiter::recordFailure(uint32_t ip, std::string_view accountName)
{
	const std::string account = normalizeAccountName(accountName);
	std::scoped_lock lock(mu);
	const int64_t now = OTSYS_TIME();
	cleanup(now);
	recordFailureLocked(ip, account, now);
}

void LoginAttemptLimiter::recordSuccess(uint32_t ip, std::string_view accountName)
{
	const std::string account = normalizeAccountName(accountName);
	std::scoped_lock lock(mu);
	cleanup(OTSYS_TIME());
	recordSuccessLocked(ip, account);
}

void ProtocolLogin::disconnectClient(std::string_view message)
{
	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0x0A);
	output->addString(message);
	send(output);
	disconnect();
}

void ProtocolLogin::getCharacterList(std::string_view accountName, std::string_view password, bool isAstraClient,
                                     uint32_t clientIP)
{
	Account account;
	const auto authentication = IOLoginData::loginserverAuthentication(accountName, password, account);
	if (authentication == IOLoginData::AuthenticationResult::Rejected) {
		LoginAttemptLimiter::getInstance().commitFailure(clientIP, accountName);
		disconnectClient("Account name or password is not correct.");
		return;
	}
	if (authentication == IOLoginData::AuthenticationResult::DatabaseError) {
		LoginAttemptLimiter::getInstance().releaseReservation(clientIP, accountName);
		disconnectClient("The login service is temporarily unavailable. Please try again later.");
		return;
	}

	LoginAttemptLimiter::getInstance().commitSuccess(clientIP, accountName);

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
			character.lookType = result->getNumber<uint16_t>("looktype");
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

void ProtocolLogin::getCastList(const std::string& password, uint32_t clientIP)
{
	auto casts = IOLoginData::getCastList(password);
	if (casts.empty()) {
		// getCastList filters on the password in SQL, so a non-empty one that matches
		// nothing is a rejected guess and has to count against the per-IP guard. An
		// empty password is the ordinary "list the public casts" request and an empty
		// result there only means nobody is streaming, so it is not recorded.
		if (!password.empty()) {
			recordRejectedCastPassword(clientIP);
		} else {
			LoginAttemptLimiter::getInstance().releaseReservation(clientIP, "");
		}
		disconnectClient("There are no casts available at this time.");
		return;
	}
	LoginAttemptLimiter::getInstance().commitSuccess(clientIP, "");

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

void ProtocolLogin::recordRejectedCastPassword(uint32_t ip)
{
	LoginAttemptLimiter::getInstance().commitFailure(ip, "");
}

void ProtocolLogin::getAstraCastList()
{
	const auto casters = g_game.getLiveCasters({});
	std::vector<LoginCastEntry> entries;
	entries.reserve(std::min(ASTRA_LOGIN_CAST_LIST_LIMIT, casters.size()));

	uint32_t totalViewers = 0;
	for (const auto& caster : casters) {
		if (entries.size() >= ASTRA_LOGIN_CAST_LIST_LIMIT) {
			break;
		}

		if (!caster || !caster->client || !caster->client->isBroadcasting()) {
			continue;
		}

		const size_t viewers = caster->client->spectatorList().size();
		totalViewers += static_cast<uint32_t>(
		    std::min<size_t>(viewers, std::numeric_limits<uint32_t>::max() - totalViewers));
		entries.push_back(LoginCastEntry{
		    caster->getName(),
		    static_cast<uint16_t>(std::min<size_t>(viewers, std::numeric_limits<uint16_t>::max())),
		    !caster->client->password().empty(),
		});
	}

	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(ASTRA_LOGIN_CAST_LIST_MARKER);
	output->addByte(ASTRA_LOGIN_CAST_LIST_VERSION);
	output->addString(getString(ConfigManager::SERVER_NAME));
	output->add<uint32_t>(getIP(getString(ConfigManager::IP)));
	output->add<uint16_t>(getInteger(ConfigManager::GAME_PORT));
	output->add<uint16_t>(static_cast<uint16_t>(entries.size()));
	output->add<uint32_t>(totalViewers);

	for (const auto& entry : entries) {
		output->addString(entry.name);
		output->add<uint16_t>(entry.viewers);
		output->addByte(entry.hasPassword ? 1 : 0);
	}

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

	// Always detect AstraClient and FonticakClient, regardless of astraClientOnly setting.
	// This allows sending the correct packet format (0x65 vs 0x64) to each client.
	bool isFonticakClient_ = false;
	if (msg.getBufferPosition() + 2 <= msg.getLength()) {
		uint16_t markerLength = msg.get<uint16_t>();
		if (markerLength > 0 && markerLength <= 64 && msg.getBufferPosition() + markerLength <= msg.getLength()) {
			const auto marker = msg.getString(markerLength);
			if (marker == AstraClient::LOGIN_MARKER && msg.getBufferPosition() + sizeof(uint32_t) <= msg.getLength()) {
				isAstraClient_ =
				    msg.get<uint32_t>() == AstraClient::generateSignature(operatingSystem, version, key);
			} else if (marker == FonticakClient::LOGIN_MARKER && msg.getBufferPosition() + sizeof(uint32_t) <= msg.getLength()) {
				isFonticakClient_ =
				    msg.get<uint32_t>() == FonticakClient::generateSignature(operatingSystem, version, key);
			}
		}
	}

	// When astraClientOnly is true, reject any client that is not AstraClient.
	if (getBoolean(ConfigManager::ASTRA_CLIENT_ONLY) && !isAstraClient_) {
		LOG_WARN("[AstraClient] Client rejected: AstraClient required");
		disconnectClient(AstraClient::REQUIRED_MESSAGE);
		return;
	}

	// When fonticakClientOnly is true, reject any client that is not FonticakClient.
	if (getBoolean(ConfigManager::FONTICAK_CLIENT_ONLY) && !isFonticakClient_) {
		LOG_WARN("[FonticakClient] Client rejected: OTC-Fonticak required");
		disconnectClient(FonticakClient::REQUIRED_MESSAGE);
		return;
	}

	const bool isAstraCastListRequest =
	    accountName.empty() && isAstraClient_ && password == ASTRA_LOGIN_CAST_LIST_REQUEST;
	if (isAstraClient_ && !isAstraCastListRequest) {
		LOG_DEBUG("[AstraClient] Login protocol client accepted");
	}
	if (isFonticakClient_) {
		LOG_DEBUG("[FonticakClient] Login protocol client accepted");
	}

	// Brute force check before dispatching the login task. The account name is
	// passed so failures are counted per account rather than per IP; a cast list
	// request carries no account name and is only subject to the per-IP guard, so
	// watching a stream still works while one account on the same address is
	// locked out.
	uint32_t clientIP = connection ? connection->getIP() : 0;
	if (!LoginAttemptLimiter::getInstance().reserveLogin(clientIP, accountName)) {
		disconnectClient("Too many failed login attempts. Please try again later.");
		return;
	}

	g_dispatcher.addTask([=, thisPtr = std::static_pointer_cast<ProtocolLogin>(shared_from_this()),
	                      accountName = std::string{accountName},
	                      password = std::string{password},
	                      astraCastListRequest = isAstraCastListRequest]() {
		if (astraCastListRequest) {
			LoginAttemptLimiter::getInstance().releaseReservation(clientIP, accountName);
			thisPtr->getAstraCastList();
		} else if (accountName.empty()) {
			thisPtr->getCastList(password, clientIP);
		} else {
			thisPtr->getCharacterList(accountName, password, thisPtr->isAstraClient_, clientIP);
		}
	});
}
