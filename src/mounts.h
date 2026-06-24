// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_MOUNTS_H
#define FS_MOUNTS_H

#include "enums.h"

struct Mount
{
	Mount(uint16_t id, uint16_t clientId, std::string_view name, int32_t speed, bool premium) :
	    name{name}, speed{speed}, clientId{clientId}, id{id}, premium{premium}
	{
		std::memset(skills, 0, sizeof(skills));
		std::memset(stats,  0, sizeof(stats));
	}

	std::string name;
	int32_t speed;
	uint16_t clientId;
	uint16_t id;
	bool premium;

	std::string type;

	bool manaShield = false;
	bool invisible  = false;

	bool    regeneration = false;
	int32_t healthGain   = 0; 
	int32_t healthTicks  = 0;
	int32_t manaGain     = 0;
	int32_t manaTicks    = 0;

	int32_t attackSpeed = 0;

	double lifeLeechChance = 0.0; 
	double lifeLeechAmount = 0.0;
	double manaLeechChance = 0.0;
	double manaLeechAmount = 0.0;
	double criticalChance  = 0.0;
	double criticalDamage  = 0.0;

	int32_t skills[SKILL_LAST + 1];
	int32_t stats [STAT_LAST  + 1];
};

class Mounts
{
public:
	bool reload();
	bool loadFromXml();
	Mount* getMountByID(uint16_t id);
	Mount* getMountByName(std::string_view name);
	Mount* getMountByClientID(uint16_t clientId);

	bool addAttributes(uint32_t playerId, uint16_t mountId);
	bool removeAttributes(uint32_t playerId, uint16_t mountId);

	// Direct value storage with O(log n) lookup by ID — deterministic iteration order.
	const std::map<uint16_t, Mount>& getMounts() const { return mounts; }

private:
	std::map<uint16_t, Mount> mounts;
	std::unordered_map<uint16_t, uint16_t> clientIdToId;
	std::unordered_map<std::string, uint16_t> nameToId;
};

#endif // FS_MOUNTS_H
