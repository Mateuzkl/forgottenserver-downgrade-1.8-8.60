#ifndef FS_PROTOCOLSPECTATOR_H
#define FS_PROTOCOLSPECTATOR_H

#include "chat.h"
#include "creature.h"
#include "protocol.h"
#include "protocolgame.h"
#include "tasks.h"
#include "tools.h"

#include <map>
#include <set>
#include <string_view>

class NetworkMessage;
class Player;
class Game;
class House;
class Container;
class Tile;
class Connection;
class Quest;
class ProtocolSpectator;
using ProtocolSpectator_ptr = std::shared_ptr<ProtocolSpectator>;
extern Game g_game;

typedef std::map<std::string, uint32_t> DataList;

class ProtocolSpectator
{
public:
	explicit ProtocolSpectator(ProtocolGame_ptr protocol) : owner(protocol) {}
	~ProtocolSpectator() { setBroadcast(false); }

	void clear(bool reset = true)
	{
		for (auto &it : spectators) it->disconnect();
		spectators.clear();
		mutes.clear();
		bans.clear();

		if (reset) {
			cast_password = "";
			cast_description = "";
			broadcast = false;
		}
	}

	void addSpectator(ProtocolGame_ptr spectator);
	void removeSpectator(ProtocolGame_ptr spectator);
	void spectatorSay(ProtocolGame_ptr spectator, std::string_view text);
	void sendCastMessage(const std::string &author, std::string_view text, SpeakClasses type,
	                     ProtocolGame_ptr target = nullptr);

	void sendCastChannel()
	{
		auto o = owner.lock();
		if (o) o->sendCastChannel();
	}

	DataList spectatorList() const
	{
		DataList ret;
		for (auto &it : spectators) ret[it->getSpectatorName()] = it->getIP();
		return ret;
	}

	void kick(const DataList &_kicks);
	DataList muteList() const { return mutes; }
	void mute(const DataList &_mutes) { mutes = _mutes; }
	DataList banList() const { return bans; }
	void ban(const DataList &_bans);
	bool isBanned(uint32_t ip) const;

	const std::string password() const { return cast_password; }
	void setPassword(const std::string &new_password) { cast_password = new_password; }

	const std::string description() const { return cast_description; }
	void setDescription(const std::string &new_description) { cast_description = new_description; }

	bool isBroadcasting() const { return broadcast; }
	void setBroadcast(bool value);

	bool isWaitingForUpdate() const { return update_status; }
	void setUpdateStatus(bool value) { update_status = value; }

	ProtocolGame_ptr protocol() { return owner.lock(); }

	void sendPing()
	{
		auto o = owner.lock();
		if (o) o->sendPing();

		for (auto &it : spectators) it->sendPing();
	}

private:
	uint16_t getVersion() const
	{
		auto o = owner.lock();
		if (!o) return 0;

		return o->getVersion();
	}

	void disconnect() const
	{
		auto o = owner.lock();
		if (o) o->disconnect();

		for (auto &it : spectators) it->disconnect();
	}

	void setOwner(ProtocolGame_ptr new_owner)
	{
		owner = new_owner;
		if (!new_owner) clear();
	}

	uint32_t getIP() const
	{
		auto o = owner.lock();
		if (o) return o->getIP();

		return 0;
	}

	void connect(uint32_t playerId, OperatingSystem_t operatingSystem)
	{
		auto o = owner.lock();
		if (o) o->connect(playerId, operatingSystem);

		for (auto &it : spectators) it->connect(playerId, operatingSystem);
	}

	void logout(bool displayEffect, bool forced)
	{
		auto o = owner.lock();
		if (o) o->logout(displayEffect, forced);
		clear();
	}

	void disconnectClient(std::string_view message) const
	{
		auto o = owner.lock();
		if (o) o->disconnectClient(message);

		for (auto &it : spectators) it->disconnectClient(message);
	}

	void writeToOutputBuffer(const NetworkMessage &msg)
	{
		auto o = owner.lock();
		if (o) o->writeToOutputBuffer(msg);
	}

	void release()
	{
		clear();

		auto o = owner.lock();
		if (o) o->release();
	}

	bool canSee(int32_t x, int32_t y, int32_t z) const
	{
		auto o = owner.lock();
		if (!o) return false;

		return o->canSee(x, y, z);
	}

