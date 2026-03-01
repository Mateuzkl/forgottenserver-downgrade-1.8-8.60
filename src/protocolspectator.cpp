/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2016  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include "protocolspectator.h"

#include "actions.h"
#include "ban.h"
#include "configmanager.h"
#include "game.h"
#include "items.h"
#include "outputmessage.h"
#include "player.h"
#include "scheduler.h"
#include "spells.h"
#include "tile.h"

#include <boost/range/adaptor/reversed.hpp>

extern Actions actions;
extern CreatureEvents* g_creatureEvents;
extern Chat* g_chat;
extern Spells* g_spells;

void ProtocolSpectator::release()
{
	acceptPackets = false;

	if (caster) {
		caster->removeSpectator(this);
		if (player && caster->isLiveCasting()) {
			std::stringstream ss;
			ss << player->getName() << " has left the cast.";
			caster->sendChannelMessage("", ss.str(), TALKTYPE_CHANNEL_O, CHANNEL_CAST);
		}
	}

	if (player && player->client == shared_from_this()) {
		player->client.reset();
		player = nullptr;
	}
	OutputMessagePool::getInstance().removeProtocolFromAutosend(shared_from_this());
	Protocol::release();
}

void ProtocolSpectator::login(const std::string& name, const std::string& password, OperatingSystem_t operatingSystem)
{
	std::string realName = name;
	size_t pos = name.find(" (Lv ");
	if (pos != std::string::npos) {
		realName = name.substr(0, pos);
	}

	Player* foundPlayer = g_game.getPlayerByName(realName);

	if (foundPlayer && foundPlayer->isLiveCasting()) {
		if (foundPlayer->castPassword != password) {
			disconnectClient("The password you have entered is incorrect.");
			return;
		}

		for (auto it : foundPlayer->spectatorBans) {
			if (it.second == getIP()) {
				disconnectClient("You have been banned from this live cast.");
				return;
			}
		}

		if ((int32_t)foundPlayer->spectatorCount >= ConfigManager::getInteger(ConfigManager::LIVE_CAST_MAX) &&
		    ConfigManager::getInteger(ConfigManager::LIVE_CAST_MAX) != 0) {
			disconnectClient("This live cast is full, please try again later.");
			return;
		}

		player = new Player(getThis());
		uint16_t spectatorCount = foundPlayer->spectatorCount;
		std::stringstream ss;
		ss << "Spectator " << (spectatorCount + 1);
		while (g_game.getPlayerByName(ss.str()) != nullptr ||
		       foundPlayer->spectatorBans.find(name) != foundPlayer->spectatorBans.end()) {
			ss.str(std::string());
			ss << "Spectator " << (++spectatorCount);
		}
		player->setName(ss.str());
		player->setGroup(g_game.groups.getGroup(1));
		player->setVocation(0);
		player->setSex(PLAYERSEX_MALE);
		player->level = 1;
		player->health = 100;
		player->healthMax = 100;
		player->mana = 0;
		player->manaMax = 0;
		player->capacity = 40000;

		player->isSpectator = true;

		if (g_game.getGameState() == GAME_STATE_CLOSING) {
			disconnectClient("The game is just going down.\nPlease try again later.");
			return;
		}

		if (g_game.getGameState() == GAME_STATE_CLOSED) {
			disconnectClient("Server is currently closed.\nPlease try again later.");
			return;
		}

		player->setOperatingSystem(operatingSystem);
		acceptPackets = true;
	} else {
		disconnectClient("Live cast could not be found.");
		return;
	}

	// OTCv8 features and extended opcodes
	if (isOTCv8) {
		sendFeatures();
		NetworkMessage opcodeMessage;
		opcodeMessage.addByte(0x32);
		opcodeMessage.addByte(0x00);
		opcodeMessage.add<uint16_t>(0x00);
		writeToOutputBuffer(opcodeMessage);
	}

	connect(foundPlayer->getID(), operatingSystem);

	OutputMessagePool::getInstance().addProtocolToAutosend(shared_from_this());
}

void ProtocolSpectator::syncOpenContainers()
{
	for (const auto& it : caster->openContainers) {
		Container* container = it.second.container;
		if (container) {
			sendContainer(it.first, container, container->getParent() != nullptr, caster->getContainerIndex(it.first));
		}
	}
}

