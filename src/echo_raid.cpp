// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "echo_raid.h"

#include "bestiary_charm.h"
#include "configmanager.h"
#include "container.h"
#include "events.h"
#include "game.h"
#include "item.h"
#include "kv/kv.h"
#include "logger.h"
#include "monster.h"
#include "monsters.h"
#include "player.h"
#include "scriptmanager.h"
#include "tile.h"
#include "tools.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <limits>
#include <sstream>

extern Game g_game;
extern Monsters g_monsters;

EchoRaidManager g_echoRaidManager;

namespace {

constexpr uint64_t ECHO_TICK_INTERVAL_MS = 250;

std::string trim(std::string_view value)
{
	const size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) {
		return {};
	}
	const size_t last = value.find_last_not_of(" \t\r\n");
	return std::string(value.substr(first, last - first + 1));
}

std::string lower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

uint32_t saturatingScaledWeight(uint32_t weight, double multiplier)
{
	if (weight == 0 || !std::isfinite(multiplier) || multiplier <= 0.0) {
		return 0;
	}
	const double scaled = std::round(static_cast<double>(weight) * multiplier);
	return static_cast<uint32_t>(std::clamp(scaled, 1.0, static_cast<double>(std::numeric_limits<uint32_t>::max())));
}

} // namespace

size_t EchoRaidManager::PositionKeyHash::operator()(const PositionKey& key) const noexcept
{
	uint64_t value = static_cast<uint64_t>(key.position.x) |
	                 (static_cast<uint64_t>(key.position.y) << 16) |
	                 (static_cast<uint64_t>(key.position.z) << 32);
	value ^= static_cast<uint64_t>(key.instanceId) * 0x9E3779B185EBCA87ULL;
	value ^= value >> 33;
	value *= 0xFF51AFD7ED558CCDULL;
	value ^= value >> 33;
	return static_cast<size_t>(value);
}

bool EchoRaidManager::configure(EchoRaidConfig newConfig)
{
	cleanupAll();
	configured = false;

	std::string error;
	if (!validateConfig(newConfig, error)) {
		LOG_ERROR("[EchoRaid] Disabled: {}", error);
		return false;
	}

	config = std::move(newConfig);
	configured = true;
	nextTickAt = 0;
	LOG_INFO("[EchoRaid] Enabled");
	LOG_INFO("[EchoRaid] Echo item id: {}", config.portalItemId);
	LOG_INFO("[EchoRaid] Raid lifetime: {} ms", config.lifetimeMs);
	return true;
}

bool EchoRaidManager::isEnabled() const
{
	return configured && config.enabled && ConfigManager::getBoolean(ConfigManager::ECHO_RAID_SYSTEM_ENABLED) &&
	       BestiaryCharmSystem::isEnabled();
}

