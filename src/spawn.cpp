// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "spawn.h"

#include "configmanager.h"
#include "events.h"
#include "game.h"
#include "logger.h"
#include "monster.h"
#include "pugicast.h"
#include "scheduler.h"

#include <fmt/format.h>

extern Monsters g_monsters;
extern Game g_game;
extern Events* g_events;

inline constexpr int32_t MINSPAWN_INTERVAL = 10 * 1000;           // 10 seconds to match RME
inline constexpr int32_t MAXSPAWN_INTERVAL = 24 * 60 * 60 * 1000; // 1 day

bool Spawns::loadFromXml(std::string_view filename)
{
	if (loaded) {
		return true;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.data());
	if (!result) {
		printXMLError("Error - Spawns::loadFromXml", filename, result);
		return false;
	}

	this->filename = filename;
	loaded = true;

	for (auto& spawnNode : doc.child("spawns").children()) {
		Position centerPos(pugi::cast<uint16_t>(spawnNode.attribute("centerx").value()),
		                   pugi::cast<uint16_t>(spawnNode.attribute("centery").value()),
		                   pugi::cast<uint16_t>(spawnNode.attribute("centerz").value()));

		int32_t radius;
		pugi::xml_attribute radiusAttribute = spawnNode.attribute("radius");
		if (radiusAttribute) {
			radius = pugi::cast<int32_t>(radiusAttribute.value());
		} else {
			radius = -1;
		}

		if (radius > 30) {
			LOG_WARN(fmt::format(
			    "[Warning - Spawns::loadFromXml] Radius size bigger than 30 at position: {}, consider lowering it.",
			    centerPos));
		}

		if (!spawnNode.first_child()) {
			LOG_WARN(fmt::format("[Warning - Spawns::loadFromXml] Empty spawn at position: {} with radius: {}.",
			                     centerPos, radius));
			continue;
		}

		spawnList.emplace_front(centerPos, radius);
		Spawn& spawn = spawnList.front();

		for (auto& childNode : spawnNode.children()) {
			if (caseInsensitiveEqual(childNode.name(), "monsters")) {
				Position pos(centerPos.x + pugi::cast<uint16_t>(childNode.attribute("x").value()),
				             centerPos.y + pugi::cast<uint16_t>(childNode.attribute("y").value()), centerPos.z);

				int32_t interval = pugi::cast<int32_t>(childNode.attribute("spawntime").value()) * 1000;
				if (interval < MINSPAWN_INTERVAL) {
					LOG_WARN(
					    fmt::format("[Warning - Spawns::loadFromXml] {} spawntime can not be less than {} seconds.",
					                pos, MINSPAWN_INTERVAL / 1000));
					continue;
				} else if (interval > MAXSPAWN_INTERVAL) {
					LOG_WARN(
					    fmt::format("[Warning - Spawns::loadFromXml] {} spawntime can not be more than {} seconds.",
					                pos, MAXSPAWN_INTERVAL / 1000));
					continue;
				}

				size_t monstersCount = std::distance(childNode.children().begin(), childNode.children().end());
				if (monstersCount == 0) {
					LOG_WARN(fmt::format("[Warning - Spawns::loadFromXml] {} empty monsters set.", pos));
					continue;
				}

				uint16_t totalChance = 0;
				spawnBlock_t sb;
				sb.pos = pos;
				sb.direction = DIRECTION_NORTH;
				sb.interval = interval;
				sb.lastSpawn = 0;
				sb.effectInitialInterval = 0;

				for (auto& monsterNode : childNode.children()) {
					pugi::xml_attribute nameAttribute = monsterNode.attribute("name");
					if (!nameAttribute) {
						continue;
					}

					MonsterType* mType = g_monsters.getMonsterType(nameAttribute.as_string());
					if (!mType) {
						LOG_WARN(fmt::format("[Warning - Spawn::loadFromXml] {} can not find {}", pos,
						                     nameAttribute.as_string()));
						continue;
					}

					uint16_t chance = 100 / monstersCount;
					pugi::xml_attribute chanceAttribute = monsterNode.attribute("chance");
					if (chanceAttribute) {
						chance = pugi::cast<uint16_t>(chanceAttribute.value());
					}

					if (chance + totalChance > 100) {
						chance = 100 - totalChance;
						totalChance = 100;
						LOG_WARN(fmt::format(
						    "[Warning - Spawns::loadFromXml] {} {} total chance for set can not be higher than 100.",
						    mType->name, pos));
					} else {
						totalChance += chance;
					}

					sb.mTypes.push_back({mType, chance});
				}

				if (sb.mTypes.empty()) {
					LOG_WARN(fmt::format("[Warning - Spawns::loadFromXml] {} empty monsters set.", pos));
					continue;
				}

				sb.mTypes.shrink_to_fit();
				if (sb.mTypes.size() > 1) {
					std::sort(sb.mTypes.begin(), sb.mTypes.end(),
					          [](std::pair<MonsterType*, uint16_t> a, std::pair<MonsterType*, uint16_t> b) {
						          return a.second > b.second;
					          });
				}

				spawn.addBlock(sb);
			} else if (caseInsensitiveEqual(childNode.name(), "monster")) {
				pugi::xml_attribute nameAttribute = childNode.attribute("name");
				if (!nameAttribute) {
					continue;
				}

				Direction dir;

				pugi::xml_attribute directionAttribute = childNode.attribute("direction");
				if (directionAttribute) {
					dir = static_cast<Direction>(pugi::cast<uint16_t>(directionAttribute.value()));
				} else {
					dir = DIRECTION_NORTH;
				}

				Position pos(centerPos.x + pugi::cast<uint16_t>(childNode.attribute("x").value()),
				             centerPos.y + pugi::cast<uint16_t>(childNode.attribute("y").value()), centerPos.z);
				int32_t interval = pugi::cast<int32_t>(childNode.attribute("spawntime").value()) * 1000;
				if (interval >= MINSPAWN_INTERVAL && interval <= MAXSPAWN_INTERVAL) {
					spawn.addMonster(nameAttribute.as_string(), pos, dir, static_cast<uint32_t>(interval));
				} else {
					if (interval < MINSPAWN_INTERVAL) {
						LOG_WARN(fmt::format(
						    "[Warning - Spawns::loadFromXml] {} {} spawntime can not be less than {} seconds.",
						    nameAttribute.as_string(), pos, MINSPAWN_INTERVAL / 1000));
					} else {
						LOG_WARN(fmt::format(
						    "[Warning - Spawns::loadFromXml] {} {} spawntime can not be more than {} seconds.",
						    nameAttribute.as_string(), pos, MAXSPAWN_INTERVAL / 1000));
					}
				}
			} else if (caseInsensitiveEqual(childNode.name(), "npc")) {
				pugi::xml_attribute nameAttribute = childNode.attribute("name");
				if (!nameAttribute) {
					continue;
				}

				Npc* npc = Npc::createNpc(nameAttribute.as_string());
				if (!npc) {
					continue;
				}

				pugi::xml_attribute directionAttribute = childNode.attribute("direction");
				if (directionAttribute) {
					npc->setDirection(static_cast<Direction>(pugi::cast<uint16_t>(directionAttribute.value())));
				}

				npc->setMasterPos(
				    Position(centerPos.x + pugi::cast<uint16_t>(childNode.attribute("x").value()),
				             centerPos.y + pugi::cast<uint16_t>(childNode.attribute("y").value()), centerPos.z),
				    radius);
				npcList.push_front(npc);
			}
		}
	}
	return true;
}