	bool canSee(const Creature *creature) const
	{
		auto o = owner.lock();
		if (!o) return false;

		return o->canSee(creature);
	}

	bool canSee(const Position &pos) const
	{
		auto o = owner.lock();
		if (!o) return false;

		return o->canSee(pos);
	}

	// Send functions
	void sendFYIBox(std::string_view message)
	{
		auto o = owner.lock();
		if (o) {
			o->sendFYIBox(message);
		}
	}

	void sendChannelMessage(std::string_view author, std::string_view text, SpeakClasses type, uint16_t channel)
	{
		auto o = owner.lock();
		if (o) o->sendChannelMessage(author, text, type, channel);

		for (auto &it : spectators) it->sendChannelMessage(author, text, type, channel);
	}

	void sendClosePrivate(uint16_t channelId)
	{
		auto o = owner.lock();
		if (o) o->sendClosePrivate(channelId);

		for (auto &it : spectators) it->sendClosePrivate(channelId);
	}

	void sendCreatePrivateChannel(uint16_t channelId, std::string_view channelName)
	{
		auto o = owner.lock();
		if (o) o->sendCreatePrivateChannel(channelId, channelName);

		for (auto &it : spectators) it->sendCreatePrivateChannel(channelId, channelName);
	}

	void sendChannelsDialog()
	{
		auto o = owner.lock();
		if (o) o->sendChannelsDialog();
	}

	void sendChannel(uint16_t channelId, std::string_view channelName)
	{
		auto o = owner.lock();
		if (o) o->sendChannel(channelId, channelName);

		for (auto &it : spectators) it->sendChannel(channelId, channelName);
	}

	void sendTutorial(uint8_t tutorialId)
	{
		auto o = owner.lock();
		if (o) {
			o->sendTutorial(tutorialId);
		}
	}

	void sendAddMarker(const Position &pos, uint8_t markType, std::string_view desc)
	{
		auto o = owner.lock();
		if (o) {
			o->sendAddMarker(pos, markType, desc);
		}
	}

	void sendOpenPrivateChannel(std::string_view receiver)
	{
		auto o = owner.lock();
		if (o) o->sendOpenPrivateChannel(receiver);

		for (auto &it : spectators) it->sendOpenPrivateChannel(receiver);
	}

	void sendToChannel(const Creature *creature, SpeakClasses type, std::string_view text, uint16_t channelId)
	{
		auto o = owner.lock();
		if (o) o->sendToChannel(creature, type, text, channelId);

		for (auto &it : spectators) it->sendToChannel(creature, type, text, channelId);
	}

	void sendShop(const ShopInfoList &itemList)
	{
		auto o = owner.lock();
		if (o) o->sendShop(itemList);

		for (auto &it : spectators) it->sendShop(itemList);
	}

	void sendSaleItemList(const ShopInfoList &shop)
	{
		auto o = owner.lock();
		if (o) o->sendSaleItemList(shop);

		for (auto &it : spectators) it->sendSaleItemList(shop);
	}

	void sendCloseShop()
	{
		auto o = owner.lock();
		if (o) o->sendCloseShop();

		for (auto &it : spectators) it->sendCloseShop();
	}

	void sendPrivateMessage(const Player *speaker, SpeakClasses type, std::string_view text)
	{
		auto o = owner.lock();
		if (o) o->sendPrivateMessage(speaker, type, text);
	}

	void sendIcons(uint16_t icons)
	{
		auto o = owner.lock();
		if (o) o->sendIcons(icons);

		for (auto &it : spectators) it->sendIcons(icons);
	}

	void sendDistanceShoot(const Position &from, const Position &to, uint16_t type)
	{
		auto o = owner.lock();
		if (o) o->sendDistanceShoot(from, to, type);

		for (auto &it : spectators) it->sendDistanceShoot(from, to, type);
	}

	void sendMagicEffect(const Position &pos, uint16_t type)
	{
		auto o = owner.lock();
		if (o) o->sendMagicEffect(pos, type);

		for (auto &it : spectators) it->sendMagicEffect(pos, type);
	}

	void sendCreatureHealth(const Creature *creature)
	{
		auto o = owner.lock();
		if (o) o->sendCreatureHealth(creature);

		for (auto &it : spectators) it->sendCreatureHealth(creature);
	}