bool EchoRaidManager::validateConfig(const EchoRaidConfig& candidate, std::string& error) const
{
	if (!candidate.enabled) {
		error = "disabled by EchoConfig";
		return false;
	}
	if (!ConfigManager::getBoolean(ConfigManager::ECHO_RAID_SYSTEM_ENABLED)) {
		error = "echoRaidSystemEnabled is false";
		return false;
	}
	if (!BestiaryCharmSystem::isEnabled()) {
		error = "Bestiary must be enabled";
		return false;
	}

	auto validItem = [](uint16_t itemId) {
		return itemId != 0 && Item::items[itemId].id == itemId &&
		       Item::items[itemId].group != ITEM_GROUP_DEPRECATED;
	};
	if (!validItem(candidate.portalItemId)) {
		error = fmt::format("portal item {} is missing from items.otb", candidate.portalItemId);
		return false;
	}
	if (candidate.portalTtlMs == 0 || candidate.lifetimeMs == 0 || candidate.spawnChanceDenominator == 0 ||
	    candidate.spawnChanceNumerator > candidate.spawnChanceDenominator) {
		error = "invalid lifetime or spawn chance";
		return false;
	}
	if (candidate.normalWeight + candidate.influencedWeight + candidate.wardenWeight == 0) {
		error = "outcome weights sum to zero";
		return false;
	}
	if (candidate.normalCountMin == 0 || candidate.normalCountMin > candidate.normalCountMax ||
	    candidate.influencedCount == 0 || candidate.influencedLevelMin == 0 ||
	    candidate.influencedLevelMin > candidate.influencedLevelMax || candidate.wardenMinionCountMin == 0 ||
	    candidate.wardenMinionCountMin > candidate.wardenMinionCountMax || candidate.auraRange == 0 ||
	    candidate.auraIntervalMs == 0 || candidate.spawnRadius == 0) {
		error = "invalid raid size, level, aura, or radius";
		return false;
	}
	if (!std::isfinite(candidate.completedBestiaryWardenMultiplier) ||
	    candidate.completedBestiaryWardenMultiplier <= 0.0 || !std::isfinite(candidate.wardenHealthMultiplier) ||
	    candidate.wardenHealthMultiplier <= 0.0 || !std::isfinite(candidate.wardenAttackMultiplier) ||
	    candidate.wardenAttackMultiplier <= 0.0 || !std::isfinite(candidate.auraDodgeChancePercent) ||
	    candidate.auraDodgeChancePercent < 0.0 || candidate.auraDodgeChancePercent > 100.0) {
		error = "invalid multiplier or dodge chance";
		return false;
	}
	if (candidate.basicScrollItemIds.empty() || candidate.catalystItems.empty()) {
		error = "Warden loot tables are empty";
		return false;
	}
	for (uint16_t itemId : candidate.basicScrollItemIds) {
		if (!validItem(itemId)) {
			error = fmt::format("basic scroll item {} is missing from items.otb", itemId);
			return false;
		}
	}
	uint64_t catalystWeight = 0;
	for (const EchoRaidWeightedItem& entry : candidate.catalystItems) {
		if (!validItem(entry.itemId)) {
			error = fmt::format("catalyst item {} is missing from items.otb", entry.itemId);
			return false;
		}
		catalystWeight += entry.weight;
	}
	if (catalystWeight == 0 || catalystWeight > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
		error = "invalid catalyst weights";
		return false;
	}
	return true;
}

bool EchoRaidManager::isOccurrenceEligible(uint8_t occurrence,
	                                        const std::array<bool, 5>& eligibleOccurrences)
{
	return occurrence < eligibleOccurrences.size() && eligibleOccurrences[occurrence];
}

bool EchoRaidManager::passesEligibilityPolicy(uint8_t occurrence,
	                                           const std::array<bool, 5>& eligibleOccurrences, bool summon,
	                                           bool boss, bool rewardBoss, bool echoSpawn, bool influenced,
	                                           bool fiendish, bool warden)
{
	return isOccurrenceEligible(occurrence, eligibleOccurrences) && !summon && !boss && !rewardBoss &&
	       !echoSpawn && !influenced && !fiendish && !warden;
}

EchoRaidOutcome EchoRaidManager::selectOutcome(uint64_t roll, uint32_t normalWeight, uint32_t influencedWeight,
	                                            uint32_t wardenWeight, bool bestiaryCompleted,
	                                            double completedBestiaryWardenMultiplier)
{
	const uint32_t effectiveWardenWeight = bestiaryCompleted
	                                          ? saturatingScaledWeight(wardenWeight,
	                                                                   completedBestiaryWardenMultiplier)
	                                          : wardenWeight;
	const uint64_t total = static_cast<uint64_t>(normalWeight) + influencedWeight + effectiveWardenWeight;
	if (total == 0) {
		return EchoRaidOutcome::Normal;
	}
	roll %= total;
	if (roll < normalWeight) {
		return EchoRaidOutcome::Normal;
	}
	roll -= normalWeight;
	if (roll < influencedWeight) {
		return EchoRaidOutcome::Influenced;
	}
	return EchoRaidOutcome::Warden;
}

