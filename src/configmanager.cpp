// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found
// in the LICENSE file.

#include "otpch.h"

#include "configmanager.h"

#include "game.h"
#include "logger.h"
#include "lua.hpp"
#include "luascript.h"
#include "monster.h"
#include "pugicast.h"

#include <fmt/format.h>

#if LUA_VERSION_NUM >= 502
#undef lua_strlen
#define lua_strlen lua_rawlen
#endif

extern Game g_game;

namespace {

std::array<std::string, ConfigManager::String::LAST_STRING> strings = {};
std::array<int64_t, ConfigManager::Integer::LAST_INTEGER> integers = {};
std::array<bool, ConfigManager::Boolean::LAST_BOOLEAN> booleans = {};
std::array<float, ConfigManager::LAST_FLOAT_CONFIG> floats = {};

using ExperienceStages = std::vector<std::tuple<uint32_t, uint32_t, float>>;
ExperienceStages expStages;
ExperienceStages skillStages;
ExperienceStages magicLevelStages;

using BlockedTeleportIds = std::vector<uint16_t>;
using TokenProtectionExceptions = std::vector<uint16_t>;
BlockedTeleportIds blockedTeleportIds;
TokenProtectionExceptions tokenProtectionExceptions;

bool loaded = false;

template <typename T>
auto getEnv(const char* envVar, T&& defaultValue)
{
	if (auto value = std::getenv(envVar)) {
		return pugi::cast<std::decay_t<T>>(value);
	}
	return defaultValue;
}

std::string getGlobalString(lua_State* L, const char* identifier, const char* defaultValue)
{
	lua_getglobal(L, identifier);
	if (!lua_isstring(L, -1)) {
		lua_pop(L, 1);
		return defaultValue;
	}

	size_t len = lua_strlen(L, -1);
	std::string ret{lua_tostring(L, -1), len};
	lua_pop(L, 1);
	return ret;
}

int64_t getGlobalInteger(lua_State* L, const char* identifier, const int64_t defaultValue = 0)
{
	lua_getglobal(L, identifier);
	if (!lua_isinteger(L, -1)) {
		lua_pop(L, 1);
		return defaultValue;
	}

	int64_t val = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return val;
}

bool getGlobalBoolean(lua_State* L, const char* identifier, const bool defaultValue)
{
	lua_getglobal(L, identifier);
	if (!lua_isboolean(L, -1)) {
		if (!lua_isstring(L, -1)) {
			lua_pop(L, 1);
			return defaultValue;
		}

		size_t len = lua_strlen(L, -1);
		std::string ret{lua_tostring(L, -1), len};
		lua_pop(L, 1);
		return booleanString(ret);
	}

	int val = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return val != 0;
}

float getGlobalFloat(lua_State* L, const char* identifier, const float defaultValue = 0.0f)
{
	lua_getglobal(L, identifier);
	if (!lua_isnumber(L, -1)) {
		lua_pop(L, 1);
		return defaultValue;
	}
	float val = static_cast<float>(lua_tonumber(L, -1));
	lua_pop(L, 1);
	return val;
}

ExperienceStages loadLuaStages(lua_State* L)
{
	ExperienceStages stages;

	lua_getglobal(L, "experienceStages");
	if (!lua_istable(L, -1)) {
		return {};
	}

	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		const auto tableIndex = lua_gettop(L);
		auto minLevel = Lua::getField<uint32_t>(L, tableIndex, "minlevel", 1);
		auto maxLevel = Lua::getField<uint32_t>(L, tableIndex, "maxlevel", std::numeric_limits<uint32_t>::max());
		auto multiplier = Lua::getField<float>(L, tableIndex, "multiplier", 1);
		stages.emplace_back(minLevel, maxLevel, multiplier);
		lua_pop(L, 4);
	}
	lua_pop(L, 1);

	std::ranges::sort(stages);
	return stages;
}

ExperienceStages loadLuaSkillStages(lua_State* L)
{
	ExperienceStages stages;

	lua_getglobal(L, "skillStages");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return {};
	}

	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		const auto tableIndex = lua_gettop(L);
		auto minLevel = Lua::getField<uint32_t>(L, tableIndex, "minlevel", 1);
		auto maxLevel = Lua::getField<uint32_t>(L, tableIndex, "maxlevel", std::numeric_limits<uint32_t>::max());
		auto multiplier = Lua::getField<float>(L, tableIndex, "multiplier", 1);
		stages.emplace_back(minLevel, maxLevel, multiplier);
		lua_pop(L, 4);
	}
	lua_pop(L, 1);

	std::ranges::sort(stages);
	return stages;
}

ExperienceStages loadLuaMagicLevelStages(lua_State* L)
{
	ExperienceStages stages;

	lua_getglobal(L, "magicLevelStages");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return {};
	}

	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		const auto tableIndex = lua_gettop(L);
		auto minLevel = Lua::getField<uint32_t>(L, tableIndex, "minlevel", 0);
		auto maxLevel = Lua::getField<uint32_t>(L, tableIndex, "maxlevel", std::numeric_limits<uint32_t>::max());
		auto multiplier = Lua::getField<float>(L, tableIndex, "multiplier", 1);
		stages.emplace_back(minLevel, maxLevel, multiplier);
		lua_pop(L, 4);
	}
	lua_pop(L, 1);

	std::ranges::sort(stages);
	return stages;
}

