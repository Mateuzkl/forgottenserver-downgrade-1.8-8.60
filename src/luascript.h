// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_LUASCRIPT_H
#define FS_LUASCRIPT_H

#include "database.h"
#include "enums.h"
#include "position.h"
#include "spectators.h"

#if LUA_VERSION_NUM >= 502
#ifndef LUA_COMPAT_ALL
#ifndef LUA_COMPAT_MODULE
#define luaL_register(L, libname, l) (luaL_newlib(L, l), lua_pushvalue(L, -1), lua_setglobal(L, libname))
#endif
#undef lua_equal
#define lua_equal(L, i1, i2) lua_compare(L, (i1), (i2), LUA_OPEQ)
#endif
#endif

enum LuaDataType
{
	LuaData_Unknown,

	LuaData_Item,
	LuaData_Container,
	LuaData_Teleport,
	LuaData_Creature,
	LuaData_Player,
	LuaData_Monster,
	LuaData_Npc,
	LuaData_Tile,
	LuaData_Condition,

	LuaData_Combat,
	LuaData_Group,
	LuaData_Guild,
	LuaData_House,
	LuaData_ItemType,
	LuaData_ModalWindow,
	LuaData_MonsterType,
	LuaData_NetworkMessage,
	LuaData_Party,
	LuaData_Vocation,
	LuaData_Town,
	LuaData_LuaVariant,
	LuaData_Position,

	LuaData_Outfit,
	LuaData_Loot,
	LuaData_MonsterSpell,
	LuaData_Spell,
	LuaData_Action,
	LuaData_TalkAction,
	LuaData_CreatureEvent,
	LuaData_MoveEvent,
	LuaData_GlobalEvent,
	LuaData_Weapon,

	LuaData_XMLDocument,
	LuaData_XMLNode,
};

template <class T>
inline constexpr LuaDataType LuaDataTypeByClass = LuaData_Unknown;

#define NEW_LUA_DATA_TYPE(CLASS) \
	template <> \
	inline constexpr LuaDataType LuaDataTypeByClass<CLASS> = LuaData_##CLASS; \
	template <> \
	inline constexpr LuaDataType LuaDataTypeByClass<const CLASS> = LuaData_##CLASS;

class Action;
class AreaCombat;
class Combat;
class Condition;
class Container;
class Container;
class Creature;
class CreatureEvent;
class Cylinder;
class GlobalEvent;
class Guild;
class House;
class InstantSpell;
class Item;
class ItemType;
class Loot;
class LuaScriptInterface;
class LuaVariant;
class Monster;
class MonsterSpell;
class MonsterType;
class MoveEvent;
class NetworkMessage;
class Npc;
class Party;
class Player;
class RuneSpell;
class SpectatorVec;
class Spell;
class TalkAction;
class Teleport;
class Thing;
class Tile;
class Town;
class Vocation;
class Weapon;
class WeaponDistance;
class WeaponMelee;
class WeaponWand;

struct Group;
struct LootBlock;
struct ModalWindow;
struct Mount;
struct Outfit;
struct Position;

using Combat_ptr = std::shared_ptr<Combat>;

using XMLDocument = pugi::xml_document;
using XMLNode = pugi::xml_node;

inline constexpr int32_t EVENT_ID_LOADING = 1;
inline constexpr int32_t EVENT_ID_USER = 1000;

NEW_LUA_DATA_TYPE(Item)
NEW_LUA_DATA_TYPE(Container)
NEW_LUA_DATA_TYPE(Teleport)
NEW_LUA_DATA_TYPE(Creature)
NEW_LUA_DATA_TYPE(Player)
NEW_LUA_DATA_TYPE(Monster)
NEW_LUA_DATA_TYPE(Npc)
NEW_LUA_DATA_TYPE(Tile)
NEW_LUA_DATA_TYPE(Condition)

