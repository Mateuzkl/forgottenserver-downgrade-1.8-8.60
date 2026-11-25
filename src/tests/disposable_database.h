#pragma once

#include "../configmanager.h"
#include "../database.h"
#include "test_support.h"

inline void connectDisposableDatabase()
{
	const char* name = std::getenv("TFS_TEST_DB_NAME");
	CHECK(name && std::string_view{name}.starts_with("tfs_market_test_"));
	auto setting = [](const char* key, const char* fallback) {
		const char* value = std::getenv(key);
		return value ? value : fallback;
	};
	ConfigManager::setString(ConfigManager::MYSQL_HOST, setting("TFS_TEST_DB_HOST", "127.0.0.1"));
	ConfigManager::setString(ConfigManager::MYSQL_USER, setting("TFS_TEST_DB_USER", "root"));
	ConfigManager::setString(ConfigManager::MYSQL_PASS, setting("TFS_TEST_DB_PASS", ""));
	ConfigManager::setString(ConfigManager::MYSQL_DB, name);
	ConfigManager::setString(ConfigManager::MYSQL_SOCK, setting("TFS_TEST_DB_SOCKET", ""));
	ConfigManager::setInteger(ConfigManager::SQL_PORT, std::atoi(setting("TFS_TEST_DB_PORT", "3306")));
	CHECK(Database::getInstance().connect());
}