BlockedTeleportIds loadLuaBlockedTeleportIds(lua_State* L)
{
	BlockedTeleportIds ids;

	lua_getglobal(L, "blockedTeleportIds");
	if (!lua_istable(L, -1)) {
		return {};
	}

	ids.reserve(lua_rawlen(L, -1));
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		const auto id = static_cast<uint16_t>(lua_tointeger(L, -1));
		ids.push_back(id);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return ids;
}

TokenProtectionExceptions loadLuaTokenProtectionExceptions(lua_State* L)
{
	TokenProtectionExceptions ids;

	lua_getglobal(L, "tokenProtectionExceptions");
	if (!lua_istable(L, -1)) {
		return {};
	}

	ids.reserve(lua_rawlen(L, -1));
	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		const auto id = static_cast<uint16_t>(lua_tointeger(L, -1));
		ids.push_back(id);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return ids;
}

} // namespace

bool ConfigManager::load()
{
	LuaStatePtr ownedL(luaL_newstate());
	if (!ownedL) {
		throw std::runtime_error("Failed to allocate memory");
	}

	lua_State* L = ownedL.get();
	luaL_openlibs(L);

	if (strings[CONFIG_FILE].empty()) {
		strings[CONFIG_FILE] = "config.lua";
	}
	if (luaL_dofile(L, getString(String::CONFIG_FILE).data())) {
		g_logger().error("{}", lua_tostring(L, -1));
		return false;
	}

	// parse config
	if (!loaded) { // info that must be loaded one time (unless we reset the modules involved)
		booleans[Boolean::BIND_ONLY_GLOBAL_ADDRESS] = getGlobalBoolean(L, "bindOnlyGlobalAddress", false);
		booleans[Boolean::OPTIMIZE_DATABASE] = getGlobalBoolean(L, "startupDatabaseOptimization", true);

		if (strings[String::IP] == "") {
			strings[String::IP] = getGlobalString(L, "ip", "127.0.0.1");
		}

		strings[String::MAP_NAME] = getGlobalString(L, "mapName", "forgotten");
		strings[String::MAP_AUTHOR] = getGlobalString(L, "mapAuthor", "Unknown");
		strings[String::HOUSE_RENT_PERIOD] = getGlobalString(L, "houseRentPeriod", "never");

		strings[String::MYSQL_HOST] = getGlobalString(L, "mysqlHost", getEnv("MYSQL_HOST", "127.0.0.1"));
		strings[String::MYSQL_USER] = getGlobalString(L, "mysqlUser", getEnv("MYSQL_USER", "forgottenserver"));
		strings[String::MYSQL_PASS] = getGlobalString(L, "mysqlPass", getEnv("MYSQL_PASSWORD", ""));
		strings[String::MYSQL_DB] = getGlobalString(L, "mysqlDatabase", getEnv("MYSQL_DATABASE", "forgottenserver"));
		strings[String::MYSQL_SOCK] = getGlobalString(L, "mysqlSock", getEnv("MYSQL_SOCK", ""));

		integers[Integer::SQL_PORT] = getGlobalInteger(L, "mysqlPort", getEnv<uint16_t>("MYSQL_PORT", 3306));

		if (integers[Integer::GAME_PORT] == 0) {
			integers[Integer::GAME_PORT] = getGlobalInteger(L, "gameProtocolPort", 7172);
		}

		if (integers[Integer::LOGIN_PORT] == 0) {
			integers[Integer::LOGIN_PORT] = getGlobalInteger(L, "loginProtocolPort", 7171);
		}

		integers[Integer::STATUS_PORT] = getGlobalInteger(L, "statusProtocolPort", 7171);
		integers[Integer::ADMIN_PORT] = getGlobalInteger(L, "adminPort", 7170); // Default admin port to 7170

		integers[RESET_LEVEL] = getGlobalInteger(L, "resetLevel", 100);
		integers[RESET_STATBONUS] = getGlobalInteger(L, "resetStats", 5);
		integers[RESET_DMGBONUS] = getGlobalInteger(L, "resetDmgBonus", 0);
		integers[RESET_REDUCTION_PERCENTAGE] = getGlobalInteger(L, "resetReductionPercentage", 0);

		integers[Integer::MARKET_OFFER_DURATION] = getGlobalInteger(L, "marketOfferDuration", 30 * 24 * 60 * 60);
	}

	booleans[Boolean::ALLOW_CHANGEOUTFIT] = getGlobalBoolean(L, "allowChangeOutfit", true);
	booleans[Boolean::ONE_PLAYER_ON_ACCOUNT] = getGlobalBoolean(L, "onePlayerOnlinePerAccount", true);
	booleans[Boolean::AIMBOT_HOTKEY_ENABLED] = getGlobalBoolean(L, "hotkeyAimbotEnabled", true);
	booleans[Boolean::SPAWN_START_EFFECT_ENABLED] = getGlobalBoolean(L, "spawnStartEffectEnabled", true);
	booleans[Boolean::REMOVE_RUNE_CHARGES] = getGlobalBoolean(L, "removeChargesFromRunes", true);
	booleans[Boolean::REMOVE_WEAPON_AMMO] = getGlobalBoolean(L, "removeWeaponAmmunition", true);
	booleans[Boolean::REMOVE_WEAPON_CHARGES] = getGlobalBoolean(L, "removeWeaponCharges", true);
	booleans[Boolean::REMOVE_POTION_CHARGES] = getGlobalBoolean(L, "removeChargesFromPotions", true);
	booleans[Boolean::EXPERIENCE_FROM_PLAYERS] = getGlobalBoolean(L, "experienceByKillingPlayers", false);
	booleans[Boolean::FREE_PREMIUM] = getGlobalBoolean(L, "freePremium", false);
	booleans[Boolean::REPLACE_KICK_ON_LOGIN] = getGlobalBoolean(L, "replaceKickOnLogin", true);
	booleans[Boolean::ALLOW_CLONES] = getGlobalBoolean(L, "allowClones", false);
	booleans[Boolean::ALLOW_WALKTHROUGH] = getGlobalBoolean(L, "allowWalkthrough", true);
	booleans[Boolean::MARKET_PREMIUM] = getGlobalBoolean(L, "premiumToCreateMarketOffer", true);
	booleans[Boolean::EMOTE_SPELLS] = getGlobalBoolean(L, "emoteSpells", false);
	booleans[Boolean::STAMINA_SYSTEM] = getGlobalBoolean(L, "staminaSystem", true);
	booleans[Boolean::WARN_UNSAFE_SCRIPTS] = true;
	booleans[Boolean::CONVERT_UNSAFE_SCRIPTS] = true;
	booleans[Boolean::CLASSIC_EQUIPMENT_SLOTS] = getGlobalBoolean(L, "classicEquipmentSlots", false);
	booleans[Boolean::CLASSIC_ATTACK_SPEED] = getGlobalBoolean(L, "classicAttackSpeed", false);
	booleans[Boolean::SCRIPTS_CONSOLE_LOGS] = getGlobalBoolean(L, "showScriptsLogInConsole", true);
	booleans[Boolean::SERVER_SAVE_NOTIFY_MESSAGE] = getGlobalBoolean(L, "serverSaveNotifyMessage", true);
	booleans[Boolean::SERVER_SAVE_CLEAN_MAP] = getGlobalBoolean(L, "serverSaveCleanMap", false);
	booleans[Boolean::SERVER_SAVE_CLOSE] = getGlobalBoolean(L, "serverSaveClose", false);
	booleans[Boolean::SERVER_SAVE_SHUTDOWN] = getGlobalBoolean(L, "serverSaveShutdown", true);
	booleans[Boolean::ONLINE_OFFLINE_CHARLIST] = getGlobalBoolean(L, "showOnlineStatusInCharlist", false);
	booleans[Boolean::YELL_ALLOW_PREMIUM] = getGlobalBoolean(L, "yellAlwaysAllowPremium", false);
	booleans[Boolean::PREMIUM_TO_SEND_PRIVATE] = getGlobalBoolean(L, "premiumToSendPrivate", false);
	booleans[Boolean::FORCE_MONSTERTYPE_LOAD] = getGlobalBoolean(L, "forceMonsterTypesOnLoad", true);
	booleans[Boolean::DEFAULT_WORLD_LIGHT] = getGlobalBoolean(L, "defaultWorldLight", true);
	booleans[Boolean::HOUSE_OWNED_BY_ACCOUNT] = getGlobalBoolean(L, "houseOwnedByAccount", false);
	booleans[Boolean::CLEAN_PROTECTION_ZONES] = getGlobalBoolean(L, "cleanProtectionZones", false);
	booleans[Boolean::HOUSE_DOOR_SHOW_PRICE] = getGlobalBoolean(L, "houseDoorShowPrice", true);
	booleans[Boolean::ONLY_INVITED_CAN_MOVE_HOUSE_ITEMS] = getGlobalBoolean(L, "onlyInvitedCanMoveHouseItems", true);
	booleans[Boolean::REMOVE_ON_DESPAWN] = getGlobalBoolean(L, "removeOnDespawn", true);
	booleans[Boolean::MONSTER_OVERSPAWN] = getGlobalBoolean(L, "monsterOverspawn", false);
	booleans[Boolean::MANASHIELD_BREAKABLE] = getGlobalBoolean(L, "useBreakableManaShield", false);
	booleans[Boolean::BED_OFFLINE_TRAINING] = getGlobalBoolean(L, "bedOfflineTraining", true);
	booleans[Boolean::ALLOW_AUTO_ATTACK_WITHOUT_EXHAUSTION] =
	    getGlobalBoolean(L, "allowAutoAttackWithoutExhaustion", true);
	booleans[Boolean::POTION_CAN_EXHAUST_ITEM] = getGlobalBoolean(L, "exhaustItemAtUsePotion", true);
	booleans[Boolean::SEPARATE_RING_NECKLACE_EXHAUSTION] =
	    getGlobalBoolean(L, "separateRingNecklaceExhaustion", true);
	booleans[Boolean::ACCOUNT_MANAGER] = getGlobalBoolean(L, "accountManager", false);
	booleans[Boolean::NAMELOCK_MANAGER] = getGlobalBoolean(L, "namelockManager", false);
	booleans[Boolean::START_CHOOSEVOC] = getGlobalBoolean(L, "newPlayerChooseVoc", false);
	booleans[Boolean::GENERATE_ACCOUNT_NUMBER] = getGlobalBoolean(L, "generateAccountNumber", false);
	booleans[Boolean::CHECK_DUPLICATE_STORAGE_KEYS] = getGlobalBoolean(L, "checkDuplicateStorageKeys", false);
	booleans[Boolean::DLL_CHECK_KICK] = getGlobalBoolean(L, "dllCheckKick", false);
	booleans[Boolean::RESET_SYSTEM_ENABLED] = getGlobalBoolean(L, "resetSystemEnabled", false); // reset system
	booleans[Boolean::RESET_SKILLS] = getGlobalBoolean(L, "resetSkills", true); // reset skills/magic on reset
	booleans[Boolean::NPCS_USING_BANK_MONEY] = getGlobalBoolean(L, "npcsUsingBankMoney", false);
	booleans[Boolean::STAMINA_TRAINER] = getGlobalBoolean(L, "staminaTrainer", false);
	booleans[Boolean::STAMINA_PZ] = getGlobalBoolean(L, "staminaPz", false);
	booleans[Boolean::PUSH_CREATURE_ZONE] = getGlobalBoolean(L, "pushCreatureZone", false);
	booleans[Boolean::REMOVE_SUMMONS_ON_PZ] = getGlobalBoolean(L, "removeSummonsOnPz", false);
	booleans[Boolean::FAMILIAR_ENTER_PZ] = getGlobalBoolean(L, "familiarEnterPz", true);
	booleans[Boolean::TELEPORT_SUMMON] = getGlobalBoolean(L, "teleportSummon", false);
	booleans[Boolean::FORGE_SYSTEM_ENABLED] = getGlobalBoolean(L, "forgeSystemEnabled", false);
	booleans[Boolean::IMBUEMENT_SYSTEM_ENABLED] = getGlobalBoolean(L, "imbuementSystemEnabled", false);
	booleans[Boolean::MONK_VOCATION_ENABLED] = getGlobalBoolean(L, "monkVocationEnabled", false);
	booleans[Boolean::FAMILIAR_SYSTEM_ENABLED] = getGlobalBoolean(L, "familiarSystemEnabled", false);
	booleans[Boolean::BESTIARY_SYSTEM_ENABLED] = getGlobalBoolean(L, "bestiarySystemEnabled", false);
	booleans[Boolean::MARKET_SYSTEM_ENABLED] = getGlobalBoolean(L, "marketSystemEnabled", false);
	booleans[Boolean::PREY_SYSTEM_ENABLED] = getGlobalBoolean(L, "preySystemEnabled", false);
	booleans[Boolean::ALLOW_MOUNT_IN_PZ] = getGlobalBoolean(L, "allowMountInPz", false);

	// Admin Config
	booleans[Boolean::ADMIN_LOCALHOST_ONLY] = getGlobalBoolean(L, "adminLocalhostOnly", true);
	booleans[Boolean::ADMIN_REQUIRE_LOGIN] = getGlobalBoolean(L, "adminRequireLogin", true);
	booleans[Boolean::ADMIN_LOGS] = getGlobalBoolean(L, "adminLogs", false);
	booleans[Boolean::SLOW_TASK_WARNING] = getGlobalBoolean(L, "slowTaskWarning", true);

	strings[String::DEFAULT_PRIORITY] = getGlobalString(L, "defaultPriority", "high");
	strings[String::SERVER_NAME] = getGlobalString(L, "serverName", "");
	strings[String::OWNER_NAME] = getGlobalString(L, "ownerName", "");
	strings[String::OWNER_EMAIL] = getGlobalString(L, "ownerEmail", "");
	strings[String::URL] = getGlobalString(L, "url", "");
	strings[String::LOCATION] = getGlobalString(L, "location", "");
	strings[String::MOTD] = getGlobalString(L, "motd", "");
	strings[String::WORLD_TYPE] = getGlobalString(L, "worldType", "pvp");
	strings[String::NPC_SYSTEM] = getGlobalString(L, "npcSystem", "tfs");

	Monster::despawnRange = getGlobalInteger(L, "deSpawnRange", 2);
	Monster::despawnRadius = getGlobalInteger(L, "deSpawnRadius", 50);

	integers[Integer::MAX_PLAYERS] = getGlobalInteger(L, "maxPlayers");
	integers[Integer::PZ_LOCKED] = getGlobalInteger(L, "pzLocked", 60000);
	integers[Integer::DEFAULT_DESPAWNRANGE] = Monster::despawnRange;
	integers[Integer::DEFAULT_DESPAWNRADIUS] = Monster::despawnRadius;
	integers[Integer::DEFAULT_WALKTOSPAWNRADIUS] = getGlobalInteger(L, "walkToSpawnRadius", 15);
	integers[Integer::FAMILIAR_TELEPORT_RANGE] = getGlobalInteger(L, "familiarTeleportRange", 15);
	integers[Integer::RATE_EXPERIENCE] = getGlobalInteger(L, "rateExp", 5);
	integers[Integer::RATE_SKILL] = getGlobalInteger(L, "rateSkill", 3);
	integers[Integer::RATE_LOOT] = getGlobalInteger(L, "rateLoot", 2);
	integers[Integer::RATE_MAGIC] = getGlobalInteger(L, "rateMagic", 3);
	integers[Integer::RATE_SPAWN] = getGlobalInteger(L, "rateSpawn", 1);
	integers[Integer::SPAWN_MULTIPLIER] = getGlobalInteger(L, "spawnMultiplier", 1);
	integers[Integer::RATE_START_EFFECT] = getGlobalInteger(L, "timeStartEffect", 4200);
	integers[Integer::RATE_BETWEEN_EFFECT] = getGlobalInteger(L, "timeBetweenTeleportEffects", 1400);
	integers[Integer::HOUSE_LEVEL] = getGlobalInteger(L, "houseLevel", 150);
	integers[Integer::HOUSE_PRICE] = getGlobalInteger(L, "housePriceEachSQM", 1000);
	integers[Integer::KILLS_TO_RED] = getGlobalInteger(L, "killsToRedSkull", 3);
	integers[Integer::KILLS_TO_BLACK] = getGlobalInteger(L, "killsToBlackSkull", 6);
	integers[Integer::ACTIONS_DELAY_INTERVAL] = getGlobalInteger(L, "timeBetweenActions", 200);
	integers[Integer::EX_ACTIONS_DELAY_INTERVAL] = getGlobalInteger(L, "timeBetweenExActions", 1000);
	integers[Integer::NECKLACE_DELAY_INTERVAL] = getGlobalInteger(L, "timeBetweenNecklace", 500);
	integers[Integer::RING_DELAY_INTERVAL] = getGlobalInteger(L, "timeBetweenRing", 500);
	integers[Integer::EXHAUST_POTION_INTERVAL] = getGlobalInteger(L, "exhaustPotionMiliSeconds", 1500);
	integers[Integer::MAX_MESSAGEBUFFER] = getGlobalInteger(L, "maxMessageBuffer", 4);
	integers[Integer::KICK_AFTER_MINUTES] = getGlobalInteger(L, "kickIdlePlayerAfterMinutes", 15);
	integers[Integer::PROTECTION_LEVEL] = getGlobalInteger(L, "protectionLevel", 1);
	integers[Integer::DEATH_LOSE_PERCENT] = getGlobalInteger(L, "deathLosePercent", -1);
	integers[Integer::STATUSQUERY_TIMEOUT] = getGlobalInteger(L, "statusTimeout", 5000);
	integers[Integer::STATUS_COUNT_MAX_PLAYERS_PER_IP] = getGlobalInteger(L, "statusCountMaxPlayersPerIp", 0);
	integers[Integer::FRAG_TIME] = getGlobalInteger(L, "timeToDecreaseFrags", 24 * 60 * 60);
	integers[Integer::WHITE_SKULL_TIME] = getGlobalInteger(L, "whiteSkullTime", 15 * 60);
	integers[Integer::STAIRHOP_DELAY] = getGlobalInteger(L, "stairJumpExhaustion", 2000);
	integers[Integer::EXP_SHARE_RANGE] = getGlobalInteger(L, "experienceShareRange", 30);
	integers[Integer::EXP_SHARE_FLOORS] = getGlobalInteger(L, "experienceShareFloors", 1);
	integers[Integer::EXP_FROM_PLAYERS_LEVEL_RANGE] = getGlobalInteger(L, "expFromPlayersLevelRange", 75);
	integers[Integer::MAX_PACKETS_PER_SECOND] = getGlobalInteger(L, "maxPacketsPerSecond", 25);
	integers[Integer::SERVER_SAVE_NOTIFY_DURATION] = getGlobalInteger(L, "serverSaveNotifyDuration", 5);
	integers[Integer::YELL_MINIMUM_LEVEL] = getGlobalInteger(L, "yellMinimumLevel", 2);
	integers[Integer::MINIMUM_LEVEL_TO_SEND_PRIVATE] = getGlobalInteger(L, "minimumLevelToSendPrivate", 1);
	integers[Integer::VIP_FREE_LIMIT] = getGlobalInteger(L, "vipFreeLimit", 20);
	integers[Integer::VIP_PREMIUM_LIMIT] = getGlobalInteger(L, "vipPremiumLimit", 100);
	integers[Integer::DEPOT_FREE_LIMIT] = getGlobalInteger(L, "depotFreeLimit", 2000);
	integers[Integer::DEPOT_PREMIUM_LIMIT] = getGlobalInteger(L, "depotPremiumLimit", 15000);
	integers[Integer::PROTECTION_TIME] = getGlobalInteger(L, "protectionTime", 10);
	integers[Integer::STAMINA_REGEN_MINUTE] = getGlobalInteger(L, "timeToRegenMinuteStamina", 3 * 60);
	integers[Integer::STAMINA_REGEN_PREMIUM] = getGlobalInteger(L, "timeToRegenMinutePremiumStamina", 6 * 60);
	integers[Integer::STAMINA_PZ_GAIN] = getGlobalInteger(L, "staminaPzGain", 1);
	integers[Integer::STAMINA_ORANGE_DELAY] = getGlobalInteger(L, "staminaOrangeDelay", 1);
	integers[Integer::STAMINA_GREEN_DELAY] = getGlobalInteger(L, "staminaGreenDelay", 5);
	integers[Integer::STAMINA_TRAINER_DELAY] = getGlobalInteger(L, "staminaTrainerDelay", 5);
	integers[Integer::STAMINA_TRAINER_GAIN] = getGlobalInteger(L, "staminaTrainerGain", 1);
	strings[String::STAMINATRAINER_NAMES] = getGlobalString(L, "staminaTrainerNames", "");
	integers[Integer::HEALTH_GAIN_COLOUR] = getGlobalInteger(L, "healthGainColour", TEXTCOLOR_MAYABLUE);
	integers[Integer::MANA_GAIN_COLOUR] = getGlobalInteger(L, "manaGainColour", TEXTCOLOR_MAYABLUE);
	integers[Integer::MANA_LOSS_COLOUR] = getGlobalInteger(L, "manaLossColour", TEXTCOLOR_BLUE);
	integers[Integer::MAX_PROTOCOL_OUTFITS] = getGlobalInteger(L, "maxProtocolOutfits", 255);
	integers[Integer::MAX_ADDON_ATTRIBUTES] = getGlobalInteger(L, "maxAddonAttributes", 3);
	integers[Integer::MOVE_CREATURE_INTERVAL] = getGlobalInteger(L, "MOVE_CREATURE_INTERVAL", MOVE_CREATURE_INTERVAL);
	integers[Integer::RANGE_MOVE_CREATURE_INTERVAL] =
	    getGlobalInteger(L, "RANGE_MOVE_CREATURE_INTERVAL", RANGE_MOVE_CREATURE_INTERVAL);
	integers[Integer::RANGE_USE_WITH_CREATURE_INTERVAL] =
	    getGlobalInteger(L, "RANGE_USE_WITH_CREATURE_INTERVAL", RANGE_USE_WITH_CREATURE_INTERVAL);
	integers[Integer::RANGE_MOVE_ITEM_INTERVAL] =
	    getGlobalInteger(L, "RANGE_MOVE_ITEM_INTERVAL", RANGE_MOVE_ITEM_INTERVAL);
	integers[Integer::RANGE_USE_ITEM_INTERVAL] =
	    getGlobalInteger(L, "RANGE_USE_ITEM_INTERVAL", RANGE_USE_ITEM_INTERVAL);
	integers[Integer::RANGE_USE_ITEM_EX_INTERVAL] =
	    getGlobalInteger(L, "RANGE_USE_ITEM_EX_INTERVAL", RANGE_USE_ITEM_EX_INTERVAL);
	integers[Integer::RANGE_ROTATE_ITEM_INTERVAL] =
	    getGlobalInteger(L, "RANGE_ROTATE_ITEM_INTERVAL", RANGE_ROTATE_ITEM_INTERVAL);
	integers[Integer::PLAYER_SPEED_PER_LEVEL] = getGlobalInteger(L, "playerSpeedPerLevel", 2);
	integers[Integer::PLAYER_MIN_SPEED] = getGlobalInteger(L, "playerMinSpeed", 120);
	integers[Integer::PLAYER_MAX_SPEED] = getGlobalInteger(L, "playerMaxSpeed", 900);
	integers[Integer::MAX_GOD_SPEED] = getGlobalInteger(L, "maxGodSpeed", 5000);

	integers[Integer::REWARD_CHEST_EXPIRE_DAYS] = getGlobalInteger(L, "rewardChestExpireDays", 7);

	floats[REWARD_BASE_RATE] = getGlobalFloat(L, "rewardBaseRate", 1.0f);
	floats[REWARD_RATE_DAMAGE_DONE] = getGlobalFloat(L, "rewardRateDamageDone", 1.0f);
	floats[REWARD_RATE_DAMAGE_TAKEN] = getGlobalFloat(L, "rewardRateDamageTaken", 1.0f);
	floats[REWARD_RATE_HEALING_DONE] = getGlobalFloat(L, "rewardRateHealingDone", 1.0f);
	floats[OFFLINE_TRAINING_EFFICIENCY] = getGlobalFloat(L, "offlineTrainingEfficiency", 0.5f);
	floats[OFFLINE_TRAINING_MAGE_ML] = getGlobalFloat(L, "offlineTrainingMageML", 1.0f);
	floats[OFFLINE_TRAINING_PALADIN_DIST] = getGlobalFloat(L, "offlineTrainingPaladinDist", 0.5f);
	floats[OFFLINE_TRAINING_PALADIN_ML] = getGlobalFloat(L, "offlineTrainingPaladinML", 0.25f);
	floats[OFFLINE_TRAINING_PALADIN_SHIELD] = getGlobalFloat(L, "offlineTrainingPaladinShield", 0.25f);
	floats[OFFLINE_TRAINING_KNIGHT_MELEE] = getGlobalFloat(L, "offlineTrainingKnightMelee", 0.5f);
	floats[OFFLINE_TRAINING_KNIGHT_SHIELD] = getGlobalFloat(L, "offlineTrainingKnightShield", 0.5f);
	floats[OFFLINE_TRAINING_MONK_MELEE] = getGlobalFloat(L, "offlineTrainingMonkMelee", 0.5f);
	floats[OFFLINE_TRAINING_MONK_SHIELD] = getGlobalFloat(L, "offlineTrainingMonkShield", 0.5f);

	floats[FORGE_FATAL_A] = getGlobalFloat(L, "forgeFatalA", 0.05f);
	floats[FORGE_FATAL_B] = getGlobalFloat(L, "forgeFatalB", 0.4f);
	floats[FORGE_FATAL_C] = getGlobalFloat(L, "forgeFatalC", 0.05f);
	floats[FORGE_DODGE_A] = getGlobalFloat(L, "forgeDodgeA", 0.0307576f);
	floats[FORGE_DODGE_B] = getGlobalFloat(L, "forgeDodgeB", 0.440697f);
	floats[FORGE_DODGE_C] = getGlobalFloat(L, "forgeDodgeC", 0.026f);
	floats[FORGE_MOMENTUM_A] = getGlobalFloat(L, "forgeMomentumA", 0.05f);
	floats[FORGE_MOMENTUM_B] = getGlobalFloat(L, "forgeMomentumB", 1.9f);
	floats[FORGE_MOMENTUM_C] = getGlobalFloat(L, "forgeMomentumC", 0.05f);
	floats[FORGE_TRANSCENDENCE_A] = getGlobalFloat(L, "forgeTranscendenceA", 0.0127f);
	floats[FORGE_TRANSCENDENCE_B] = getGlobalFloat(L, "forgeTranscendenceB", 0.1070f);
	floats[FORGE_TRANSCENDENCE_C] = getGlobalFloat(L, "forgeTranscendenceC", 0.0073f);
	floats[BOOSTED_EXP_MULTIPLIER] = getGlobalFloat(L, "boostedExpMultiplier", 2.0f);
	floats[BOOSTED_LOOT_MULTIPLIER] = getGlobalFloat(L, "boostedLootMultiplier", 2.0f);
	floats[BOOSTED_SPAWN_MULTIPLIER] = getGlobalFloat(L, "boostedSpawnMultiplier", 0.5f);

	integers[Integer::NEW_PLAYER_SPAWN_POS_X] = getGlobalInteger(L, "newPlayerSpawnPosX", 0);
	integers[Integer::NEW_PLAYER_SPAWN_POS_Y] = getGlobalInteger(L, "newPlayerSpawnPosY", 0);
	integers[Integer::NEW_PLAYER_SPAWN_POS_Z] = getGlobalInteger(L, "newPlayerSpawnPosZ", 0);
	integers[Integer::NEW_PLAYER_TOWN_ID] = getGlobalInteger(L, "newPlayerTownId", 1);
	integers[Integer::NEW_PLAYER_LEVEL] = getGlobalInteger(L, "newPlayerLevel", 1);
	integers[Integer::NEW_PLAYER_MAGIC_LEVEL] = getGlobalInteger(L, "newPlayerMagicLevel", 0);
	integers[Integer::NEW_PLAYER_HEALTH] = getGlobalInteger(L, "newPlayerHealth", 150);
	integers[Integer::NEW_PLAYER_MANA] = getGlobalInteger(L, "newPlayerMana", 60);
	integers[Integer::NEW_PLAYER_CAP] = getGlobalInteger(L, "newPlayerCap", 400);
	integers[Integer::MAX_ALLOWED_ON_A_DUMMY] = getGlobalInteger(L, "maxAllowedOnADummy", 5);
	integers[Integer::RATE_EXERCISE_TRAINING_SPEED] = getGlobalInteger(L, "rateExerciseTrainingSpeed", 1.0);
	integers[Integer::DLL_CHECK_KICK_TIME] = getGlobalInteger(L, "dllCheckKickTime", 300);
	integers[Integer::OFFLINE_TRAINING_THRESHOLD] = getGlobalInteger(L, "offlineTrainingThreshold", 600);

	integers[Integer::STATS_DUMP_INTERVAL] = getGlobalInteger(L, "statsDumpInterval", 30000);
	integers[Integer::STATS_SLOW_LOG_TIME] = getGlobalInteger(L, "statsSlowLogTime", 10);
	integers[Integer::STATS_VERY_SLOW_LOG_TIME] = getGlobalInteger(L, "statsVerySlowLogTime", 50);

	expStages = loadLuaStages(L);
	expStages.shrink_to_fit();

	skillStages = loadLuaSkillStages(L);
	skillStages.shrink_to_fit();

	magicLevelStages = loadLuaMagicLevelStages(L);
	magicLevelStages.shrink_to_fit();

	blockedTeleportIds = loadLuaBlockedTeleportIds(L);
	tokenProtectionExceptions = loadLuaTokenProtectionExceptions(L);

	// AutoLoot Config
	booleans[Boolean::AUTOLOOT_ENABLED] = getGlobalBoolean(L, "Autoloot_enabled", true);
	strings[String::AUTOLOOT_BLOCKIDS] = getGlobalString(L, "AutoLoot_BlockIDs", "");
	strings[String::AUTOLOOT_MONEYIDS] = getGlobalString(L, "AutoLoot_MoneyIDs", "2148;2152;2160");
	integers[Integer::AUTOLOOT_MAXITEMS_FREE] = getGlobalInteger(L, "AutoLoot_MaxItemFree", 5);
	integers[Integer::AUTOLOOT_MAXITEMS_PREMIUM] = getGlobalInteger(L, "AutoLoot_MaxItemPremium", 10);

	// Guild War Config
	integers[Integer::GUILD_WAR_MIN_FRAG_LIMIT] = getGlobalInteger(L, "guildWarMinFragLimit", 10);
	integers[Integer::GUILD_WAR_MAX_FRAG_LIMIT] = getGlobalInteger(L, "guildWarMaxFragLimit", 1000);
	booleans[Boolean::GUILD_WAR_ANNOUNCE_KILLS] = getGlobalBoolean(L, "guildWarAnnounceKills", true);

	strings[String::ADMIN_PASSWORD] = getGlobalString(L, "adminPassword", "");
	strings[String::ADMIN_ENCRYPTION] = getGlobalString(L, "adminEncryption", "");
	strings[String::ADMIN_ENCRYPTION_DATA] = getGlobalString(L, "adminEncryptionData", "");
	integers[Integer::ADMIN_CONNECTIONS_LIMIT] = getGlobalInteger(L, "adminConnectionsLimit", 1);

	// Connection Limits
	integers[Integer::MAX_CONNECTIONS] = getGlobalInteger(L, "maxConnections", 2000);
	integers[Integer::MAX_CONNECTIONS_PER_IP] = getGlobalInteger(L, "maxConnectionsPerIP", 10);
	integers[Integer::NETWORK_THREADS] = getGlobalInteger(L, "networkThreads", 2);
	integers[Integer::CONNECTION_RATE_LIMIT_ALLOWED] = getGlobalInteger(L, "connectionRateLimitAllowed", 10);
	integers[Integer::CONNECTION_RATE_LIMIT_MS] = getGlobalInteger(L, "connectionRateLimitMS", 500);

	loaded = true;
	// ownedL destructor calls lua_close via LuaStateDeleter

	return true;
}