void Spawns::startup()
{
	if (!loaded || isStarted()) {
		return;
	}

	for (Npc* npc : npcList) {
		if (!g_game.placeCreature(npc, npc->getMasterPos(), false, true)) {
			LOG_WARN(fmt::format("[Warning - Spawns::startup] Couldn't spawn npc \"{}\" on position: {}.",
			                     npc->getName(), npc->getMasterPos()));
			delete npc;
		}
	}
	npcList.clear();

	for (Spawn& spawn : spawnList) {
		spawn.startup();
	}

	started = true;
}

void Spawns::clear()
{
	for (Spawn& spawn : spawnList) {
		spawn.clearMonsters();
		spawn.stopEvent();
	}
	spawnList.clear();

	loaded = false;
	started = false;
	filename.clear();
}

bool Spawns::isInZone(const Position& centerPos, int32_t radius, const Position& pos)
{
	if (radius == -1) {
		return true;
	}

	return ((pos.getX() >= centerPos.getX() - radius) && (pos.getX() <= centerPos.getX() + radius) &&
	        (pos.getY() >= centerPos.getY() - radius) && (pos.getY() <= centerPos.getY() + radius));
}

void Spawn::startSpawnCheck()
{
	if (checkSpawnEvent == 0) {
		checkSpawnEvent = g_scheduler.addEvent(getInterval(), [this]() { checkSpawn(); });
	}
}

