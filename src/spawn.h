// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_SPAWN_H
#define FS_SPAWN_H

#include "position.h"
#include "tile.h"

#include <utility>
#include <vector>

class Monster;
class MonsterType;
class Npc;

struct spawnBlock_t
{
	Position pos;
	std::vector<std::pair<MonsterType*, uint16_t>> mTypes;
	int64_t lastSpawn;
	uint32_t interval;
	uint32_t effectInitialInterval; // original respawn interval (ms) used to accelerate effects as spawn approaches
	Direction direction;
};

class Spawn
{
public:
	Spawn(Position pos, int32_t radius) : centerPos(std::move(pos)), radius(radius) {}
	~Spawn();

	// non-copyable
	Spawn(const Spawn&) = delete;
	Spawn& operator=(const Spawn&) = delete;

	bool addBlock(const spawnBlock_t& sb);
	bool addMonster(const std::string& name, const Position& pos, Direction dir, uint32_t interval);
	void removeMonster(Monster* monster);

	uint32_t getInterval() const { return interval; }
	void startup();

	void startSpawnCheck();
	void stopEvent();

	void cleanup();
	void clearMonsters();

	uint32_t getSpawnedCount() const { return spawnedMap.size(); }
	size_t getMonsterCount() const { return spawnMap.size(); }

private:
	// map of the spawned creatures
	using SpawnedMap = std::map<uint32_t, Monster*>;
	SpawnedMap spawnedMap;

	// map of creatures in the spawn
	std::map<uint32_t, spawnBlock_t> spawnMap;

	Position centerPos;
	int32_t radius;

	uint32_t interval = 60000;
	uint32_t checkSpawnEvent = 0;

	static bool findPlayer(const Position& pos);
	bool spawnMonster(uint32_t spawnId, const spawnBlock_t& sb, bool startup = false);
	bool spawnMonster(uint32_t spawnId, MonsterType* mType, const Position& pos, Direction dir, bool startup = false);
	void checkSpawn();
	void scheduleSpawn(uint32_t spawnId, uint32_t interval, bool blocked = false);
};

class Spawns
{
public:
	static bool isInZone(const Position& centerPos, int32_t radius, const Position& pos);

	bool loadFromXml(std::string_view filename);
	void startup();
	void clear();

	bool isStarted() const { return started; }

	size_t getNpcCount() const { return std::distance(npcList.begin(), npcList.end()); }
	size_t getMonsterCount() const
	{
		size_t count = 0;
		for (const auto& spawn : spawnList) {
			count += spawn.getMonsterCount();
		}
		return count;
	}

private:
	std::forward_list<Npc*> npcList;
	std::forward_list<Spawn> spawnList;
	std::string filename;
	bool loaded = false;
	bool started = false;
};

#endif