bool EchoRaidManager::isEligibleMonster(const Monster& monster) const
{
	if (!isEnabled()) {
		return false;
	}

	const MonsterType* monsterType = monster.getMonsterType();
	if (!monsterType || monsterType->raceId == 0 || monsterType->raceId > std::numeric_limits<uint16_t>::max()) {
		return false;
	}
	const auto entry = g_bestiaryCharmSystem.getMonster(static_cast<uint16_t>(monsterType->raceId));
	return entry && passesEligibilityPolicy(entry->get().occurrence, config.eligibleOccurrences, monster.isSummon(),
	                                        monsterType->info.isBoss, monster.isRewardBoss(),
	                                        monster.isEchoRaidSpawn(), monster.isInfluenced(), monster.isFiendish(),
	                                        monster.isEchoWarden());
}

void EchoRaidManager::onMonsterDeath(Monster& monster)
{
	if (!isEligibleMonster(monster)) {
		return;
	}
	if (uniform_random(1, static_cast<int32_t>(config.spawnChanceDenominator)) >
	    static_cast<int32_t>(config.spawnChanceNumerator)) {
		return;
	}

	const MonsterType* monsterType = monster.getMonsterType();
	const PositionKey key{monster.getPosition(), monster.getInstanceID()};
	if (pendingEchoes.contains(key)) {
		return;
	}
	for (const auto& [_, portal] : portals) {
		if (portal.position == key.position && portal.instanceId == key.instanceId) {
			return;
		}
	}

	PendingEcho pending;
	pending.raceId = static_cast<uint16_t>(monsterType->raceId);
	pending.monsterName = monster.getName();
	pending.dueAt = static_cast<uint64_t>(OTSYS_TIME()) + config.portalDelayMs;
	pendingEchoes.emplace(key, std::move(pending));
}

bool EchoRaidManager::isValidPortalTile(const Position& position, uint32_t instanceId) const
{
	const Tile* tile = g_game.map.getTile(position);
	if (!tile || !tile->getGround() || tile->hasFlag(TILESTATE_BLOCKSOLID | TILESTATE_PROTECTIONZONE |
	                                                TILESTATE_FLOORCHANGE | TILESTATE_TELEPORT)) {
		return false;
	}
	if (const TileItemVector* items = tile->getItemList()) {
		for (const auto& item : *items) {
			if (item && item->getID() == config.portalItemId && item->compareInstance(instanceId)) {
				return false;
			}
		}
	}
	return true;
}

bool EchoRaidManager::createPortal(const PositionKey& key, const PendingEcho& pending)
{
	if (!isEnabled() || !isValidPortalTile(key.position, key.instanceId)) {
		return false;
	}

	Tile* tile = g_game.map.getTile(key.position);
	auto item = Item::CreateItem(config.portalItemId, 1);
	if (!item) {
		LOG_ERROR("[EchoRaid] Item::CreateItem({}) failed after startup validation", config.portalItemId);
		return false;
	}
	item->setInstanceID(key.instanceId);
	item->setCustomAttribute("echo_raid_race_id", static_cast<int64_t>(pending.raceId));
	if (g_game.internalAddItem(tile, item.get(), INDEX_WHEREEVER, FLAG_NOLIMIT) != RETURNVALUE_NOERROR) {
		return false;
	}

	const uint64_t token = item->getItemUID();
	if (token == 0) {
		g_game.internalRemoveItem(item.get(), -1, false, FLAG_NOLIMIT | FLAG_IGNORECANREMOVE);
		LOG_ERROR("[EchoRaid] Portal {} has no runtime item UID", config.portalItemId);
		return false;
	}

	PortalRecord portal;
	portal.item = item;
	portal.position = key.position;
	portal.instanceId = key.instanceId;
	portal.raceId = pending.raceId;
	portal.monsterName = pending.monsterName;
	portal.expiresAt = static_cast<uint64_t>(OTSYS_TIME()) + config.portalTtlMs;
	portals.emplace(token, std::move(portal));
	g_game.addMagicEffect(key.position, CONST_ME_TELEPORT, key.instanceId);
	return true;
}

