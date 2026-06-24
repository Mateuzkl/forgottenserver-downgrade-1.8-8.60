// Copyright 2026 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_WORLD_H
#define FS_WORLD_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum WorldType_t : uint8_t
{
	WORLD_TYPE_NO_PVP = 1,
	WORLD_TYPE_PVP = 2,
	WORLD_TYPE_PVP_ENFORCED = 3,
};

struct WorldInfo
{
	uint16_t id = 1;
	std::string name;
	WorldType_t type = WORLD_TYPE_PVP;
	std::string motd;
	std::string locationName;
	std::string ip;
	uint16_t gamePort = 7172;
	uint16_t statusPort = 7171;
	uint32_t creation = 0;
};

class GameWorlds
{
public:
	bool load();
	bool ensureDefaultWorld();

	const WorldInfo* getWorld(uint16_t id) const;
	const WorldInfo* getCurrentWorld() const;
	const std::vector<WorldInfo>& getWorlds() const { return worlds; }

	void setCurrentWorld(uint16_t id) { currentWorldId = id; }
	uint16_t getCurrentWorldId() const { return currentWorldId; }

	static WorldType_t parseWorldType(std::string_view type);
	static const char* getWorldTypeName(WorldType_t type);

private:
	std::vector<WorldInfo> worlds;
	uint16_t currentWorldId = 1;
};

#endif // FS_WORLD_H
