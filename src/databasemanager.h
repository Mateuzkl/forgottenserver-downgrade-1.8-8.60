// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_DATABASEMANAGER_H
#define FS_DATABASEMANAGER_H

#include "database.h"

class DatabaseManager
{
public:
	static bool tableExists(std::string_view tableName);

	static int32_t getDatabaseVersion();
	static bool isDatabaseSetup();
	static bool isMarketSchemaReady();

	static bool optimizeTables();
	static void updateDatabase();

	static bool getDatabaseConfig(std::string_view config, int32_t& value);
	static void registerDatabaseConfig(std::string_view config, int32_t value);
};
#endif