bool ConfigManager::getBoolean(Boolean what)
{
	if (what >= Boolean::LAST_BOOLEAN) {
		LOG_WARN(
		    fmt::format("[Warning - ConfigManager::getBoolean] Accessing invalid index: {}", static_cast<int>(what)));
		return false;
	}
	return booleans[what];
}

std::string_view ConfigManager::getString(String what)
{
	if (what >= String::LAST_STRING) {
		LOG_WARN(
		    fmt::format("[Warning - ConfigManager::getString] Accessing invalid index: {}", static_cast<int>(what)));
		return "";
	}
	return strings[what];
}

int64_t ConfigManager::getInteger(Integer what)
{
	if (what >= Integer::LAST_INTEGER) {
		LOG_WARN(
		    fmt::format("[Warning - ConfigManager::getInteger] Accessing invalid index: {}", static_cast<int>(what)));
		return 0;
	}
	return integers[what];
}

float ConfigManager::getExperienceStage(uint32_t level)
{
	auto it = std::find_if(expStages.begin(), expStages.end(), [level](auto&& stage) {
		auto&& [minLevel, maxLevel, _] = stage;
		return level >= minLevel && level <= maxLevel;
	});

	if (it == expStages.end()) {
		return getInteger(Integer::RATE_EXPERIENCE);
	}

	return std::get<2>(*it);
}