Spawn::~Spawn()
{
	for (const auto& it : spawnedMap) {
		Monster* monster = it.second;
		monster->setSpawn(nullptr);
		monster->decrementReferenceCounter();
	}
}

bool Spawn::findPlayer(const Position& pos)
{
	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, pos, false, true);
	for (Creature* spectator : spectators) {
		assert(dynamic_cast<Player*>(spectator) != nullptr);
		if (!static_cast<Player*>(spectator)->hasFlag(PlayerFlag_IgnoredByMonsters)) {
			return true;
		}
	}
	return false;
}

bool Spawn::spawnMonster(uint32_t spawnId, spawnBlock_t sb, bool startup /* = false*/)
{
	bool isBlocked = !startup && findPlayer(sb.pos);
	size_t monstersCount = sb.mTypes.size(), blockedMonsters = 0;

	const auto spawnFunc = [&](bool roll) {
		for (const auto& pair : sb.mTypes) {
			if (isBlocked && !pair.first->info.isIgnoringSpawnBlock) {
				++blockedMonsters;
				continue;
			}

			if (!roll) {
				return spawnMonster(spawnId, pair.first, sb.pos, sb.direction, startup);
			}

			if (pair.second >= normal_random(1, 100) &&
			    spawnMonster(spawnId, pair.first, sb.pos, sb.direction, startup)) {
				return true;
			}
		}

		return false;
	};

	// Try to spawn something with chance check, unless it's single spawn
	if (spawnFunc(monstersCount > 1)) {
		return true;
	}

	// Every monster spawn is blocked, bail out
	if (monstersCount == blockedMonsters) {
		return false;
	}

	// Just try to spawn something without chance check
	return spawnFunc(false);
}