	void sendSkills()
	{
		auto o = owner.lock();
		if (o) o->sendSkills();

		for (auto &it : spectators) it->sendSkills();
	}

	void sendAnimatedText(std::string_view message, const Position &pos, TextColor_t color) const
	{
		auto o = owner.lock();
		if (o) {
			o->sendAnimatedText(message, pos, color);
		}

		for (auto &it : spectators) it->sendAnimatedText(message, pos, color);
	}

	void sendCreatureTurn(const Creature *creature, uint32_t stackPos)
	{
		auto o = owner.lock();
		if (o) o->sendCreatureTurn(creature, stackPos);

		for (auto &it : spectators) it->sendCreatureTurn(creature, stackPos);
	}

	void sendCreatureSay(const Creature *creature, SpeakClasses type, std::string_view text,
	                     const Position *pos = nullptr)
	{
		auto o = owner.lock();
		if (o) o->sendCreatureSay(creature, type, text, pos);

		for (auto &it : spectators) it->sendCreatureSay(creature, type, text, pos);
	}

	void sendCancelWalk()
	{
		auto o = owner.lock();
		if (o) o->sendCancelWalk();

		for (auto &it : spectators) it->sendCancelWalk();
	}

	void sendChangeSpeed(const Creature *creature, uint32_t speed)
	{
		auto o = owner.lock();
		if (o) o->sendChangeSpeed(creature, speed);

		for (auto &it : spectators) it->sendChangeSpeed(creature, speed);
	}

	void sendCancelTarget()
	{
		auto o = owner.lock();
		if (o) o->sendCancelTarget();

		for (auto &it : spectators) it->sendCancelTarget();
	}

	void sendCreatureOutfit(const Creature *creature, const Outfit_t &outfit)
	{
		auto o = owner.lock();
		if (o) o->sendCreatureOutfit(creature, outfit);

		for (auto &it : spectators) it->sendCreatureOutfit(creature, outfit);
	}

	void sendStats()
	{
		auto o = owner.lock();
		if (o) o->sendStats();

		for (auto &it : spectators) it->sendStats();
	}

	void sendTextMessage(const TextMessage &message)
	{
		auto o = owner.lock();
		if (o) o->sendTextMessage(message);

		for (auto &it : spectators) it->sendTextMessage(message);
	}

	void sendReLoginWindow() const
	{
		auto o = owner.lock();
		if (o) {
			o->sendReLoginWindow();
		}

		for (auto &it : spectators) it->sendReLoginWindow();
	}

	void sendCreatureShield(const Creature *creature)
	{
		auto o = owner.lock();
		if (o) o->sendCreatureShield(creature);

		for (auto &it : spectators) it->sendCreatureShield(creature);
	}

	void sendCreatureSkull(const Creature *creature)
	{
		auto o = owner.lock();
		if (o) o->sendCreatureSkull(creature);

		for (auto &it : spectators) it->sendCreatureSkull(creature);
	}

	void sendTradeItemRequest(std::string_view traderName, const Item *item, bool ack)
	{
		auto o = owner.lock();
		if (o) o->sendTradeItemRequest(traderName, item, ack);

		for (auto &it : spectators) it->sendTradeItemRequest(traderName, item, ack);
	}

	void sendCloseTrade()
	{
		auto o = owner.lock();
		if (o) o->sendCloseTrade();

		for (auto &it : spectators) it->sendCloseTrade();
	}

	void sendTextWindow(uint32_t windowTextId, Item *item, uint16_t maxlen, bool canWrite)
	{
		auto o = owner.lock();
		if (o) o->sendTextWindow(windowTextId, item, maxlen, canWrite);

		for (auto &it : spectators) it->sendTextWindow(windowTextId, item, maxlen, canWrite);
	}

	void sendTextWindow(uint32_t windowTextId, uint32_t itemId, std::string_view text)
	{
		auto o = owner.lock();
		if (o) o->sendTextWindow(windowTextId, itemId, text);

		for (auto &it : spectators) it->sendTextWindow(windowTextId, itemId, text);
	}

	void sendTextWindow(uint32_t windowTextId, uint16_t itemId, std::string_view text)
	{
		sendTextWindow(windowTextId, static_cast<uint32_t>(itemId), text);
	}

