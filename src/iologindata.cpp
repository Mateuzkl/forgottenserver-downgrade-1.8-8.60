// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.


#include "otpch.h"

#include "iologindata.h"

#include "configmanager.h"
#include "stats.h"
#include "game.h"
#include "inbox.h"
#include "player.h"
#include "vocation.h"
#include "logger.h"
#include <fmt/format.h>

extern Game g_game;
extern Vocations g_vocations;

Account IOLoginData::loadAccount(uint32_t accno)
{
	Account account;

	DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
	    "SELECT `id`, `name`, `type`, `premium_ends_at`, `tibia_coins` FROM `accounts` WHERE `id` = {:d}", accno));
	if (!result) {
		return account;
	}

	account.id = result->getNumber<uint32_t>("id");
	account.name = result->getString("name");
	account.accountType = static_cast<AccountType_t>(result->getNumber<int32_t>("type"));
	account.premiumEndsAt = result->getNumber<time_t>("premium_ends_at");
	account.tibiaCoins = result->getNumber<uint64_t>("tibia_coins");
	return account;
}

std::string decodeSecret(std::string_view secret)
{
	// simple base32 decoding
	std::string key;
	key.reserve(10);

	uint32_t buffer = 0, left = 0;
	for (const auto& ch : secret) {
		buffer <<= 5;
		if (ch >= 'A' && ch <= 'Z') {
			buffer |= (ch & 0x1F) - 1;
		} else if (ch >= '2' && ch <= '7') {
			buffer |= ch - 24;
		} else {
			// if a key is broken, return empty and the comparison
			// will always be false since the token must not be empty
			return {};
		}

		left += 5;
		if (left >= 8) {
			left -= 8;
			key.push_back(static_cast<char>(buffer >> left));
		}
	}

	return key;
}

bool IOLoginData::loginserverAuthentication(std::string_view name, std::string_view password, Account& account)
{
    Database& db = Database::getInstance();


    DBResult_ptr result = db.storeQuery(fmt::format(
        "SELECT `id`, `name`, `password`, `secret`, `type`, `premium_ends_at`, `tibia_coins` FROM `accounts` WHERE LOWER(`name`) = LOWER({:s})",
        db.escapeString(name)));
    if (!result) {
        return false;
    }

    std::string storedPassword = std::string{result->getString("password")};
    std::string inputPassword = transformToSHA1Hex(password);
    
    if (storedPassword != inputPassword) {
        return false;
    }
    

	account.id = result->getNumber<uint32_t>("id");
	account.name = result->getString("name");
	account.key = decodeSecret(result->getString("secret"));
	account.accountType = static_cast<AccountType_t>(result->getNumber<int32_t>("type"));
	account.premiumEndsAt = result->getNumber<time_t>("premium_ends_at");
	account.tibiaCoins = result->getNumber<uint64_t>("tibia_coins");

    result = db.storeQuery(fmt::format(
        "SELECT `name` FROM `players` WHERE `account_id` = {:d} AND `deletion` = 0 ORDER BY `name` ASC", account.id));
    if (result) {
        do {
            std::string charName = std::string{result->getString("name")};
            account.characters.push_back(charName);
        } while (result->next());
    } else {
    }
    return true;
}

std::pair<uint32_t, uint32_t> IOLoginData::gameworldAuthentication(std::string_view accountName,
                                                                   std::string_view password,
                                                                   std::string_view characterName)
{
    Database& db = Database::getInstance();
    
    std::string query = fmt::format(
        "SELECT `a`.`id` AS `account_id`, `a`.`password`, `a`.`secret`, `p`.`id` AS `character_id` FROM `accounts` `a` JOIN `players` `p` ON `a`.`id` = `p`.`account_id` WHERE LOWER(`a`.`name`) = LOWER({:s}) AND LOWER(`p`.`name`) = LOWER({:s}) AND `p`.`deletion` = 0",
        db.escapeString(accountName), db.escapeString(characterName));
    
    DBResult_ptr result = db.storeQuery(query);
    if (!result) {
        // Fallback path: validate account and use the first available character of the account
        DBResult_ptr accountCheck = db.storeQuery(fmt::format(
            "SELECT `id`, `name`, `password`, `secret` FROM `accounts` WHERE LOWER(`name`) = LOWER({:s})",
            db.escapeString(accountName)));
        if (!accountCheck) {
            return {};
        }

        uint32_t fallbackAccountId = accountCheck->getNumber<uint32_t>("id");
        std::string storedPassword = std::string{accountCheck->getString("password")};
        std::string inputPassword = transformToSHA1Hex(password);
        if (storedPassword != inputPassword) {
            return {};
        }

        // Special-case: Account Manager selection from non-1 account
        if (ConfigManager::getBoolean(ConfigManager::ACCOUNT_MANAGER) && characterName == "Account Manager" && fallbackAccountId != 1) {
            DBResult_ptr accMgrRes = db.storeQuery("SELECT `id` FROM `players` WHERE `name` = 'Account Manager' AND `account_id` = 1 AND `deletion` = 0");
            if (!accMgrRes) {
                return {};
            }
            uint32_t accountManagerId = accMgrRes->getNumber<uint32_t>("id");
			return {fallbackAccountId, accountManagerId};
        }

        // Pick the first character from this account if specific character was not found/matched
        DBResult_ptr firstCharRes = db.storeQuery(fmt::format(
            "SELECT `id`, `name` FROM `players` WHERE `account_id` = {:d} AND `deletion` = 0 ORDER BY `name` ASC LIMIT 1",
            fallbackAccountId));
        if (!firstCharRes) {
            return {};
        }

        uint32_t fallbackCharacterId = firstCharRes->getNumber<uint32_t>("id");
        std::string fallbackCharacterName = std::string{firstCharRes->getString("name")};
        return {fallbackAccountId, fallbackCharacterId};
    }

    std::string storedPassword = std::string{result->getString("password")};
    std::string inputPassword = transformToSHA1Hex(password);
    
    if (storedPassword != inputPassword) {
        return {};
    }
    

	uint32_t accountId = result->getNumber<uint32_t>("account_id");
	uint32_t characterId = result->getNumber<uint32_t>("character_id");

	if (ConfigManager::getBoolean(ConfigManager::ACCOUNT_MANAGER) && characterName == "Account Manager" && accountId != 1) {
        result = db.storeQuery("SELECT `id` FROM `players` WHERE `name` = 'Account Manager' AND `account_id` = 1 AND `deletion` = 0");
        if (!result) {
            return {};
        }
        uint32_t accountManagerId = result->getNumber<uint32_t>("id");
        // Return the user's authenticated account id with the Account Manager character id
        return {accountId, accountManagerId};
    }

	return {accountId, characterId};
}

uint32_t IOLoginData::getAccountIdByPlayerName(std::string_view playerName)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(
	    fmt::format("SELECT `account_id` FROM `players` WHERE `name` = {:s}", db.escapeString(playerName)));
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("account_id");
}

uint32_t IOLoginData::getAccountIdByPlayerId(uint32_t playerId)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `account_id` FROM `players` WHERE `id` = {:d}", playerId));
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("account_id");
}

std::vector<LiveCastInfo> IOLoginData::liveCastAuthentication(const std::string& password)
{
	std::vector<LiveCastInfo> casts;

	for(Player* caster : g_game.getLiveCasters(password)) {
		LiveCastInfo info;
		info.name = caster->getName();
		info.level = caster->getLevel();
		info.spectatorCount = caster->getSpectatorCount();
		casts.push_back(info);
	}

	std::sort(casts.begin(), casts.end(), [](const LiveCastInfo& a, const LiveCastInfo& b) {
		return a.name < b.name;
	});
	return casts;
}

