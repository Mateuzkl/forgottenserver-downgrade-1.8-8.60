// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_PROTOCOLGAME_H
#define FS_PROTOCOLGAME_H

#include "chat.h"
#include "creature.h"
#include "protocol.h"
#include "tasks.h"

class NetworkMessage;
class Player;
class Game;
class House;
class Container;
class Tile;
class Connection;
class ProtocolGame;
using ProtocolGame_ptr = std::shared_ptr<ProtocolGame>;
class ProtocolSpectator;

extern Game g_game;

struct TextMessage
{
	MessageClasses type = MESSAGE_STATUS_DEFAULT;
	std::string text;
	Position position;
	uint16_t channelId;
	struct
	{
		int32_t value = 0;
		TextColor_t color;
	} primary, secondary;

	TextMessage() = default;
	TextMessage(MessageClasses type, std::string_view text) : type{type}, text{text} {}
};

inline constexpr auto OTCV8_NAME = "OTCv8";
inline constexpr auto OTCV8_LENGTH = 5;

class ProtocolGame final : public Protocol
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
	static const char* protocol_name() { return "gameworld protocol"; }

	explicit ProtocolGame(Connection_ptr connection) : Protocol(connection) {}

	void login(uint32_t characterId, uint32_t accountId, OperatingSystem_t operatingSystem);
	void spectate(const std::string& name, const std::string& password);
	void logout(bool displayEffect, bool forced);

	uint16_t getVersion() const { return version; }

	static uint32_t spectatorId;
	static std::set<std::string> spectatorNames;
	const std::string getSpectatorName() const { return spectator_name; }
	void setSpectatorName(const std::string& new_name) { spectator_name = new_name; }