void EchoRaidManager::expirePortals(uint64_t now)
{
	std::vector<uint64_t> expired;
	expired.reserve(portals.size());
	for (const auto& [token, portal] : portals) {
		if (now >= portal.expiresAt || portal.item.expired()) {
			expired.push_back(token);
		}
	}
	for (uint64_t token : expired) {
		auto it = portals.find(token);
		if (it == portals.end()) {
			continue;
		}
		auto item = it->second.item.lock();
		portals.erase(it);
		if (item && !item->isRemoved()) {
			g_game.internalRemoveItem(item.get(), -1, false, FLAG_NOLIMIT | FLAG_IGNORECANREMOVE);
		}
	}
}

EchoRaidOutcome EchoRaidManager::rollOutcome(const Player& player, uint16_t raceId) const
{
	bool completed = false;
	if (const auto entry = g_bestiaryCharmSystem.getMonster(raceId)) {
		completed = player.getBestiaryKillCount(raceId) >= entry->get().toKill;
	}
	const uint32_t effectiveWardenWeight = completed
	                                          ? saturatingScaledWeight(config.wardenWeight,
	                                                                   config.completedBestiaryWardenMultiplier)
	                                          : config.wardenWeight;
	const uint64_t total = static_cast<uint64_t>(config.normalWeight) + config.influencedWeight +
	                       effectiveWardenWeight;
	const uint64_t roll = static_cast<uint64_t>(uniform_random(0, static_cast<int32_t>(total - 1)));
	return selectOutcome(roll, config.normalWeight, config.influencedWeight, config.wardenWeight, completed,
	                     config.completedBestiaryWardenMultiplier);
}

bool EchoRaidManager::activateEcho(Player& player, Item& item, std::string& message)
{
	if (!isEnabled()) {
		message = "The Echo Raid system is disabled.";
		return false;
	}
	if (item.getID() != config.portalItemId || !item.compareInstance(player.getInstanceID())) {
		message = "This is not an active Echo.";
		return false;
	}

	const uint64_t token = item.getItemUID();
	auto it = portals.find(token);
	if (it == portals.end()) {
		message = "This Echo has already been consumed or expired.";
		return false;
	}
	PortalRecord portal = it->second;
	auto trackedItem = portal.item.lock();
	if (!trackedItem || trackedItem.get() != &item || static_cast<uint64_t>(OTSYS_TIME()) >= portal.expiresAt) {
		portals.erase(it);
		message = "This Echo has expired.";
		return false;
	}

	// Erasing first is the atomic consume marker. Step-in and use execute on the
	// game dispatcher, so a second hook in the same tick cannot start another raid.
	portals.erase(it);
	if (g_game.internalRemoveItem(&item, -1, false, FLAG_NOLIMIT | FLAG_IGNORECANREMOVE) != RETURNVALUE_NOERROR) {
		portals.emplace(token, std::move(portal));
		message = "The Echo could not be consumed.";
		return false;
	}

	const EchoRaidOutcome outcome = rollOutcome(player, portal.raceId);
	return startRaid(portal.position, portal.instanceId, portal.raceId, portal.monsterName, outcome, message);
}

std::optional<Position> EchoRaidManager::findSpawnPosition(Monster& monster, const RaidInstance& raid) const
{
	const int32_t radius = config.spawnRadius;
	const int32_t width = radius * 2 + 1;
	const int32_t candidates = width * width;
	const int32_t offset = uniform_random(0, candidates - 1);
	for (int32_t index = 0; index < candidates; ++index) {
		const int32_t candidate = (offset + index) % candidates;
		const int32_t dx = candidate % width - radius;
		const int32_t dy = candidate / width - radius;
		const int32_t x = static_cast<int32_t>(raid.origin.x) + dx;
		const int32_t y = static_cast<int32_t>(raid.origin.y) + dy;
		if (x <= 0 || x > std::numeric_limits<uint16_t>::max() || y <= 0 ||
		    y > std::numeric_limits<uint16_t>::max()) {
			continue;
		}
		const Position position{static_cast<uint16_t>(x), static_cast<uint16_t>(y), raid.origin.z};
		Tile* tile = g_game.map.getTile(position);
		if (!tile || !tile->getGround() || tile->hasFlag(TILESTATE_BLOCKSOLID | TILESTATE_PROTECTIONZONE |
		                                                TILESTATE_FLOORCHANGE | TILESTATE_TELEPORT)) {
			continue;
		}
		if (tile->queryAdd(0, monster, 1, 0, &monster) == RETURNVALUE_NOERROR) {
			return position;
		}
	}
	return std::nullopt;
}

