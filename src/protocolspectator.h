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

#ifndef FS_PROTOCOSPECTATOR_H_D3CA2A5D1AC348D7FE8F37E40036BA80
#define FS_PROTOCOSPECTATOR_H_D3CA2A5D1AC348D7FE8F37E40036BA80

#include "protocolgame.h"

class NetworkMessage;
class Player;
class Game;
class House;
class Container;
class Tile;
class Connection;
class Quest;
class ProtocolSpectator;
typedef std::shared_ptr<ProtocolSpectator> ProtocolSpectator_ptr;

extern Game g_game;

class ProtocolSpectator final : public ProtocolGame
{
public:
	// static protocol information
	enum
	{
		server_sends_first = true
	};
	enum
	{
		protocol_identifier = 0
	}; // Not required as we send first
	enum
	{
		use_checksum = true
	};
	static const char* protocol_name() { return "live caster protocol"; }

	explicit ProtocolSpectator(Connection_ptr connection) : ProtocolGame(connection) {}

	void login(const std::string& name, const std::string& password, OperatingSystem_t operatingSystem);
	void logout(bool displayEffect = false, bool forced = false);

	uint16_t getVersion() const { return version; }

	// Known creature management - fix for creature desync bug
	void removeKnownCreature(uint32_t creatureId);

	bool isAcceptingPackets() const { return acceptPackets; }

private:
	ProtocolSpectator_ptr getThis() { return std::static_pointer_cast<ProtocolSpectator>(shared_from_this()); }
	void syncOpenContainers();
	void connect(uint32_t playerId, OperatingSystem_t operatingSystem);
	void disconnectClient(const std::string& message) const;
	void writeToOutputBuffer(const NetworkMessage& msg);
	void sendPingBack();
	void sendPing();
	void sendDllCheck();

	void release() final;

	void checkCreatureAsKnown(uint32_t id, bool& known, uint32_t& removedKnown);

	bool canSee(int32_t x, int32_t y, int32_t z) const;
	bool canSee(const Creature*) const;
	bool canSee(const Position& pos) const;

	// we have all the parse methods
	void parsePacket(NetworkMessage& msg) final;
	void onRecvFirstMessage(NetworkMessage& msg) final;
	void onConnect() final;

	// Parse methods
	void parseExecuteCommand(const std::string& text);
	void parseSay(NetworkMessage& msg);
	void parseCloseChannel(NetworkMessage& msg);
	void parseLookAt(NetworkMessage& msg);
	void parseSwitchCast(uint8_t direction); // 0 = previous, 1 = next

	// Send functions
	void sendChannelMessage(const std::string& author, const std::string& text, SpeakClasses type, uint16_t channel);
	void sendChannel(uint16_t channelId, const std::string& channelName, const UsersMap* channelUsers,
	                 const InvitedMap* invitedUsers);
	void sendWelcomeMessage();
	void sendTextMessage(MessageClasses mclass, const std::string& message);

	void sendFeatures();

	void sendSkills();
	void sendCreatureTurn(const Creature* creature, uint32_t stackpos);

	void sendCancelWalk();
	void sendStats();
	void sendBasicData();

	void sendPendingStateEntered();
	void sendEnterWorld();

	void sendCreatureLight(const Creature* creature);
	void sendWorldLight(const LightInfo& lightInfo);
	void sendMagicEffect(const Position& pos, uint16_t type);
	void sendDistanceShoot(const Position& from, const Position& to, uint16_t type);

	// tiles
	void sendMapDescription(const Position& pos);

	void sendAddCreature(const Creature* creature, const Position& pos, int32_t stackpos, bool isLogin);
	void sendAddTileCreature(const Creature* creature, const Position& pos, int32_t stackpos);
	void sendMoveCreature(const Creature* creature, const Position& newPos, int32_t newStackPos, const Position& oldPos,
	                      int32_t oldStackPos, bool teleport);
	void MoveDownCreature(NetworkMessage& msg, const Position& newPos, const Position& oldPos);
	void MoveUpCreature(NetworkMessage& msg, const Position& newPos, const Position& oldPos);

	void sendContainer(uint8_t cid, const Container* container, bool hasParent, uint16_t firstIndex);

	// inventory
	void sendInventoryItem(slots_t slot, const Item* item);

	// Help functions

	// translate a tile to clientreadable format
	void GetTileDescription(const Tile* tile, NetworkMessage& msg);

	// translate a floor to clientreadable format
	void GetFloorDescription(NetworkMessage& msg, int32_t x, int32_t y, int32_t z, int32_t width, int32_t height,
	                         int32_t offset, int32_t& skip);

	// translate a map area to clientreadable format
	void GetMapDescription(int32_t x, int32_t y, int32_t z, int32_t width, int32_t height, NetworkMessage& msg);

	void AddCreature(NetworkMessage& msg, const Creature* creature, bool known, uint32_t remove);
	void AddPlayerStats(NetworkMessage& msg);
	void AddOutfit(NetworkMessage& msg, const Outfit_t& outfit);
	void AddPlayerSkills(NetworkMessage& msg);
	void AddCreatureLight(NetworkMessage& msg, const Creature* creature);

	friend class Player;
	friend class ProtocolGame;

	// Helpers so we don't need to bind every time
	template <typename Callable, typename... Args>
	void addGameTaskWithStats(Callable function, const std::string& description, const std::string& caller,
	                          Args&&... args)
	{
		g_dispatcher.addTask(
		    createTaskWithStats(std::bind(function, &g_game, std::forward<Args>(args)...), description, caller));
	}

	template <typename Callable, typename... Args>
	void addGameTaskTimedWithStats(uint32_t delay, Callable function, const std::string& description,
	                               const std::string& caller, Args&&... args)
	{
		g_dispatcher.addTask(
		    createTaskWithStats(delay, std::bind(function, &g_game, std::forward<Args>(args)...), description, caller));
	}

	std::unordered_set<uint32_t> knownCreatureSet;
	Player* caster = nullptr;

	uint32_t eventConnect = 0;
	uint32_t challengeTimestamp = 0;
	uint32_t dllCheckSequence = 0;
	uint16_t version = CLIENT_VERSION_MIN;

	uint8_t challengeRandom = 0;

	bool isOTCv8 = false;
	bool debugAssertSent = false;
	bool acceptPackets = false;
};

#endif