float ConfigManager::getSkillStage(uint32_t level)
{
	auto it = std::find_if(skillStages.begin(), skillStages.end(), [level](auto&& stage) {
		auto&& [minLevel, maxLevel, _] = stage;
		return level >= minLevel && level <= maxLevel;
	});

	if (it == skillStages.end()) {
		return getInteger(Integer::RATE_SKILL);
	}

	return std::get<2>(*it);
}

float ConfigManager::getMagicLevelStage(uint32_t level)
{
	auto it = std::find_if(magicLevelStages.begin(), magicLevelStages.end(), [level](auto&& stage) {
		auto&& [minLevel, maxLevel, _] = stage;
		return level >= minLevel && level <= maxLevel;
	});

	if (it == magicLevelStages.end()) {
		return getInteger(Integer::RATE_MAGIC);
	}

	return std::get<2>(*it);
}

bool ConfigManager::setBoolean(Boolean what, bool value)
{
	if (what >= Boolean::LAST_BOOLEAN) {
		LOG_WARN(
		    fmt::format("[Warning - ConfigManager::setBoolean] Accessing invalid index: {}", static_cast<int>(what)));
		return false;
	}

	booleans[what] = value;
	return true;
}

bool ConfigManager::setString(String what, std::string_view value)
{
	if (what >= String::LAST_STRING) {
		LOG_WARN(
		    fmt::format("[Warning - ConfigManager::setString] Accessing invalid index: {}", static_cast<int>(what)));
		return false;
	}

	strings[what] = value;
	return true;
}

bool ConfigManager::setInteger(Integer what, int64_t value)
{
	if (what >= Integer::LAST_INTEGER) {
		LOG_WARN(
		    fmt::format("[Warning - ConfigManager::setInteger] Accessing invalid index: {}", static_cast<int>(what)));
		return false;
	}

	integers[what] = value;
	return true;
}

float ConfigManager::getFloat(float_config_t what)
{
	if (what >= LAST_FLOAT_CONFIG) {
		LOG_WARN(
		    fmt::format("[Warning - ConfigManager::getFloat] Accessing invalid index: {}", static_cast<int>(what)));
		return 0.0f;
	}
	return floats[what];
}

bool ConfigManager::setFloat(float_config_t what, float value)
{
	if (what >= LAST_FLOAT_CONFIG) {
		LOG_WARN(
		    fmt::format("[Warning - ConfigManager::setFloat] Accessing invalid index: {}", static_cast<int>(what)));
		return false;
	}
	floats[what] = value;
	return true;
}

const BlockedTeleportIds& ConfigManager::getBlockedTeleportIds() { return blockedTeleportIds; }

const TokenProtectionExceptions& ConfigManager::getTokenProtectionExceptions() { return tokenProtectionExceptions; }
