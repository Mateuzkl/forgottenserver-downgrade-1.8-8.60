// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_NPC_H
#define FS_NPC_H

#include <string>
#include <map>
#include <memory>
#include <vector>
#include <cstdint>
#include "creature.h"
#include "luascript.h"
#include "lua.hpp"
#include "observer_ptr.h"

class Npc;
class NpcType;
class Player;

class NpcScriptInterface final : public LuaScriptInterface
{
public:
	NpcScriptInterface();

	bool loadNpcLib(std::string_view file);

private:
	void registerFunctions();

	static int luaActionSay(lua_State* L);
	static int luaActionMove(lua_State* L);
	static int luaActionMoveTo(lua_State* L);
	static int luaActionTurn(lua_State* L);
	static int luaActionFollow(lua_State* L);
	static int luagetDistanceTo(lua_State* L);
	static int luaSetNpcFocus(lua_State* L);
	static int luaGetNpcCid(lua_State* L);
	static int luaGetNpcParameter(lua_State* L);
	static int luaOpenShopWindow(lua_State* L);
	static int luaCloseShopWindow(lua_State* L);
	static int luaDoSellItem(lua_State* L);

	// metatable
	static int luaNpcGetParameter(lua_State* L);
	static int luaNpcSetFocus(lua_State* L);

	static int luaNpcOpenShopWindow(lua_State* L);
	static int luaNpcCloseShopWindow(lua_State* L);

private:
	bool initState() override;
	bool closeState() override;

	bool libLoaded;
};

class NpcEventsHandler
{
public:
	NpcEventsHandler(const std::string& file, const std::shared_ptr<Npc>& npc);
	NpcEventsHandler();
	~NpcEventsHandler();

	void onCreatureAppear(Creature* creature) const;
	void onCreatureDisappear(Creature* creature) const;
	void onCreatureMove(Creature* creature, const Position& oldPos, const Position& newPos) const;
	void onCreatureSay(Creature* creature, SpeakClasses, std::string_view text) const;
	void onPlayerTrade(Player* player, int32_t callback, uint16_t itemId, uint8_t count, uint8_t amount,
	                   bool ignore = false, bool inBackpacks = false) const;
	void onPlayerCloseChannel(Player* player) const;
	void onPlayerEndTrade(Player* player) const;
	void onThink() const;

	void setNpc(Npc* n);
	void setNpc(const std::shared_ptr<Npc>& n) { npc = n.get(); }

	bool isLoaded() const;

	int32_t creatureAppearEvent = -1;
	int32_t creatureDisappearEvent = -1;
	int32_t creatureMoveEvent = -1;
	int32_t creatureSayEvent = -1;
	int32_t playerCloseChannelEvent = -1;
	int32_t playerEndTradeEvent = -1;
	int32_t thinkEvent = -1;

	std::shared_ptr<NpcScriptInterface> scriptInterface;

	friend class NpcScriptInterface;
public:
	bool loaded = false;

private:
	ObserverPtr<Npc> npc = nullptr;
};

class NpcType
{
public:
	NpcType() = default;

	// non-copyable
	NpcType(const NpcType&) = delete;
	NpcType& operator=(const NpcType&) = delete;

	bool loadCallback(NpcScriptInterface* scriptInterface);
	bool loadFromXml();

	std::string name;
	std::string filename;
	std::string scriptFilename;

	uint8_t speechBubble = 0;

	uint32_t walkTicks = 1500;
	uint32_t baseSpeed = 100;

	int32_t masterRadius = 2;
	int32_t health = 100;
	int32_t healthMax = 100;

	bool floorChange = false;
	bool attackable = false;
	bool ignoreHeight = false;
	bool loaded = false;
	bool isIdle = true;
	bool pushable = true;
	bool fromLua = false;

	Outfit_t defaultOutfit;
	Skulls_t skull = SKULL_NONE;
	GuildEmblems_t emblem = GUILDEMBLEM_NONE;

	uint16_t moneyType = 0;

	std::map<std::string, std::string> parameters;

	std::string eventType;
	std::unique_ptr<NpcEventsHandler> npcEventHandler = std::make_unique<NpcEventsHandler>();
};

namespace Npcs {
void load(bool reload = false);
bool loadScripts(bool reload = false);
void reload();
void addNpcType(const std::string& name, const std::shared_ptr<NpcType>& npcType);
void clearNpcTypes();
const std::map<std::string, std::shared_ptr<NpcType>>& getNpcTypes();
std::shared_ptr<NpcType> getNpcType(const std::string& name);
NpcScriptInterface* getScriptInterface();
std::shared_ptr<Npc> makeScriptHandle(Npc* npc);
static const int32_t ViewportX = 15 * 2 + 2; // Approximate or use Map constants if available
static const int32_t ViewportY = 11 * 2 + 2;
static constexpr int32_t TalkRadius = 3;
} // namespace Npcs