void ProtocolSpectator::connect(uint32_t playerId, OperatingSystem_t operatingSystem)
{
	eventConnect = 0;

	Player* foundPlayer = g_game.getPlayerByID(playerId);
	if (!foundPlayer) {
		disconnectClient("Caster could not be found.");
		return;
	}

	if (isConnectionExpired()) {
		return;
	}

	caster = foundPlayer;

	g_chat->removeUserFromAllChannels(*player);
	player->clearModalWindows();
	player->setOperatingSystem(operatingSystem);
	player->isConnecting = false;
	caster->addSpectator(this);

	player->client = getThis();
	sendAddCreature(caster, caster->getPosition(), 0, false);
	player->lastIP = player->getIP();
	acceptPackets = true;

	// Send Live Cast channel to spectator BEFORE sending map
	NetworkMessage msg;
	msg.addByte(0xAC);
	msg.add<uint16_t>(CHANNEL_CAST);
	msg.addString("Live Cast");
	writeToOutputBuffer(msg);

	// Notify everyone that spectator joined
	std::stringstream ss;
	ss << player->getName() << " has joined the cast.";
	caster->sendChannelMessage("", ss.str(), TALKTYPE_CHANNEL_O, CHANNEL_CAST);
	syncOpenContainers();

	// Send welcome message with MOTD
	sendWelcomeMessage();
}

void ProtocolSpectator::logout(bool, bool) { disconnect(); }