NEW_LUA_DATA_TYPE(Combat)
NEW_LUA_DATA_TYPE(Group)
NEW_LUA_DATA_TYPE(Guild)
NEW_LUA_DATA_TYPE(House)
NEW_LUA_DATA_TYPE(ItemType)
NEW_LUA_DATA_TYPE(ModalWindow)
NEW_LUA_DATA_TYPE(MonsterType)
NEW_LUA_DATA_TYPE(NetworkMessage)
NEW_LUA_DATA_TYPE(Party)
NEW_LUA_DATA_TYPE(Vocation)
NEW_LUA_DATA_TYPE(Town)
NEW_LUA_DATA_TYPE(Position)

NEW_LUA_DATA_TYPE(LuaVariant)

NEW_LUA_DATA_TYPE(Outfit)
NEW_LUA_DATA_TYPE(Loot)
NEW_LUA_DATA_TYPE(MonsterSpell)
NEW_LUA_DATA_TYPE(Spell)
NEW_LUA_DATA_TYPE(Action)
NEW_LUA_DATA_TYPE(TalkAction)
NEW_LUA_DATA_TYPE(CreatureEvent)
NEW_LUA_DATA_TYPE(MoveEvent)
NEW_LUA_DATA_TYPE(GlobalEvent)
NEW_LUA_DATA_TYPE(Weapon)

template <>
inline constexpr LuaDataType LuaDataTypeByClass<WeaponMelee> = LuaData_Weapon;
template <>
inline constexpr LuaDataType LuaDataTypeByClass<const WeaponMelee> = LuaData_Weapon;

template <>
inline constexpr LuaDataType LuaDataTypeByClass<WeaponDistance> = LuaData_Weapon;
template <>
inline constexpr LuaDataType LuaDataTypeByClass<const WeaponDistance> = LuaData_Weapon;

template <>
inline constexpr LuaDataType LuaDataTypeByClass<WeaponWand> = LuaData_Weapon;
template <>
inline constexpr LuaDataType LuaDataTypeByClass<const WeaponWand> = LuaData_Weapon;

NEW_LUA_DATA_TYPE(XMLDocument)
NEW_LUA_DATA_TYPE(XMLNode)

struct LuaTimerEventDesc
{
	int32_t scriptId = -1;
	int32_t function = -1;
	std::vector<int32_t> parameters;
	uint32_t eventId = 0;

	LuaTimerEventDesc() = default;
	LuaTimerEventDesc(LuaTimerEventDesc&& other) = default;
};

class ScriptEnvironment
{
public:
	ScriptEnvironment();
	~ScriptEnvironment();

	// non-copyable
	ScriptEnvironment(const ScriptEnvironment&) = delete;
	ScriptEnvironment& operator=(const ScriptEnvironment&) = delete;

	void resetEnv();

	void setScriptId(int32_t scriptId, LuaScriptInterface* scriptInterface)
	{
		this->scriptId = scriptId;
		interface = scriptInterface;
	}
	bool setCallbackId(int32_t callbackId, LuaScriptInterface* scriptInterface);

	int32_t getScriptId() const { return scriptId; }
	LuaScriptInterface* getScriptInterface() { return interface; }
	void setScriptInterface(LuaScriptInterface* scriptInterface) { interface = scriptInterface; }

	void setTimerEvent() { timerEvent = true; }

	void getEventInfo(int32_t& scriptId, LuaScriptInterface*& scriptInterface, int32_t& callbackId,
	                  bool& timerEvent) const;

	void addTempItem(Item* item);
	static void removeTempItem(Item* item);
	uint32_t addThing(Thing* thing);
	void insertItem(uint32_t uid, Item* item);

	static DBResult_ptr getResultByID(uint32_t id);
	static uint32_t addResult(DBResult_ptr res);
	static bool removeResult(uint32_t id);

	void setNpc(Npc* npc) { curNpc = npc; }
	Npc* getNpc() const { return curNpc; }

	Thing* getThingByUID(uint32_t uid);
	Item* getItemByUID(uint32_t uid);
	Container* getContainerByUID(uint32_t uid);
	void removeItemByUID(uint32_t uid);

private:
	using StorageMap = std::map<uint32_t, int32_t>;
	using DBResultMap = std::map<uint32_t, DBResult_ptr>;

