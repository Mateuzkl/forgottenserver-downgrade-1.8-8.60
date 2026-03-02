// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.


#ifndef FS_IOLOGINDATA_H
#define FS_IOLOGINDATA_H

#include "account.h"
#include "database.h"
#include "player.h"

using ItemBlockList = std::list<std::pair<int32_t, Item*>>;

class IOLoginData
{
public:
	static Account loadAccount(uint32_t accno);

	static bool loginserverAuthentication(std::string_view name, std::string_view password, Account& account);
	static std::pair<uint32_t, uint32_t> gameworldAuthentication(std::string_view accountName,
	                                                             std::string_view password,
	                                                             std::string_view characterName, bool& cast);
	static uint32_t getAccountIdByPlayerName(std::string_view playerName);
	static uint32_t getAccountIdByPlayerId(uint32_t playerId);

	static AccountType_t getAccountType(uint32_t accountId);
	static void setAccountType(uint32_t accountId, AccountType_t accountType);
	static void updateOnlineStatus(uint32_t guid, bool login, bool broadcasting, const std::string& cast_password, const std::string& cast_description, uint32_t spectators);
	static void removeOnlineStatus(uint32_t guid);
	static bool preloadPlayer(Player* player);

	static bool loadPlayerById(Player* player, uint32_t id);
	static bool loadPlayerByName(Player* player, std::string_view name);
	static bool loadPlayer(Player* player, DBResult_ptr result);
	static bool savePlayer(Player* player);
	static bool addRewardItems(uint32_t playerId, const ItemBlockList& itemList, DBInsert& query_insert, PropWriteStream& propWriteStream);
	static uint32_t getGuidByName(std::string_view name);
	static bool getGuidByNameEx(uint32_t& guid, bool& specialVip, std::string& name);
	static bool saveAutoLootConfig(Player* player);
	static bool loadAutoLootConfig(Player* player);
	static std::string_view getNameByGuid(uint32_t guid);
	static bool formatPlayerName(std::string& name);
	static void increaseBankBalance(uint32_t guid, uint64_t bankBalance);
	static 	bool hasBiddedOnHouse(uint32_t guid_guild);

	static std::forward_list<VIPEntry> getVIPEntries(uint32_t accountId);
	static void addVIPEntry(uint32_t accountId, uint32_t guid);
	static void removeVIPEntry(uint32_t accountId, uint32_t guid);

	static void updatePremiumTime(uint32_t accountId, time_t endTime);

	static uint64_t getTibiaCoins(uint32_t accountId);
	static void updateTibiaCoins(uint32_t accountId, uint64_t tibiaCoins);

	static bool createAccount(const std::string& name, const std::string& password, uint32_t& accountId);
	static bool setPassword(uint32_t accountId, const std::string& newPassword);
	static bool setRecoveryKey(uint32_t accountId, const std::string& recoveryKey);
	static bool createPlayer(uint32_t accountId, const std::string& name, uint16_t vocationId, PlayerSex_t sex);
	static bool deletePlayer(uint32_t playerId);
	static std::vector<std::string> getPlayersByAccountId(uint32_t accountId);
	static bool playerNameExists(const std::string& name);
	static bool accountNameExists(const std::string& name);

	static GuildWarVector getWarList(uint32_t guildId);
	static std::vector<std::pair<std::string, std::string>> getCastList(const std::string& password);

private:
	using ItemMap = std::map<uint32_t, std::pair<Item*, uint32_t>>;

	static void loadItems(ItemMap& itemMap, DBResult_ptr result);
	static bool saveItems(const Player* player, const ItemBlockList& itemList, DBInsert& query_insert,
	                      PropWriteStream& propWriteStream);
};

#endif