bool Spawn::spawnMonster(uint32_t spawnId, MonsterType* mType, const Position& pos, Direction dir,
                         bool startup /*= false*/)
{
	auto monster_ptr = std::make_unique<Monster>(mType);
	Monster* monster = monster_ptr.get();

	if (!g_events->eventMonsterOnSpawn(monster, pos, startup, false)) {
		return false;
	}

	Position finalPos = pos;
	if (startup) {
		// No need to send out events to the surrounding since there is no one out there to listen!
		if (!g_game.internalPlaceCreature(monster_ptr.get(), pos, true)) {
			LOG_WARN(fmt::format("[Warning - Spawns::startup] Couldn't spawn monster \"{}\" on position: {}.",
			                     monster_ptr->getName(), finalPos));
			return false;
		}
	} else {
		// Try to place normally (non-forced) on the desired position first.
		// If there is a player blocking the tile, attempt to place the monster
		// on an adjacent free tile (search radius 1). If none found, fallback
		// to forced placement (compatibility).
		if (g_game.placeCreature(monster, finalPos, false, false)) {
			// placed on original position
		} else {
			bool placed = false;
			// If a player is blocking the original position, try adjacent tiles.
			if (findPlayer(pos)) {
				for (int dx = -1; dx <= 1 && !placed; ++dx) {
					for (int dy = -1; dy <= 1 && !placed; ++dy) {
						if (dx == 0 && dy == 0) {
							continue;
						}
						Position tryPos(pos.x + dx, pos.y + dy, pos.z);
						if (g_game.placeCreature(monster, tryPos, false, false)) {
							finalPos = tryPos;
							placed = true;
						}
					}
				}
			}

			// If not placed yet, attempt the old forced placement as a fallback.
			if (!placed) {
				if (!g_game.placeCreature(monster, finalPos, false, true)) {
					return false;
				}
			}
		}
	}

	monster_ptr.release();
	monster->setDirection(dir);
	monster->setSpawn(this);
	monster->setMasterPos(finalPos);
	monster->incrementReferenceCounter();

	spawnedMap.insert({spawnId, monster});
	spawnMap[spawnId].lastSpawn = OTSYS_TIME();
	return true;
}

void Spawn::startup()
{
	for (const auto& it : spawnMap) {
		uint32_t spawnId = it.first;
		const spawnBlock_t& sb = it.second;
		spawnMonster(spawnId, sb, true);
	}
}

void Spawn::checkSpawn()
{
	checkSpawnEvent = 0;
	cleanup();

	uint32_t spawnCount = 0;
	const int64_t now = OTSYS_TIME();
	const int64_t rate = std::max<int64_t>(1, ConfigManager::getInteger(ConfigManager::RATE_SPAWN));
	const uint32_t effectDuration =
	    ConfigManager::getBoolean(ConfigManager::SPAWN_START_EFFECT_ENABLED)
	        ? static_cast<uint32_t>(ConfigManager::getInteger(ConfigManager::RATE_START_EFFECT))
	        : 0;

	for (auto& [spawnId, sb] : spawnMap) {
		if (spawnedMap.contains(spawnId)) {
			continue;
		}

		const uint32_t spawnInterval = static_cast<uint32_t>(sb.interval / rate);

		if (now >= sb.lastSpawn + std::max<uint32_t>(MINSPAWN_INTERVAL, spawnInterval)) {
			// If there is a player blocking and no monster in the set ignores the block,
			// we show POFF and retry on the next cycle (no teleport effect).
			bool playerBlocking = findPlayer(sb.pos);
			bool anyIgnoresBlock =
			    std::ranges::any_of(sb.mTypes, [](const auto& pair) { return pair.first->info.isIgnoringSpawnBlock; });

			if (playerBlocking && !anyIgnoresBlock) {
				if (++spawnCount == 1) {
					scheduleSpawn(spawnId, effectDuration, true);
					break;
				}
			}

			if (++spawnCount == 1) {
				scheduleSpawn(spawnId, effectDuration);
				break;
			}
		}
	}

	if (spawnedMap.size() < spawnMap.size()) {
		checkSpawnEvent = g_scheduler.addEvent(getInterval(), [this]() { checkSpawn(); });
	}
}