std::shared_ptr<Monster> EchoRaidManager::spawnRaidMonster(RaidInstance& raid, bool warden, bool influenced)
{
	auto uniqueMonster = Monster::createMonster(raid.monsterName);
	if (!uniqueMonster) {
		LOG_ERROR("[EchoRaid] Monster '{}' is no longer registered", raid.monsterName);
		return nullptr;
	}
	auto monster = std::shared_ptr<Monster>(std::move(uniqueMonster));
	monster->setInstanceID(raid.instanceId);
	monster->setEchoRaidId(raid.id);
	const auto position = findSpawnPosition(*monster, raid);
	if (!position || !g_events->eventMonsterOnSpawn(monster.get(), *position, false, true) ||
	    !g_game.placeCreature(monster.get(), *position, false, false, CONST_ME_TELEPORT)) {
		return nullptr;
	}

	if (warden && !monster->applyEchoWarden(config.wardenHealthMultiplier, config.wardenAttackMultiplier)) {
		g_game.removeCreature(monster.get(), false);
		return nullptr;
	}
	if (influenced) {
		monster->setInfluenced(true);
		monster->setInfluencedLevel(static_cast<uint8_t>(uniform_random(config.influencedLevelMin,
		                                                               config.influencedLevelMax)));
	}

	raid.creatureIds.insert(monster->getID());
	creatureToRaid[monster->getID()] = raid.id;
	if (warden) {
		raid.wardenId = monster->getID();
	}
	return monster;
}

bool EchoRaidManager::startRaid(const Position& origin, uint32_t instanceId, uint16_t raceId,
	                             std::string_view monsterName, EchoRaidOutcome outcome, std::string& message)
{
	RaidInstance raid;
	raid.id = nextRaidId++;
	raid.raceId = raceId;
	raid.monsterName = std::string(monsterName);
	raid.origin = origin;
	raid.instanceId = instanceId;
	raid.outcome = outcome;
	raid.createdAt = static_cast<uint64_t>(OTSYS_TIME());
	raid.expiresAt = raid.createdAt + config.lifetimeMs;
	raid.nextAuraAt = raid.createdAt + config.auraIntervalMs;
	const uint64_t raidId = raid.id;
	raids.emplace(raidId, std::move(raid));
	RaidInstance& activeRaid = raids.at(raidId);

	uint32_t requestedCount = 0;
	bool warden = false;
	bool influenced = false;
	switch (outcome) {
		case EchoRaidOutcome::Normal:
			requestedCount = static_cast<uint32_t>(uniform_random(config.normalCountMin, config.normalCountMax));
			message = "A normal Echo Raid has begun.";
			break;
		case EchoRaidOutcome::Influenced:
			requestedCount = config.influencedCount;
			influenced = true;
			message = "An influenced Echo Raid has begun.";
			break;
		case EchoRaidOutcome::Warden:
			warden = true;
			requestedCount = static_cast<uint32_t>(uniform_random(config.wardenMinionCountMin,
			                                                       config.wardenMinionCountMax));
			message = "An Echo Warden has emerged.";
			break;
	}

	if (warden && !spawnRaidMonster(activeRaid, true, false)) {
		cleanupRaid(raidId);
		message = "The Echo Warden could not find a valid spawn tile.";
		return false;
	}
	uint32_t spawned = 0;
	for (uint32_t index = 0; index < requestedCount; ++index) {
		if (spawnRaidMonster(activeRaid, false, influenced)) {
			++spawned;
		}
	}
	if (!warden && spawned == 0) {
		cleanupRaid(raidId);
		message = "The Echo Raid could not find a valid spawn tile.";
		return false;
	}

	g_game.addMagicEffect(origin, CONST_ME_AGONY, instanceId);
	if (warden) {
		updateWardenAura(activeRaid, activeRaid.createdAt);
	}
	return true;
}

