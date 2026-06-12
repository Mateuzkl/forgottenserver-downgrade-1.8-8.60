// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "spawn.h"

#include "configmanager.h"
#include "events.h"
#include "game.h"
#include "monster.h"
#include "pugicast.h"
#include "scheduler.h"
#include "scriptmanager.h"
#include "logger.h"
#include <fmt/format.h>

#include <limits>
#include <optional>

extern Monsters g_monsters;
extern Game g_game;

inline constexpr int32_t MINSPAWN_INTERVAL = 10 * 1000;           // 10 seconds to match RME
inline constexpr int32_t MAXSPAWN_INTERVAL = 24 * 60 * 60 * 1000; // 1 day

namespace {
std::optional<Position> getSpawnPosition(const Position& centerPos, const pugi::xml_node& childNode,
                                         int16_t z, std::string_view context)
{
	const int32_t xOffset = pugi::cast<int32_t>(childNode.attribute("x").value());
	const int32_t yOffset = pugi::cast<int32_t>(childNode.attribute("y").value());
	const int32_t x = static_cast<int32_t>(centerPos.x) + xOffset;
	const int32_t y = static_cast<int32_t>(centerPos.y) + yOffset;

	if (x < 0 || x > std::numeric_limits<uint16_t>::max() ||
	    y < 0 || y > std::numeric_limits<uint16_t>::max() ||
	    z < 0 || z > std::numeric_limits<uint8_t>::max()) {
		LOG_WARN(fmt::format("[Warning - {}] Spawn position ({}, {}, {}) is outside the supported coordinate range.",
		                     context, x, y, z));
		return std::nullopt;
	}

	return Position(static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint8_t>(z));
}
} // namespace