AccountType_t IOLoginData::getAccountType(uint32_t accountId)
{
	DBResult_ptr result =
	    Database::getInstance().storeQuery(fmt::format("SELECT `type` FROM `accounts` WHERE `id` = {:d}", accountId));
	if (!result) {
		return ACCOUNT_TYPE_NORMAL;
	}
	return static_cast<AccountType_t>(result->getNumber<uint16_t>("type"));
}

void IOLoginData::setAccountType(uint32_t accountId, AccountType_t accountType)
{
	Database::getInstance().executeQuery(fmt::format("UPDATE `accounts` SET `type` = {:d} WHERE `id` = {:d}",
	                                                 static_cast<uint16_t>(accountType), accountId));
}

void IOLoginData::updateOnlineStatus(uint32_t guid, bool login)
{
	if (guid == 1) {
		return;
	}

	if (getBoolean(ConfigManager::ALLOW_CLONES)) {
		return;
	}

	if (login) {
		Database::getInstance().executeQuery(fmt::format("INSERT INTO `players_online` (`player_id`) VALUES ({:d})", guid));
	} else {
		Database::getInstance().executeQuery(
		    fmt::format("DELETE FROM `players_online` WHERE `player_id` = {:d}", guid));
	}
}

bool IOLoginData::preloadPlayer(Player* player)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format(
	    "SELECT `p`.`name`, `p`.`account_id`, `p`.`group_id`, `a`.`type`, `a`.`premium_ends_at` FROM `players` as `p` JOIN `accounts` as `a` ON `a`.`id` = `p`.`account_id` WHERE `p`.`id` = {:d} AND `p`.`deletion` = 0",
	    player->getGUID()));
	if (!result) {
		return false;
	}

	player->setName(result->getString("name"));
	Group* group = g_game.groups.getGroup(result->getNumber<uint16_t>("group_id"));
	if (!group) {
		LOG_ERROR(fmt::format("[Error - IOLoginData::preloadPlayer] {} has Group ID {} which doesn't exist.", player->name, result->getNumber<uint16_t>("group_id")));
		return false;
	}
	player->setGroup(group);
	player->accountNumber = result->getNumber<uint32_t>("account_id");
	player->accountType = static_cast<AccountType_t>(result->getNumber<uint16_t>("type"));
	player->premiumEndsAt = result->getNumber<time_t>("premium_ends_at");
	return true;
}

bool IOLoginData::loadPlayerById(Player* player, uint32_t id)
{
	Database& db = Database::getInstance();
	return loadPlayer(
	    player,
	    db.storeQuery(fmt::format(
	        "SELECT `id`, `name`, `account_id`, `group_id`, `sex`, `vocation`, `experience`, `level`, `reset`, `maglevel`, `health`, `healthmax`, `blessings`, `mana`, `manamax`, `manaspent`, `soul`, `lookbody`, `lookfeet`, `lookhead`, `looklegs`, `looktype`, `lookaddons`, `lookmount`, `currentmount`, `randomizemount`, `posx`, `posy`, `posz`, `cap`, `lastlogin`, `lastlogout`, `lastip`, `conditions`, `skulltime`, `skull`, `town_id`, `balance`, `stamina`, `skill_fist`, `skill_fist_tries`, `skill_club`, `skill_club_tries`, `skill_sword`, `skill_sword_tries`, `skill_axe`, `skill_axe_tries`, `skill_dist`, `skill_dist_tries`, `skill_shielding`, `skill_shielding_tries`, `skill_fishing`, `skill_fishing_tries`, `direction`, `offlinetraining_time`, `offlinetraining_skill` FROM `players` WHERE `id` = {:d}",
	        id)));
}

bool IOLoginData::loadPlayerByName(Player* player, std::string_view name)
{
	Database& db = Database::getInstance();
	return loadPlayer(
	    player,
	    db.storeQuery(fmt::format(
	        "SELECT `id`, `name`, `account_id`, `group_id`, `sex`, `vocation`, `experience`, `level`, `reset`, `maglevel`, `health`, `healthmax`, `blessings`, `mana`, `manamax`, `manaspent`, `soul`, `lookbody`, `lookfeet`, `lookhead`, `looklegs`, `looktype`, `lookaddons`, `lookmount`, `currentmount`, `randomizemount`, `posx`, `posy`, `posz`, `cap`, `lastlogin`, `lastlogout`, `lastip`, `conditions`, `skulltime`, `skull`, `town_id`, `balance`, `stamina`, `skill_fist`, `skill_fist_tries`, `skill_club`, `skill_club_tries`, `skill_sword`, `skill_sword_tries`, `skill_axe`, `skill_axe_tries`, `skill_dist`, `skill_dist_tries`, `skill_shielding`, `skill_shielding_tries`, `skill_fishing`, `skill_fishing_tries`, `direction`, `offlinetraining_time`, `offlinetraining_skill` FROM `players` WHERE `name` = {:s}",
	        db.escapeString(name))));
}

GuildWarVector IOLoginData::getWarList(uint32_t guildId)
{
	DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
	    "SELECT `guild1`, `guild2` FROM `guild_wars` WHERE (`guild1` = {:d} OR `guild2` = {:d}) AND `status` = 1",
	    guildId, guildId));
	if (!result) {
		return {};
	}

	GuildWarVector guildWarVector;
	do {
		uint32_t guild1 = result->getNumber<uint32_t>("guild1");
		if (guildId != guild1) {
			guildWarVector.push_back(guild1);
		} else {
			guildWarVector.push_back(result->getNumber<uint32_t>("guild2"));
		}
	} while (result->next());
	return guildWarVector;
}

