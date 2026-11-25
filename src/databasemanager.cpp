// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "databasemanager.h"

#include "configmanager.h"
#include "logger.h"
#include "luascript.h"

bool DatabaseManager::optimizeTables()
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format(
	    "SELECT `TABLE_NAME` FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA` = {:s} AND `DATA_FREE` > 0",
	    db.escapeString(getString(ConfigManager::MYSQL_DB))));
	if (!result) {
		return false;
	}

	do {
		auto tableName = result->getString("TABLE_NAME");
		LOG_INFO(fmt::format("> Optimizing table {:s}...", tableName));

		if (db.executeQuery(fmt::format("OPTIMIZE TABLE `{:s}`", tableName))) {
			LOG_INFO(" [success]");
		} else {
			LOG_INFO(" [failed]");
		}
	} while (result->next());
	return true;
}

bool DatabaseManager::tableExists(std::string_view tableName)
{
	Database& db = Database::getInstance();
	return db
	           .storeQuery(fmt::format(
	               "SELECT `TABLE_NAME` FROM `information_schema`.`tables` WHERE `TABLE_SCHEMA` = {:s} AND `TABLE_NAME` = {:s} LIMIT 1",
	               db.escapeString(getString(ConfigManager::MYSQL_DB)), db.escapeString(tableName)))
	           .get() != nullptr;
}

bool DatabaseManager::isDatabaseSetup()
{
	Database& db = Database::getInstance();
	return db.storeQuery(
	             fmt::format("SELECT `TABLE_NAME` FROM `information_schema`.`tables` WHERE `TABLE_SCHEMA` = {:s}",
	                         db.escapeString(getString(ConfigManager::MYSQL_DB))))
	           .get() != nullptr;
}

bool DatabaseManager::isMarketSchemaReady()
{
	Database& db = Database::getInstance();
	const auto result = db.storeQuery(
	    "SELECT COUNT(*) AS `count` FROM `information_schema`.`COLUMNS` WHERE `TABLE_SCHEMA` = DATABASE() AND "
	    "((`TABLE_NAME` = 'market_offers' AND `COLUMN_NAME` IN "
	    "('id', 'player_id', 'sale', 'itemtype', 'amount', 'created', 'anonymous', 'price', 'tier', 'attributes', 'delivery_depot_id')) OR "
	    "(`TABLE_NAME` = 'market_delivery_pending' AND `COLUMN_NAME` IN "
	    "('id', 'event_key', 'offer_id', 'player_id', 'depot_id', 'itemtype', 'amount', 'delivered_amount', 'retry_after', "
	    "'attributes', 'bank_credit', 'state', 'created_at', 'applied_at')))");
	return result && result->getNumber<uint32_t>("count") == 25;
}

int32_t DatabaseManager::getDatabaseVersion()
{
	if (!tableExists("server_config")) {
		Database& db = Database::getInstance();
		db.executeQuery(
		    "CREATE TABLE `server_config` (`config` VARCHAR(50) NOT NULL, `value` VARCHAR(256) NOT NULL DEFAULT '', UNIQUE(`config`)) ENGINE = InnoDB");
		db.executeQuery("INSERT INTO `server_config` VALUES ('db_version', 0)");
		return 0;
	}

	int32_t version = 0;
	if (getDatabaseConfig("db_version", version)) {
		return version;
	}
	return -1;
}

void DatabaseManager::updateDatabase()
{
	LuaStatePtr ownedL(luaL_newstate());
	if (!ownedL) {
		return;
	}

	lua_State* L = ownedL.get();
	luaL_openlibs(L);

	// db table
	luaL_register(L, "db", LuaScriptInterface::luaDatabaseTable);

	// result table
	luaL_register(L, "result", LuaScriptInterface::luaResultTable);

	// migrations logging
	lua_register(L, "logMigration", LuaScriptInterface::luaLogMigration);
	lua_register(L, "logInfo", LuaScriptInterface::luaLogMigration); // alias for backward compatibility

	int32_t version = getDatabaseVersion();
	do {
		const std::string migrationFile = fmt::format("data/migrations/{:d}.lua", version);
		if (!std::filesystem::exists(migrationFile)) {
			g_logger().info("No database migration file found for version {}; database is up to date.", version);
			break;
		}

		if (luaL_dofile(L, migrationFile.c_str()) != 0) {
			g_logger().error("{}", lua_tostring(L, -1));
			break;
		}

		if (!LuaScriptInterface::reserveScriptEnv()) {
			break;
		}

		lua_getglobal(L, "onUpdateDatabase");
		if (lua_pcall(L, 0, 1, 0) != 0) {
			LuaScriptInterface::resetScriptEnv();
			g_logger().error("{}", lua_tostring(L, -1));
			break;
		}

		if (!Lua::getBoolean(L, -1, false)) {
			LuaScriptInterface::resetScriptEnv();
			break;
		}

		version++;
		g_logger().info("Database has been updated to version {}", version);
		registerDatabaseConfig("db_version", version);

		LuaScriptInterface::resetScriptEnv();
	} while (true);
	// ownedL destructor calls lua_close via LuaStateDeleter
}

bool DatabaseManager::getDatabaseConfig(std::string_view config, int32_t& value)
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(
	    fmt::format("SELECT `value` FROM `server_config` WHERE `config` = {:s}", db.escapeString(config)));
	if (!result) {
		return false;
	}

	value = result->getNumber<int32_t>("value");
	return true;
}

void DatabaseManager::registerDatabaseConfig(std::string_view config, int32_t value)
{
	Database& db = Database::getInstance();

	int32_t tmp;

	if (!getDatabaseConfig(config, tmp)) {
		db.executeQuery(
		    fmt::format("INSERT INTO `server_config` VALUES ({:s}, '{:d}')", db.escapeString(config), value));
	} else {
		db.executeQuery(fmt::format("UPDATE `server_config` SET `value` = '{:d}' WHERE `config` = {:s}", value,
		                            db.escapeString(config)));
	}
}