	void sendHouseWindow(uint32_t windowTextId, std::string_view text)
	{
		auto o = owner.lock();
		if (o) o->sendHouseWindow(windowTextId, text);

		for (auto &it : spectators) it->sendHouseWindow(windowTextId, text);
	}

	void sendOutfitWindow()
	{
		auto o = owner.lock();
		if (o) o->sendOutfitWindow();
	}

	void sendUpdatedVIPStatus(uint32_t guid, VipStatus_t newStatus)
	{
		auto o = owner.lock();
		if (o) o->sendUpdatedVIPStatus(guid, newStatus);

		for (auto &it : spectators) it->sendUpdatedVIPStatus(guid, newStatus);
	}

	void sendVIP(uint32_t guid, std::string_view name, VipStatus_t status)
	{
		auto o = owner.lock();
		if (o) o->sendVIP(guid, name, status);

		for (auto &it : spectators) it->sendVIP(guid, name, status);
	}

	void sendFightModes()
	{
		auto o = owner.lock();
		if (o) o->sendFightModes();

		for (auto &it : spectators) it->sendFightModes();
	}

	void sendCreatureLight(const Creature *creature)
	{
		auto o = owner.lock();
		if (o) o->sendCreatureLight(creature);

		for (auto &it : spectators) it->sendCreatureLight(creature);
	}

	void sendCreatureWalkthrough(const Creature *creature, bool walkthrough)
	{
		auto o = owner.lock();
		if (o) o->sendCreatureWalkthrough(creature, walkthrough);

		for (auto &it : spectators) it->sendCreatureWalkthrough(creature, walkthrough);
	}

	void sendWorldLight(LightInfo lightInfo)
	{
		auto o = owner.lock();
		if (o) o->sendWorldLight(lightInfo);

		for (auto &it : spectators) it->sendWorldLight(lightInfo);
	}

	void sendCreatureSquare(const Creature *creature, SquareColor_t color)
	{
		auto o = owner.lock();
		if (o) o->sendCreatureSquare(creature, color);

		for (auto &it : spectators) it->sendCreatureSquare(creature, color);
	}

	// tiles
	void sendMapDescription(const Position &pos)
	{
		auto o = owner.lock();
		if (o) o->sendMapDescription(pos);

		for (auto &it : spectators) it->sendMapDescription(pos);
	}

	void sendAddTileItem(const Position &pos, uint32_t stackpos, const Item *item)
	{
		auto o = owner.lock();
		if (o) o->sendAddTileItem(pos, stackpos, item);

		for (auto &it : spectators) it->sendAddTileItem(pos, stackpos, item);
	}

	void sendUpdateTileItem(const Position &pos, uint32_t stackpos, const Item *item)
	{
		auto o = owner.lock();
		if (o) o->sendUpdateTileItem(pos, stackpos, item);

		for (auto &it : spectators) it->sendUpdateTileItem(pos, stackpos, item);
	}

	void sendRemoveTileThing(const Position &pos, uint32_t stackpos)
	{
		auto o = owner.lock();
		if (o) o->sendRemoveTileThing(pos, stackpos);

		for (auto &it : spectators) it->sendRemoveTileThing(pos, stackpos);
	}

	void sendUpdateTileCreature(const Position &pos, uint32_t stackpos, const Creature *creature)
	{
		auto o = owner.lock();
		if (o) o->sendUpdateTileCreature(pos, stackpos, creature);

		for (auto &it : spectators) it->sendUpdateTileCreature(pos, stackpos, creature);
	}

	void sendRemoveTileCreature(const Creature * /*creature*/, const Position &pos, uint32_t stackpos)
	{
		auto o = owner.lock();
		if (o) o->sendRemoveTileThing(pos, stackpos);

		for (auto &it : spectators) it->sendRemoveTileThing(pos, stackpos);
	}

	void sendUpdateTile(const Tile *tile, const Position &pos)
	{
		auto o = owner.lock();
		if (o) o->sendUpdateTile(tile, pos);

		for (auto &it : spectators) it->sendUpdateTile(tile, pos);
	}