void ProtocolSpectator::onRecvFirstMessage(NetworkMessage& msg)
{
	if (g_game.getGameState() == GAME_STATE_SHUTDOWN) {
		disconnect();
		return;
	}

	OperatingSystem_t operatingSystem = static_cast<OperatingSystem_t>(msg.get<uint16_t>());
	version = msg.get<uint16_t>();

	if (!Protocol::RSA_decrypt(msg)) {
		disconnect();
		return;
	}

	uint32_t key[4];
	key[0] = msg.get<uint32_t>();
	key[1] = msg.get<uint32_t>();
	key[2] = msg.get<uint32_t>();
	key[3] = msg.get<uint32_t>();
	enableXTEAEncryption();
	setXTEAKey({key[0], key[1], key[2], key[3]});

	msg.skipBytes(1); // gamemaster flag

	std::string accountName = std::string(msg.getString());
	std::string characterName = std::string(msg.getString());
	std::string password = std::string(msg.getString());

	uint32_t timeStamp = msg.get<uint32_t>();
	uint8_t randNumber = msg.getByte();
	if (challengeTimestamp != timeStamp || challengeRandom != randNumber) {
		disconnect();
		return;
	}

	// OTCv8 version detection
	if (msg.getBufferPosition() < msg.getLength()) {
		uint16_t otcV8StringLength = msg.get<uint16_t>();
		if (otcV8StringLength == 5 && msg.getString(5) == "OTCv8") {
			isOTCv8 = true;
			msg.get<uint16_t>();
		}
	}

	if (version < CLIENT_VERSION_MIN || version > CLIENT_VERSION_MAX) {
		std::ostringstream ss;
		ss << "Only clients with protocol " << CLIENT_VERSION_STR << " allowed!";
		disconnectClient(ss.str());
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

	BanInfo banInfo;
	if (IOBan::isIpBanned(getIP(), banInfo)) {
		if (banInfo.reason.empty()) {
			banInfo.reason = "(none)";
		}

		std::ostringstream ss;
		ss << "Your IP has been banned until " << formatDateShort(banInfo.expiresAt) << " by " << banInfo.bannedBy
		   << ".\n\nReason specified:\n"
		   << banInfo.reason;
		disconnectClient(ss.str());
		return;
	}

	g_dispatcher.addTask(
	    createTask(std::bind(&ProtocolSpectator::login, getThis(), characterName, password, operatingSystem)));
}

void ProtocolSpectator::onConnect()
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

void ProtocolSpectator::disconnectClient(const std::string& message) const
{
	auto output = OutputMessagePool::getOutputMessage();
	output->addByte(0x14);
	output->addString(message);
	send(output);
	disconnect();
}

void ProtocolSpectator::writeToOutputBuffer(const NetworkMessage& msg)
{
	if (!acceptPackets) {
		return;
	}
	auto out = getOutputBuffer(msg.getLength());
	if (out) {
		out->append(msg);
	}
}

void ProtocolSpectator::sendPingBack()
{
	NetworkMessage msg;
	msg.addByte(0x1E);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendPing()
{
	NetworkMessage msg;
	msg.addByte(0x1E);
	writeToOutputBuffer(msg);
}

extern std::string dllCheckKey;
extern std::string base64Encode(const std::string& decoded_string);
extern void xorCrypt(std::string& buffer, const std::string& key);

void ProtocolSpectator::sendDllCheck()
{
	if (isOTCv8) {
		return;
	}

	if (!getBoolean(ConfigManager::DLL_CHECK_KICK)) {
		return;
	}

	std::string cryptStr;
	cryptStr.reserve(48);
	cryptStr.append(std::to_string(OTSYS_TIME()));
	cryptStr.append(";");
	cryptStr.append(std::to_string(dllCheckSequence++));
	cryptStr.append(";3puZ8qrriHA");

	xorCrypt(cryptStr, dllCheckKey);
	cryptStr = base64Encode(cryptStr);

	NetworkMessage msg;
	msg.addByte(0xBB);
	msg.addString(cryptStr);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendFeatures()
{
	if (!isOTCv8) {
		return;
	}

	std::map<GameFeature, bool> features;
	// place for non-standard OTCv8 features
	features[GameExtendedOpcode] = false;
	features[GameSkillsBase] = true;
	features[GamePlayerMounts] = true;
	features[GameMagicEffectU16] = true;
	features[GameOfflineTrainingTime] = true;
	features[GameDoubleSkills] = true;
	features[GameBaseSkillU16] = true;
	features[GameAdditionalSkills] = true;
	features[GameExtendedClientPing] = true;

	if (features.empty()) return;

	NetworkMessage msg;
	msg.addByte(0x43);
	msg.add<uint16_t>(features.size());
	for (auto& feature : features) {
		msg.addByte((uint8_t)feature.first);
		msg.addByte(feature.second ? 1 : 0);
	}
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::parsePacket(NetworkMessage& msg)
{
	if (!acceptPackets || g_game.getGameState() == GAME_STATE_SHUTDOWN || msg.getLength() <= 0) {
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
	if (caster->isRemoved() || caster->getHealth() <= 0) {
		if (recvbyte == 0x0F) {
			disconnect();
			return;
		}

		if (recvbyte != 0x14) {
			return;
		}
	}

	switch (recvbyte) {
		case 0x14:
			g_dispatcher.addTask(createTask(std::bind(&ProtocolSpectator::logout, getThis(), true, false)));
			break;
		case 0x1D:
			sendPingBack();
			break;
		case 0x1E:
			break;
		case 0x64:
		case 0x65:
		case 0x66:
		case 0x67:
		case 0x68:
		case 0x6A:
		case 0x6B:
		case 0x6C:
		case 0x6D:
			sendCancelWalk();
			break;
		case 0x70: // Turn East - used for Next Cast (CTRL + RIGHT)
			g_dispatcher.addTask(createTask(std::bind(&ProtocolSpectator::parseSwitchCast, getThis(), uint8_t(1))));
			break;
		case 0x72: // Turn West - used for Prev Cast (CTRL + LEFT)
			g_dispatcher.addTask(createTask(std::bind(&ProtocolSpectator::parseSwitchCast, getThis(), uint8_t(0))));
			break;
		case 0x8C:
			parseLookAt(msg);
			break; // Look at tile/item
		case 0x96:
			parseSay(msg);
			break;
		case 0x99:
			parseCloseChannel(msg);
			break;
		case 0xAC:
			msg.getString();
			break;

		default:
			break;
	}

	if (msg.isOverrun()) {
		disconnect();
	}
}

void ProtocolSpectator::GetTileDescription(const Tile* tile, NetworkMessage& msg)
{
	int32_t count = 0;
	if (const auto ground = tile->getGround()) {
		msg.addItem(ground);
		++count;
	}

	const bool isStacked = caster && (caster->getPosition() == tile->getPosition());

	const TileItemVector* items = tile->getItemList();
	if (items) {
		for (auto it = items->getBeginTopItem(), end = items->getEndTopItem(); it != end; ++it) {
			msg.addItem(*it);

			if (!isOTCv8) {
				if (++count == 9 && isStacked) {
					break;
				} else if (count == MAX_STACKPOS_THINGS) {
					return;
				}
			} else if (++count == MAX_STACKPOS_THINGS) {
				break;
			}
		}
	}

	const CreatureVector* creatures = tile->getCreatures();
	if (creatures) {
		for (auto it = creatures->rbegin(), end = creatures->rend(); it != end; ++it) {
			if (!isOTCv8 && count == 9 && isStacked) {
				bool known;
				uint32_t removedKnown;
				checkCreatureAsKnown(caster->getID(), known, removedKnown);
				AddCreature(msg, caster, known, removedKnown);
			} else {
				const Creature* creature = (*it);
				if (!caster->canSeeCreature(creature)) {
					continue;
				}

				bool known;
				uint32_t removedKnown;
				checkCreatureAsKnown(creature->getID(), known, removedKnown);
				AddCreature(msg, creature, known, removedKnown);
			}

			if (++count == MAX_STACKPOS_THINGS && !isOTCv8) {
				return;
			}
		}
	}

	if (items && count < MAX_STACKPOS_THINGS) {
		for (auto it = items->getBeginDownItem(), end = items->getEndDownItem(); it != end; ++it) {
			msg.addItem(*it);

			if (++count == MAX_STACKPOS_THINGS) {
				return;
			}
		}
	}
}

void ProtocolSpectator::GetMapDescription(int32_t x, int32_t y, int32_t z, int32_t width, int32_t height,
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
		msg.addByte(skip);
		msg.addByte(0xFF);
	}
}

void ProtocolSpectator::GetFloorDescription(NetworkMessage& msg, int32_t x, int32_t y, int32_t z, int32_t width,
                                            int32_t height, int32_t offset, int32_t& skip)
{
	for (int32_t nx = 0; nx < width; nx++) {
		for (int32_t ny = 0; ny < height; ny++) {
			Tile* tile = g_game.map.getTile(x + nx + offset, y + ny + offset, z);
			if (tile) {
				if (skip >= 0) {
					msg.addByte(skip);
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

void ProtocolSpectator::checkCreatureAsKnown(uint32_t id, bool& known, uint32_t& removedKnown)
{
	auto result = knownCreatureSet.insert(id);
	if (!result.second) {
		known = true;
		return;
	}

	known = false;

	if (knownCreatureSet.size() > 1300) {
		// Look for a creature to remove
		for (auto it = knownCreatureSet.begin(), end = knownCreatureSet.end(); it != end; ++it) {
			Creature* creature = g_game.getCreatureByID(*it);
			if (!canSee(creature)) {
				removedKnown = *it;
				knownCreatureSet.erase(it);
				return;
			}
		}

		// Bad situation. Let's just remove anyone.
		auto it = knownCreatureSet.begin();
		if (*it == id) {
			++it;
		}

		removedKnown = *it;
		knownCreatureSet.erase(it);
	} else {
		removedKnown = 0;
	}
}

void ProtocolSpectator::removeKnownCreature(uint32_t creatureId) { knownCreatureSet.erase(creatureId); }

bool ProtocolSpectator::canSee(const Creature* c) const
{
	if (!c || !caster || c->isRemoved()) {
		return false;
	}

	if (!caster->canSeeCreature(c)) {
		return false;
	}

	return canSee(c->getPosition());
}

bool ProtocolSpectator::canSee(const Position& pos) const { return canSee(pos.x, pos.y, pos.z); }

bool ProtocolSpectator::canSee(int32_t x, int32_t y, int32_t z) const
{
	if (!caster) {
		return false;
	}

	const Position& myPos = caster->getPosition();
	if (myPos.z <= 7) {
		// we are on ground level or above (7 -> 0)
		// view is from 7 -> 0
		if (z > 7) {
			return false;
		}
	} else if (myPos.z >= 8) {
		// we are underground (8 -> 15)
		// view is +/- 2 from the floor we stand on
		if (std::abs(myPos.getZ() - z) > 2) {
			return false;
		}
	}

	// negative offset means that the action taken place is on a lower floor than ourself
	int32_t offsetz = myPos.getZ() - z;
	if ((x >= myPos.getX() - 8 + offsetz) && (x <= myPos.getX() + 9 + offsetz) &&
	        (y >= myPos.getY() - 6 + offsetz) && (y <= myPos.getY() + 7 + offsetz)) {
		return true;
	}
	return false;
}

void ProtocolSpectator::parseCloseChannel(NetworkMessage& msg)
{
	uint16_t channelId = msg.get<uint16_t>();
	addGameTask(&Game::playerCloseChannel, caster->getID(), channelId);
}

void ProtocolSpectator::parseExecuteCommand(const std::string& text)
{
	size_t pos = text.find(' ');
	std::string command = text.substr(1, pos - 1);
	if (command.empty()) {
		return;
	}

	if (command == "commands") {
		sendChannelMessage("", "========================================", TALKTYPE_CHANNEL_O, CHANNEL_CAST);
		sendChannelMessage("", "       SPECTATOR COMMANDS", TALKTYPE_CHANNEL_O, CHANNEL_CAST);
		sendChannelMessage("", "========================================", TALKTYPE_CHANNEL_O, CHANNEL_CAST);
		sendChannelMessage("", "/name <new name> - Change your nickname", TALKTYPE_CHANNEL_O, CHANNEL_CAST);
		sendChannelMessage("", "/spectators - View all spectators", TALKTYPE_CHANNEL_O, CHANNEL_CAST);
		sendChannelMessage("", "/commands - Show this help", TALKTYPE_CHANNEL_O, CHANNEL_CAST);
		sendChannelMessage("", "========================================", TALKTYPE_CHANNEL_O, CHANNEL_CAST);
	} else if (command == "name") {
		std::string name = text.substr(pos + 1);
		int i = std::count_if(name.begin(), name.end(), [](char c) { return (!(std::isalpha(c)) && c != ' '); });
		if (name.empty() || name.length() < 2 || name.length() > 30 || name == "/name" || i > 0) {
			sendChannelMessage("", "[Error] Invalid name. Must be 2-30 characters, letters only.", TALKTYPE_CHANNEL_R1,
			                   CHANNEL_CAST);
			sendChannelMessage("", "Example: /name Marksman Jack", TALKTYPE_CHANNEL_R1, CHANNEL_CAST);
			return;
		}
		if (name == player->getName() || name == caster->getName() ||
		    caster->spectatorBans.find(name) != caster->spectatorBans.end()) {
			sendChannelMessage("", "[Error] This name is already in use.", TALKTYPE_CHANNEL_R1, CHANNEL_CAST);
			return;
		}
		for (ProtocolSpectator* Spectator : caster->getSpectators()) {
			if (Spectator->player->getName() == name) {
				sendChannelMessage("", "[Error] This name is already in use.", TALKTYPE_CHANNEL_R1, CHANNEL_CAST);
				return;
			}
		}

		caster->sendChannelMessage("", player->getName() + " changed name to " + name, TALKTYPE_CHANNEL_O,
		                           CHANNEL_CAST);
		player->setName(name);

	} else if (command == "spectators") {
		sendChannelMessage("", "========================================", TALKTYPE_CHANNEL_O, CHANNEL_CAST);
		std::stringstream ss;
		ss << "       SPECTATORS (" << caster->getSpectatorCount() << ")";
		sendChannelMessage("", ss.str(), TALKTYPE_CHANNEL_O, CHANNEL_CAST);
		sendChannelMessage("", "========================================", TALKTYPE_CHANNEL_O, CHANNEL_CAST);
		for (ProtocolSpectator* Spectator : caster->getSpectators()) {
			sendChannelMessage("", "  - " + Spectator->player->getName(), TALKTYPE_CHANNEL_O, CHANNEL_CAST);
		}
		sendChannelMessage("", "========================================", TALKTYPE_CHANNEL_O, CHANNEL_CAST);
	}
}

void ProtocolSpectator::parseSay(NetworkMessage& msg)
{
	if (std::find(caster->spectatorMutes.begin(), caster->spectatorMutes.end(), player) !=
	    caster->spectatorMutes.end()) {
		sendChannelMessage("", "You are muted.", TALKTYPE_CHANNEL_R1, CHANNEL_CAST);
		return;
	}

	if (caster->liveChatDisabled) {
		sendChannelMessage("", "The live cast chat is currently disabled.", TALKTYPE_CHANNEL_R1, CHANNEL_CAST);
		return;
	}

	SpeakClasses type = static_cast<SpeakClasses>(msg.getByte());
	switch (type) {
		case TALKTYPE_PRIVATE_PN:
		case TALKTYPE_PRIVATE_RED:
			msg.getString(); // reciever
			break;

		case TALKTYPE_CHANNEL_Y:
		case TALKTYPE_CHANNEL_R1:
			msg.get<uint16_t>();
			break;

		default:
			break;
	}

	const std::string text = std::string(msg.getString());
	if (text.length() > 255) {
		return;
	}

	InstantSpell* instantSpell = g_spells->getInstantSpell(text);
	if (instantSpell) {
		return;
	}

	if (!text.empty() && text.front() == '/') {
		parseExecuteCommand(text);
	} else {
		caster->sendChannelMessage(player->getName(), text, TALKTYPE_CHANNEL_Y, CHANNEL_CAST);
	}
}

void ProtocolSpectator::sendCreatureLight(const Creature* creature)
{
	NetworkMessage msg;
	AddCreatureLight(msg, creature);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendWorldLight(const LightInfo& lightInfo)
{
	NetworkMessage msg;
	msg.addByte(0x82);
	if (caster && caster->isAccessPlayer()) {
		msg.addByte(0xFF);
	} else {
		msg.addByte(lightInfo.level);
	}
	msg.addByte(lightInfo.color);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendMagicEffect(const Position& pos, uint16_t type)
{
	if (!canSee(pos)) {
		return;
	}

	NetworkMessage msg;
	msg.addByte(0x83);
	msg.addPosition(pos);
	msg.add<uint16_t>(type);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendDistanceShoot(const Position& from, const Position& to, uint16_t type)
{
	NetworkMessage msg;
	msg.addByte(0x85);
	msg.addPosition(from);
	msg.addPosition(to);
	msg.add<uint16_t>(type);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendStats()
{
	NetworkMessage msg;
	AddPlayerStats(msg);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendBasicData()
{
	NetworkMessage msg;
	msg.addByte(0x9F);
	if (caster->isPremium()) {
		msg.addByte(1);
		msg.add<uint32_t>(time(nullptr) + (caster->premiumEndsAt > time(nullptr)
		                                       ? (caster->premiumEndsAt - time(nullptr)) / 86400
		                                       : 0));
	} else {
		msg.addByte(0);
		msg.add<uint32_t>(0);
	}
	msg.addByte(caster->getVocation()->getClientId());
	msg.add<uint16_t>(0xFF); // number of known spells
	for (uint8_t spellId = 0x00; spellId < 0xFF; spellId++) {
		msg.addByte(spellId);
	}
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::AddOutfit(NetworkMessage& msg, const Outfit_t& outfit)
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

	if (isOTCv8 || version != 861) {
		msg.add<uint16_t>(outfit.lookMount);
	}
}

void ProtocolSpectator::sendChannel(uint16_t channelId, const std::string& channelName, const UsersMap* channelUsers,
                                    const InvitedMap* invitedUsers)
{
	NetworkMessage msg;
	msg.addByte(0xAC);

	msg.add<uint16_t>(channelId);
	msg.addString(channelName);

	if (channelUsers) {
		msg.add<uint16_t>(channelUsers->size());
		for (const auto& it : *channelUsers) {
			msg.addString(it.second->getName());
		}
	} else {
		msg.add<uint16_t>(0x00);
	}

	if (invitedUsers) {
		msg.add<uint16_t>(invitedUsers->size());
		for (const auto& it : *invitedUsers) {
			msg.addString(it.second->getName());
		}
	} else {
		msg.add<uint16_t>(0x00);
	}
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendChannelMessage(const std::string& author, const std::string& text, SpeakClasses type,
                                           uint16_t channel)
{
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

void ProtocolSpectator::sendContainer(uint8_t cid, const Container* container, bool hasParent, uint16_t firstIndex)
{
	NetworkMessage msg;
	msg.addByte(0x6E);

	msg.addByte(cid);

	msg.addItem(container);
	msg.addString(container->getName());

	msg.addByte(static_cast<uint8_t>(container->capacity()));

	msg.addByte(hasParent ? 0x01 : 0x00);

	msg.addByte(static_cast<uint8_t>(std::min<uint32_t>(0xFF, container->size())));

	uint32_t i = 0;
	const ItemDeque& itemList = container->getItemList();
	for (ItemDeque::const_iterator cit = itemList.begin() + firstIndex, end = itemList.end(); i < 0xFF && cit != end;
	     ++cit, ++i) {
		msg.addItem(*cit);
	}
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendCreatureTurn(const Creature* creature, uint32_t stackPos)
{
	NetworkMessage msg;
	msg.addByte(0x6B);
	msg.addPosition(creature->getPosition());
	msg.addByte(stackPos);
	msg.add<uint16_t>(0x63);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(creature->getDirection());
	msg.addByte(caster->canWalkthroughEx(creature) ? 0x00 : 0x01);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendCancelWalk()
{
	NetworkMessage msg;
	msg.addByte(0xB5);
	msg.addByte(caster->getDirection());
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendSkills()
{
	NetworkMessage msg;
	AddPlayerSkills(msg);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendMapDescription(const Position& pos)
{
	NetworkMessage msg;
	msg.addByte(0x64);
	msg.addPosition(caster->getPosition());
	GetMapDescription(pos.x - 8, pos.y - 6, pos.z, 18, 14, msg);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendPendingStateEntered()
{
	NetworkMessage msg;
	msg.addByte(0x0A);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendEnterWorld()
{
	NetworkMessage msg;
	msg.addByte(0x0F);
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::sendAddCreature(const Creature* creature, const Position& pos, int32_t, bool)
{
	NetworkMessage msg;
	msg.addByte(0x0A);

	msg.add<uint32_t>(caster->getID());
	msg.add<uint16_t>(0x32); // beat duration (50)

	msg.addByte(0x00); // can report bugs? (always false for spectators)

	writeToOutputBuffer(msg);

	sendMapDescription(pos);

	sendInventoryItem(CONST_SLOT_HEAD, caster->getInventoryItem(CONST_SLOT_HEAD));
	sendInventoryItem(CONST_SLOT_NECKLACE, caster->getInventoryItem(CONST_SLOT_NECKLACE));
	sendInventoryItem(CONST_SLOT_BACKPACK, caster->getInventoryItem(CONST_SLOT_BACKPACK));
	sendInventoryItem(CONST_SLOT_ARMOR, caster->getInventoryItem(CONST_SLOT_ARMOR));
	sendInventoryItem(CONST_SLOT_RIGHT, caster->getInventoryItem(CONST_SLOT_RIGHT));
	sendInventoryItem(CONST_SLOT_LEFT, caster->getInventoryItem(CONST_SLOT_LEFT));
	sendInventoryItem(CONST_SLOT_LEGS, caster->getInventoryItem(CONST_SLOT_LEGS));
	sendInventoryItem(CONST_SLOT_FEET, caster->getInventoryItem(CONST_SLOT_FEET));
	sendInventoryItem(CONST_SLOT_RING, caster->getInventoryItem(CONST_SLOT_RING));
	sendInventoryItem(CONST_SLOT_AMMO, caster->getInventoryItem(CONST_SLOT_AMMO));

	sendStats();
	sendSkills();

	// gameworld light-settings
	LightInfo lightInfo = g_game.getWorldLightInfo();
	sendWorldLight(lightInfo);

	// player light level
	sendCreatureLight(creature);

	// sendBasicData(); // Not needed for 8.60 or might be incorrect
}

void ProtocolSpectator::sendAddTileCreature(const Creature* creature, const Position& pos, int32_t stackpos)
{
	if (!canSee(pos)) {
		return;
	}

	if (stackpos >= 0 && stackpos < MAX_STACKPOS_THINGS) {
		NetworkMessage msg;
		msg.addByte(0x6A);
		msg.addPosition(pos);
		msg.addByte(static_cast<uint8_t>(stackpos));

		bool known;
		uint32_t removedKnown;
		checkCreatureAsKnown(creature->getID(), known, removedKnown);
		AddCreature(msg, creature, known, removedKnown);
		writeToOutputBuffer(msg);
	}
}

void ProtocolSpectator::sendMoveCreature(const Creature* creature, const Position& newPos, int32_t newStackPos,
                                    const Position& oldPos, int32_t oldStackPos, bool teleport)
{
	if (canSee(oldPos) && canSee(creature->getPosition())) {
		if (teleport || (oldPos.z == 7 && newPos.z >= 8) || oldStackPos >= MAX_STACKPOS_THINGS) {
			sendRemoveTileThing(oldPos, oldStackPos);
			sendAddCreature(creature, newPos, newStackPos, 0);
		} else {
			NetworkMessage msg;
			msg.addByte(0x6D);
			msg.addPosition(oldPos);
			msg.addByte(static_cast<uint8_t>(oldStackPos));
			msg.addPosition(newPos);

			if (newPos.z > oldPos.z) {
				MoveDownCreature(msg, newPos, oldPos);
			} else if (newPos.z < oldPos.z) {
				MoveUpCreature(msg, newPos, oldPos);
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
		}
	} else if (canSee(oldPos)) {
		sendRemoveTileThing(oldPos, oldStackPos);
	} else if (canSee(creature->getPosition())) {
		sendAddCreature(creature, newPos, newStackPos, 0);
	}
}

void ProtocolSpectator::MoveDownCreature(NetworkMessage& msg, const Position& newPos,
                                    const Position& oldPos)
{
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

void ProtocolSpectator::MoveUpCreature(NetworkMessage& msg, const Position& newPos,
                                  const Position& oldPos)
{
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

void ProtocolSpectator::sendInventoryItem(slots_t slot, const Item* item)
{
	NetworkMessage msg;
	if (item) {
		msg.addByte(0x78);
		msg.addByte(slot);
		msg.addItem(item);
	} else {
		msg.addByte(0x79);
		msg.addByte(slot);
	}
	writeToOutputBuffer(msg);
}

void ProtocolSpectator::AddCreature(NetworkMessage& msg, const Creature* creature, bool known, uint32_t remove)
{
	const Player* otherPlayer = creature->getPlayer();

	if (known) {
		msg.add<uint16_t>(0x62);
		msg.add<uint32_t>(creature->getID());
	} else {
		msg.add<uint16_t>(0x61);
		msg.add<uint32_t>(remove);
		msg.add<uint32_t>(creature->getID());
		msg.addString(creature->getName());
	}

	if (creature->isHealthHidden()) {
		msg.addByte(0x00);
	} else {
		msg.addByte(std::ceil(
		    (static_cast<double>(creature->getHealth()) / std::max<int32_t>(creature->getMaxHealth(), 1)) * 100));
	}

	msg.addByte(creature->getDirection());

	if (!creature->isInGhostMode() && !creature->isInvisible()) {
		AddOutfit(msg, creature->getCurrentOutfit());
	} else {
		static Outfit_t outfit;
		AddOutfit(msg, outfit);
	}

	LightInfo lightInfo = creature->getCreatureLight();
	// Access player (GM) check for light level
	msg.addByte(player->isAccessPlayer() ? 0xFF : lightInfo.level);
	msg.addByte(lightInfo.color);

	msg.add<uint16_t>(creature->getStepSpeed());

	msg.addByte(caster->getSkullClient(creature));
	msg.addByte(caster->getPartyShield(otherPlayer));

	if (!known) {
		msg.addByte(caster->getGuildEmblem(otherPlayer));
	}

	msg.addByte(caster->canWalkthroughEx(creature) ? 0x00 : 0x01);
}

void ProtocolSpectator::AddPlayerStats(NetworkMessage& msg)
{
	msg.addByte(0xA0);

	msg.add<uint32_t>(caster->getHealth());
	msg.add<uint32_t>(caster->getMaxHealth());

	msg.add<uint32_t>(caster->getFreeCapacity());

	msg.add<uint32_t>(std::min<uint64_t>(caster->getExperience(), std::numeric_limits<int32_t>::max()));

	msg.add<uint16_t>(caster->getLevel());
	msg.addByte(caster->getLevelPercent());

	msg.add<uint32_t>(caster->getMana());
	msg.add<uint32_t>(caster->getMaxMana());

	msg.addByte(std::min<uint32_t>(caster->getMagicLevel(), std::numeric_limits<uint8_t>::max()));

	if (isOTCv8) {
		msg.addByte(std::min<uint32_t>(caster->getBaseMagicLevel(), std::numeric_limits<uint8_t>::max()));
	}

	msg.addByte(caster->getMagicLevelPercent());

	msg.addByte(caster->getSoul());

	msg.add<uint16_t>(caster->getStaminaMinutes());

	if (isOTCv8) {
		msg.add<uint16_t>(caster->getOfflineTrainingTime() / 60 / 1000);
	}

	if (isOTCv8) {
		msg.add<uint16_t>(caster->getBaseSpeed() / 2);
	}
}

void ProtocolSpectator::AddPlayerSkills(NetworkMessage& msg)
{
	msg.addByte(0xA1);

	if (!isOTCv8) {
		for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
			msg.addByte(std::min<int32_t>(caster->getSkillLevel(i), std::numeric_limits<uint8_t>::max()));
			msg.addByte(caster->getSkillPercent(i));
		}
	} else {
		for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
			msg.add<uint16_t>(std::min<int32_t>(caster->getSkillLevel(i), std::numeric_limits<uint16_t>::max()));
			msg.add<uint16_t>(caster->getBaseSkill(i));
			msg.addByte(caster->getSkillPercent(i));
		}

		for (uint8_t i = SPECIALSKILL_FIRST; i <= SPECIALSKILL_LAST; ++i) {
			msg.add<uint16_t>(std::min<int32_t>(10000, caster->varSpecialSkills[i]));
			msg.add<uint16_t>(0);
		}
	}
}

void ProtocolSpectator::AddCreatureLight(NetworkMessage& msg, const Creature* creature)
{
	LightInfo lightInfo = creature->getCreatureLight();
	msg.addByte(0x8D);
	msg.add<uint32_t>(creature->getID());
	msg.addByte(lightInfo.level);
	msg.addByte(lightInfo.color);
}

void ProtocolSpectator::parseLookAt(NetworkMessage& msg)
{
	Position pos = msg.getPosition();
	msg.skipBytes(2);
	uint8_t stackpos = msg.getByte();

	if (!caster) {
		return;
	}

	if (pos.x != 0xFFFF && !canSee(pos)) {
		return;
	}

	g_dispatcher.addTask(DISPATCHER_TASK_EXPIRATION,
	                     [=, casterID = caster->getID()]() { g_game.playerLookAt(casterID, pos, stackpos); });
}

void ProtocolSpectator::parseSwitchCast(uint8_t direction)
{
	if (!caster || !player) {
		return;
	}

	std::vector<Player*> casters = g_game.getLiveCasters("");

	if (casters.empty()) {
		sendTextMessage(MESSAGE_STATUS_SMALL, "No live casts available.");
		return;
	}

	auto it = std::find(casters.begin(), casters.end(), caster);
	if (it == casters.end()) {
		if (!casters.empty()) {
			Player* newCaster = casters[0];
			if (newCaster && newCaster != caster) {
				caster->removeSpectator(this);
				caster->sendChannelMessage("", player->getName() + " has left the cast.", TALKTYPE_CHANNEL_O,
				                           CHANNEL_CAST);

				knownCreatureSet.clear();
				caster = newCaster;
				caster->addSpectator(this);
				sendAddCreature(caster, caster->getPosition(), 0, false);
				syncOpenContainers();
				caster->sendChannelMessage("", player->getName() + " has joined the cast.", TALKTYPE_CHANNEL_O,
				                           CHANNEL_CAST);
				sendMagicEffect(caster->getPosition(), CONST_ME_TELEPORT);
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
	if (!newCaster || newCaster == caster) {
		return;
	}

	caster->removeSpectator(this);
	caster->sendChannelMessage("", player->getName() + " has left the cast.", TALKTYPE_CHANNEL_O, CHANNEL_CAST);

	knownCreatureSet.clear();
	caster = newCaster;
	caster->addSpectator(this);
	sendAddCreature(caster, caster->getPosition(), 0, false);
	syncOpenContainers();
	caster->sendChannelMessage("", player->getName() + " has joined the cast.", TALKTYPE_CHANNEL_O, CHANNEL_CAST);
	sendMagicEffect(caster->getPosition(), CONST_ME_TELEPORT);

	std::stringstream ss;
	ss << "Switched to cast: " << caster->getName();
	sendTextMessage(MESSAGE_STATUS_CONSOLE_BLUE, ss.str());
}

void ProtocolSpectator::sendWelcomeMessage()
{
	std::string message =
	    "Welcome to the Live Cast System!\n\n"
	    "Do you know you can use CTRL + ARROWS to switch casts?\n\n"
	    "Voce sabia que pode usar CTRL + SETAS para alternar casts?\n\n"
	    "Type /commands in the cast channel to see available commands.";

	sendTextMessage(MESSAGE_EVENT_ADVANCE, message);
}

void ProtocolSpectator::sendTextMessage(MessageClasses mclass, const std::string& message)
{
	NetworkMessage msg;
	msg.addByte(0xB4);
	msg.addByte(mclass);
	msg.addString(message);
	writeToOutputBuffer(msg);
}