bool IOLoginData::loadPlayer(Player* player, DBResult_ptr result)
{
	if (!result) {
		return false;
	}

	Database& db = Database::getInstance();

	uint32_t accno = result->getNumber<uint32_t>("account_id");
	Account acc = loadAccount(accno);

	player->setGUID(result->getNumber<uint32_t>("id"));
	player->name = result->getString("name");
	player->accountNumber = accno;

	player->accountType = acc.accountType;

	player->premiumEndsAt = acc.premiumEndsAt;

	Group* group = g_game.groups.getGroup(result->getNumber<uint16_t>("group_id"));
	if (!group) {
		LOG_ERROR(fmt::format("[Error - IOLoginData::loadPlayer] {} has Group ID {} which doesn't exist", player->name, result->getNumber<uint16_t>("group_id")));
		return false;
	}
	player->setGroup(group);

	player->bankBalance = result->getNumber<uint64_t>("balance");

	player->setSex(static_cast<PlayerSex_t>(result->getNumber<uint16_t>("sex")));
	player->level = std::max<uint32_t>(1, result->getNumber<uint32_t>("level"));
	player->reset = std::max<uint32_t>(0, result->getNumber<uint32_t>("reset"));

	uint64_t experience = result->getNumber<uint64_t>("experience");

	uint64_t currExpCount = Player::getExpForLevel(player->level);
	uint64_t nextExpCount = Player::getExpForLevel(player->level + 1);
	if (experience < currExpCount || experience > nextExpCount) {
		experience = currExpCount;
	}

	player->experience = experience;

	if (currExpCount < nextExpCount) {
		uint32_t expBasisPoints = Player::getBasisPointLevel(player->experience - currExpCount, nextExpCount - currExpCount);
		uint32_t expPercent = expBasisPoints / 100;
		if (expPercent > 100) {
			expPercent = 100;
		}
		player->levelPercent = static_cast<uint8_t>(expPercent);
	} else {
		player->levelPercent = 0;
	}

	player->soul = result->getNumber<uint16_t>("soul");
	player->capacity = result->getNumber<uint32_t>("cap") * 100;
	player->blessings = result->getNumber<uint16_t>("blessings");

	auto conditions = result->getString("conditions");
	PropStream propStream;
	propStream.init(conditions.data(), conditions.size());

	Condition_ptr condition = Condition::createCondition(propStream);
	while (condition) {
		if (condition->unserialize(propStream)) {
			player->storedConditionList.push_front(condition.release());
		}
		condition = Condition::createCondition(propStream);
	}

	if (!player->setVocation(result->getNumber<uint16_t>("vocation"))) {
		LOG_ERROR(fmt::format("[Error - IOLoginData::loadPlayer] {} has Vocation ID {} which doesn't exist", player->name, result->getNumber<uint16_t>("vocation")));
		return false;
	}

	player->mana = result->getNumber<uint32_t>("mana");
	player->manaMax = result->getNumber<uint32_t>("manamax");
	player->magLevel = result->getNumber<uint32_t>("maglevel");

	uint64_t nextManaCount = player->vocation->getReqMana(player->magLevel + 1);
	uint64_t manaSpent = result->getNumber<uint64_t>("manaspent");
	if (manaSpent > nextManaCount) {
		manaSpent = 0;
	}

	player->manaSpent = manaSpent;
	
	uint32_t magBasisPoints = Player::getBasisPointLevel(player->manaSpent, nextManaCount);
	uint32_t magPercent = magBasisPoints / 100;
	if (magPercent > 100) {
		magPercent = 100;
	}
	player->magLevelPercent = static_cast<uint8_t>(magPercent);

	player->health = result->getNumber<int32_t>("health");
	player->healthMax = result->getNumber<int32_t>("healthmax");

	player->defaultOutfit.lookType = result->getNumber<uint16_t>("looktype");
	player->defaultOutfit.lookHead = result->getNumber<uint16_t>("lookhead");
	player->defaultOutfit.lookBody = result->getNumber<uint16_t>("lookbody");
	player->defaultOutfit.lookLegs = result->getNumber<uint16_t>("looklegs");
	player->defaultOutfit.lookFeet = result->getNumber<uint16_t>("lookfeet");
	player->defaultOutfit.lookAddons = result->getNumber<uint16_t>("lookaddons");
	player->defaultOutfit.lookMount = result->getNumber<uint16_t>("lookmount");
	player->currentOutfit = player->defaultOutfit;
	player->currentMount = result->getNumber<uint16_t>("currentmount");
	player->direction = static_cast<Direction>(result->getNumber<uint16_t>("direction"));
	player->randomizeMount = result->getNumber<uint8_t>("randomizemount") != 0;

	if (g_game.getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
		const time_t skullSeconds = result->getNumber<time_t>("skulltime") - time(nullptr);
		if (skullSeconds > 0) {
			// ensure that we round up the number of ticks
			player->skullTicks = (skullSeconds + 2);

			uint16_t skull = result->getNumber<uint16_t>("skull");
			if (skull == SKULL_RED) {
				player->skull = SKULL_RED;
			} else if (skull == SKULL_BLACK) {
				player->skull = SKULL_BLACK;
			}
		}
	}

	player->loginPosition.x = result->getNumber<uint16_t>("posx");
	player->loginPosition.y = result->getNumber<uint16_t>("posy");
	player->loginPosition.z = result->getNumber<uint16_t>("posz");

	player->lastLoginSaved = result->getNumber<time_t>("lastlogin");
	player->lastLogout = result->getNumber<time_t>("lastlogout");

	player->offlineTrainingTime = result->getNumber<int32_t>("offlinetraining_time") * 1000;
	player->offlineTrainingSkill = result->getNumber<int32_t>("offlinetraining_skill");

	Town* town = g_game.map.towns.getTown(result->getNumber<uint32_t>("town_id"));
	if (!town) {
		LOG_ERROR(fmt::format("[Error - IOLoginData::loadPlayer] {} has Town ID {} which doesn't exist", player->name, result->getNumber<uint32_t>("town_id")));
		return false;
	}

	player->town = town;

	const Position& loginPos = player->loginPosition;
	if (loginPos.x == 0 && loginPos.y == 0 && loginPos.z == 0) {
		player->loginPosition = player->getTemplePosition();
	}

	player->staminaMinutes = result->getNumber<uint16_t>("stamina");

	static const std::string skillNames[] = {"skill_fist", "skill_club",      "skill_sword",  "skill_axe",
	                                         "skill_dist", "skill_shielding", "skill_fishing"};
	static const std::string skillNameTries[] = {"skill_fist_tries",   "skill_club_tries", "skill_sword_tries",
	                                             "skill_axe_tries",    "skill_dist_tries", "skill_shielding_tries",
	                                             "skill_fishing_tries"};
	static constexpr size_t size = sizeof(skillNames) / sizeof(std::string);
	for (uint8_t i = 0; i < size; ++i) {
		uint16_t skillLevel = result->getNumber<uint16_t>(skillNames[i]);
		uint64_t skillTries = result->getNumber<uint64_t>(skillNameTries[i]);
		uint64_t nextSkillTries = player->vocation->getReqSkillTries(static_cast<skills_t>(i), skillLevel + 1);
		if (skillTries > nextSkillTries) {
			skillTries = 0;
		}

		player->skills[i].level = skillLevel;
		player->skills[i].tries = skillTries;
		
		uint32_t basisPoints = Player::getBasisPointLevel(skillTries, nextSkillTries);
		uint32_t percent = basisPoints / 100;
		if (percent > 100) {
			percent = 100;
		}
		player->skills[i].percent = static_cast<uint16_t>(percent);
	}

	if ((result = db.storeQuery(
	         fmt::format("SELECT `guild_id`, `rank_id`, `nick` FROM `guild_membership` WHERE `player_id` = {:d}",
	                     player->getGUID())))) {
		uint32_t guildId = result->getNumber<uint32_t>("guild_id");
		uint32_t playerRankId = result->getNumber<uint32_t>("rank_id");
		player->guildNick = result->getString("nick");

		auto guild = g_game.getGuild(guildId);
		if (!guild) {
			guild = IOGuild::loadGuild(guildId);
			if (guild) {
				g_game.addGuild(guild);
			} else {
				LOG_WARN(fmt::format("[Warning - IOLoginData::loadPlayer] {} has Guild ID {} which doesn't exist", player->name, guildId));
			}
		}

		if (guild) {
			player->guild = guild;
			auto rank = guild->getRankById(playerRankId);
			if (!rank) {
				if ((result = db.storeQuery(fmt::format(
				         "SELECT `id`, `name`, `level` FROM `guild_ranks` WHERE `id` = {:d}", playerRankId)))) {
					guild->addRank(result->getNumber<uint32_t>("id"), result->getString("name"),
					               result->getNumber<uint16_t>("level"));
				}

				rank = guild->getRankById(playerRankId);
				if (!rank) {
					player->guild = nullptr;
				}
			}

			player->guildRank = rank;

			player->guildWarVector = getWarList(guildId);

			if ((result = db.storeQuery(fmt::format(
			         "SELECT COUNT(*) AS `members` FROM `guild_membership` WHERE `guild_id` = {:d}", guildId)))) {
				guild->setMemberCount(result->getNumber<uint32_t>("members"));
			}
		}
	}

	if ((result = db.storeQuery(fmt::format("SELECT `player_id`, `name` FROM `player_spells` WHERE `player_id` = {:d}",
	                                        player->getGUID())))) {
		do {
			player->learnedInstantSpellList.emplace_front(result->getString("name"));
		} while (result->next());
	}

	// load inventory items
	ItemMap itemMap;

	if ((result = db.storeQuery(fmt::format(
	         "SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_items` WHERE `player_id` = {:d} ORDER BY `sid` DESC",
	         player->getGUID())))) {
		loadItems(itemMap, result);

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;
			int32_t pid = pair.second;
			if (pid >= CONST_SLOT_FIRST && pid <= CONST_SLOT_LAST) {
				player->internalAddThing(pid, item);
				player->postAddNotification(item, nullptr, pid);
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);
				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
				}
			}
		}
	}

	// load depot locker items
	itemMap.clear();

	if ((result = db.storeQuery(fmt::format(
	         "SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_depotlockeritems` WHERE `player_id` = {:d} ORDER BY `sid` DESC",
	         player->getGUID())))) {
		loadItems(itemMap, result);

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;

			if (item->getID() == ITEM_INBOX) {
				delete item;
				continue;
			}

			int32_t pid = pair.second;
			if (pid >= 0 && pid < 100) {
				DepotLocker* depotLocker = player->getDepotLocker(pid);
				if (depotLocker) {
					depotLocker->internalAddThing(item);
				}
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);
				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
				}
			}
		}
	}

	// load depot items
	itemMap.clear();

	if ((result = db.storeQuery(fmt::format(
	         "SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_depotitems` WHERE `player_id` = {:d} ORDER BY `sid` DESC",
	         player->getGUID())))) {
		loadItems(itemMap, result);

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;

			int32_t pid = pair.second;
			if (pid >= 0 && pid < 100) {
				DepotChest* depotChest = player->getDepotChest(pid, true);
				if (depotChest) {
					depotChest->internalAddThing(item);
				}
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);
				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
				}
			}
		}
	}

	// load inbox items
	itemMap.clear();

	if ((result = db.storeQuery(fmt::format(
	         "SELECT `pid`, `sid`, `itemtype`, `count`, `attributes` FROM `player_inboxitems` WHERE `player_id` = {:d} ORDER BY `sid` DESC",
	         player->getGUID())))) {
		loadItems(itemMap, result);

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, int32_t>& pair = it->second;
			Item* item = pair.first;

			int32_t pid = pair.second;
			if (pid >= 0 && pid < 100) {
				DepotLocker* depotLocker = player->getDepotLocker(pid);
				if (depotLocker) {
					Item* inbox = nullptr;
					for (Item* depotItem : depotLocker->getItemList()) {
						if (depotItem->getID() == ITEM_INBOX) {
							inbox = depotItem;
							break;
						}
					}

					if (inbox) {
						Container* inboxContainer = inbox->getContainer();
						if (inboxContainer) {
							inboxContainer->internalAddThing(item);
						}
					}
				}
			} else {
				ItemMap::const_iterator it2 = itemMap.find(pid);
				if (it2 == itemMap.end()) {
					continue;
				}

				Container* container = it2->second.first->getContainer();
				if (container) {
					container->internalAddThing(item);
				}
			}
		}
	}

	// Load reward items
	itemMap.clear();

	if ((result = db.storeQuery(fmt::format("SELECT `sid`, `pid`, `itemtype`, `count`, `attributes` FROM `player_rewarditems` WHERE `player_id` = {:d} ORDER BY `sid` DESC", player->getGUID())))) {
		loadItems(itemMap, result);

		// Map to store containers (bags) for each unique DATE attribute
		std::unordered_map<int64_t, Container*> dateContainers;

		// Get the current time and calculate the time 7 days ago
		time_t now = std::time(nullptr);
		time_t seven_days_ago = now - (7 * 24 * 60 * 60); // 7 days in seconds
		

		for (ItemMap::const_reverse_iterator it = itemMap.rbegin(), end = itemMap.rend(); it != end; ++it) {
			const std::pair<Item*, uint32_t>& pair = it->second;
			Item* item = pair.first;

			int64_t rewardDate = item->getIntAttr(ITEM_ATTRIBUTE_DATE);

			// Skip items older than 7 days
			if (rewardDate < static_cast<int64_t>(seven_days_ago)) {
				continue;
			}

			// Create or get existing container for the given DATE attribute
			Container* container = nullptr;
			auto containerIt = dateContainers.find(rewardDate);
			if (containerIt != dateContainers.end()) {
				container = containerIt->second;
			}
			else {
				container = new Container(ITEM_REWARD_CONTAINER);
				container->setIntAttr(ITEM_ATTRIBUTE_DATE, rewardDate); // Set the DATE attribute on the container
				container->setIntAttr(ITEM_ATTRIBUTE_REWARDID, item->getIntAttr(ITEM_ATTRIBUTE_REWARDID));
				dateContainers[rewardDate] = container;
			}

			container->internalAddThing(item);
		}

		for (auto& pair : dateContainers) {
			player->getRewardChest().internalAddThing(pair.second);
		}
	}

	// load auto loot config
	if (!loadAutoLootConfig(player)) {
		return false;
	}

	// load storage map
	if ((result = db.storeQuery(
	         fmt::format("SELECT `key`, `value` FROM `player_storage` WHERE `player_id` = {:d}", player->getGUID())))) {
		do {
			player->setStorageValue(result->getNumber<uint32_t>("key"), result->getNumber<int64_t>("value"), true);
		} while (result->next());
	}

	// load vip list
	if ((result = db.storeQuery(fmt::format("SELECT `player_id` FROM `account_viplist` WHERE `account_id` = {:d}",
	                                        player->getAccount())))) {
		do {
			player->addVIPInternal(result->getNumber<uint32_t>("player_id"));
		} while (result->next());
	}

	// load outfits & addons
	if ((result = db.storeQuery(fmt::format(
	         "SELECT `outfit_id`, `addons` FROM `player_outfits` WHERE `player_id` = {:d}", player->getGUID())))) {
		do {
			player->addOutfit(result->getNumber<uint16_t>("outfit_id"),
			                  static_cast<uint8_t>(result->getNumber<uint16_t>("addons")));
		} while (result->next());
	}

	// load mounts
	if ((result = db.storeQuery(
	         fmt::format("SELECT `mount_id` FROM `player_mounts` WHERE `player_id` = {:d}", player->getGUID())))) {
		do {
			player->tameMount(result->getNumber<uint16_t>("mount_id"));
		} while (result->next());
	}

	player->updateBaseSpeed();
	player->updateInventoryWeight();
	player->updateItemsLight(true);
	return true;
}