Spawns::~Spawns()
{
	npcList.clear();

	spawnList.clear();
}

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
			LOG_WARN(fmt::format("[Warning - Spawns::loadFromXml] Radius size bigger than 30 at position: {}, consider lowering it.", centerPos));
		}

		if (!spawnNode.first_child()) {
			LOG_WARN(fmt::format("[Warning - Spawns::loadFromXml] Empty spawn at position: {} with radius: {}.", centerPos, radius));
			continue;
		}

		spawnList.push_front(std::make_shared<Spawn>(centerPos, radius));
		Spawn& spawn = *spawnList.front();

		for (auto& childNode : spawnNode.children()) {
			if (caseInsensitiveEqual(childNode.name(), "monsters")) {
				auto spawnPos = getSpawnPosition(centerPos, childNode, centerPos.z, "Spawns::loadFromXml");
				if (!spawnPos) {
					continue;
				}
				const Position& pos = *spawnPos;

				int32_t interval = pugi::cast<int32_t>(childNode.attribute("spawntime").value()) * 1000;
				if (interval < MINSPAWN_INTERVAL) {
					LOG_WARN(fmt::format("[Warning - Spawns::loadFromXml] {} spawntime can not be less than {} seconds.", pos, MINSPAWN_INTERVAL / 1000));
					continue;
				} else if (interval > MAXSPAWN_INTERVAL) {
					LOG_WARN(fmt::format("[Warning - Spawns::loadFromXml] {} spawntime can not be more than {} seconds.", pos, MAXSPAWN_INTERVAL / 1000));
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

					auto mType = g_monsters.getSharedMonsterType(nameAttribute.as_string());
					if (!mType) {
						LOG_WARN(fmt::format("[Warning - Spawn::loadFromXml] {} can not find {}", pos, nameAttribute.as_string()));
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
						LOG_WARN(fmt::format("[Warning - Spawns::loadFromXml] {} {} total chance for set can not be higher than 100.", mType->name, pos));
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
					std::ranges::sort(sb.mTypes,
					          [](const std::pair<std::shared_ptr<MonsterType>, uint16_t>& a, const std::pair<std::shared_ptr<MonsterType>, uint16_t>& b) {
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

				auto spawnPos = getSpawnPosition(centerPos, childNode, centerPos.z, "Spawns::loadFromXml");
				if (!spawnPos) {
					continue;
				}
				const Position& pos = *spawnPos;
				int32_t interval = pugi::cast<int32_t>(childNode.attribute("spawntime").value()) * 1000;
				if (interval >= MINSPAWN_INTERVAL && interval <= MAXSPAWN_INTERVAL) {
					spawn.addMonster(nameAttribute.as_string(), pos, dir, static_cast<uint32_t>(interval));
				} else {
					if (interval < MINSPAWN_INTERVAL) {
						LOG_WARN(fmt::format("[Warning - Spawns::loadFromXml] {} {} spawntime can not be less than {} seconds.", nameAttribute.as_string(), pos, MINSPAWN_INTERVAL / 1000));
					} else {
						LOG_WARN(fmt::format("[Warning - Spawns::loadFromXml] {} {} spawntime can not be more than {} seconds.", nameAttribute.as_string(), pos, MAXSPAWN_INTERVAL / 1000));
					}
				}
			} else if (caseInsensitiveEqual(childNode.name(), "npc")) {
				pugi::xml_attribute nameAttribute = childNode.attribute("name");
				if (!nameAttribute) {
					continue;
				}

				auto npc = Npc::createNpc(nameAttribute.as_string());
				if (!npc) {
					continue;
				}

				pugi::xml_attribute directionAttribute = childNode.attribute("direction");
				if (directionAttribute) {
					npc->setDirection(static_cast<Direction>(pugi::cast<uint16_t>(directionAttribute.value())));
				}

				pugi::xml_attribute instanceIdAttribute = childNode.attribute("instanceId");
				if (instanceIdAttribute) {
					npc->setInstanceID(pugi::cast<uint32_t>(instanceIdAttribute.value()));
				}

				auto spawnPos = getSpawnPosition(centerPos, childNode, centerPos.z, "Spawns::loadFromXml");
				if (!spawnPos) {
					continue;
				}

				npc->setMasterPos(*spawnPos, radius);
				npcList.push_front(std::move(npc));
			}
		}
	}
	return true;
}

bool Spawns::loadFromMonsterNpcXml(std::string_view filename)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.data());
	if (!result) {
		printXMLError("Error - Spawns::loadFromMonsterNpcXml", filename, result);
		return false;
	}

	auto rootNode = doc.document_element();

	auto processSpawnNode = [this](pugi::xml_node spawnNode) {
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
			LOG_WARN(fmt::format("[Warning - Spawns::loadFromMonsterNpcXml] Radius size bigger than 30 at position: {}, consider lowering it.", centerPos));
		}

		if (!spawnNode.first_child()) {
			LOG_WARN(fmt::format("[Warning - Spawns::loadFromMonsterNpcXml] Empty spawn at position: {} with radius: {}.", centerPos, radius));
			return;
		}

		spawnList.push_front(std::make_shared<Spawn>(centerPos, radius));
		Spawn& spawn = *spawnList.front();

		for (auto& childNode : spawnNode.children()) {
			int16_t childZ = centerPos.z;
			pugi::xml_attribute zAttribute = childNode.attribute("z");
			if (zAttribute) {
				childZ = pugi::cast<int16_t>(zAttribute.value());
			}

			if (caseInsensitiveEqual(childNode.name(), "monster")) {
				pugi::xml_attribute nameAttribute = childNode.attribute("name");
				if (!nameAttribute) {
					continue;
				}

				Direction dir = DIRECTION_NORTH;
				pugi::xml_attribute directionAttribute = childNode.attribute("direction");
				if (directionAttribute) {
					dir = static_cast<Direction>(pugi::cast<uint16_t>(directionAttribute.value()));
				}

				auto spawnPos = getSpawnPosition(centerPos, childNode, childZ, "Spawns::loadFromMonsterNpcXml");
				if (!spawnPos) {
					continue;
				}
				const Position& pos = *spawnPos;
				int32_t interval = pugi::cast<int32_t>(childNode.attribute("spawntime").value()) * 1000;
				if (interval >= MINSPAWN_INTERVAL && interval <= MAXSPAWN_INTERVAL) {
					spawn.addMonster(nameAttribute.as_string(), pos, dir, static_cast<uint32_t>(interval));
				} else {
					if (interval < MINSPAWN_INTERVAL) {
						LOG_WARN(fmt::format("[Warning - Spawns::loadFromMonsterNpcXml] {} {} spawntime can not be less than {} seconds.", nameAttribute.as_string(), pos, MINSPAWN_INTERVAL / 1000));
					} else {
						LOG_WARN(fmt::format("[Warning - Spawns::loadFromMonsterNpcXml] {} {} spawntime can not be more than {} seconds.", nameAttribute.as_string(), pos, MAXSPAWN_INTERVAL / 1000));
					}
				}
			} else if (caseInsensitiveEqual(childNode.name(), "npc")) {
				pugi::xml_attribute nameAttribute = childNode.attribute("name");
				if (!nameAttribute) {
					continue;
				}

				auto npc = Npc::createNpc(nameAttribute.as_string());
				if (!npc) {
					continue;
				}

				pugi::xml_attribute directionAttribute = childNode.attribute("direction");
				if (directionAttribute) {
					npc->setDirection(static_cast<Direction>(pugi::cast<uint16_t>(directionAttribute.value())));
				}

				pugi::xml_attribute instanceIdAttribute = childNode.attribute("instanceId");
				if (instanceIdAttribute) {
					npc->setInstanceID(pugi::cast<uint32_t>(instanceIdAttribute.value()));
				}

				auto spawnPos = getSpawnPosition(centerPos, childNode, childZ, "Spawns::loadFromMonsterNpcXml");
				if (!spawnPos) {
					continue;
				}

				npc->setMasterPos(*spawnPos, radius);
				npcList.push_front(std::move(npc));
			}
		}
	};

	if (rootNode.attribute("centerx")) {
		processSpawnNode(rootNode);
	}

	for (auto& spawnNode : rootNode.children()) {
		if (spawnNode.attribute("centerx")) {
			processSpawnNode(spawnNode);
		}
	}

	loaded = true;
	return true;
}