class Npc final : public Creature
{
public:
	explicit Npc(const std::string& name);
	~Npc();

	using Creature::onWalk;

	// non-copyable
	Npc(const Npc&) = delete;
	Npc& operator=(const Npc&) = delete;

	Npc* getNpc() override { return this; }
	const Npc* getNpc() const override { return this; }

	bool isPushable() const override { return pushable && walkTicks != 0; }

	void setID() override
	{
		if (id == 0) {
			id = ++npcAutoID;
		}
	}

	void removeList() override;
	void addList() override;

	static std::shared_ptr<Npc> createNpc(const std::string& name);

	bool canSee(const Position& pos) const override;

	bool load();
	void reload();

	const std::string& getName() const override { return name; }
	void setName(const std::string& n) { name = n; }
	const std::string& getNameDescription() const override { return name; }

	CreatureType_t getType() const override { return CREATURETYPE_NPC; }

	uint16_t getMoneyType() const { return moneyType; }

	void doSay(std::string_view text);
	void doSayToPlayer(Player* player, std::string_view text);

	bool doMoveTo(const Position& pos, int32_t minTargetDist = 1, int32_t maxTargetDist = 1, bool fullPathSearch = true,
	              bool clearSight = true, int32_t maxSearchDist = 0);

	int32_t getMasterRadius() const { return masterRadius; }
	const Position& getMasterPos() const { return masterPos; }
	void setMasterPos(Position pos, int32_t radius = 1)
	{
		masterPos = pos;
		if (masterRadius == -1) {
			masterRadius = radius;
		}
	}

	void onPlayerCloseChannel(Player* player);
	void onPlayerTrade(Player* player, int32_t callback, uint16_t itemId, uint8_t count, uint8_t amount,
	                   bool ignore = false, bool inBackpacks = false);
	void onPlayerEndTrade(Player* player, int32_t buyCallback, int32_t sellCallback);

	void turnToCreature(Creature* creature);
	void setCreatureFocus(Creature* creature);
	bool hasCreatureFocus() const { return !m_focusCreature.expired(); }
	bool setFollowCreature(Creature* creature) override;
	void clearFocusIfNeeded(Player* player);

	const auto& getSpectators() { return spectators; }

	uint8_t getSpeechBubble() const { return speechBubble; }
	void setSpeechBubble(const uint8_t bubble) { 
		if (bubble <= SPEECHBUBBLE_QUESTTRADER || bubble == SPEECHBUBBLE_HIRELING) {
			speechBubble = bubble;
		}
	}

	void loadNpcTypeInfo();

	std::unique_ptr<NpcEventsHandler> npcEventHandler;
	bool fromLua = false;
	std::shared_ptr<NpcType> npcType;

	void closeAllShopWindows();
	void addShopPlayer(const std::shared_ptr<Player>& player);
	void removeShopPlayer(const std::shared_ptr<Player>& player);
	std::map<std::string, std::string> parameters;

	static uint32_t npcAutoID;

private:
	void onCreatureAppear(Creature* creature, bool isLogin) override;
	void onRemoveCreature(Creature* creature, bool isLogout) override;
	void onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos, const Tile* oldTile,
	                    const Position& oldPos, bool teleport) override;

	void onCreatureSay(Creature* creature, SpeakClasses type, std::string_view text) override;
	void onThink(uint32_t interval) override;
	std::string getDescription(int32_t lookDistance) const override;

	bool isImmune(CombatType_t) const override { return !attackable; }
	bool isImmune(ConditionType_t) const override { return !attackable; }
	bool isAttackable() const override { return attackable; }
	bool getNextStep(Direction& dir, uint32_t& flags) override;

	void setIdle(const bool idle);

	bool canWalkTo(const Position& fromPos, Direction dir) const;
	bool getRandomStep(Direction& direction) const;

	void reset(bool reload = false);

	std::set<std::shared_ptr<Player>> shopPlayerSet;
	std::set<std::shared_ptr<Player>> spectators;

	std::string name;
	std::string filename;

	Position masterPos;

	uint32_t walkTicks;
	uint32_t baseSpeed;
	std::weak_ptr<Player> m_focusCreature;
	int32_t masterRadius;

	uint8_t speechBubble;

	bool floorChange;
	bool attackable;
	bool ignoreHeight;
	bool loaded;
	bool isIdle;
	bool pushable;

	uint16_t moneyType;

	friend class NpcType;
	friend class NpcScriptInterface;
};

#endif
