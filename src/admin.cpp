// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "admin.h"

#include "configmanager.h"
#include "connection.h"
#include "iologindata.h"
#include "logger.h"
#include "pugicast.h"
#include "tools.h"
#include "town.h"

#include <fmt/format.h>

Admin::Admin() : currentConnections(0), encryptionEnabled(false) {}

Admin::~Admin() {}

bool Admin::addConnection()
{
	if (currentConnections >= ConfigManager::getInteger(ConfigManager::ADMIN_CONNECTIONS_LIMIT)) {
		return false;
	}

	currentConnections++;
	return true;
}

void Admin::removeConnection()
{
	if (currentConnections > 0) {
		currentConnections--;
	}
}

uint16_t Admin::getPolicy() const
{
	uint16_t policy = 0;
	if (ConfigManager::getBoolean(ConfigManager::ADMIN_REQUIRE_LOGIN)) {
		policy |= REQUIRE_LOGIN;
	}

	if (encryptionEnabled) {
		policy |= REQUIRE_ENCRYPTION;
	}

	return policy;
}

uint32_t Admin::getOptions() const
{
	uint32_t ret = 0;
	// if (encryptionEnabled) {
	// 	ret |= ENCRYPTION_RSA1024XTEA;
	// }
	return ret;
}

Item* Admin::createMail(const std::string& xmlData, std::string& name, uint32_t& depotId)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_string(xmlData.c_str());
	if (!result) {
		return nullptr;
	}

	pugi::xml_node root = doc.child("mail");
	if (!root) {
		return nullptr;
	}

	name = root.attribute("to").as_string();

	std::string townName = root.attribute("town").as_string();
	if (!townName.empty()) {
		Town* town = g_game.map.towns.getTown(townName);
		if (!town) {
			return nullptr;
		}
		depotId = town->getID();
	} else {
		return nullptr;
	}

	uint16_t itemId = root.attribute("id").as_uint(ITEM_PARCEL);
	Item* mailItem = Item::CreateItem(itemId);
	if (!mailItem) {
		return nullptr;
	}

	// if (mailItem->getContainer()) {
	// 	// Mail content parsing disabled
	// }

	return mailItem;
}

bool Admin::allow(uint32_t ip) const
{
	if (!ConfigManager::getBoolean(ConfigManager::ADMIN_LOCALHOST_ONLY)) {
		return true;
	}

	if (ip == 0x0100007F) { // 127.0.0.1
		return true;
	}

	if (ConfigManager::getBoolean(ConfigManager::ADMIN_LOGS)) {
		LOG_WARN("[Admin] Forbidden connection try from {}", convertIPToString(ip));
	}

	return false;
}