	LuaScriptInterface* interface;

	// for npc scripts
	Npc* curNpc = nullptr;

	// temporary item list
	static std::multimap<ScriptEnvironment*, Item*> tempItems;

	// local item map
	std::unordered_map<uint32_t, Item*> localMap;
	uint32_t lastUID = std::numeric_limits<uint16_t>::max();

	// script file id
	int32_t scriptId;
	int32_t callbackId;
	bool timerEvent;

	// result map
	static uint32_t lastResultId;
	static DBResultMap tempResults;
};

#define reportErrorFunc(L, a) LuaScriptInterface::reportError(__FUNCTION__, a, L, true)

enum class LuaErrorCode
{
	PLAYER_NOT_FOUND,
	CREATURE_NOT_FOUND,
	ITEM_NOT_FOUND,
	THING_NOT_FOUND,
	TILE_NOT_FOUND,
	HOUSE_NOT_FOUND,
	COMBAT_NOT_FOUND,
	CONDITION_NOT_FOUND,
	AREA_NOT_FOUND,
	CONTAINER_NOT_FOUND,
	VARIANT_NOT_FOUND,
	VARIANT_UNKNOWN,
	SPELL_NOT_FOUND,
	CALLBACK_NOT_FOUND,
};

class LuaScriptInterface
{
public:
	explicit LuaScriptInterface(std::string_view interfaceName);
	virtual ~LuaScriptInterface();

	// non-copyable
	LuaScriptInterface(const LuaScriptInterface&) = delete;
	LuaScriptInterface& operator=(const LuaScriptInterface&) = delete;

	virtual bool initState();
	bool reInitState();

	int32_t loadFile(std::string_view file, Npc* npc = nullptr);

	std::string_view getFileById(int32_t scriptId);
	const std::string& getFileByIdForStats(int32_t scriptId);
	int32_t getEvent(std::string_view eventName);
	int32_t getEvent();
	int32_t getMetaEvent(std::string_view globalName, std::string_view eventName);
	void removeEvent(int32_t scriptId);

	static ScriptEnvironment* getScriptEnv()
	{
		assert(scriptEnvIndex >= 0 && scriptEnvIndex < 16);
		return scriptEnv + scriptEnvIndex;
	}

	static bool reserveScriptEnv() { return ++scriptEnvIndex < 16; }

	static void resetScriptEnv()
	{
		assert(scriptEnvIndex >= 0);
		scriptEnv[scriptEnvIndex--].resetEnv();
	}

	static void reportError(const char* function, std::string_view error_desc, lua_State* L = nullptr,
	                        bool stack_trace = false);

	std::string_view getInterfaceName() const { return interfaceName; }
	std::string_view getLastLuaError() const { return lastLuaError; }

	lua_State* getLuaState() const { return luaState; }

	bool pushFunction(int32_t functionId);

	static int luaErrorHandler(lua_State* L);
	bool callFunction(int params);
	void callVoidFunction(int params);
	ReturnValue callReturnValueFunction(int params);

	static std::string escapeString(std::string string);

	static const luaL_Reg luaConfigManagerTable[4];
	static const luaL_Reg luaDatabaseTable[9];
	static const luaL_Reg luaResultTable[6];

	static int protectedCall(lua_State* L, int nargs, int nresults);

	static int luaLogMigration(lua_State* L);

	static std::string_view getErrorDesc(LuaErrorCode code);

protected:
	virtual bool closeState();

	void registerFunctions();