bool IOLoginData::saveItems(const Player* player, const ItemBlockList& itemList, DBInsert& query_insert,
                            PropWriteStream& propWriteStream)
{
	using ContainerBlock = std::pair<Container*, int32_t>;
	std::vector<ContainerBlock> containers;
	containers.reserve(32);

	int32_t runningId = 100;

	Database& db = Database::getInstance();
	for (const auto& it : itemList) {
		int32_t pid = it.first;
		Item* item = it.second;
		++runningId;

		propWriteStream.clear();
		item->serializeAttr(propWriteStream);

		if (!query_insert.addRow(fmt::format("{:d}, {:d}, {:d}, {:d}, {:d}, {:s}", player->getGUID(), pid, runningId,
		                                     item->getID(), item->getSubType(),
		                                     db.escapeString(propWriteStream.getStream())))) {
			return false;
		}

		if (Container* container = item->getContainer()) {
			containers.emplace_back(container, runningId);
		}
	}

	for (size_t i = 0; i < containers.size(); i++) {
		const ContainerBlock& cb = containers[i];
		Container* container = cb.first;
		int32_t parentId = cb.second;

		for (Item* item : container->getItemList()) {
			++runningId;

			Container* subContainer = item->getContainer();
			if (subContainer) {
				containers.emplace_back(subContainer, runningId);
			}

			propWriteStream.clear();
			item->serializeAttr(propWriteStream);

			if (!query_insert.addRow(fmt::format("{:d}, {:d}, {:d}, {:d}, {:d}, {:s}", player->getGUID(), parentId,
			                                     runningId, item->getID(), item->getSubType(),
			                                     db.escapeString(propWriteStream.getStream())))) {
				return false;
			}
		}
	}

	return query_insert.execute();
}

