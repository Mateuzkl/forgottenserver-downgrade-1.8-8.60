// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_BOTMANAGER_H
#define FS_BOTMANAGER_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Player;

class BotManager
{
public:
	struct Result
	{
		bool success = false;
		std::string message;
		std::shared_ptr<Player> player;
	};

	struct RegistrationResult
	{
		bool success = false;
		bool created = false;
		uint32_t guid = 0;
		std::string name;
		std::string message;
	};

	static BotManager& getInstance();

	BotManager(const BotManager&) = delete;
	BotManager& operator=(const BotManager&) = delete;

	Result spawnByGuid(uint32_t guid, bool broadcast = false, bool requireMarked = true);
	Result spawnByName(std::string_view name, bool broadcast = false, bool requireMarked = true);
	RegistrationResult registerByName(std::string_view name, bool autoSpawn = false, bool createIfMissing = true,
	                                  uint16_t vocationId = 4, uint16_t sex = 1);

	bool despawnByGuid(uint32_t guid, bool save, std::string* message = nullptr);
	bool despawnByName(std::string_view name, bool save, std::string* message = nullptr);
	size_t despawnAll(bool save);

	bool setBroadcast(uint32_t guid, bool enabled, std::string* message = nullptr);
	bool setBroadcast(std::string_view name, bool enabled, std::string* message = nullptr);

	bool isManagedBot(uint32_t guid) const;
	std::vector<std::shared_ptr<Player>> getActiveBots() const;
	void forget(uint32_t guid);

	bool ensureTables();
	bool isMarkedBot(uint32_t guid, bool* enabled = nullptr);
	std::optional<uint32_t> getGuidByName(std::string_view name) const;

private:
	BotManager() = default;

	void cleanupRemoved();
	void touchRuntime(uint32_t guid, bool spawned);

	std::unordered_map<uint32_t, std::shared_ptr<Player>> activeBots;
};

#endif