void EchoRaidManager::clearWardenProtection(RaidInstance& raid)
{
	for (uint32_t creatureId : raid.protectedCreatureIds) {
		if (auto monster = g_game.getMonsterByIDShared(creatureId)) {
			monster->setEchoWardProtected(false);
		}
	}
	raid.protectedCreatureIds.clear();
}

void EchoRaidManager::updateWardenAura(RaidInstance& raid, uint64_t now)
{
	auto warden = g_game.getMonsterByIDShared(raid.wardenId);
	if (!warden || warden->isRemoved() || !warden->isEchoWarden()) {
		clearWardenProtection(raid);
		raid.wardenId = 0;
		return;
	}

	std::unordered_set<uint32_t> protectedNow;
	SpectatorVec spectators;
	g_game.map.getSpectators(spectators, warden->getPosition(), false, false, config.auraRange, config.auraRange,
	                         config.auraRange, config.auraRange);
	for (const auto& spectator : spectators.monsters()) {
		Monster* monster = static_cast<Monster*>(spectator.get());
		const MonsterType* monsterType = monster ? monster->getMonsterType() : nullptr;
		if (!monster || monster == warden.get() || monster->isRemoved() || monster->isSummon() || monster->isBoss() ||
		    monster->isFiendish() || monster->isInfluenced() || !monster->compareInstance(raid.instanceId) ||
		    !monsterType || monsterType->raceId != raid.raceId) {
			continue;
		}
		protectedNow.insert(monster->getID());
		monster->setEchoWardProtected(true);
	}

	for (uint32_t previousId : raid.protectedCreatureIds) {
		if (!protectedNow.contains(previousId)) {
			if (auto monster = g_game.getMonsterByIDShared(previousId)) {
				monster->setEchoWardProtected(false);
			}
		}
	}
	raid.protectedCreatureIds = std::move(protectedNow);
	raid.nextAuraAt = now + config.auraIntervalMs;
}

void EchoRaidManager::cleanupRaid(uint64_t raidId)
{
	auto node = raids.extract(raidId);
	if (node.empty()) {
		return;
	}
	RaidInstance raid = std::move(node.mapped());
	for (uint32_t creatureId : raid.creatureIds) {
		creatureToRaid.erase(creatureId);
	}
	clearWardenProtection(raid);
	for (uint32_t creatureId : raid.creatureIds) {
		if (auto creature = g_game.getCreatureByIDShared(creatureId); creature && !creature->isRemoved()) {
			g_game.removeCreature(creature.get(), false);
		}
	}
}

void EchoRaidManager::tick(uint64_t now)
{
	if (!isEnabled() || now < nextTickAt) {
		return;
	}
	nextTickAt = now + ECHO_TICK_INTERVAL_MS;

	std::vector<std::pair<PositionKey, PendingEcho>> ready;
	for (auto it = pendingEchoes.begin(); it != pendingEchoes.end();) {
		if (now >= it->second.dueAt) {
			ready.emplace_back(it->first, std::move(it->second));
			it = pendingEchoes.erase(it);
		} else {
			++it;
		}
	}
	for (const auto& [key, pending] : ready) {
		(void)createPortal(key, pending);
	}
	expirePortals(now);

	std::vector<uint64_t> expiredRaids;
	for (auto& [raidId, raid] : raids) {
		if (now >= raid.expiresAt) {
			expiredRaids.push_back(raidId);
		} else if (raid.wardenId != 0 && now >= raid.nextAuraAt) {
			updateWardenAura(raid, now);
		}
	}
	for (uint64_t raidId : expiredRaids) {
		cleanupRaid(raidId);
	}
}

void EchoRaidManager::onCreatureRemoved(uint32_t creatureId)
{
	auto mapping = creatureToRaid.find(creatureId);
	if (mapping == creatureToRaid.end()) {
		return;
	}
	const uint64_t raidId = mapping->second;
	creatureToRaid.erase(mapping);
	auto raidIt = raids.find(raidId);
	if (raidIt == raids.end()) {
		return;
	}
	RaidInstance& raid = raidIt->second;
	raid.creatureIds.erase(creatureId);
	raid.protectedCreatureIds.erase(creatureId);
	if (raid.wardenId == creatureId) {
		raid.wardenId = 0;
		clearWardenProtection(raid);
	}
	if (raid.creatureIds.empty()) {
		raids.erase(raidIt);
	}
}