bool IOLoginData::addRewardItems(uint32_t playerId, const ItemBlockList& itemList, DBInsert& query_insert, PropWriteStream& propWriteStream)
{
    using ContainerBlock = std::pair<Container*, int32_t>;
    std::list<ContainerBlock> queue;
    Database& db = Database::getInstance();
    DBResult_ptr result = db.storeQuery(fmt::format("SELECT MAX(pid) as max_pid FROM `player_rewarditems` WHERE `player_id` = {:d}", playerId));
    int32_t runningId = 1;
    int32_t pidCounter = 1;
    if (result) {
        int32_t maxPid = result->getNumber<int32_t>("max_pid");
        if (maxPid > 0) {
            pidCounter = maxPid + 1; 
        }
    }
    int32_t parentPid = pidCounter;
    for (const auto& it : itemList) {
        Item* item = it.second;
        propWriteStream.clear();
        item->serializeAttr(propWriteStream);

        if (!query_insert.addRow(fmt::format("{:d}, {:d}, {:d}, {:d}, {:d}, {:s}", 
            playerId, parentPid, runningId, item->getID(), item->getSubType(), db.escapeString(propWriteStream.getStream())))) {
            return false;
        }

        if (Container* container = item->getContainer()) {
            queue.emplace_back(container, runningId);
        }
        ++runningId; // Always increment SID upwards
    }
    while (!queue.empty()) {
        const ContainerBlock& cb = queue.front();
        Container* container = cb.first;
        int32_t parentId = cb.second;
        queue.pop_front();
        for (Item* item : container->getItemList()) {
            propWriteStream.clear();
            item->serializeAttr(propWriteStream);

            if (!query_insert.addRow(fmt::format("{:d}, {:d}, {:d}, {:d}, {:d}, {:s}", 
                playerId, parentId, runningId, item->getID(), item->getSubType(), db.escapeString(propWriteStream.getStream())))) {
                return false;
            }
            Container* subContainer = item->getContainer();
            if (subContainer) {
                queue.emplace_back(subContainer, runningId);
            }
            ++runningId; // Always increment SID upwards
        }
    }
    return query_insert.execute();
}