void Spawns::startup()
{
	if (!loaded || isStarted()) {
		return;
	}

	for (auto& npc : npcList) {
		std::shared_ptr<Npc> npcRef(std::move(npc));
		if (!g_game.placeCreature(npcRef.get(), npcRef->getMasterPos(), false, true)) {
			LOG_WARN(fmt::format("[Warning - Spawns::startup] Couldn't spawn npc \"{}\" on position: {}.", npcRef->getName(), npcRef->getMasterPos()));
		}
	}
	npcList.clear();

	for (const auto& spawn : spawnList) {
		spawn->startup();
	}

	started = true;
}

void Spawns::clear()
{
	for (const auto& spawn : spawnList) {
		spawn->clearMonsters();
		spawn->stopEvent();
	}
	spawnList.clear();

	// unique_ptr delete automatically
	npcList.clear();

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
	// Do not schedule new events if the game is ending.
	if (g_game.getGameState() >= GAME_STATE_SHUTDOWN) {
		return;
	}

	if (checkSpawnEvent == 0) {
		checkSpawnEvent = g_scheduler.addEvent(getInterval(), [this]() { checkSpawn(); });
	}
}

Spawn::~Spawn()
{
	// Cancels pending event in scheduler before destroying it.
	if (checkSpawnEvent != 0) {
		g_scheduler.stopEvent(checkSpawnEvent);
		checkSpawnEvent = 0;
	}

	// Safety net: cancel any scheduleSpawn chain events not yet stopped.
	for (uint32_t eventId : pendingSpawnEvents) {
		g_scheduler.stopEvent(eventId);
	}
	pendingSpawnEvents.clear();

	for (const auto& it : spawnedMap) {
		Monster* monster = it.second.get();
		monster->setSpawn(nullptr);
	}
}

bool Spawn::findPlayer(const Position& pos)
{
	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, pos, false, true);
	for (const auto& spectator : spectators.players()) {
		if (!static_cast<Player*>(spectator.get())->hasFlag(PlayerFlag_IgnoredByMonsters)) {
			return true;
		}
	}
	return false;
}