void EchoRaidManager::cleanupAll()
{
	pendingEchoes.clear();

	std::vector<uint64_t> portalTokens;
	portalTokens.reserve(portals.size());
	for (const auto& [token, _] : portals) {
		portalTokens.push_back(token);
	}
	for (uint64_t token : portalTokens) {
		auto it = portals.find(token);
		if (it == portals.end()) {
			continue;
		}
		auto item = it->second.item.lock();
		portals.erase(it);
		if (item && !item->isRemoved()) {
			g_game.internalRemoveItem(item.get(), -1, false, FLAG_NOLIMIT | FLAG_IGNORECANREMOVE);
		}
	}

	std::vector<uint64_t> raidIds;
	raidIds.reserve(raids.size());
	for (const auto& [raidId, _] : raids) {
		raidIds.push_back(raidId);
	}
	for (uint64_t raidId : raidIds) {
		cleanupRaid(raidId);
	}
	creatureToRaid.clear();
}

uint32_t EchoRaidManager::firstWardenCharmPoints(uint8_t stars) const
{
	const size_t index = std::min<size_t>(stars, config.charmPointsByStars.size() - 1);
	return config.charmPointsByStars[index];
}

void EchoRaidManager::grantWardenRewards(Monster& monster,
	                                      const std::vector<std::shared_ptr<Player>>& recipients)
{
	if (!isEnabled() || !monster.isEchoWarden() || recipients.empty()) {
		return;
	}
	const MonsterType* monsterType = monster.getMonsterType();
	if (!monsterType || monsterType->raceId == 0 || monsterType->raceId > std::numeric_limits<uint16_t>::max()) {
		return;
	}
	const uint16_t raceId = static_cast<uint16_t>(monsterType->raceId);
	const auto entry = g_bestiaryCharmSystem.getMonster(raceId);
	if (!entry) {
		return;
	}
	const uint32_t amount = firstWardenCharmPoints(entry->get().stars);
	for (const auto& player : recipients) {
		if (!player || player->isRemoved()) {
			continue;
		}
		auto rewardKv = KVStore::getInstance()
		                    .scoped("player")
		                    ->scoped(fmt::format("{}", player->getGUID()))
		                    ->scoped("echo_warden_first_kill");
		const std::string key = fmt::format("race_{}", raceId);
		if (const auto existing = rewardKv->get(key, true); existing && existing->get<BooleanType>()) {
			continue;
		}

		player->addBestiaryCharmPoints(amount);
		rewardKv->set(key, true);
		player->sendEchoWardenReward(raceId, amount);
		player->sendTextMessage(MESSAGE_EVENT_ADVANCE,
		                        fmt::format("First Echo Warden defeated for this species: +{} Charm Points.", amount));
	}
}

uint16_t EchoRaidManager::selectBasicScroll() const
{
	return config.basicScrollItemIds[static_cast<size_t>(
	    uniform_random(0, static_cast<int32_t>(config.basicScrollItemIds.size() - 1)))];
}

uint16_t EchoRaidManager::selectCatalyst() const
{
	uint32_t totalWeight = 0;
	for (const EchoRaidWeightedItem& entry : config.catalystItems) {
		totalWeight += entry.weight;
	}
	uint32_t roll = static_cast<uint32_t>(uniform_random(1, static_cast<int32_t>(totalWeight)));
	for (const EchoRaidWeightedItem& entry : config.catalystItems) {
		if (roll <= entry.weight) {
			return entry.itemId;
		}
		roll -= entry.weight;
	}
	return config.catalystItems.back().itemId;
}