void Spawn::scheduleSpawn(uint32_t spawnId, uint32_t interval, bool blocked)
{
	auto it = spawnMap.find(spawnId);
	if (interval <= 0 || it == spawnMap.end()) {
		if (it == spawnMap.end()) {
			return;
		}

		spawnBlock_t& sb = it->second;
		if (blocked) {
			bool playerBlocking = findPlayer(sb.pos);
			if (playerBlocking) {
				bool anyIgnoresBlock = false;
				for (const auto& pair : sb.mTypes) {
					if (pair.first->info.isIgnoringSpawnBlock) {
						anyIgnoresBlock = true;
						break;
					}
				}

				if (!anyIgnoresBlock) {
					g_game.addMagicEffect(sb.pos, CONST_ME_POFF);
					sb.lastSpawn = OTSYS_TIME();
					sb.effectInitialInterval = 0;
					return;
				}
			}
		}

		spawnMonster(spawnId, sb);
		sb.effectInitialInterval = 0;
		return;
	}

	spawnBlock_t& sb = it->second;
	g_game.addMagicEffect(sb.pos, CONST_ME_TELEPORT);

	uint32_t initialInterval = sb.effectInitialInterval;
	if (initialInterval == 0) {
		sb.effectInitialInterval = interval;
		initialInterval = interval;
	}

	// Base delay between effects from config
	const uint32_t normalRate = static_cast<uint32_t>(ConfigManager::getInteger(ConfigManager::RATE_BETWEEN_EFFECT));
	// Minimum delay when accelerating (avoid zero) and enforce scheduler minimum
	const uint32_t minRate = std::max<uint32_t>(std::max<int32_t>(SCHEDULER_MINTICKS, 50), normalRate / 5);
	// Use the initial interval as the scaling range and avoid division by zero
	const uint32_t accelStart = std::max<uint32_t>(1, initialInterval);

	uint32_t nextDelay = normalRate;
	if (interval <= accelStart) {
		uint32_t rangeRate = (normalRate > minRate) ? (normalRate - minRate) : 0;
		uint32_t scaled = minRate + static_cast<uint32_t>((static_cast<uint64_t>(interval) * rangeRate) / accelStart);
		nextDelay = std::max<uint32_t>(minRate, std::min<uint32_t>(normalRate, scaled));
	}

	nextDelay = std::max<uint32_t>(nextDelay, static_cast<uint32_t>(SCHEDULER_MINTICKS));

	uint32_t remaining = (interval > nextDelay) ? (interval - nextDelay) : 0;
	g_scheduler.addEvent(nextDelay,
	                     [this, spawnId, remaining, blocked]() { scheduleSpawn(spawnId, remaining, blocked); });
}

void Spawn::cleanup()
{
	std::erase_if(spawnedMap, [](auto& it) {
		auto& [spawnId, monster] = it;
		if (monster->isRemoved()) {
			monster->decrementReferenceCounter();
			return true;
		}
		return false;
	});
}

void Spawn::clearMonsters()
{
	for (const auto& it : spawnedMap) {
		Monster* monster = it.second;
		monster->setSpawn(nullptr);
		monster->decrementReferenceCounter();
	}
	spawnedMap.clear();
}

bool Spawn::addBlock(spawnBlock_t sb)
{
	interval = std::min(interval, sb.interval);
	spawnMap[spawnMap.size() + 1] = sb;

	return true;
}

bool Spawn::addMonster(const std::string& name, const Position& pos, Direction dir, uint32_t interval)
{
	MonsterType* mType = g_monsters.getMonsterType(name);
	if (!mType) {
		LOG_WARN(fmt::format("[Warning - Spawn::addMonster] Can not find {}", name));
		return false;
	}

	spawnBlock_t sb;
	sb.mTypes = {{mType, 100}};
	sb.pos = pos;
	sb.direction = dir;
	sb.interval = interval;
	sb.lastSpawn = 0;
	sb.effectInitialInterval = 0;

	return addBlock(std::move(sb));
}

void Spawn::removeMonster(Monster* monster)
{
	for (auto it = spawnedMap.begin(), end = spawnedMap.end(); it != end; ++it) {
		if (it->second == monster) {
			monster->decrementReferenceCounter();
			spawnedMap.erase(it);
			break;
		}
	}
}

void Spawn::stopEvent()
{
	if (checkSpawnEvent != 0) {
		g_scheduler.stopEvent(checkSpawnEvent);
		checkSpawnEvent = 0;
	}
}
