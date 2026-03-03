#ifndef FS_PROTOCOLSPECTATOR_H
#define FS_PROTOCOLSPECTATOR_H

#include "protocol.h"
#include "protocolgame.h"
#include "chat.h"
#include "creature.h"
#include "tasks.h"
#include "tools.h"
#include <set>
#include <map>
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

class ProtocolSpectator {
    public:
        explicit ProtocolSpectator(ProtocolGame_ptr protocol) : owner(protocol) {}
        ~ProtocolSpectator() {
            setBroadcast(false);
        }

        void clear(bool reset = true) {
            for(auto& it : spectators)
                it->disconnect();
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
        void sendCastMessage(const std::string& author, std::string_view text, SpeakClasses type, ProtocolGame_ptr target = nullptr);

        void sendCastChannel() {
            if(owner)
                owner->sendCastChannel();
        }

        DataList spectatorList() const {
            DataList ret;
            for(auto& it : spectators)
                ret[it->getSpectatorName()] = it->getIP();
            return ret;
        }

        void kick(const DataList& _kicks);
        DataList muteList() const {
            return mutes;
        }
        void mute(const DataList& _mutes) {
            mutes = _mutes;
        }
        DataList banList() const {
            return bans;
        }
        void ban(const DataList& _bans);
        bool isBanned(uint32_t ip) const;

        const std::string password() const {
            return cast_password;
        }
        void setPassword(const std::string& new_password) {
            cast_password = new_password;
        }

        const std::string description() const {
            return cast_description;
        }
        void setDescription(const std::string& new_description) {
            cast_description = new_description;
        }

        bool isBroadcasting() const {
            return broadcast;
        }
        void setBroadcast(bool value);

        bool isWaitingForUpdate() const {
            return update_status;
        }
        void setUpdateStatus(bool value) {
            update_status = value;
        }

	    ProtocolGame_ptr protocol() {
		    return owner;
	    }

        void sendPing() {
            if (owner)
                owner->sendPing();

            for (auto &it : spectators)
                it->sendPing();
        }


    private:
        uint16_t getVersion() const {
            if (!owner)
                return 0;

            return owner->getVersion();
        }

        void disconnect() const {
            if (owner)
                owner->disconnect();

            for (auto &it : spectators)
                it->disconnect();
        }

        void setOwner(ProtocolGame_ptr new_owner) {
            owner = new_owner;
            if(!owner)
                clear();
        }

        uint32_t getIP() const {
            if(owner)
                return owner->getIP();

            return 0;
        }

        void connect(uint32_t playerId, OperatingSystem_t operatingSystem) {
            if (owner)
                owner->connect(playerId, operatingSystem);

            for (auto &it : spectators)
                it->connect(playerId, operatingSystem);
        }

        void logout(bool displayEffect, bool forced) {
            if(owner)
                owner->logout(displayEffect, forced);
            clear();
        }

        void disconnectClient(std::string_view message) const {
            if (owner)
                owner->disconnectClient(message);

            for (auto &it : spectators)
                it->disconnectClient(message);
        }

        void writeToOutputBuffer(const NetworkMessage& msg) {
            if (owner)
                owner->writeToOutputBuffer(msg);
        }

        void release() {
            clear();

            if (owner)
                owner->release();
        }

        bool canSee(int32_t x, int32_t y, int32_t z) const {
            if (!owner)
                return false;

            return owner->canSee(x, y, z);
        }

        bool canSee(const Creature * creature) const {
            if (!owner)
                return false;

            return owner->canSee(creature);
        }

        bool canSee(const Position &pos) const {
            if (!owner)
                return false;

            return owner->canSee(pos);
        }

        //Send functions
        void sendFYIBox(std::string_view message) {
            if (owner) {
                owner->sendFYIBox(message);
            }
        }

        void sendChannelMessage(std::string_view author, std::string_view text, SpeakClasses type, uint16_t channel) {
            if (owner)
                owner->sendChannelMessage(author, text, type, channel);

            for (auto &it : spectators)
                it->sendChannelMessage(author, text, type, channel);
        }

        void sendClosePrivate(uint16_t channelId) {
            if (owner)
                owner->sendClosePrivate(channelId);

            for (auto &it : spectators)
                it->sendClosePrivate(channelId);
        }

        void sendCreatePrivateChannel(uint16_t channelId, std::string_view channelName) {
            if (owner)
                owner->sendCreatePrivateChannel(channelId, channelName);

            for (auto &it : spectators)
                it->sendCreatePrivateChannel(channelId, channelName);
        }

        void sendChannelsDialog() {
            if (owner)
                owner->sendChannelsDialog();
        }

        void sendChannel(uint16_t channelId, std::string_view channelName) {
            if (owner)
                owner->sendChannel(channelId, channelName);

            for (auto& it : spectators)
                it->sendChannel(channelId, channelName);
        }

        void sendTutorial(uint8_t tutorialId) {
            if (owner) {
                owner->sendTutorial(tutorialId);
            }
        }

        void sendAddMarker(const Position& pos, uint8_t markType, std::string_view desc) {
            if (owner) {
                owner->sendAddMarker(pos, markType, desc);
            }
        }

        void sendOpenPrivateChannel(std::string_view receiver) {
            if (owner)
                owner->sendOpenPrivateChannel(receiver);

            for (auto &it : spectators)
                it->sendOpenPrivateChannel(receiver);
        }

        void sendToChannel(const Creature *creature, SpeakClasses type, std::string_view text, uint16_t channelId) {
            if (owner)
                owner->sendToChannel(creature, type, text, channelId);

            for (auto &it : spectators)
                it->sendToChannel(creature, type, text, channelId);
        }

        void sendShop(const ShopInfoList& itemList) {
            if (owner)
                owner->sendShop(itemList);

            for (auto& it : spectators)
                it->sendShop(itemList);
        }

        void sendSaleItemList(const ShopInfoList& shop) {
            if (owner)
                owner->sendSaleItemList(shop);

            for (auto& it : spectators)
                it->sendSaleItemList(shop);
        }

        void sendCloseShop() {
            if (owner)
                owner->sendCloseShop();

            for (auto& it : spectators)
                it->sendCloseShop();
        }

        void sendPrivateMessage(const Player *speaker, SpeakClasses type, std::string_view text) {
            if (owner)
                owner->sendPrivateMessage(speaker, type, text);
        }

        void sendIcons(uint16_t icons) {
            if (owner)
                owner->sendIcons(icons);

            for (auto &it : spectators)
                it->sendIcons(icons);
        }

        void sendDistanceShoot(const Position &from, const Position &to, uint16_t type) {
            if (owner)
                owner->sendDistanceShoot(from, to, type);

            for (auto &it : spectators)
                it->sendDistanceShoot(from, to, type);
        }

        void sendMagicEffect(const Position &pos, uint16_t type) {
            if (owner)
                owner->sendMagicEffect(pos, type);

            for (auto &it : spectators)
                it->sendMagicEffect(pos, type);
        }

        void sendCreatureHealth(const Creature *creature) {
            if (owner)
                owner->sendCreatureHealth(creature);

            for (auto &it : spectators)
                it->sendCreatureHealth(creature);
        }

        void sendSkills() {
            if (owner)
                owner->sendSkills();

            for (auto &it : spectators)
                it->sendSkills();
        }

        void sendAnimatedText(std::string_view message, const Position& pos, TextColor_t color) const {
            if (owner) {
                owner->sendAnimatedText(message, pos, color);
            }

            for (auto& it : spectators)
                it->sendAnimatedText(message, pos, color);
        }

        void sendCreatureTurn(const Creature *creature, uint32_t stackPos) {
            if (owner)
                owner->sendCreatureTurn(creature, stackPos);

            for (auto &it : spectators)
                it->sendCreatureTurn(creature, stackPos);
        }

        void sendCreatureSay(const Creature *creature, SpeakClasses type, std::string_view text,
                             const Position *pos = nullptr) {
            if (owner)
                owner->sendCreatureSay(creature, type, text, pos);

            for (auto &it : spectators)
                it->sendCreatureSay(creature, type, text, pos);
        }

        void sendCancelWalk() {
            if (owner)
                owner->sendCancelWalk();

            for (auto &it : spectators)
                it->sendCancelWalk();
        }

        void sendChangeSpeed(const Creature *creature, uint32_t speed) {
            if (owner)
                owner->sendChangeSpeed(creature, speed);

            for (auto &it : spectators)
                it->sendChangeSpeed(creature, speed);
        }

        void sendCancelTarget() {
            if (owner)
                owner->sendCancelTarget();

            for (auto &it : spectators)
                it->sendCancelTarget();
        }

        void sendCreatureOutfit(const Creature *creature, const Outfit_t &outfit) {
            if (owner)
                owner->sendCreatureOutfit(creature, outfit);

            for (auto &it : spectators)
                it->sendCreatureOutfit(creature, outfit);
        }

        void sendStats() {
            if (owner)
                owner->sendStats();

            for (auto &it : spectators)
                it->sendStats();
        }

        void sendTextMessage(const TextMessage &message) {
            if (owner)
                owner->sendTextMessage(message);

            for (auto &it : spectators)
                it->sendTextMessage(message);
        }

        void sendReLoginWindow() const {
            if (owner) {
                owner->sendReLoginWindow();
            }

            for (auto& it : spectators)
                it->sendReLoginWindow();
        }

        void sendCreatureShield(const Creature *creature) {
            if (owner)
                owner->sendCreatureShield(creature);

            for (auto &it : spectators)
                it->sendCreatureShield(creature);
        }

        void sendCreatureSkull(const Creature *creature) {
            if (owner)
                owner->sendCreatureSkull(creature);

            for (auto &it : spectators)
                it->sendCreatureSkull(creature);
        }

        void sendTradeItemRequest(std::string_view traderName, const Item *item, bool ack) {
            if (owner)
                owner->sendTradeItemRequest(traderName, item, ack);

            for (auto &it : spectators)
                it->sendTradeItemRequest(traderName, item, ack);
        }

        void sendCloseTrade() {
            if (owner)
                owner->sendCloseTrade();

            for (auto &it : spectators)
                it->sendCloseTrade();
        }

        void sendTextWindow(uint32_t windowTextId, Item *item, uint16_t maxlen, bool canWrite) {
            if (owner)
                owner->sendTextWindow(windowTextId, item, maxlen, canWrite);

            for (auto &it : spectators)
                it->sendTextWindow(windowTextId, item, maxlen, canWrite);
        }

        void sendTextWindow(uint32_t windowTextId, uint32_t itemId, std::string_view text) {
            if (owner)
                owner->sendTextWindow(windowTextId, itemId, text);

            for (auto &it : spectators)
                it->sendTextWindow(windowTextId, itemId, text);
        }

        void sendTextWindow(uint32_t windowTextId, uint16_t itemId, std::string_view text) {
            sendTextWindow(windowTextId, static_cast<uint32_t>(itemId), text);
        }

        void sendHouseWindow(uint32_t windowTextId, std::string_view text) {
            if (owner)
                owner->sendHouseWindow(windowTextId, text);

            for (auto &it : spectators)
                it->sendHouseWindow(windowTextId, text);
        }

        void sendOutfitWindow() {
            if (owner)
                owner->sendOutfitWindow();
        }

        void sendUpdatedVIPStatus(uint32_t guid, VipStatus_t newStatus) {
            if (owner)
                owner->sendUpdatedVIPStatus(guid, newStatus);

            for (auto &it : spectators)
                it->sendUpdatedVIPStatus(guid, newStatus);
        }

        void sendVIP(uint32_t guid, std::string_view name, VipStatus_t status) {
            if (owner)
                owner->sendVIP(guid, name, status);

            for (auto &it : spectators)
                it->sendVIP(guid, name, status);
        }

        void sendFightModes() {
            if (owner)
                owner->sendFightModes();

            for (auto &it : spectators)
                it->sendFightModes();
        }

        void sendCreatureLight(const Creature *creature) {
            if (owner)
                owner->sendCreatureLight(creature);

            for (auto &it : spectators)
                it->sendCreatureLight(creature);
        }

        void sendCreatureWalkthrough(const Creature* creature, bool walkthrough) {
            if (owner)
                owner->sendCreatureWalkthrough(creature, walkthrough);

            for (auto& it : spectators)
                it->sendCreatureWalkthrough(creature, walkthrough);
        }

        void sendWorldLight(LightInfo lightInfo) {
            if (owner)
                owner->sendWorldLight(lightInfo);

            for (auto &it : spectators)
                it->sendWorldLight(lightInfo);
        }


        void sendCreatureSquare(const Creature *creature, SquareColor_t color) {
            if (owner)
                owner->sendCreatureSquare(creature, color);

            for (auto &it : spectators)
                it->sendCreatureSquare(creature, color);
        }

        //tiles
        void sendMapDescription(const Position &pos) {
            if (owner)
                owner->sendMapDescription(pos);

            for (auto &it : spectators)
                it->sendMapDescription(pos);
        }


        void sendAddTileItem(const Position& pos, uint32_t stackpos, const Item* item) {
            if (owner)
                owner->sendAddTileItem(pos, stackpos, item);

            for (auto &it : spectators)
                it->sendAddTileItem(pos, stackpos, item);
        }

        void sendUpdateTileItem(const Position &pos, uint32_t stackpos, const Item *item) {
            if (owner)
                owner->sendUpdateTileItem(pos, stackpos, item);

            for (auto &it : spectators)
                it->sendUpdateTileItem(pos, stackpos, item);
        }

        void sendRemoveTileThing(const Position &pos, uint32_t stackpos) {
            if (owner)
                owner->sendRemoveTileThing(pos, stackpos);

            for (auto &it : spectators)
                it->sendRemoveTileThing(pos, stackpos);
        }

        void sendUpdateTileCreature(const Position& pos, uint32_t stackpos, const Creature* creature) {
            if (owner)
                owner->sendUpdateTileCreature(pos, stackpos, creature);

            for (auto& it : spectators)
                it->sendUpdateTileCreature(pos, stackpos, creature);
        }

        void sendRemoveTileCreature(const Creature* /*creature*/, const Position& pos, uint32_t stackpos)
        {
            if (owner)
                owner->sendRemoveTileThing(pos, stackpos);

            for (auto& it : spectators)
                it->sendRemoveTileThing(pos, stackpos);
        }

        void sendUpdateTile(const Tile *tile, const Position &pos) {
            if (owner)
                owner->sendUpdateTile(tile, pos);

            for (auto &it : spectators)
                it->sendUpdateTile(tile, pos);
        }


        void sendAddCreature(const Creature *creature, const Position &pos, int32_t stackpos, MagicEffectClasses magicEffect = CONST_ME_NONE) {
            if (owner)
                owner->sendAddCreature(creature, pos, stackpos, magicEffect);

            for (auto &it : spectators)
                it->sendAddCreature(creature, pos, stackpos, magicEffect);
        }

        void sendMoveCreature(const Creature *creature, const Position &newPos, int32_t newStackPos, const Position &oldPos,
                              int32_t oldStackPos, bool teleport) {
            if (owner)
                owner->sendMoveCreature(creature, newPos, newStackPos, oldPos, oldStackPos, teleport);

            for (auto &it : spectators)
                it->sendMoveCreature(creature, newPos, newStackPos, oldPos, oldStackPos, teleport);
        }

        //containers
        void sendAddContainerItem(uint8_t cid, const Item *item) {
            if (owner)
                owner->sendAddContainerItem(cid, item);

            for (auto &it : spectators)
                it->sendAddContainerItem(cid, item);
        }

        void sendUpdateContainerItem(uint8_t cid, uint16_t slot, const Item *item) {
            if (owner)
                owner->sendUpdateContainerItem(cid, slot, item);

            for (auto &it : spectators)
                it->sendUpdateContainerItem(cid, slot, item);
        }

        void sendRemoveContainerItem(uint8_t cid, uint16_t slot) {
            if (owner)
                owner->sendRemoveContainerItem(cid, slot);

            for (auto &it : spectators)
                it->sendRemoveContainerItem(cid, slot);
        }


        void sendContainer(uint8_t cid, const Container *container, bool hasParent, uint16_t firstIndex) {
            if (owner)
                owner->sendContainer(cid, container, hasParent, firstIndex);

            for (auto &it : spectators)
                it->sendContainer(cid, container, hasParent, firstIndex);
        }

        void sendCloseContainer(uint8_t cid) {
            if (owner)
                owner->sendCloseContainer(cid);

            for (auto &it : spectators)
                it->sendCloseContainer(cid);
        }

        void sendCreatureEmblem(const Creature* creature) {
            if (owner)
                owner->sendCreatureEmblem(creature);

            for (auto &it : spectators)
                it->sendCreatureEmblem(creature);
        }

        void sendDllCheck() {
            if (owner)
                owner->sendDllCheck();

            for (auto &it : spectators)
                it->sendDllCheck();
        }

        void sendModalWindow(const ModalWindow& modalWindow) {
            if (owner)
                owner->sendModalWindow(modalWindow);

            for (auto &it : spectators)
                it->sendModalWindow(modalWindow);
        }

        void sendSpellCooldown(uint8_t spellId, uint32_t time) {
            if (owner)
                owner->sendSpellCooldown(spellId, time);

            for (auto &it : spectators)
                it->sendSpellCooldown(spellId, time);
        }

        void sendSpellGroupCooldown(SpellGroup_t groupId, uint32_t time) {
            if (owner)
                owner->sendSpellGroupCooldown(groupId, time);

            for (auto &it : spectators)
                it->sendSpellGroupCooldown(groupId, time);
        }

        void sendUseItemCooldown(uint32_t time) {
            if (owner)
                owner->sendUseItemCooldown(time);

            for (auto &it : spectators)
                it->sendUseItemCooldown(time);
        }

        void sendInventoryItem(slots_t slot, const Item *item) {
            if (owner)
                owner->sendInventoryItem(slot, item);

            for (auto &it : spectators)
                it->sendInventoryItem(slot, item);
        }

        void parseLookAt(NetworkMessage &msg) {
		    if (!owner) {
			    return;
		    }

		    owner->parseLookAt(msg);
	    }

        friend class ProtocolGame;
        friend class Player;

        ProtocolGame_ptr owner;
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