	// lua modules
	void registerGame();
	void registerVariant();
	void registerPosition();
	void registerTile();
	void registerNetworkMessage();
	void registerItem();
	void registerImbuement();
	void registerContainer();
	void registerTeleport();
	void registerCreature();
	void registerPlayer();
	void registerModalWindow();
	void registerMonster();
	void registerNpc();
	void registerGuild();
	void registerGroup();
	void registerVocation();
	void registerTown();
	void registerHouse();
	void registerItemType();
	void registerCombat();
	void registerCondition();
	void registerOutfit();
	void registerMonsterType();
	void registerLoot();
	void registerMonsterSpell();
	void registerParty();
	void registerSpells();
	void registerActions();
	void registerTalkActions();
	void registerCreatureEvents();
	void registerMoveEvents();
	void registerGlobalEvents();
	void registerWeapons();
	void registerXML();

	void registerMethod(std::string_view globalName, std::string_view methodName, lua_CFunction func);

	lua_State* luaState = nullptr;

	int32_t eventTableRef = -1;
	int32_t runningEventId = EVENT_ID_USER;

	// script file cache
	std::map<int32_t, std::string> cacheFiles;

private:
	void registerClass(const std::string& className, const std::string& baseClass, lua_CFunction newFunction = nullptr);
	void registerTable(std::string_view tableName);
	void registerMetaMethod(std::string_view className, std::string_view methodName, lua_CFunction func);
	void registerGlobalMethod(std::string_view functionName, lua_CFunction func);
	void registerVariable(std::string_view tableName, std::string_view name, lua_Integer value);
	void registerGlobalVariable(std::string_view name, lua_Integer value);
	void registerGlobalBoolean(std::string_view name, bool value);

	static std::string getStackTrace(lua_State* L, std::string_view error_desc);

	static bool getArea(lua_State* L, std::vector<uint32_t>& vec, uint32_t& rows);

	// lua functions
	static int luaDoPlayerAddItem(lua_State* L);

	// get item info
	static int luaGetDepotId(lua_State* L);

	// get world infoAdd commentMore actions
	static int luaGetWorldTime(lua_State* L);
	static int luaGetWorldUpTime(lua_State* L);
	static int luaGetWorldLight(lua_State* L);
	static int luaSetWorldLight(lua_State* L);

	// get subtype name
	static int luaGetSubTypeName(lua_State* L);

	// type validation
	static int luaIsDepot(lua_State* L);
	static int luaIsMoveable(lua_State* L);
	static int luaIsValidUID(lua_State* L);

	// container
	static int luaDoAddContainerItem(lua_State* L);

	//
	static int luaCreateCombatArea(lua_State* L);

	static int luaDoAreaCombat(lua_State* L);
	static int luaDoTargetCombat(lua_State* L);

	static int luaDoChallengeCreature(lua_State* L);

	static int luaDebugPrint(lua_State* L);
	static int luaLogInfo(lua_State* L);
	static int luaLogWarning(lua_State* L);
	static int luaLogError(lua_State* L);
	static int luaAddEvent(lua_State* L);
	static int luaStopEvent(lua_State* L);

	static int luaSaveServer(lua_State* L);
	static int luaCleanMap(lua_State* L);

	static int luaIsInWar(lua_State* L);

	static int luaGetWaypointPositionByName(lua_State* L);

	static int luaSendChannelMessage(lua_State* L);
	static int luaSendGuildChannelMessage(lua_State* L);

	static int luaIsScriptsInterface(lua_State* L);

	static int luaConfigManagerGetString(lua_State* L);
	static int luaConfigManagerGetNumber(lua_State* L);
	static int luaConfigManagerGetBoolean(lua_State* L);

	static int luaDatabaseExecute(lua_State* L);
	static int luaDatabaseAsyncExecute(lua_State* L);
	static int luaDatabaseStoreQuery(lua_State* L);
	static int luaDatabaseAsyncStoreQuery(lua_State* L);
	static int luaDatabaseEscapeString(lua_State* L);
	static int luaDatabaseEscapeBlob(lua_State* L);
	static int luaDatabaseLastInsertId(lua_State* L);
	static int luaDatabaseTableExists(lua_State* L);

	static int luaResultGetNumber(lua_State* L);
	static int luaResultGetString(lua_State* L);
	static int luaResultGetStream(lua_State* L);
	static int luaResultNext(lua_State* L);
	static int luaResultFree(lua_State* L);