	void sendAddCreature(const Creature *creature, const Position &pos, int32_t stackpos,
	                     MagicEffectClasses magicEffect = CONST_ME_NONE)
	{
		auto o = owner.lock();
		if (o) o->sendAddCreature(creature, pos, stackpos, magicEffect);

		for (auto &it : spectators) it->sendAddCreature(creature, pos, stackpos, magicEffect);
	}

	void sendMoveCreature(const Creature *creature, const Position &newPos, int32_t newStackPos, const Position &oldPos,
	                      int32_t oldStackPos, bool teleport)
	{
		auto o = owner.lock();
		if (o) o->sendMoveCreature(creature, newPos, newStackPos, oldPos, oldStackPos, teleport);

		for (auto &it : spectators) it->sendMoveCreature(creature, newPos, newStackPos, oldPos, oldStackPos, teleport);
	}

	// containers
	void sendAddContainerItem(uint8_t cid, const Item *item)
	{
		auto o = owner.lock();
		if (o) o->sendAddContainerItem(cid, item);

		for (auto &it : spectators) it->sendAddContainerItem(cid, item);
	}

	void sendUpdateContainerItem(uint8_t cid, uint16_t slot, const Item *item)
	{
		auto o = owner.lock();
		if (o) o->sendUpdateContainerItem(cid, slot, item);

		for (auto &it : spectators) it->sendUpdateContainerItem(cid, slot, item);
	}

	void sendRemoveContainerItem(uint8_t cid, uint16_t slot)
	{
		auto o = owner.lock();
		if (o) o->sendRemoveContainerItem(cid, slot);

		for (auto &it : spectators) it->sendRemoveContainerItem(cid, slot);
	}

	void sendContainer(uint8_t cid, const Container *container, bool hasParent, uint16_t firstIndex)
	{
		auto o = owner.lock();
		if (o) o->sendContainer(cid, container, hasParent, firstIndex);

		for (auto &it : spectators) it->sendContainer(cid, container, hasParent, firstIndex);
	}

	void sendCloseContainer(uint8_t cid)
	{
		auto o = owner.lock();
		if (o) o->sendCloseContainer(cid);

		for (auto &it : spectators) it->sendCloseContainer(cid);
	}

	void sendCreatureEmblem(const Creature *creature)
	{
		auto o = owner.lock();
		if (o) o->sendCreatureEmblem(creature);

		for (auto &it : spectators) it->sendCreatureEmblem(creature);
	}

	void sendDllCheck()
	{
		auto o = owner.lock();
		if (o) o->sendDllCheck();

		for (auto &it : spectators) it->sendDllCheck();
	}

	void sendModalWindow(const ModalWindow &modalWindow)
	{
		auto o = owner.lock();
		if (o) o->sendModalWindow(modalWindow);

		for (auto &it : spectators) it->sendModalWindow(modalWindow);
	}

	void sendSpellCooldown(uint8_t spellId, uint32_t time)
	{
		auto o = owner.lock();
		if (o) o->sendSpellCooldown(spellId, time);

		for (auto &it : spectators) it->sendSpellCooldown(spellId, time);
	}

	void sendSpellGroupCooldown(SpellGroup_t groupId, uint32_t time)
	{
		auto o = owner.lock();
		if (o) o->sendSpellGroupCooldown(groupId, time);

		for (auto &it : spectators) it->sendSpellGroupCooldown(groupId, time);
	}

	void sendUseItemCooldown(uint32_t time)
	{
		auto o = owner.lock();
		if (o) o->sendUseItemCooldown(time);

		for (auto &it : spectators) it->sendUseItemCooldown(time);
	}

	void sendInventoryItem(slots_t slot, const Item *item)
	{
		auto o = owner.lock();
		if (o) o->sendInventoryItem(slot, item);

		for (auto &it : spectators) it->sendInventoryItem(slot, item);
	}

	void parseLookAt(NetworkMessage &msg)
	{
		auto o = owner.lock();
		if (!o) {
			return;
		}

		o->parseLookAt(msg);
	}

	friend class ProtocolGame;
	friend class Player;

	std::weak_ptr<ProtocolGame> owner;
	std::set<ProtocolGame_ptr> spectators;

	bool broadcast = false;
	bool update_status = false;
	bool debugAssertSent = false;
	bool isOTCv8 = false;
	std::string cast_password = "";
	std::string cast_description = "";

	DataList mutes;
	DataList bans;
};

#endif