bool IOLoginData::savePlayer(Player* player)
{
	AutoStat stat("savePlayer", "full");
	if(player->isSpectator) {
		return false;
	}
	
	if (player->isDead()) {
		player->changeHealth(1);
	}

	Database& db = Database::getInstance();

	DBResult_ptr result =
	    db.storeQuery(fmt::format("SELECT `save` FROM `players` WHERE `id` = {:d}", player->getGUID()));
	if (!result) {
		return false;
	}

	if (result->getNumber<uint16_t>("save") == 0) {
		return db.executeQuery(fmt::format("UPDATE `players` SET `lastlogin` = {:d}, `lastip` = {:d} WHERE `id` = {:d}",
		                                   player->lastLoginSaved, player->lastIP, player->getGUID()));
	}

	// serialize conditions
	PropWriteStream propWriteStream;
	for (Condition* condition : player->conditions) {
		if (condition->isPersistent() || condition->isConstant()) {
			condition->serialize(propWriteStream);
			propWriteStream.write<uint8_t>(CONDITIONATTR_END);
		}
	}

	// First, an UPDATE query to write the player itself
	std::ostringstream query;
	query << "UPDATE `players` SET ";
	query << "`level` = " << player->level << ',';
	query << "`reset` = " << player->reset << ',';
	query << "`group_id` = " << player->group->id << ',';
	query << "`vocation` = " << player->getVocationId() << ',';
	query << "`health` = " << player->health << ',';
	query << "`healthmax` = " << player->healthMax << ',';
	query << "`experience` = " << player->experience << ',';
	query << "`lookbody` = " << static_cast<uint32_t>(player->defaultOutfit.lookBody) << ',';
	query << "`lookfeet` = " << static_cast<uint32_t>(player->defaultOutfit.lookFeet) << ',';
	query << "`lookhead` = " << static_cast<uint32_t>(player->defaultOutfit.lookHead) << ',';
	query << "`looklegs` = " << static_cast<uint32_t>(player->defaultOutfit.lookLegs) << ',';
	query << "`looktype` = " << player->defaultOutfit.lookType << ',';
	query << "`lookaddons` = " << static_cast<uint32_t>(player->defaultOutfit.lookAddons) << ',';
	query << "`lookmount` = " << player->defaultOutfit.lookMount << ',';
	query << "`currentmount` = " << static_cast<uint16_t>(player->currentMount) << ',';
	query << "`randomizemount` = " << player->randomizeMount << ",";
	query << "`maglevel` = " << player->magLevel << ',';
	query << "`mana` = " << player->mana << ',';
	query << "`manamax` = " << player->manaMax << ',';
	query << "`manaspent` = " << player->manaSpent << ',';
	query << "`soul` = " << static_cast<uint16_t>(player->soul) << ',';
	query << "`town_id` = " << player->town->getID() << ',';

	const Position& loginPosition = player->getLoginPosition();
	query << "`posx` = " << loginPosition.getX() << ',';
	query << "`posy` = " << loginPosition.getY() << ',';
	query << "`posz` = " << loginPosition.getZ() << ',';

	query << "`cap` = " << (player->capacity / 100) << ',';
	query << "`sex` = " << static_cast<uint16_t>(player->sex) << ',';

	if (player->lastLoginSaved != 0) {
		query << "`lastlogin` = " << player->lastLoginSaved << ',';
	}

	if (player->lastIP != 0) {
		query << "`lastip` = " << player->lastIP << ",";
	}

	query << "`conditions` = " << db.escapeString(propWriteStream.getStream()) << ',';

	if (g_game.getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
		int64_t skullTime = 0;

		if (player->skullTicks > 0) {
			skullTime = time(nullptr) + player->skullTicks;
		}
		query << "`skulltime` = " << skullTime << ',';

		Skulls_t skull = SKULL_NONE;
		if (player->skull == SKULL_RED) {
			skull = SKULL_RED;
		} else if (player->skull == SKULL_BLACK) {
			skull = SKULL_BLACK;
		}
		query << "`skull` = " << static_cast<int64_t>(skull) << ',';
	}

	query << "`lastlogout` = " << player->getLastLogout() << ',';
	query << "`balance` = " << player->bankBalance << ',';
	query << "`offlinetraining_time` = " << player->getOfflineTrainingTime() / 1000 << ',';
	query << "`offlinetraining_skill` = " << player->getOfflineTrainingSkill() << ',';
	query << "`stamina` = " << player->getStaminaMinutes() << ',';

	query << "`skill_fist` = " << player->skills[SKILL_FIST].level << ',';
	query << "`skill_fist_tries` = " << player->skills[SKILL_FIST].tries << ',';
	query << "`skill_club` = " << player->skills[SKILL_CLUB].level << ',';
	query << "`skill_club_tries` = " << player->skills[SKILL_CLUB].tries << ',';
	query << "`skill_sword` = " << player->skills[SKILL_SWORD].level << ',';
	query << "`skill_sword_tries` = " << player->skills[SKILL_SWORD].tries << ',';
	query << "`skill_axe` = " << player->skills[SKILL_AXE].level << ',';
	query << "`skill_axe_tries` = " << player->skills[SKILL_AXE].tries << ',';
	query << "`skill_dist` = " << player->skills[SKILL_DISTANCE].level << ',';
	query << "`skill_dist_tries` = " << player->skills[SKILL_DISTANCE].tries << ',';
	query << "`skill_shielding` = " << player->skills[SKILL_SHIELD].level << ',';
	query << "`skill_shielding_tries` = " << player->skills[SKILL_SHIELD].tries << ',';
	query << "`skill_fishing` = " << player->skills[SKILL_FISHING].level << ',';
	query << "`skill_fishing_tries` = " << player->skills[SKILL_FISHING].tries << ',';
	query << "`reset` = " << player->getResetCount() << ',';
	query << "`direction` = " << static_cast<uint16_t>(player->getDirection()) << ',';

	if (!player->isOffline()) {
		query << "`onlinetime` = `onlinetime` + " << (time(nullptr) - player->lastLoginSaved) << ',';
	}
	query << "`blessings` = " << player->blessings.to_ulong();
	query << " WHERE `id` = " << player->getGUID();

	DBTransaction transaction;
	if (!transaction.begin()) {
		return false;
	}

	if (!db.executeQuery(query.str())) {
		return false;
	}

	// learned spells
	{
		AutoStat statSpells("savePlayer", "spells");
		if (!db.executeQuery(fmt::format("DELETE FROM `player_spells` WHERE `player_id` = {:d}", player->getGUID()))) {
			return false;
		}

		DBInsert spellsQuery("INSERT INTO `player_spells` (`player_id`, `name` ) VALUES ");
		for (std::string_view spellName : player->learnedInstantSpellList) {
			if (!spellsQuery.addRow(fmt::format("{:d}, {:s}", player->getGUID(), db.escapeString(spellName)))) {
				return false;
			}
		}

		if (!spellsQuery.execute()) {
			return false;
		}
	}

	// item saving
	{
		AutoStat statItems("savePlayer", "items_inventory");
		if (!db.executeQuery(fmt::format("DELETE FROM `player_items` WHERE `player_id` = {:d}", player->getGUID()))) {
			return false;
		}

		DBInsert itemsQuery(
		    "INSERT INTO `player_items` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");

		ItemBlockList itemList;
		for (int32_t slotId = CONST_SLOT_FIRST; slotId <= CONST_SLOT_LAST; ++slotId) {
			Item* item = player->inventory[slotId];
			if (item) {
				itemList.emplace_back(slotId, item);
			}
		}

		if (!saveItems(player, itemList, itemsQuery, propWriteStream)) {
			return false;
		}
	}

	// save depot locker items
	bool needsSave = false;

	for (const auto& it : player->depotLockerMap) {
		if (it.second->needsSave()) {
			needsSave = true;
			break;
		}
	}

	if (needsSave) {
		AutoStat statDepot("savePlayer", "items_depot");
		if (!db.executeQuery(
		        fmt::format("DELETE FROM `player_depotlockeritems` WHERE `player_id` = {:d}", player->getGUID()))) {
			return false;
		}

		DBInsert lockerQuery(
		    "INSERT INTO `player_depotlockeritems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
		ItemBlockList itemList;

		for (const auto& it : player->depotLockerMap) {
			for (Item* item : it.second->getItemList()) {
				if (item->getID() != ITEM_DEPOT && item->getID() != ITEM_INBOX) {
					itemList.emplace_back(it.first, item);
				}
			}
		}

		if (!saveItems(player, itemList, lockerQuery, propWriteStream)) {
			return false;
		}

		// save depot items
		if (needsSave) {
			if (!db.executeQuery(
			        fmt::format("DELETE FROM `player_depotitems` WHERE `player_id` = {:d}", player->getGUID()))) {
				return false;
			}

			DBInsert depotQuery(
			    "INSERT INTO `player_depotitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
			itemList.clear();

			for (const auto& it : player->depotChests) {
				for (Item* item : it.second->getItemList()) {
					itemList.emplace_back(it.first, item);
				}
			}

			if (!saveItems(player, itemList, depotQuery, propWriteStream)) {
				return false;
			}
		}
	}

	// save inbox items
	{
		AutoStat statInbox("savePlayer", "items_inbox");
		if (!db.executeQuery(fmt::format("DELETE FROM `player_inboxitems` WHERE `player_id` = {:d}", player->getGUID()))) {
			return false;
		}

	DBInsert inboxQuery(
		    "INSERT INTO `player_inboxitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
		ItemBlockList itemList;

		int inboxItemsCount = 0;
		for (const auto& it : player->depotLockerMap) {
			for (Item* item : it.second->getItemList()) {
				if (item->getID() == ITEM_INBOX) {
					if (Container* container = item->getContainer()) {
						for (Item* subItem : container->getItemList()) {
							if (++inboxItemsCount > 100) {
								continue;
							}
							itemList.emplace_back(it.first, subItem);
						}
					}
				}
			}
		}

		if (!saveItems(player, itemList, inboxQuery, propWriteStream)) {
			return false;
		}
	}

	// save reward items
	{
		AutoStat statReward("savePlayer", "items_reward");
		if (!db.executeQuery(fmt::format("DELETE FROM `player_rewarditems` WHERE `player_id` = {:d}", player->getGUID()))) {
			return false;
		}

		DBInsert rewardQuery("INSERT INTO `player_rewarditems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
		ItemBlockList itemList;

		int32_t pidCounter = 1;

		for (Item* item : player->getRewardChest().getItemList()) {
			if (Container* container = item->getContainer()) {
				int32_t currentPid = pidCounter++;
				for (Item* subItem : container->getItemList()) {
					itemList.emplace_back(currentPid, subItem);
				}
			}
			else {
				itemList.emplace_back(0, item);
			}
		}

		if (!saveItems(player, itemList, rewardQuery, propWriteStream)) {
			return false;
		}
	}

	// save auto loot
	if (!saveAutoLootConfig(player)) {
		return false;
	}

	{
		AutoStat statStorage("savePlayer", "storage");
		if (!db.executeQuery(fmt::format("DELETE FROM `player_storage` WHERE `player_id` = {:d}", player->getGUID()))) {
			return false;
		}

		DBInsert storageQuery("INSERT INTO `player_storage` (`player_id`, `key`, `value`) VALUES ");

		for (const auto& [key, value] : player->getStorageMap()) {
			if (!storageQuery.addRow(fmt::format("{:d}, {:d}, {:d}", player->getGUID(), key, value))) {
				return false;
			}
		}

		if (!storageQuery.execute()) {
			return false;
		}
	}

	// save outfits & addons
	{
		AutoStat statOutfits("savePlayer", "outfits");
		if (!db.executeQuery(fmt::format("DELETE FROM `player_outfits` WHERE `player_id` = {:d}", player->getGUID()))) {
			return false;
		}

		DBInsert outfitQuery("INSERT INTO `player_outfits` (`player_id`, `outfit_id`, `addons`) VALUES ");

		for (const auto& [lookType, addon] : player->outfits) {
			if (!outfitQuery.addRow(fmt::format("{:d}, {:d}, {:d}", player->getGUID(), lookType, addon))) {
				return false;
			}
		}

		if (!outfitQuery.execute()) {
			return false;
		}
	}

	// save mounts
	{
		AutoStat statMounts("savePlayer", "mounts");
		if (!db.executeQuery(fmt::format("DELETE FROM `player_mounts` WHERE `player_id` = {:d}", player->getGUID()))) {
			return false;
		}

		DBInsert mountQuery("INSERT INTO `player_mounts` (`player_id`, `mount_id`) VALUES ");

		for (const auto& it : player->mounts) {
			if (!mountQuery.addRow(fmt::format("{:d}, {:d}", player->getGUID(), it))) {
				return false;
			}
		}

		if (!mountQuery.execute()) {
			return false;
		}
	}

	// End the transaction
	AutoStat statCommit("savePlayer", "commit");
	return transaction.commit();
}

bool IOLoginData::loadAutoLootConfig(Player* player)
{
	Database& db = Database::getInstance();
	std::ostringstream query;
	query << "SELECT `config` FROM `player_autolootconfig` WHERE `player_id` = " << player->getGUID();
	if (DBResult_ptr result = db.storeQuery(query.str())) {
		unsigned long size;
		std::string_view config = result->getStream("config", size);
		PropStream propStream;
		propStream.init(config.data(), size);

		uint16_t itemListSize;
		if (!propStream.read<uint16_t>(itemListSize)) {
			return false;
		}

		uint16_t itemId;
		uint16_t backpackId;
		uint8_t enabled;
		for (int i = 0; i < itemListSize; i++) {
			if (!propStream.read<uint16_t>(itemId)) {
				return false;
			}

			if (!propStream.read<uint16_t>(backpackId)) {
				return false;
			}

			if (!propStream.read<uint8_t>(enabled)) {
				return false;
			}

			player->autolootConfig.itemList.insert(std::make_pair(itemId, std::make_pair(backpackId, enabled != 0)));
		}

		std::pair<std::string_view, bool> res = propStream.readString();
		if (!res.second) {
			return false;
		}
		std::string text(res.first);

		player->autolootConfig.text = text;

		uint8_t lootAnything;
		if (!propStream.read<uint8_t>(lootAnything)) {
			return false;
		}

		player->autolootConfig.lootAnything = lootAnything != 0;
	}

	return true;
}

bool IOLoginData::saveAutoLootConfig(Player* player)
{
	Database& db = Database::getInstance();
	std::ostringstream query;
	query.str(std::string());
	query << "DELETE FROM `player_autolootconfig` WHERE `player_id` = " << player->getGUID();

	if (!db.executeQuery(query.str())) {
		return false;
	}

	query.str(std::string());
	query << "INSERT INTO `player_autolootconfig` (`player_id`, `config`) VALUES (";

	query << player->getGUID() << ',';

	PropWriteStream propWriteStream;
	propWriteStream.write<uint16_t>(player->autolootConfig.itemList.size());
	for (const auto& it : player->autolootConfig.itemList) {
		propWriteStream.write<uint16_t>(it.first);
		propWriteStream.write<uint16_t>(it.second.first);
		propWriteStream.write<uint8_t>(it.second.second);
	}
	propWriteStream.writeString(player->autolootConfig.text);
	propWriteStream.write<uint8_t>(player->autolootConfig.lootAnything);

	std::string_view config = propWriteStream.getStream();
	query << db.escapeBlob(config.data(), config.size()) << ')';

	if (!db.executeQuery(query.str())) {
		return false;
	}

	return true;
}

std::string_view IOLoginData::getNameByGuid(uint32_t guid)
{
	DBResult_ptr result =
	    Database::getInstance().storeQuery(fmt::format("SELECT `name` FROM `players` WHERE `id` = {:d}", guid));
	if (!result) {
		return {};
	}
	return result->getString("name");
}

uint32_t IOLoginData::getGuidByName(std::string_view name)
{
	Database& db = Database::getInstance();

	DBResult_ptr result =
	    db.storeQuery(fmt::format("SELECT `id` FROM `players` WHERE `name` = {:s}", db.escapeString(name)));
	if (!result) {
		return 0;
	}
	return result->getNumber<uint32_t>("id");
}

bool IOLoginData::getGuidByNameEx(uint32_t& guid, bool& specialVip, std::string& name)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format(
	    "SELECT `name`, `id`, `group_id`, `account_id` FROM `players` WHERE `name` = {:s}", db.escapeString(name)));
	if (!result) {
		return false;
	}

	name = result->getString("name");
	guid = result->getNumber<uint32_t>("id");
	Group* group = g_game.groups.getGroup(result->getNumber<uint16_t>("group_id"));

	uint64_t flags;
	if (group) {
		flags = group->flags;
	} else {
		flags = 0;
	}

	specialVip = (flags & PlayerFlag_SpecialVIP) != 0;
	return true;
}

bool IOLoginData::formatPlayerName(std::string& name)
{
	Database& db = Database::getInstance();

	DBResult_ptr result =
	    db.storeQuery(fmt::format("SELECT `name` FROM `players` WHERE `name` = {:s}", db.escapeString(name)));
	if (!result) {
		return false;
	}

	name = result->getString("name");
	return true;
}

void IOLoginData::loadItems(ItemMap& itemMap, DBResult_ptr result)
{
	do {
		uint32_t sid = result->getNumber<uint32_t>("sid");
		uint32_t pid = result->getNumber<uint32_t>("pid");
		uint16_t type = result->getNumber<uint16_t>("itemtype");
		uint16_t count = result->getNumber<uint16_t>("count");

		auto attr = result->getString("attributes");
		PropStream propStream;
		propStream.init(attr.data(), attr.size());

		Item* item = Item::CreateItem(type, count);
		if (item) {
			if (!item->unserializeAttr(propStream)) {
				LOG_WARN("WARNING: Serialize error in IOLoginData::loadItems");
			}

			std::pair<Item*, uint32_t> pair(item, pid);
			itemMap[sid] = pair;
		}
	} while (result->next());
}

void IOLoginData::increaseBankBalance(uint32_t guid, uint64_t bankBalance)
{
	Database::getInstance().executeQuery(
	    fmt::format("UPDATE `players` SET `balance` = `balance` + {:d} WHERE `id` = {:d}", bankBalance, guid));
}

bool IOLoginData::hasBiddedOnHouse(uint32_t guid_guild)
{
	Database& db = Database::getInstance();
	return db.storeQuery(fmt::format("SELECT `id` FROM `houses` WHERE `highest_bidder` = {:d} LIMIT 1", guid_guild)).get() !=
	       nullptr;
}

std::forward_list<VIPEntry> IOLoginData::getVIPEntries(uint32_t accountId)
{
	std::forward_list<VIPEntry> entries;

	DBResult_ptr result = Database::getInstance().storeQuery(fmt::format(
	    "SELECT `player_id`, (SELECT `name` FROM `players` WHERE `id` = `player_id`) AS `name` FROM `account_viplist` WHERE `account_id` = {:d}",
	    accountId));
	if (result) {
		do {
			entries.emplace_front(result->getNumber<uint32_t>("player_id"), result->getString("name"));
		} while (result->next());
	}
	return entries;
}

void IOLoginData::addVIPEntry(uint32_t accountId, uint32_t guid)
{
	Database& db = Database::getInstance();
	db.executeQuery(
	    fmt::format("INSERT INTO `account_viplist` (`account_id`, `player_id`) VALUES ({:d}, {:d})", accountId, guid));
}

void IOLoginData::removeVIPEntry(uint32_t accountId, uint32_t guid)
{
	Database::getInstance().executeQuery(
	    fmt::format("DELETE FROM `account_viplist` WHERE `account_id` = {:d} AND `player_id` = {:d}", accountId, guid));
}

void IOLoginData::updatePremiumTime(uint32_t accountId, time_t endTime)
{
	Database::getInstance().executeQuery(
	    fmt::format("UPDATE `accounts` SET `premium_ends_at` = {:d} WHERE `id` = {:d}", endTime, accountId));
}

uint64_t IOLoginData::getTibiaCoins(uint32_t accountId)
{
	DBResult_ptr result = Database::getInstance().storeQuery(
	    fmt::format("SELECT `tibia_coins` FROM `accounts` WHERE `id` = {:d}", accountId));
	if (!result) {
		return 0;
	}
	return result->getNumber<uint64_t>("tibia_coins");
}

void IOLoginData::updateTibiaCoins(uint32_t accountId, uint64_t tibiaCoins)
{
	Database::getInstance().executeQuery(
	    fmt::format("UPDATE `accounts` SET `tibia_coins` = {:d} WHERE `id` = {:d}", tibiaCoins, accountId));
}

bool IOLoginData::createAccount(const std::string& name, const std::string& password, uint32_t& accountId)
{
	Database& db = Database::getInstance();
    DBResult_ptr result = db.storeQuery(fmt::format("SELECT `id` FROM `accounts` WHERE LOWER(`name`) = LOWER({:s})", db.escapeString(name)));
    if (result) {
        return false;
    }
    std::string hashedPassword = transformToSHA1Hex(password);
    if (!db.executeQuery(fmt::format("INSERT INTO `accounts` (`name`, `password`, `type`, `premium_ends_at`, `email`, `creation`) VALUES ({:s}, {:s}, 1, 0, '', {:d})",
        db.escapeString(name), db.escapeString(hashedPassword), static_cast<uint32_t>(time(nullptr))))) {
        return false;
    }
    result = db.storeQuery(fmt::format("SELECT `id` FROM `accounts` WHERE LOWER(`name`) = LOWER({:s})", db.escapeString(name)));
    if (!result) {
        return false;
    }
    accountId = result->getNumber<uint32_t>("id");
    return true;
}

bool IOLoginData::setPassword(uint32_t accountId, const std::string& newPassword)
{
	Database& db = Database::getInstance();
	std::string hashedPassword = transformToSHA1Hex(newPassword);
	return db.executeQuery(fmt::format("UPDATE `accounts` SET `password` = {:s} WHERE `id` = {:d}", db.escapeString(hashedPassword), accountId));
}

bool IOLoginData::setRecoveryKey(uint32_t accountId, const std::string& recoveryKey)
{
	Database& db = Database::getInstance();
	std::string hashedKey = transformToSHA1Hex(recoveryKey);
	return db.executeQuery(fmt::format("UPDATE `accounts` SET `secret` = {:s} WHERE `id` = {:d}", db.escapeString(hashedKey), accountId));
}

bool IOLoginData::createPlayer(uint32_t accountId, const std::string& name, uint16_t vocationId, PlayerSex_t sex)
{
	Database& db = Database::getInstance();

	if (playerNameExists(name)) {
		return false;
	}

	uint16_t level = static_cast<uint16_t>(ConfigManager::getInteger(ConfigManager::NEW_PLAYER_LEVEL));
	uint16_t magicLevel = static_cast<uint16_t>(ConfigManager::getInteger(ConfigManager::NEW_PLAYER_MAGIC_LEVEL));
	uint32_t townId = static_cast<uint32_t>(ConfigManager::getInteger(ConfigManager::NEW_PLAYER_TOWN_ID));
	int32_t posX = static_cast<int32_t>(ConfigManager::getInteger(ConfigManager::NEW_PLAYER_SPAWN_POS_X));
	int32_t posY = static_cast<int32_t>(ConfigManager::getInteger(ConfigManager::NEW_PLAYER_SPAWN_POS_Y));
	int32_t posZ = static_cast<int32_t>(ConfigManager::getInteger(ConfigManager::NEW_PLAYER_SPAWN_POS_Z));

	uint64_t experience = Player::getExpForLevel(level);
	
	uint32_t health = static_cast<uint32_t>(ConfigManager::getInteger(ConfigManager::NEW_PLAYER_HEALTH));
	uint32_t mana = static_cast<uint32_t>(ConfigManager::getInteger(ConfigManager::NEW_PLAYER_MANA));
	uint32_t cap = static_cast<uint32_t>(ConfigManager::getInteger(ConfigManager::NEW_PLAYER_CAP));

	uint16_t lookType = (sex == PLAYERSEX_FEMALE) ? 136 : 128;

	std::ostringstream query;
	query << "INSERT INTO `players` (`name`, `group_id`, `account_id`, `level`, `vocation`, `health`, `healthmax`, `experience`, "
		  << "`lookbody`, `lookfeet`, `lookhead`, `looklegs`, `looktype`, `lookaddons`, `direction`, `maglevel`, `mana`, `manamax`, "
		  << "`manaspent`, `soul`, `town_id`, `posx`, `posy`, `posz`, `cap`, `sex`, `lastlogin`, `lastip`, `save`, `skull`, "
		  << "`skulltime`, `lastlogout`, `blessings`, `onlinetime`, `deletion`, `balance`, `offlinetraining_time`, `offlinetraining_skill`, "
		  << "`stamina`, `skill_fist`, `skill_fist_tries`, `skill_club`, `skill_club_tries`, `skill_sword`, `skill_sword_tries`, "
		  << "`skill_axe`, `skill_axe_tries`, `skill_dist`, `skill_dist_tries`, `skill_shielding`, `skill_shielding_tries`, "
		  << "`skill_fishing`, `skill_fishing_tries`) VALUES (";

	query << db.escapeString(name) << ", 1, " << accountId << ", " << level << ", " << vocationId << ", "
		  << health << ", " << health << ", " << experience << ", 0, 0, 0, 0, " << lookType << ", 0, 2, " << magicLevel << ", "
		  << mana << ", " << mana << ", 0, 0, " << townId << ", " << posX << ", " << posY << ", " << posZ
		  << ", " << cap << ", " << static_cast<uint16_t>(sex) << ", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 43200, -1, 2520, "
		  << "10, 0, 10, 0, 10, 0, 10, 0, 10, 0, 10, 0, 10, 0)";

	return db.executeQuery(query.str());
}

bool IOLoginData::deletePlayer(uint32_t playerId)
{
	if (playerId == 1) {
		LOG_WARN("[Security] Attempted to delete Account Manager (guid=1) - BLOCKED");
		return false;
	}

	Database& db = Database::getInstance();
	return db.executeQuery(fmt::format("UPDATE `players` SET `deletion` = {:d} WHERE `id` = {:d}", static_cast<uint64_t>(time(nullptr) + 86400), playerId));
}

std::vector<std::string> IOLoginData::getPlayersByAccountId(uint32_t accountId)
{
	std::vector<std::string> players;
	Database& db = Database::getInstance();
	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `name` FROM `players` WHERE `account_id` = {:d} AND `deletion` = 0 AND `name` != 'Account Manager' ORDER BY `name` ASC", accountId));
	if (result) {
		do {
			players.push_back(std::string{result->getString("name")});
		} while (result->next());
	}
	return players;
}

bool IOLoginData::playerNameExists(const std::string& name)
{
	Database& db = Database::getInstance();
	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `id` FROM `players` WHERE `name` = {:s}", db.escapeString(name)));
	return result != nullptr;
}

bool IOLoginData::accountNameExists(const std::string& name)
{
	Database& db = Database::getInstance();
	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `id` FROM `accounts` WHERE LOWER(`name`) = LOWER({:s})", db.escapeString(name)));
	return result != nullptr;
}