bool Spawn::spawnMonster(uint32_t spawnId, const spawnBlock_t& sb, bool startup /* = false*/)
{
	bool isBlocked = !startup && findPlayer(sb.pos);
	size_t monstersCount = sb.mTypes.size(), blockedMonsters = 0;

	const auto spawnFunc = [&](bool roll) {
		for (const auto& pair : sb.mTypes) {
			if (isBlocked && pair.first->info.isBlockable) {
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

bool Spawn::spawnMonster(uint32_t spawnId, const std::shared_ptr<MonsterType>& mType, const Position& pos, Direction dir,
                         bool startup /*= false*/)
{
	auto monsterRef = std::make_shared<Monster>(mType);
	Monster* monster = monsterRef.get();

	if (!g_events->eventMonsterOnSpawn(monster, pos, startup, false)) {
    	return false;
	}

	Position finalPos = pos;
	if (startup) {
		// No need to send out events to the surrounding since there is no one out there to listen!
		if (!g_game.internalPlaceCreature(monsterRef.get(), pos, true)) {
			LOG_WARN(fmt::format("[Warning - Spawns::startup] Couldn't spawn monster \"{}\" on position: {}.", monsterRef->getName(), finalPos ));
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

	auto spawnRef = weak_from_this().lock();
	if (!spawnRef) {
		g_game.removeCreature(monster);
		return false;
	}

	auto [it, inserted] = spawnedMap.insert({spawnId, g_game.getCreatureSharedRef<Monster>(monster)});
	if (!inserted) {
		g_game.removeCreature(monster);
		return false;
	}

	monster->setDirection(dir);
	monster->setSpawn(std::move(spawnRef));
	monster->setMasterPos(finalPos);

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
	// Guard: does not process or reschedule if the game is closing.
	if (g_game.getGameState() >= GAME_STATE_SHUTDOWN) {
		return;
	}

	checkSpawnEvent = 0;

	cleanup();

	const int64_t now = OTSYS_TIME();
	const int64_t rate = std::max<int64_t>(1, ConfigManager::getInteger(ConfigManager::RATE_SPAWN));
	// OPTIMIZATION: Restored rate limiting from upstream to distribute
  	// spawn load over time instead of processing everything at once.
	uint32_t spawnCount = 0;

	for (auto& [spawnId, sb] : spawnMap) {
		if (spawnedMap.contains(spawnId)) {
			continue;
		}

		uint32_t currentRate = static_cast<uint32_t>(rate * std::max<int64_t>(1, g_game.getSpawnRate()));
		for (const auto& pair : sb.mTypes) {
			if (caseInsensitiveEqual(pair.first->name, g_game.getBoostedCreature())) {
				float spawnMult = ConfigManager::getFloat(ConfigManager::BOOSTED_SPAWN_MULTIPLIER);
				if (spawnMult > 0.0f) {
					currentRate = std::max(1u, static_cast<uint32_t>(currentRate / spawnMult));
				}
				break;
			}
		}
		const uint32_t spawnInterval = static_cast<uint32_t>(sb.interval / currentRate);
		if (now >= sb.lastSpawn + std::max<uint32_t>(MINSPAWN_INTERVAL, spawnInterval)) {
			if (ConfigManager::getBoolean(ConfigManager::SPAWN_START_EFFECT_ENABLED)) {
				if (sb.effectInitialInterval == 0) {
					uint32_t startInterval = ConfigManager::getInteger(ConfigManager::RATE_START_EFFECT);
					sb.effectInitialInterval = startInterval;
					scheduleSpawn(spawnId, startInterval, true);
					sb.lastSpawn = now;
				}
			} else {
				if (!spawnMonster(spawnId, sb)) {
					sb.lastSpawn = OTSYS_TIME();
					continue;
				}
			}

			if (++spawnCount >= static_cast<uint32_t>(rate)) {
				break;
			}
		}
	}

	if (spawnedMap.size() < spawnMap.size()) {
		checkSpawnEvent = g_scheduler.addEvent(getInterval(), [this]() { checkSpawn(); });
	}
}

void Spawn::scheduleSpawn(uint32_t spawnId, uint32_t interval, bool /*blocked*/)
{
	// Guard: if the game is shutting down, bail immediately.
	if (g_game.getGameState() >= GAME_STATE_SHUTDOWN) {
		return;
	}

	auto it = spawnMap.find(spawnId);
	if (it == spawnMap.end()) {
		return;
	}

	spawnBlock_t& sb = it->second;

	if (interval > 0) {
		g_game.addMagicEffect(sb.pos, CONST_ME_TELEPORT, 0);

		uint32_t timeBetweenEffects = ConfigManager::getInteger(ConfigManager::RATE_BETWEEN_EFFECT);
		uint32_t totalDuration = sb.effectInitialInterval;

		uint32_t nextWait;
		if (totalDuration > 0) {
			float progress = static_cast<float>(interval) / totalDuration;
			nextWait = std::max<uint32_t>(200, static_cast<uint32_t>(timeBetweenEffects * progress));
		} else {
			nextWait = timeBetweenEffects;
		}

		if (interval > nextWait) {
			auto eventIdPtr = std::make_shared<uint32_t>(0);
			*eventIdPtr = g_scheduler.addEvent(nextWait, [this, spawnId, interval, nextWait, eventIdPtr]() {
				if (auto it = std::find(pendingSpawnEvents.begin(), pendingSpawnEvents.end(), *eventIdPtr); it != pendingSpawnEvents.end()) {
					pendingSpawnEvents.erase(it);
				}
				scheduleSpawn(spawnId, interval - nextWait, true);
			});
			pendingSpawnEvents.push_back(*eventIdPtr);
		} else {
			auto eventIdPtr = std::make_shared<uint32_t>(0);
			*eventIdPtr = g_scheduler.addEvent(interval, [this, spawnId, eventIdPtr]() {
				if (auto it = std::find(pendingSpawnEvents.begin(), pendingSpawnEvents.end(), *eventIdPtr); it != pendingSpawnEvents.end()) {
					pendingSpawnEvents.erase(it);
				}
				auto mapIt = spawnMap.find(spawnId);
				if (mapIt != spawnMap.end()) {
					if (!spawnMonster(spawnId, mapIt->second)) {
						g_game.addMagicEffect(mapIt->second.pos, CONST_ME_SMALL_WHITE_ENERGYSHOCK, 0);
					}
					mapIt->second.effectInitialInterval = 0;
				}
			});
			pendingSpawnEvents.push_back(*eventIdPtr);
		}
	} else {
		spawnMonster(spawnId, sb);
		sb.effectInitialInterval = 0;
	}
}

void Spawn::cleanup()
{
	std::erase_if(spawnedMap, [this](auto& it) {
		auto& [spawnId, monsterRef] = it;
		Monster* monster = monsterRef.get();
		if (!monster || !monster->isRemoved()) {
			return false;
		}
		if (auto mapIt = spawnMap.find(spawnId); mapIt != spawnMap.end()) {
			mapIt->second.lastSpawn = monster->getRemovedTime();
		}
		return true;
	});
}

void Spawn::clearMonsters()
{
	for (const auto& it : spawnedMap) {
		Monster* monster = it.second.get();
		monster->setSpawn(nullptr);
	}
	spawnedMap.clear();
}

bool Spawn::addBlock(const spawnBlock_t& sb)
{
	interval = std::min(interval, sb.interval);
	spawnMap[spawnMap.size() + 1] = sb;

	return true;
}

bool Spawn::addMonster(const std::string& name, const Position& pos, Direction dir, uint32_t interval)
{
	auto mType = g_monsters.getSharedMonsterType(name);
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
	auto it = std::find_if(spawnedMap.begin(), spawnedMap.end(), [monster](const auto& entry) {
		return entry.second.get() == monster;
	});
	if (it != spawnedMap.end()) {
		spawnedMap.erase(it);
	}
}

void Spawn::stopEvent()
{
	if (checkSpawnEvent != 0) {
		g_scheduler.stopEvent(checkSpawnEvent);
		checkSpawnEvent = 0;
	}

	// Cancel all pending scheduleSpawn chain events.
	for (uint32_t eventId : pendingSpawnEvents) {
		g_scheduler.stopEvent(eventId);
	}
	pendingSpawnEvents.clear();
}
