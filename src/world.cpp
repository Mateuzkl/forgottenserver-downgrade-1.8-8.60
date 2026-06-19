// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "world.h"

#include "configmanager.h"
#include "database.h"
#include "tools.h"

#include <algorithm>

WorldType_t GameWorlds::parseWorldType(std::string_view value)
{
	const auto type = asLowerCaseString(std::string(value));
	if (type == "no-pvp" || type == "nopvp" || type == "optional") {
		return WORLD_TYPE_NO_PVP;
	}
	if (type == "pvp-enforced" || type == "hardcore") {
		return WORLD_TYPE_PVP_ENFORCED;
	}
	std::cout << "[Warning] Unknown world type '" << value << "', falling back to PVP." << std::endl;
	return WORLD_TYPE_PVP;
}

const char* GameWorlds::getWorldTypeName(WorldType_t type)
{
	switch (type) {
		case WORLD_TYPE_NO_PVP:
			return "no-pvp";
		case WORLD_TYPE_PVP_ENFORCED:
			return "pvp-enforced";
		case WORLD_TYPE_PVP:
		default:
			return "pvp";
	}
}

bool GameWorlds::ensureDefaultWorld()
{
	Database& db = Database::getInstance();
	// Only world 1 is bootstrapped automatically. Any explicitly selected
	// additional world must already exist, avoiding accidental configuration
	// mistakes creating a second server with the wrong endpoint.
	constexpr uint16_t worldId = 1;

	return db.executeQuery(fmt::format(
	    "INSERT IGNORE INTO `worlds` (`id`, `name`, `type`, `motd`, `location`, `ip`, `port`, `port_status`, `creation`) "
	    "VALUES ({:d}, {:s}, {:s}, {:s}, {:s}, {:s}, {:d}, {:d}, UNIX_TIMESTAMP())",
	    worldId, db.escapeString(ConfigManager::getString(ConfigManager::WORLD_NAME)),
	    db.escapeString(GameWorlds::getWorldTypeName(
	        GameWorlds::parseWorldType(ConfigManager::getString(ConfigManager::WORLD_TYPE)))),
	    db.escapeString(ConfigManager::getString(ConfigManager::MOTD)),
	    db.escapeString(ConfigManager::getString(ConfigManager::WORLD_LOCATION)),
	    db.escapeString(ConfigManager::getString(ConfigManager::WORLD_IP)),
	    ConfigManager::getInteger(ConfigManager::WORLD_GAME_PORT),
	    ConfigManager::getInteger(ConfigManager::WORLD_STATUS_PORT)));
}

bool GameWorlds::load()
{
	worlds.clear();

	DBResult_ptr result = Database::getInstance().storeQuery(
	    "SELECT `id`, `name`, `type`, `motd`, `location`, `ip`, `port`, `port_status`, `creation` FROM `worlds` ORDER BY `id` ASC");
	if (!result) {
		return false;
	}

	do {
		WorldInfo world;
		world.id = result->getNumber<uint16_t>("id");
		world.name = result->getString("name");
		world.type = parseWorldType(result->getString("type"));
		world.motd = result->getString("motd");
		world.locationName = result->getString("location");
		world.ip = result->getString("ip");
		world.gamePort = result->getNumber<uint16_t>("port");
		world.statusPort = result->getNumber<uint16_t>("port_status");
		world.creation = result->getNumber<uint32_t>("creation");
		worlds.emplace_back(std::move(world));
	} while (result->next());

	return true;
}

const WorldInfo* GameWorlds::getWorld(uint16_t id) const
{
	const auto it = std::find_if(worlds.begin(), worlds.end(), [id](const WorldInfo& world) {
		return world.id == id;
	});
	return it == worlds.end() ? nullptr : &*it;
}

const WorldInfo* GameWorlds::getCurrentWorld() const
{
	return getWorld(currentWorldId);
}
