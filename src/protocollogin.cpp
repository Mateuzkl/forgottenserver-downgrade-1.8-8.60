// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "protocollogin.h"

#include "ban.h"
#include "configmanager.h"
#include "game.h"
#include "iologindata.h"
#include "outputmessage.h"
#include "tasks.h"
#include "tools.h"

#include <iomanip>
#include <fmt/format.h>
#include <utility>

extern Game g_game;

void ProtocolLogin::disconnectClient(std::string_view message)
{
	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0x0A);
	output->addString(message);
	send(output);
	disconnect();
}

void ProtocolLogin::getCharacterList(std::string_view accountName, std::string_view password)
{
	Account account;
	if (!IOLoginData::loginserverAuthentication(accountName, password, account)) {
		disconnectClient("Account name or password is not correct.");
		return;
	}

	auto output = OutputMessagePool::getOutputMessage();

	auto motd = getString(ConfigManager::MOTD);
	if (!motd.empty()) {
		// Add MOTD
		output->addByte(0x14);
		output->addString(fmt::format("{:d}\n{:s}", g_game.getMotdNum(), motd));
	}

	// Add char list
	output->addByte(0x64);

	uint8_t size = std::min<size_t>(std::numeric_limits<uint8_t>::max(), account.characters.size());
	bool hasAccountManager = ConfigManager::getBoolean(ConfigManager::ACCOUNT_MANAGER);
	bool hasNamelock = ConfigManager::getBoolean(ConfigManager::NAMELOCK_MANAGER) && IOBan::accountHasNamelockedPlayer(account.id);
	
	if ((hasAccountManager && account.id != 1) || hasNamelock) {
		size++;
	}

	auto IP = getIP(getString(ConfigManager::IP));
	auto serverName = getString(ConfigManager::SERVER_NAME);
	auto gamePort = getInteger(ConfigManager::GAME_PORT);
	
	output->addByte(size);
	if ((hasAccountManager && account.id != 1) || hasNamelock) {
		output->addString("Account Manager");
		output->addString(serverName);
		output->add<uint32_t>(IP);
		output->add<uint16_t>(gamePort);
	}
	uint8_t charCount = size - (((hasAccountManager && account.id != 1) || hasNamelock) ? 1 : 0);
	for (uint8_t i = 0; i < charCount; i++) {
		output->addString(account.characters[i]);
		output->addString(serverName);
		output->add<uint32_t>(IP);
		output->add<uint16_t>(gamePort);
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

	msg.skipBytes(2); // client OS

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

	g_dispatcher.addTask([=, thisPtr = std::static_pointer_cast<ProtocolLogin>(shared_from_this()),
	                      accountName = std::string{accountName},
	                      password = std::string{password}]() {
		if (accountName.empty()) {
			thisPtr->getCastList(password);
		} else {
			thisPtr->getCharacterList(accountName, password);
		}
	});
}