	static int luaUserdataCompare(lua_State* L);

	static int luaIsType(lua_State* L);
	static int luaRawGetMetatable(lua_State* L);

	static int luaSystemTime(lua_State* L);
	static int luaSystemNanoTime(lua_State* L);

	static int luaTableCreate(lua_State* L);

	// global helper declared to satisfy registration in luascript.cpp
	static int luaTransformToSHA1(lua_State* L);

	static int luaPlayerGetReset(lua_State* L); // reset system
	static int luaPlayerDoReset(lua_State* L);  // reset system
	static int luaPlayerSetReset(lua_State* L); // reset system
	static int luaPlayerReloadWarList(lua_State* L);
	static int luaPlayerSendAutoLootWindow(lua_State* L);

	// Item Imbuement
	static int luaItemGetImbuementSlots(lua_State* L);
	static int luaItemGetFreeImbuementSlots(lua_State* L);
	static int luaItemCanImbue(lua_State* L);
	static int luaItemAddImbuementSlots(lua_State* L);
	static int luaItemRemoveImbuementSlots(lua_State* L);
	static int luaItemHasImbuementType(lua_State* L);
	static int luaItemHasImbuement(lua_State* L);
	static int luaItemHasImbuements(lua_State* L);
	static int luaItemAddImbuement(lua_State* L);
	static int luaItemRemoveImbuement(lua_State* L);
	static int luaItemGetImbuements(lua_State* L);
	static int luaItemCanApplyImbuement(lua_State* L);

	// Imbuement
	static int luaImbuementCreate(lua_State* L);
	static int luaImbuementGetType(lua_State* L);
	static int luaImbuementIsSkill(lua_State* L);
	static int luaImbuementIsSpecialSkill(lua_State* L);
	static int luaImbuementIsStat(lua_State* L);
	static int luaImbuementIsDamage(lua_State* L);
	static int luaImbuementIsResist(lua_State* L);
	static int luaImbuementGetValue(lua_State* L);
	static int luaImbuementSetValue(lua_State* L);
	static int luaImbuementGetDuration(lua_State* L);
	static int luaImbuementSetDuration(lua_State* L);
	static int luaImbuementSetEquipDecay(lua_State* L);
	static int luaImbuementSetInfightDecay(lua_State* L);
	static int luaImbuementIsEquipDecay(lua_State* L);
	static int luaImbuementIsInfightDecay(lua_State* L);
	static int luaImbuementGetBaseId(lua_State* L);
	static int luaGameGetImbuementByScroll(lua_State* L);
	static int luaGameGetImbuementBases(lua_State* L);
	static int luaGameGetImbuementCategories(lua_State* L);
	static int luaPlayerGetAutoLootItemCount(lua_State* L);
	static int luaPlayerSetAutoLootEnabled(lua_State* L);
	static int luaPlayerIsAutoLootEnabled(lua_State* L);
	static int luaPlayerSetAutoLootGold(lua_State* L);
	static int luaPlayerIsAutoLootGoldEnabled(lua_State* L);
	static int luaPlayerClearAutoLoot(lua_State* L);

	static int luaPlayerGetSpectators(lua_State* L);
	static int luaPlayerSetSpectators(lua_State* L);
	static int luaPlayerSendCastChannelMessage(lua_State* L);

	//
	std::string lastLuaError;

	std::string interfaceName;

	static ScriptEnvironment scriptEnv[16];
	static int32_t scriptEnvIndex;

	std::string loadingFile;
};

class LuaEnvironment : public LuaScriptInterface
{
public:
	LuaEnvironment();
	~LuaEnvironment();

	// non-copyable
	LuaEnvironment(const LuaEnvironment&) = delete;
	LuaEnvironment& operator=(const LuaEnvironment&) = delete;

	bool initState() override;
	bool reInitState();
	bool closeState() override;
	static void shutdown();

	LuaScriptInterface* getTestInterface();