void EchoRaidManager::addWardenLoot(Monster& monster, Container& corpse)
{
	if (!isEnabled() || !monster.isEchoWarden() || !monster.markEchoWardenLootGranted()) {
		return;
	}
	const uint16_t scrollId = selectBasicScroll();
	const uint16_t catalystId = selectCatalyst();
	auto scroll = Item::CreateItem(scrollId, 1);
	auto catalyst = Item::CreateItem(catalystId, 1);
	if (!scroll || !catalyst || !corpse.addItem(scroll) || !corpse.addItem(catalyst)) {
		LOG_ERROR("[EchoRaid] Failed to add verified Warden loot (scroll {}, catalyst {}) to corpse", scrollId,
		          catalystId);
	}
}

bool EchoRaidManager::tryEchoWardDodge(const Monster& monster) const
{
	return isEnabled() && monster.isEchoWardProtected() && config.auraDodgeChancePercent > 0.0 &&
	       static_cast<double>(uniform_random(1, 10000)) <= config.auraDodgeChancePercent * 100.0;
}

EchoRaidRuntimeStatus EchoRaidManager::getStatus() const
{
	return {isEnabled(), pendingEchoes.size(), portals.size(), raids.size(), creatureToRaid.size()};
}

bool EchoRaidManager::executeDebugCommand(Player& player, std::string_view command, std::string& message)
{
	std::string input = trim(command);
	std::string operation;
	std::string monsterName;
	if (const size_t separator = input.find(' '); separator != std::string::npos) {
		operation = lower(trim(input.substr(0, separator)));
		monsterName = trim(input.substr(separator + 1));
	} else {
		operation = lower(input);
	}

	if (operation == "status") {
		const EchoRaidRuntimeStatus status = getStatus();
		message = fmt::format("EchoRaid: {}, pending={}, portals={}, raids={}, creatures={}",
		                      status.enabled ? "enabled" : "disabled", status.pendingEchoes, status.portals,
		                      status.activeRaids, status.trackedCreatures);
		return true;
	}
	if (operation == "cleanup") {
		cleanupAll();
		message = "All Echo portals and raids were cleaned up.";
		return true;
	}
	if (!isEnabled()) {
		message = "The Echo Raid system is disabled.";
		return false;
	}

	uint16_t raceId = 0;
	if (monsterName.empty()) {
		if (auto target = player.getAttackedCreatureShared()) {
			if (const Monster* monster = target->getMonster()) {
				const MonsterType* monsterType = monster->getMonsterType();
				if (monsterType && monsterType->raceId <= std::numeric_limits<uint16_t>::max()) {
					raceId = static_cast<uint16_t>(monsterType->raceId);
					monsterName = monster->getName();
				}
			}
		}
	} else {
		auto probe = Monster::createMonster(monsterName);
		if (probe) {
			const MonsterType* monsterType = probe->getMonsterType();
			if (monsterType && monsterType->raceId <= std::numeric_limits<uint16_t>::max()) {
				raceId = static_cast<uint16_t>(monsterType->raceId);
				monsterName = probe->getName();
			}
		}
	}
	if (raceId == 0 || !g_bestiaryCharmSystem.getMonster(raceId)) {
		message = "Target a registered Bestiary monster or provide its name.";
		return false;
	}

	if (operation == "spawn") {
		const PositionKey key{player.getPosition(), player.getInstanceID()};
		if (pendingEchoes.contains(key) || !isValidPortalTile(key.position, key.instanceId)) {
			message = "This tile cannot receive another Echo portal.";
			return false;
		}
		PendingEcho pending{raceId, monsterName, static_cast<uint64_t>(OTSYS_TIME())};
		if (!createPortal(key, pending)) {
			message = "The Echo portal could not be created.";
			return false;
		}
		message = fmt::format("Echo portal created for {}.", monsterName);
		return true;
	}

	std::optional<EchoRaidOutcome> outcome;
	if (operation == "normal") {
		outcome = EchoRaidOutcome::Normal;
	} else if (operation == "influenced") {
		outcome = EchoRaidOutcome::Influenced;
	} else if (operation == "warden") {
		outcome = EchoRaidOutcome::Warden;
	}
	if (!outcome) {
		message = "Usage: /echo spawn|normal|influenced|warden [monster] | cleanup | status";
		return false;
	}
	return startRaid(player.getPosition(), player.getInstanceID(), raceId, monsterName, *outcome, message);
}
