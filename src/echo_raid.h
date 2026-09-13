// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_ECHO_RAID_H
#define FS_ECHO_RAID_H

#include "position.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Container;
class Item;
class Monster;
class Player;

enum class EchoRaidOutcome : uint8_t
{
	Normal,
	Influenced,
	Warden,
};

struct EchoRaidWeightedItem
{
	uint16_t itemId = 0;
	uint32_t weight = 0;
};

struct EchoRaidConfig
{
	bool enabled = true;
	uint16_t portalItemId = 54133;
	uint32_t portalDelayMs = 30000;
	uint32_t portalTtlMs = 120000;
	uint32_t spawnChanceNumerator = 100;
	uint32_t spawnChanceDenominator = 200000;
	std::array<bool, 5> eligibleOccurrences = {false, true, true, false, false};

	uint32_t normalWeight = 78;
	uint32_t influencedWeight = 20;
	uint32_t wardenWeight = 2;
	double completedBestiaryWardenMultiplier = 2.0;
	uint8_t normalCountMin = 5;
	uint8_t normalCountMax = 8;
	uint8_t influencedCount = 4;
	uint8_t influencedLevelMin = 1;
	uint8_t influencedLevelMax = 5;

	double wardenHealthMultiplier = 3.0;
	double wardenAttackMultiplier = 1.5;
	uint8_t wardenMinionCountMin = 7;
	uint8_t wardenMinionCountMax = 12;
	uint8_t auraRange = 5;
	uint32_t auraIntervalMs = 2000;
	double auraDodgeChancePercent = 10.0;
	uint8_t spawnRadius = 3;
	uint32_t spawnIntervalMs = 400;
	uint32_t lifetimeMs = 10 * 60 * 1000;

	std::array<uint32_t, 6> charmPointsByStars = {1, 2, 5, 10, 15, 30};
	std::vector<uint16_t> basicScrollItemIds;
	std::vector<EchoRaidWeightedItem> catalystItems;
};

struct EchoRaidRuntimeStatus
{
	bool enabled = false;
	size_t pendingEchoes = 0;
	size_t portals = 0;
	size_t activeRaids = 0;
	size_t trackedCreatures = 0;
	size_t pendingSpawns = 0;
};

class EchoRaidManager
{
public:
	bool configure(EchoRaidConfig config);
	[[nodiscard]] bool isEnabled() const;
	[[nodiscard]] bool isConfigured() const { return configured; }
	[[nodiscard]] const EchoRaidConfig& getConfig() const { return config; }

	void onMonsterDeath(Monster& monster);
	void grantWardenRewards(Monster& monster, const std::vector<std::shared_ptr<Player>>& recipients);
	bool activateEcho(Player& player, Item& item, std::string& message);
	bool executeDebugCommand(Player& player, std::string_view command, std::string& message);
	void addWardenLoot(Monster& monster, Container& corpse);
	[[nodiscard]] bool tryEchoWardDodge(const Monster& monster) const;

	void tick(uint64_t now);
	void onCreatureRemoved(uint32_t creatureId);
	void cleanupAll();
	[[nodiscard]] EchoRaidRuntimeStatus getStatus() const;

	[[nodiscard]] static bool isOccurrenceEligible(uint8_t occurrence,
	                                               const std::array<bool, 5>& eligibleOccurrences);
	[[nodiscard]] static bool passesEligibilityPolicy(uint8_t occurrence,
	                                                  const std::array<bool, 5>& eligibleOccurrences,
	                                                  bool summon, bool boss, bool rewardBoss, bool echoSpawn,
	                                                  bool influenced, bool fiendish, bool warden);
	[[nodiscard]] static EchoRaidOutcome selectOutcome(uint64_t roll, uint32_t normalWeight,
	                                                   uint32_t influencedWeight, uint32_t wardenWeight,
	                                                   bool bestiaryCompleted,
	                                                   double completedBestiaryWardenMultiplier);

private:
	struct PositionKey
	{
		Position position;
		uint32_t instanceId = 0;

		bool operator==(const PositionKey& other) const
		{
			return position == other.position && instanceId == other.instanceId;
		}
	};

	struct PositionKeyHash
	{
		size_t operator()(const PositionKey& key) const noexcept;
	};

	struct PendingEcho
	{
		uint16_t raceId = 0;
		std::string monsterName;
		uint64_t dueAt = 0;
	};

	struct PortalRecord
	{
		std::weak_ptr<Item> item;
		Position position;
		uint32_t instanceId = 0;
		uint16_t raceId = 0;
		std::string monsterName;
		uint64_t expiresAt = 0;
	};

	struct RaidInstance
	{
		uint64_t id = 0;
		uint16_t raceId = 0;
		std::string monsterName;
		Position origin;
		uint32_t instanceId = 0;
		EchoRaidOutcome outcome = EchoRaidOutcome::Normal;
		uint64_t createdAt = 0;
		uint64_t expiresAt = 0;
		uint64_t nextAuraAt = 0;
		uint64_t nextSpawnAt = 0;
		uint32_t pendingSpawnCount = 0;
		bool pendingInfluenced = false;
		uint32_t wardenId = 0;
		std::unordered_set<uint32_t> creatureIds;
		std::unordered_set<uint32_t> protectedCreatureIds;
	};

	[[nodiscard]] bool validateConfig(const EchoRaidConfig& candidate, std::string& error) const;
	[[nodiscard]] bool isEligibleMonster(const Monster& monster) const;
	[[nodiscard]] bool isValidPortalTile(const Position& position, uint32_t instanceId) const;
	[[nodiscard]] bool createPortal(const PositionKey& key, const PendingEcho& pending);
	void expirePortals(uint64_t now);
	[[nodiscard]] EchoRaidOutcome rollOutcome(const Player& player, uint16_t raceId) const;
	[[nodiscard]] bool startRaid(const Position& origin, uint32_t instanceId, uint16_t raceId,
	                             std::string_view monsterName, EchoRaidOutcome outcome, std::string& message);
	[[nodiscard]] std::shared_ptr<Monster> spawnRaidMonster(RaidInstance& raid, bool warden, bool influenced);
	[[nodiscard]] bool spawnNextRaidMonster(RaidInstance& raid);
	[[nodiscard]] std::optional<Position> findSpawnPosition(Monster& monster, const RaidInstance& raid) const;
	void updateWardenAura(RaidInstance& raid, uint64_t now);
	void clearWardenProtection(RaidInstance& raid);
	void cleanupRaid(uint64_t raidId);
	[[nodiscard]] uint16_t selectBasicScroll() const;
	[[nodiscard]] uint16_t selectCatalyst() const;
	[[nodiscard]] uint32_t firstWardenCharmPoints(uint8_t stars) const;

	EchoRaidConfig config;
	bool configured = false;
	uint64_t nextRaidId = 1;
	uint64_t nextTickAt = 0;
	std::unordered_map<PositionKey, PendingEcho, PositionKeyHash> pendingEchoes;
	std::unordered_map<uint64_t, PortalRecord> portals;
	std::unordered_map<uint64_t, RaidInstance> raids;
	std::unordered_map<uint32_t, uint64_t> creatureToRaid;
};

extern EchoRaidManager g_echoRaidManager;

#endif // FS_ECHO_RAID_H