	Combat_ptr getCombatObject(uint32_t id) const;
	Combat_ptr createCombatObject(LuaScriptInterface* interface);
	void clearCombatObjects(LuaScriptInterface* interface);

	AreaCombat* getAreaObject(uint32_t id) const;
	uint32_t createAreaObject(LuaScriptInterface* interface);
	void clearAreaObjects(LuaScriptInterface* interface);

private:
	void executeTimerEvent(uint32_t eventIndex);

	std::unordered_map<uint32_t, LuaTimerEventDesc> timerEvents;
	std::unordered_map<uint32_t, Combat_ptr> combatMap;
	std::unordered_map<uint32_t, AreaCombat*> areaMap;

	std::unordered_map<LuaScriptInterface*, std::vector<uint32_t>> combatIdMap;
	std::unordered_map<LuaScriptInterface*, std::vector<uint32_t>> areaIdMap;

	LuaScriptInterface* testInterface = nullptr;

	uint32_t lastEventTimerId = 1;
	uint32_t lastCombatId = 0;
	uint32_t lastAreaId = 0;

	friend class LuaScriptInterface;
	friend class CombatSpell;
};

namespace Lua {
// push/pop common structures
void pushThing(lua_State* L, Thing* thing);
void pushVariant(lua_State* L, const LuaVariant& var);
void pushString(lua_State* L, std::string_view value);
void pushCallback(lua_State* L, int32_t callback);
void pushCylinder(lua_State* L, Cylinder* cylinder);

std::string popString(lua_State* L);
int32_t popCallback(lua_State* L);

// Metatables
void setMetatable(lua_State* L, int32_t index, std::string_view name);
void setWeakMetatable(lua_State* L, int32_t index, std::string_view name);

void setItemMetatable(lua_State* L, int32_t index, const Item* item);
void setCreatureMetatable(lua_State* L, int32_t index, const Creature* creature);

// Get
LuaVariant getVariant(lua_State* L, int32_t arg);

template <typename T>
inline typename std::enable_if<std::is_enum<T>::value, T>::type getInteger(lua_State* L, int32_t arg)
{
	int isNum = 0;
	lua_Integer integer = lua_tointegerx(L, arg, &isNum);
	if (isNum == 0) {
		// Se não for inteiro, tenta pegar como número decimal e arredonda
		lua_Number number = lua_tonumber(L, arg);
		if (number == 0 && !lua_isnumber(L, arg)) {
			// Não é um número válido
			return static_cast<T>(0);
		}
		// Arredonda o número decimal para inteiro
		integer = static_cast<lua_Integer>(std::floor(number));
	}

	return static_cast<T>(static_cast<typename std::underlying_type<T>::type>(integer));
}

template <typename T>
inline typename std::enable_if<std::is_integral<T>::value, T>::type getInteger(lua_State* L, int32_t arg)
{
	int isNum = 0;
	lua_Integer integer = lua_tointegerx(L, arg, &isNum);
	if (isNum == 0) {
		// Se não for inteiro, tenta pegar como número decimal e arredonda
		lua_Number number = lua_tonumber(L, arg);
		if (number == 0 && !lua_isnumber(L, arg)) {
			// Não é um número válido
			return 0;
		}
		// Arredonda o número decimal para inteiro
		integer = static_cast<lua_Integer>(std::floor(number));
	}

	return static_cast<T>(integer);
}

template <typename T>
inline T getInteger(lua_State* L, int32_t arg, T defaultValue)
{
	if (lua_isinteger(L, arg) == 0) {
		return defaultValue;
	}
	return getInteger<T>(L, arg);
}

template <typename T>
inline typename std::enable_if<std::is_enum<T>::value, T>::type getNumber(lua_State* L, int32_t arg)
{
	int isNum = 0;
	lua_Number num = lua_tonumberx(L, arg, &isNum);
	if (isNum == 0) {
		return static_cast<T>(0);
	} else if (num < std::numeric_limits<lua_Number>::lowest()) {
		reportErrorFunc(L, fmt::format("Argument {} has out-of-range value for {}: {}", arg, typeid(T).name(), num));
		return static_cast<T>(std::numeric_limits<typename std::underlying_type<T>::type>::lowest());
	} else if (num > std::numeric_limits<lua_Number>::max()) {
		reportErrorFunc(L, fmt::format("Argument {} has out-of-range value for {}: {}", arg, typeid(T).name(), num));
		return static_cast<T>(std::numeric_limits<typename std::underlying_type<T>::type>::max());
	}

	return static_cast<T>(static_cast<typename std::underlying_type<T>::type>(num));
}

template <typename T>
inline typename std::enable_if<std::is_integral<T>::value || std::is_floating_point<T>::value, T>::type getNumber(
    lua_State* L, int32_t arg)
{
	int isNum = 0;
	lua_Number num = lua_tonumberx(L, arg, &isNum);
	if (isNum == 0) {
		return 0;
	}

	return static_cast<T>(num);
}

template <typename T>
inline T getNumber(lua_State* L, int32_t arg, T defaultValue)
{
	if (lua_isnumber(L, arg) == 0) {
		return defaultValue;
	}
	return getNumber<T>(L, arg);
}

template <class T>
inline bool isType(lua_State* L, int32_t arg);

template <class T>
inline T** getRawUserdata(lua_State* L, int32_t arg, const bool checkType = true)
{
	if (checkType && !isType<T>(L, arg)) {
		return nullptr;
	}
	return static_cast<T**>(lua_touserdata(L, arg));
}

template <class T>
inline T* getUserdata(lua_State* L, int32_t arg, const bool checkType = true)
{
	T** userdata = getRawUserdata<T>(L, arg, checkType);
	if (!userdata) {
		return nullptr;
	}
	return *userdata;
}

template <class T>
inline std::shared_ptr<T>& getSharedPtr(lua_State* L, int32_t arg)
{
	return *static_cast<std::shared_ptr<T>*>(lua_touserdata(L, arg));
}

bool getBoolean(lua_State* L, int32_t arg);
bool getBoolean(lua_State* L, int32_t arg, bool defaultValue);

std::string getString(lua_State* L, int32_t arg);
Position getPosition(lua_State* L, int32_t arg, int32_t& stackpos);
Position getPosition(lua_State* L, int32_t arg);
Outfit_t getOutfit(lua_State* L, int32_t arg);
Outfit getOutfitClass(lua_State* L, int32_t arg);
InstantSpell* getInstantSpell(lua_State* L, int32_t arg);
Reflect getReflect(lua_State* L, int32_t arg);

Thing* getThing(lua_State* L, int32_t arg);
Creature* getCreature(lua_State* L, int32_t arg);
Player* getPlayer(lua_State* L, int32_t arg);

template <typename T>
inline typename std::enable_if<std::is_enum<T>::value || std::is_integral<T>::value, T>::type getField(
    lua_State* L, int32_t arg, std::string_view key)
{
	lua_getfield(L, arg, key.data());
	return getInteger<T>(L, -1);
}

template <typename T, typename... Args>
inline typename std::enable_if<std::is_enum<T>::value || std::is_integral<T>::value, T>::type getField(
    lua_State* L, int32_t arg, std::string_view key, T&& defaultValue)
{
	lua_getfield(L, arg, key.data());
	return getInteger<T>(L, -1, std::forward<T>(defaultValue));
}

template <typename T>
inline typename std::enable_if<std::is_floating_point<T>::value, T>::type getField(lua_State* L, int32_t arg,
                                                                                   std::string_view key)
{
	lua_getfield(L, arg, key.data());
	return getNumber<T>(L, -1);
}

template <typename T, typename... Args>
inline typename std::enable_if<std::is_floating_point<T>::value, T>::type getField(lua_State* L, int32_t arg,
                                                                                   std::string_view key,
                                                                                   T&& defaultValue)
{
	lua_getfield(L, arg, key.data());
	return getNumber<T>(L, -1, std::forward<T>(defaultValue));
}

std::string getFieldString(lua_State* L, int32_t arg, std::string_view key);

LuaDataType getUserdataType(lua_State* L, int32_t arg);
std::optional<uint8_t> getBlessingId(lua_State* L, int32_t arg);

inline bool getAssociatedValue(lua_State* L, int32_t arg, int32_t index)
{
	return lua_getiuservalue(L, arg, index) != LUA_TNONE;
}

// Is
bool isNone(lua_State* L, int32_t arg);
bool isNumber(lua_State* L, int32_t arg);
bool isInteger(lua_State* L, int32_t arg);
bool isString(lua_State* L, int32_t arg);
bool isBoolean(lua_State* L, int32_t arg);
bool isTable(lua_State* L, int32_t arg);
bool isFunction(lua_State* L, int32_t arg);
bool isUserdata(lua_State* L, int32_t arg);

//
template <typename T>
inline typename std::enable_if<std::is_enum<T>::value || std::is_integral<T>::value, void>::type setField(
    lua_State* L, const char* index, T value)
{
	lua_pushinteger(L, value);
	lua_setfield(L, -2, index);
}

template <typename T>
inline typename std::enable_if<std::is_floating_point<T>::value, void>::type setField(lua_State* L, const char* index,
                                                                                      T value)
{
	lua_pushnumber(L, value);
	lua_setfield(L, -2, index);
}

inline void setField(lua_State* L, const char* index, std::string_view value)
{
	pushString(L, value);
	lua_setfield(L, -2, index);
}

template <class T>
inline bool isType(lua_State* L, int32_t arg)
{
	const LuaDataType classType = LuaDataTypeByClass<T>;
	if (classType == LuaData_Unknown) {
		return false;
	}

	const LuaDataType userdataType = getUserdataType(L, arg);
	if (classType == LuaData_Creature) {
		return userdataType >= LuaData_Creature && userdataType <= LuaData_Npc;
	} else if (classType == LuaData_Item) {
		return userdataType >= LuaData_Item && userdataType <= LuaData_Teleport;
	}

	return userdataType == classType;
}

// Push
void pushBoolean(lua_State* L, bool value);
void pushCombatDamage(lua_State* L, const CombatDamage& damage);
void pushInstantSpell(lua_State* L, const InstantSpell& spell);
void pushSpell(lua_State* L, const Spell& spell);
void pushPosition(lua_State* L, const Position& position, int32_t stackpos = 0);
void pushOutfit(lua_State* L, const Outfit_t& outfit);
void pushOutfit(lua_State* L, const Outfit* outfit);
void pushMount(lua_State* L, const Mount* mount);
void pushLoot(lua_State* L, const std::vector<LootBlock>& lootList);
void pushReflect(lua_State* L, const Reflect& reflect);

// Userdata
template <class T>
inline void pushUserdata(lua_State* L, T* value, int nuvalue = 1)
{
	T** userdata = static_cast<T**>(lua_newuserdatauv(L, sizeof(T*), nuvalue));
	*userdata = value;
}

// Shared Ptr
template <class T>
inline void pushSharedPtr(lua_State* L, T value, int nuvalue = 1)
{
	new (lua_newuserdatauv(L, sizeof(T), nuvalue)) T(std::move(value));
}

// Extra
template <class T>
inline void getSpectators(lua_State* L, int32_t arg, SpectatorVec& spectators)
{
	if (isUserdata(L, arg)) {
		if (T* creature = getUserdata<T>(L, arg)) {
			spectators.emplace_back(creature);
		}
		return;
	} else if (!isTable(L, arg)) {
		return;
	}

	lua_pushnil(L);
	while (lua_next(L, arg) != 0) {
		if (isUserdata(L, -1)) {
			if (T* creature = getUserdata<T>(L, -1)) {
				spectators.emplace_back(creature);
			}
		}
		lua_pop(L, 1);
	}

	lua_pop(L, 1);
}
} // namespace Lua

#endif