private:
	ProtocolGame_ptr getThis() { return std::static_pointer_cast<ProtocolGame>(shared_from_this()); }
	void connect(uint32_t playerId, OperatingSystem_t operatingSystem);
	void disconnectClient(std::string_view message) const;
	void writeToOutputBuffer(const NetworkMessage& msg);

	void release() override;

	std::pair<bool, uint32_t> isKnownCreature(uint32_t id);

	bool canSee(int32_t x, int32_t y, int32_t z) const;
	bool canSee(const Creature*) const;
	bool canSee(const Position& pos) const;

	// we have all the parse methods
	void parsePacket(NetworkMessage& msg) override;
	void onRecvFirstMessage(NetworkMessage& msg) override;
	void onConnect() override;

	// Parse methods
	void parseAutoWalk(NetworkMessage& msg);
	void parseSetOutfit(NetworkMessage& msg);
	void parseSay(NetworkMessage& msg);
	void parseLookAt(NetworkMessage& msg);
	void parseLookInBattleList(NetworkMessage& msg);
	void parseFightModes(NetworkMessage& msg);
	void parseAttack(NetworkMessage& msg);
	void parseFollow(NetworkMessage& msg);

	void parseBugReport(NetworkMessage& msg);
	void parseRuleViolationReport(NetworkMessage& msg);

	void parseThrow(NetworkMessage& msg);
	void parseUseItemEx(NetworkMessage& msg);
	void parseUseWithCreature(NetworkMessage& msg);
	void parseUseItem(NetworkMessage& msg);
	void parseCloseContainer(NetworkMessage& msg);
	void parseUpArrowContainer(NetworkMessage& msg);
	void parseUpdateContainer(NetworkMessage& msg);
	void parseTextWindow(NetworkMessage& msg);
	void parseHouseWindow(NetworkMessage& msg);

	void parseLookInShop(NetworkMessage& msg);
	void parsePlayerPurchase(NetworkMessage& msg);
	void parsePlayerSale(NetworkMessage& msg);

	void parseInviteToParty(NetworkMessage& msg);
	void parseJoinParty(NetworkMessage& msg);
	void parseRevokePartyInvite(NetworkMessage& msg);
	void parsePassPartyLeadership(NetworkMessage& msg);
	void parseEnableSharedPartyExperience(NetworkMessage& msg);

	void parseModalWindowAnswer(NetworkMessage& msg);

	// trade methods
	void parseRequestTrade(NetworkMessage& msg);
	void parseLookInTrade(NetworkMessage& msg);

	// VIP methods
	void parseAddVip(NetworkMessage& msg);
	void parseRemoveVip(NetworkMessage& msg);

	void parseRotateItem(NetworkMessage& msg);

	// Channel tabs
	void parseChannelInvite(NetworkMessage& msg);
	void parseChannelExclude(NetworkMessage& msg);
	void parseOpenChannel(NetworkMessage& msg);
	void parseOpenPrivateChannel(NetworkMessage& msg);
	void parseCloseChannel(NetworkMessage& msg);

	// Send functions
	void sendChannelMessage(std::string_view author, std::string_view text, SpeakClasses type, uint16_t channel);
	void sendClosePrivate(uint16_t channelId);
	void sendCreatePrivateChannel(uint16_t channelId, std::string_view channelName);
	void sendChannelsDialog();
	void sendChannel(uint16_t channelId, std::string_view channelName);
	void sendOpenPrivateChannel(std::string_view receiver);
	void sendToChannel(const Creature* creature, SpeakClasses type, std::string_view text, uint16_t channelId);
	void sendPrivateMessage(const Player* speaker, SpeakClasses type, std::string_view text);
	void sendIcons(uint16_t icons);
	void sendFYIBox(std::string_view message);

	void sendDistanceShoot(const Position& from, const Position& to, uint16_t type);
	void sendMagicEffect(const Position& pos, uint16_t type);
	void sendCreatureHealth(const Creature* creature);
	void sendSkills();
	void sendPing();
	void sendDllCheck();
	void sendCreatureTurn(const Creature* creature, uint32_t stackpos);
	void sendCreatureSay(const Creature* creature, SpeakClasses type, std::string_view text,
	                     const Position* pos = nullptr);

	void sendCancelWalk();
	void sendChangeSpeed(const Creature* creature, uint32_t speed);
	void sendCancelTarget();
	void sendCreatureOutfit(const Creature* creature, const Outfit_t& outfit);
	void sendStats();
	void sendTextMessage(const TextMessage& message);
	void sendReLoginWindow();

	void sendTutorial(uint8_t tutorialId);
	void sendAddMarker(const Position& pos, uint8_t markType, std::string_view desc);

	void sendCreatureWalkthrough(const Creature* creature, bool walkthrough);
	void sendCreatureShield(const Creature* creature);
	void sendCreatureSkull(const Creature* creature);
	void sendCreatureEmblem(const Creature* creature);

	void sendShop(const ShopInfoList& itemList);
	void sendCloseShop();
	void sendSaleItemList(const std::list<ShopInfo>& shop);
	void sendTradeItemRequest(std::string_view traderName, const Item* item, bool ack);
	void sendCloseTrade();

	void sendTextWindow(uint32_t windowTextId, Item* item, uint16_t maxlen, bool canWrite);
	void sendTextWindow(uint32_t windowTextId, uint16_t itemId, std::string_view text);
	void sendHouseWindow(uint32_t windowTextId, std::string_view text);
	void sendOutfitWindow();

	void sendUpdatedVIPStatus(uint32_t guid, VipStatus_t newStatus);
	void sendVIP(uint32_t guid, std::string_view name, VipStatus_t status);

	void sendFightModes();

	void sendAnimatedText(std::string_view message, const Position& pos, TextColor_t color);

	void sendCreatureLight(const Creature* creature);
	void sendWorldLight(LightInfo lightInfo);

	void sendCreatureSquare(const Creature* creature, SquareColor_t color);
	void sendSpellCooldown(uint8_t spellId, uint32_t time);
	void sendSpellGroupCooldown(SpellGroup_t groupId, uint32_t time);
	void sendUseItemCooldown(uint32_t time);

	// tiles
	void sendMapDescription(const Position& pos);

	void sendAddTileItem(const Position& pos, uint32_t stackpos, const Item* item);
	void sendUpdateTileItem(const Position& pos, uint32_t stackpos, const Item* item);
	void sendRemoveTileThing(const Position& pos, uint32_t stackpos);
	void sendUpdateTileCreature(const Position& pos, uint32_t stackpos, const Creature* creature);
	void sendUpdateTile(const Tile* tile, const Position& pos);

	void sendAddCreature(const Creature* creature, const Position& pos, int32_t stackpos,
	                     MagicEffectClasses magicEffect = CONST_ME_NONE);
	void sendMoveCreature(const Creature* creature, const Position& newPos, int32_t newStackPos, const Position& oldPos,
	                      int32_t oldStackPos, bool teleport);

	// containers
	void sendAddContainerItem(uint8_t cid, const Item* item);
	void sendUpdateContainerItem(uint8_t cid, uint16_t slot, const Item* item);
	void sendRemoveContainerItem(uint8_t cid, uint16_t slot);

	void sendContainer(uint8_t cid, const Container* container, bool hasParent, uint16_t firstIndex);
	void sendCloseContainer(uint8_t cid);

	// inventory
	void sendInventoryItem(slots_t slot, const Item* item);

	// messages
	void sendModalWindow(const ModalWindow& modalWindow);

	// Help functions

	// translate a tile to client-readable format
	void GetTileDescription(const Tile* tile, NetworkMessage& msg);

	// translate a floor to client-readable format
	void GetFloorDescription(NetworkMessage& msg, int32_t x, int32_t y, int32_t z, int32_t width, int32_t height,
	                         int32_t offset, int32_t& skip);

	// translate a map area to client-readable format
	void GetMapDescription(int32_t x, int32_t y, int32_t z, int32_t width, int32_t height, NetworkMessage& msg);

	void AddCreature(NetworkMessage& msg, const Creature* creature, bool known, uint32_t remove);
	void AddPlayerStats(NetworkMessage& msg);
	void AddOutfit(NetworkMessage& msg, const Outfit_t& outfit);
	void AddPlayerSkills(NetworkMessage& msg);
	void AddWorldLight(NetworkMessage& msg, LightInfo lightInfo);
	void AddCreatureLight(NetworkMessage& msg, const Creature* creature);

	// tiles
	static void RemoveTileThing(NetworkMessage& msg, const Position& pos, uint32_t stackpos);

	void MoveUpCreature(NetworkMessage& msg, const Creature* creature, const Position& newPos, const Position& oldPos);
	void MoveDownCreature(NetworkMessage& msg, const Creature* creature, const Position& newPos,
	                      const Position& oldPos);

	// shop
	void AddShopItem(NetworkMessage& msg, const ShopInfo& item);

	// otclient
	void parseExtendedOpcode(NetworkMessage& msg);

	// OTCv8
	void sendFeatures();
	void sendNewPing(uint32_t pingId);
	void parseNewPing(NetworkMessage& msg);

	friend class Player;
	friend class ProtocolSpectator;

	//cast
	void spectatorTurn(uint8_t direction);
	void parseSpectatorSay(NetworkMessage& msg);
	void spectatorSay(const std::string text, uint16_t channelId);
	void sendCastChannel();
	void syncOpenContainers();

	bool isSpectator = false;
	std::string spectator_name = "";

	std::unordered_set<uint32_t> knownCreatureSet;
	Player* player = nullptr;

	uint32_t eventConnect = 0;
	uint32_t challengeTimestamp = 0;
	uint16_t version = CLIENT_VERSION_MIN;

	uint8_t challengeRandom = 0;

	// Helpers so we don't need to bind every time
	template <typename Callable, typename... Args>
	void addGameTaskWithStats(Callable&& function, const std::string& function_str, const std::string& extra_info, Args&&... args) {
		g_dispatcher.addTask(createTaskWithStats(std::bind(std::forward<Callable>(function), &g_game, std::forward<Args>(args)...), function_str, extra_info));
	}

	template <typename Callable, typename... Args>
	void addGameTaskTimedWithStats(uint32_t delay, Callable&& function, const std::string& function_str, const std::string& extra_info, Args&&... args) {
		g_dispatcher.addTask(createTaskWithStats(delay, std::bind(std::forward<Callable>(function), &g_game, std::forward<Args>(args)...), function_str, extra_info));
	}

	bool isOTCv8 = false;
	bool debugAssertSent = false;
	bool acceptPackets = false;

	uint32_t dllCheckSequence = 0;
};

#endif
