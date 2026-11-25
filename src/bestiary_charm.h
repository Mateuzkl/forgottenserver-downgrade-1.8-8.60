// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_BESTIARY_CHARM_H
#define FS_BESTIARY_CHARM_H

#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "configmanager.h"

class Player;

enum class BestiaryCharmCategory : uint8_t
{
	Major,
	Minor
};

struct BestiaryCreatureInfo
{
	uint16_t raceId = 0;
	std::string name;
	uint32_t toKill = 0;
	uint32_t firstUnlock = 0;
	uint32_t secondUnlock = 0;
	uint16_t charmPoints = 0;
	uint8_t stars = 0;
	uint8_t occurrence = 0;
	uint16_t lookType = 0;
	uint16_t lookTypeEx = 0;
	uint8_t lookHead = 0;
	uint8_t lookBody = 0;
	uint8_t lookLegs = 0;
	uint8_t lookFeet = 0;
	uint8_t lookAddons = 0;
};

struct BestiaryCharmActionResult
{
	bool success = false;
	std::string message;
};

struct CharmSoundConfig
{
	uint8_t id = 0;
	uint16_t castSound = 0;
	uint16_t impactSound = 0;
};

class BestiaryCharmSystem
{
public:
	/// Centralized feature check — all native paths should use this.
	[[nodiscard]] static bool isEnabled()
	{
		return ConfigManager::getBoolean(ConfigManager::BESTIARY_SYSTEM_ENABLED);
	}

	CharmSoundConfig* getCharmSounds(uint8_t id);
	void registerMonster(BestiaryCreatureInfo info);
	[[nodiscard]] std::optional<std::reference_wrapper<const BestiaryCreatureInfo>> getMonster(uint16_t raceId) const;
	[[nodiscard]] static uint8_t getProgress(const BestiaryCreatureInfo& info, uint32_t kills);
	BestiaryCharmActionResult handleCharmAction(Player& player, uint8_t charmId, uint8_t action, uint16_t raceId) const;
	[[nodiscard]] bool addMinorCharmEchoes(uint32_t playerGuid, uint32_t amount) const;

	[[nodiscard]] bool isMajorCharm(uint8_t charmId) const;
	[[nodiscard]] bool isMinorCharm(uint8_t charmId) const;
	[[nodiscard]] uint8_t getAssignedCharmTier(const Player& player, uint8_t charmId, uint16_t raceId) const;
	[[nodiscard]] double getCharmBonus(uint8_t charmId, uint8_t tier) const;

private:
	std::array<CharmSoundConfig, 25> charmSounds = [] {
		std::array<CharmSoundConfig, 25> sounds{};
		for (uint8_t id = 0; id < sounds.size(); ++id) sounds[id].id = id;
		// Elemental defaults use existing catalog sounds; no dedicated charm IDs exist in CrystalServer.
		const uint16_t offensive[] = {2008, 2002, 2004, 2005, 2003, 2006};
		for (uint8_t id = 0; id < 6; ++id) sounds[id].impactSound = offensive[id];
		sounds[7].impactSound = 2008;
		sounds[16].impactSound = 2007;
		sounds[22].impactSound = 2034;
		sounds[23].impactSound = 2008;
		sounds[24].impactSound = 2010;
		return sounds;
	}();
	struct CharmState
	{
		bool unlocked = false;
		uint8_t tier = 0;
		uint16_t raceId = 0;
	};

	using CharmStateMap = std::unordered_map<uint8_t, CharmState>;
	using PlayerCharmStateCache = std::unordered_map<uint32_t, CharmStateMap>;

	[[nodiscard]] CharmStateMap loadCharmStates(uint32_t playerGuid) const;
	[[nodiscard]] const CharmStateMap& getCachedCharmStates(uint32_t playerGuid) const;
	[[nodiscard]] uint32_t getKillCount(uint32_t playerGuid, uint16_t raceId) const;
	[[nodiscard]] uint32_t getCharmPoints(uint32_t playerGuid) const;
	[[nodiscard]] bool setCharmPoints(uint32_t playerGuid, uint32_t points) const;
	[[nodiscard]] std::pair<uint32_t, uint32_t> getMinorCharmEchoes(uint32_t playerGuid) const;
	[[nodiscard]] bool setMinorCharmEchoes(uint32_t playerGuid, uint32_t echoes, uint32_t maxEchoes) const;
	[[nodiscard]] bool hasGold(const Player& player, uint64_t amount) const;
	[[nodiscard]] bool removeGold(Player& player, uint64_t amount) const;
	[[nodiscard]] uint8_t getAssignedCharmCount(const CharmStateMap& states) const;
	[[nodiscard]] uint32_t getSpentMajorCharmPoints(const CharmStateMap& states) const;
	[[nodiscard]] bool writeCharmStates(uint32_t playerGuid, const CharmStateMap& states) const;
	[[nodiscard]] bool restoreCharmStates(uint32_t playerGuid, const CharmStateMap& states) const;
	[[nodiscard]] bool restoreCharmStatesAndResources(uint32_t playerGuid, const CharmStateMap& states,
	                                                   uint32_t charmPoints, uint32_t minorEchoes,
	                                                   uint32_t maxMinorEchoes) const;
	void invalidatePlayer(uint32_t playerGuid) const;

	std::unordered_map<uint16_t, BestiaryCreatureInfo> monstersByRaceId;
	mutable PlayerCharmStateCache charmStateCache;
};

extern BestiaryCharmSystem g_bestiaryCharmSystem;

#endif
