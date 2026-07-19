// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "bestiary_charm.h"

#include "database.h"
#include "game.h"
#include "logger.h"
#include "player.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>
#include <fmt/format.h>

extern Game g_game;

namespace {

struct BestiaryCharmDefinition
{
	uint8_t id = 0;
	BestiaryCharmCategory category = BestiaryCharmCategory::Major;
	std::array<uint16_t, 3> prices {};
	std::array<double, 3> bonuses {};
};

constexpr std::array<BestiaryCharmDefinition, 25> charmDefinitions = {{
	{0, BestiaryCharmCategory::Major, {240, 360, 1200}, {5, 10, 11}},
	{1, BestiaryCharmCategory::Major, {400, 600, 2000}, {5, 10, 11}},
	{2, BestiaryCharmCategory::Major, {240, 360, 1200}, {5, 10, 11}},
	{3, BestiaryCharmCategory::Major, {320, 480, 1600}, {5, 10, 11}},
	{4, BestiaryCharmCategory::Major, {320, 480, 1600}, {5, 10, 11}},
	{5, BestiaryCharmCategory::Major, {360, 540, 1800}, {5, 10, 11}},
	{6, BestiaryCharmCategory::Minor, {100, 150, 225}, {6, 9, 12}},
	{7, BestiaryCharmCategory::Major, {400, 600, 2000}, {5, 10, 11}},
	{8, BestiaryCharmCategory::Major, {240, 360, 1200}, {5, 10, 11}},
	{9, BestiaryCharmCategory::Minor, {100, 150, 225}, {6, 9, 12}},
	{10, BestiaryCharmCategory::Minor, {100, 150, 225}, {6, 9, 12}},
	{11, BestiaryCharmCategory::Minor, {100, 150, 225}, {6, 9, 12}},
	{12, BestiaryCharmCategory::Minor, {100, 150, 225}, {6, 9, 12}},
	{13, BestiaryCharmCategory::Minor, {100, 150, 225}, {60, 90, 120}},
	{14, BestiaryCharmCategory::Minor, {100, 150, 225}, {6, 9, 12}},
	{15, BestiaryCharmCategory::Major, {800, 1200, 4000}, {4, 8, 9}},
	{16, BestiaryCharmCategory::Major, {600, 900, 3000}, {5, 10, 11}},
	{17, BestiaryCharmCategory::Minor, {100, 150, 225}, {1.6, 2.4, 3.2}},
	{18, BestiaryCharmCategory::Minor, {100, 150, 225}, {0.8, 1.2, 1.6}},
	{19, BestiaryCharmCategory::Major, {800, 1200, 4000}, {20, 40, 44}},
	{20, BestiaryCharmCategory::Minor, {100, 150, 225}, {30, 45, 60}},
	{21, BestiaryCharmCategory::Minor, {100, 150, 225}, {20, 30, 40}},
	{22, BestiaryCharmCategory::Major, {600, 900, 3000}, {10, 20, 22}},
	{23, BestiaryCharmCategory::Major, {600, 900, 3000}, {5, 10, 11}},
	{24, BestiaryCharmCategory::Major, {600, 900, 3000}, {5, 10, 11}},
}};

std::optional<BestiaryCharmDefinition> getCharmDefinition(uint8_t charmId)
{
	if (charmId >= charmDefinitions.size()) {
		return std::nullopt;
	}

	const BestiaryCharmDefinition& definition = charmDefinitions[charmId];
	if (definition.id != charmId) {
		return std::nullopt;
	}
	return definition;
}

bool sameCategory(uint8_t leftCharmId, uint8_t rightCharmId)
{
	const auto left = getCharmDefinition(leftCharmId);
	const auto right = getCharmDefinition(rightCharmId);
	return left && right && left->category == right->category;
}

uint16_t getCharmPriceForTier(const BestiaryCharmDefinition& charm, uint8_t tier)
{
	if (tier >= charm.prices.size()) {
		return 0;
	}
	return charm.prices[tier];
}

uint32_t getSpentCharmPointsForTier(const BestiaryCharmDefinition& charm, uint8_t tier)
{
	uint32_t spent = 0;
	const uint8_t cappedTier = std::min<uint8_t>(tier, static_cast<uint8_t>(charm.prices.size()));
	for (uint8_t index = 0; index < cappedTier; ++index) {
		spent += charm.prices[index];
	}
	return spent;
}

uint32_t getMinorEchoRewardForMajorTier(uint8_t tier)
{
	return 25 * tier * tier + 25 * tier + 50;
}

} // namespace

BestiaryCharmSystem g_bestiaryCharmSystem;

void BestiaryCharmSystem::registerMonster(BestiaryCreatureInfo info)
{
	if (info.raceId == 0 || info.toKill == 0) {
		LOG_ERROR("[Bestiary] Refusing invalid monster registration: raceId={}, name='{}', toKill={}",
		          info.raceId, info.name, info.toKill);
		return;
	}

	if (info.name.empty()) {
		info.name = "?";
	}
	if (info.firstUnlock == 0 || info.firstUnlock > info.toKill) {
		info.firstUnlock = info.toKill;
	}
	if (info.secondUnlock < info.firstUnlock || info.secondUnlock > info.toKill) {
		info.secondUnlock = info.toKill;
	}

	monstersByRaceId[info.raceId] = std::move(info);
}

std::optional<std::reference_wrapper<const BestiaryCreatureInfo>> BestiaryCharmSystem::getMonster(uint16_t raceId) const
{
	const auto it = monstersByRaceId.find(raceId);
	if (it == monstersByRaceId.end()) {
		return std::nullopt;
	}
	return std::cref(it->second);
}

uint8_t BestiaryCharmSystem::getProgress(const BestiaryCreatureInfo& info, uint32_t kills)
{
	if (kills == 0) {
		return 0;
	}
	if (kills >= info.toKill) {
		return 4;
	}
	if (kills >= info.secondUnlock) {
		return 3;
	}
	if (kills >= info.firstUnlock) {
		return 2;
	}
	return 1;
}

bool BestiaryCharmSystem::isMajorCharm(uint8_t charmId) const
{
	const auto definition = getCharmDefinition(charmId);
	return definition && definition->category == BestiaryCharmCategory::Major;
}

bool BestiaryCharmSystem::isMinorCharm(uint8_t charmId) const
{
	const auto definition = getCharmDefinition(charmId);
	return definition && definition->category == BestiaryCharmCategory::Minor;
}

uint8_t BestiaryCharmSystem::getAssignedCharmTier(const Player& player, uint8_t charmId, uint16_t raceId) const
{
	if (raceId == 0 || !getCharmDefinition(charmId)) {
		return 0;
	}

	const auto& states = getCachedCharmStates(player.getGUID());
	const auto it = states.find(charmId);
	if (it == states.end() || it->second.tier == 0 || it->second.raceId != raceId) {
		return 0;
	}
	return it->second.tier;
}

double BestiaryCharmSystem::getCharmBonus(uint8_t charmId, uint8_t tier) const
{
	const auto definition = getCharmDefinition(charmId);
	if (!definition || tier == 0 || tier > definition->bonuses.size()) {
		return 0.0;
	}
	return definition->bonuses[tier - 1];
}

BestiaryCharmSystem::CharmStateMap BestiaryCharmSystem::loadCharmStates(uint32_t playerGuid) const
{
	CharmStateMap states;
	auto result = Database::getInstance().storeQuery(fmt::format(
	    "SELECT `charm_id`, `unlocked`, `raceid` FROM `player_bestiary_charms` WHERE `player_id` = {:d}",
	    playerGuid));
	if (!result) {
		return states;
	}

	do {
		const uint8_t charmId = result->getNumber<uint8_t>("charm_id");
		const uint8_t tier = std::min<uint8_t>(result->getNumber<uint16_t>("unlocked"), 3);
		CharmState state;
		state.tier = tier;
		state.unlocked = tier > 0;
		state.raceId = result->getNumber<uint16_t>("raceid");
		states[charmId] = state;
	} while (result->next());

	return states;
}

const BestiaryCharmSystem::CharmStateMap& BestiaryCharmSystem::getCachedCharmStates(uint32_t playerGuid) const
{
	const auto [it, inserted] = charmStateCache.try_emplace(playerGuid);
	if (inserted) {
		it->second = loadCharmStates(playerGuid);
	}
	return it->second;
}

void BestiaryCharmSystem::invalidatePlayer(uint32_t playerGuid) const
{
	charmStateCache.erase(playerGuid);
}

uint32_t BestiaryCharmSystem::getKillCount(uint32_t playerGuid, uint16_t raceId) const
{
	if (const auto player = g_game.getPlayerByGUID(playerGuid)) {
		return player->getBestiaryKillCount(raceId);
	}

	auto result = Database::getInstance().storeQuery(fmt::format(
	    "SELECT `kills` FROM `player_bestiary_kills` WHERE `player_id` = {:d} AND `raceid` = {:d}",
	    playerGuid, raceId));
	return result ? result->getNumber<uint32_t>("kills") : 0;
}

uint32_t BestiaryCharmSystem::getCharmPoints(uint32_t playerGuid) const
{
	if (const auto player = g_game.getPlayerByGUID(playerGuid)) {
		return player->getBestiaryCharmPoints();
	}

	auto result = Database::getInstance().storeQuery(
	    fmt::format("SELECT `charmpoints` FROM `players` WHERE `id` = {:d}", playerGuid));
	return result ? result->getNumber<uint32_t>("charmpoints") : 0;
}

bool BestiaryCharmSystem::setCharmPoints(uint32_t playerGuid, uint32_t points) const
{
	if (const auto player = g_game.getPlayerByGUID(playerGuid)) {
		player->setBestiaryCharmPoints(points);
		return true;
	}

	return Database::getInstance().executeQuery(
	    fmt::format("UPDATE `players` SET `charmpoints` = {:d} WHERE `id` = {:d}", points, playerGuid));
}

std::pair<uint32_t, uint32_t> BestiaryCharmSystem::getMinorCharmEchoes(uint32_t playerGuid) const
{
	Database& db = Database::getInstance();
	db.executeQuery(fmt::format("INSERT IGNORE INTO `player_bestiary_resources` (`player_id`) VALUES ({:d})", playerGuid));
	auto result = db.storeQuery(fmt::format(
	    "SELECT `minor_charm_echoes`, `max_minor_charm_echoes` FROM `player_bestiary_resources` WHERE `player_id` = {:d}",
	    playerGuid));
	if (!result) {
		return {0, 0};
	}
	return {result->getNumber<uint32_t>("minor_charm_echoes"), result->getNumber<uint32_t>("max_minor_charm_echoes")};
}

bool BestiaryCharmSystem::setMinorCharmEchoes(uint32_t playerGuid, uint32_t echoes, uint32_t maxEchoes) const
{
	return Database::getInstance().executeQuery(fmt::format(
	    "INSERT INTO `player_bestiary_resources` (`player_id`, `minor_charm_echoes`, `max_minor_charm_echoes`) "
	    "VALUES ({:d}, {:d}, {:d}) ON DUPLICATE KEY UPDATE `minor_charm_echoes` = VALUES(`minor_charm_echoes`), "
	    "`max_minor_charm_echoes` = VALUES(`max_minor_charm_echoes`)",
	    playerGuid, echoes, maxEchoes));
}

bool BestiaryCharmSystem::hasGold(const Player& player, uint64_t amount) const
{
	if (amount == 0) {
		return true;
	}

	const uint64_t inventoryMoney = player.getMoney();
	const uint64_t bankBalance = player.getBankBalance();
	return inventoryMoney >= amount || bankBalance >= amount - inventoryMoney;
}

bool BestiaryCharmSystem::removeGold(Player& player, uint64_t amount) const
{
	if (amount == 0) {
		return true;
	}

	if (!hasGold(player, amount)) {
		return false;
	}

	const uint64_t inventoryMoney = player.getMoney();
	const uint64_t bankBalance = player.getBankBalance();
	const uint64_t fromInventory = std::min(inventoryMoney, amount);
	if (fromInventory > 0 && !g_game.removeMoney(&player, fromInventory)) {
		return false;
	}

	const uint64_t fromBank = amount - fromInventory;
	if (fromBank > 0) {
		player.setBankBalance(bankBalance - fromBank);
	}
	return true;
}

uint8_t BestiaryCharmSystem::getAssignedCharmCount(const CharmStateMap& states) const
{
	uint8_t count = 0;
	for (const auto& [charmId, state] : states) {
		if (getCharmDefinition(charmId) && state.tier > 0 && state.raceId > 0) {
			++count;
		}
	}
	return count;
}

uint32_t BestiaryCharmSystem::getSpentMajorCharmPoints(const CharmStateMap& states) const
{
	uint32_t spent = 0;
	for (const auto& [charmId, state] : states) {
		const auto charm = getCharmDefinition(charmId);
		if (!charm || charm->category != BestiaryCharmCategory::Major || state.tier == 0) {
			continue;
		}

		spent += getSpentCharmPointsForTier(*charm, state.tier);
	}
	return spent;
}

bool BestiaryCharmSystem::writeCharmStates(uint32_t playerGuid, const CharmStateMap& states) const
{
	Database& db = Database::getInstance();
	if (!db.executeQuery(fmt::format("DELETE FROM `player_bestiary_charms` WHERE `player_id` = {:d}", playerGuid))) {
		return false;
	}

	for (const auto& [charmId, state] : states) {
		if (!db.executeQuery(fmt::format(
		        "INSERT INTO `player_bestiary_charms` (`player_id`, `charm_id`, `unlocked`, `raceid`) "
		        "VALUES ({:d}, {:d}, {:d}, {:d})",
		        playerGuid, charmId, state.tier, state.raceId))) {
			return false;
		}
	}
	return true;
}

bool BestiaryCharmSystem::restoreCharmStates(uint32_t playerGuid, const CharmStateMap& states) const
{
	return DBTransaction::executeWithinTransactionRollbackOnFailure([&]() {
		return writeCharmStates(playerGuid, states);
	});
}

bool BestiaryCharmSystem::restoreCharmStatesAndResources(uint32_t playerGuid, const CharmStateMap& states,
                                                         uint32_t charmPoints, uint32_t minorEchoes,
                                                         uint32_t maxMinorEchoes) const
{
	const bool restored = DBTransaction::executeWithinTransactionRollbackOnFailure([&]() {
		return writeCharmStates(playerGuid, states) &&
		       setMinorCharmEchoes(playerGuid, minorEchoes, maxMinorEchoes);
	});
	return restored && setCharmPoints(playerGuid, charmPoints);
}

BestiaryCharmActionResult BestiaryCharmSystem::handleCharmAction(Player& player, uint8_t charmId, uint8_t action, uint16_t raceId) const
{
	const uint32_t playerGuid = player.getGUID();
	const auto charm = getCharmDefinition(charmId);
	if (action != 3 && !charm) {
		return { false, "Charm not found." };
	}

	CharmStateMap states = loadCharmStates(playerGuid);
	const auto stateIt = states.find(charmId);
	const CharmState currentState = stateIt != states.end() ? stateIt->second : CharmState {};

	if (action == 0) {
		if (currentState.tier >= 3) {
			return { false, "This charm is already fully unlocked." };
		}

		const uint8_t nextTier = currentState.tier + 1;
		const uint16_t price = getCharmPriceForTier(*charm, currentState.tier);
		if (price == 0) {
			return { false, "This charm cannot be upgraded." };
		}

		bool updated = false;
		if (charm->category == BestiaryCharmCategory::Major) {
			const uint32_t charmPoints = getCharmPoints(playerGuid);
			if (charmPoints < price) {
				return { false, "You do not have enough charm points." };
			}

			const auto [minorEchoes, maxMinorEchoes] = getMinorCharmEchoes(playerGuid);
			const uint32_t echoReward = getMinorEchoRewardForMajorTier(currentState.tier);
			updated = DBTransaction::executeWithinTransactionRollbackOnFailure([&]() {
				Database& db = Database::getInstance();
				return db.executeQuery(fmt::format(
				           "INSERT INTO `player_bestiary_charms` (`player_id`, `charm_id`, `unlocked`, `raceid`) "
				           "VALUES ({:d}, {:d}, {:d}, 0) ON DUPLICATE KEY UPDATE `unlocked` = {:d}",
				           playerGuid, charmId, nextTier, nextTier)) &&
				       setMinorCharmEchoes(playerGuid, minorEchoes + echoReward, maxMinorEchoes + echoReward);
			});
			if (updated) {
				player.setBestiaryCharmPoints(charmPoints - price);
			}
		} else {
			const auto [minorEchoes, maxMinorEchoes] = getMinorCharmEchoes(playerGuid);
			if (minorEchoes < price) {
				return { false, "You do not have enough minor charm echoes." };
			}

			updated = DBTransaction::executeWithinTransactionRollbackOnFailure([&]() {
				Database& db = Database::getInstance();
				return db.executeQuery(fmt::format(
				           "INSERT INTO `player_bestiary_charms` (`player_id`, `charm_id`, `unlocked`, `raceid`) "
				           "VALUES ({:d}, {:d}, {:d}, 0) ON DUPLICATE KEY UPDATE `unlocked` = {:d}",
				           playerGuid, charmId, nextTier, nextTier)) &&
				       setMinorCharmEchoes(playerGuid, minorEchoes - price, maxMinorEchoes);
			});
		}

		if (!updated) {
			return { false, "Could not upgrade this charm." };
		}
		invalidatePlayer(playerGuid);
		return { true, currentState.tier == 0 ? "Charm unlocked." : "Charm upgraded." };
	}

	if (action == 3) {
		const uint64_t resetCost = 100000 + (player.getLevel() > 100 ? static_cast<uint64_t>(player.getLevel()) * 11000 : 0);
		if (!hasGold(player, resetCost)) {
			return { false, "You do not have enough gold." };
		}

		const uint32_t charmPoints = getCharmPoints(playerGuid);
		const uint32_t refund = getSpentMajorCharmPoints(states);
		const auto [minorEchoes, maxMinorEchoes] = getMinorCharmEchoes(playerGuid);
		const bool reset = DBTransaction::executeWithinTransactionRollbackOnFailure([&]() {
			return Database::getInstance().executeQuery(fmt::format(
			           "UPDATE `player_bestiary_charms` SET `unlocked` = 0, `raceid` = 0 WHERE `player_id` = {:d}",
			           playerGuid)) &&
			       setMinorCharmEchoes(playerGuid, 0, 0);
		});
		if (!reset) {
			return { false, "Could not reset charms." };
		}
		player.setBestiaryCharmPoints(charmPoints + refund);
		if (!removeGold(player, resetCost)) {
			const bool restored = restoreCharmStatesAndResources(playerGuid, states, charmPoints, minorEchoes, maxMinorEchoes);
			invalidatePlayer(playerGuid);
			if (!restored) {
				return { false, "Could not charge gold or restore charm state." };
			}
			return { false, "Could not charge gold for resetting charms." };
		}
		invalidatePlayer(playerGuid);
		return { true, "All charms were reset." };
	}

	if (currentState.tier == 0) {
		return { false, "This charm is not unlocked." };
	}

	if (action == 1) {
		const auto monster = getMonster(raceId);
		if (!monster) {
			return { false, "Creature not found." };
		}

		const uint32_t requiredKills = charm->category == BestiaryCharmCategory::Major ? monster->get().toKill : monster->get().secondUnlock;
		if (getKillCount(playerGuid, raceId) < requiredKills) {
			const std::string message = charm->category == BestiaryCharmCategory::Major ?
			                                "This creature is not fully unlocked." :
			                                "This creature has not reached the charm assignment stage.";
			return { false, message };
		}

		if (currentState.raceId == 0) {
			const uint8_t usedSlots = getAssignedCharmCount(states);
			const uint8_t maxSlots = player.isPremium() ? 6 : 2;
			if (usedSlots >= maxSlots) {
				return { false, "You do not have any charm slots available." };
			}
		}

		for (const auto& [otherCharmId, state] : states) {
			if (otherCharmId == charmId || state.tier == 0 || state.raceId != raceId) {
				continue;
			}

			if (sameCategory(charmId, otherCharmId)) {
				return { false, fmt::format("You already have this creature set on another {} charm.",
				                            charm->category == BestiaryCharmCategory::Major ? "major" : "minor") };
			}
		}

		if (!Database::getInstance().executeQuery(fmt::format(
		        "UPDATE `player_bestiary_charms` SET `raceid` = {:d} WHERE `player_id` = {:d} AND `charm_id` = {:d}",
		        raceId, playerGuid, charmId))) {
			return { false, "Could not assign this charm." };
		}
		invalidatePlayer(playerGuid);
		return { true, "Charm assigned." };
	}

	if (action == 2) {
		if (currentState.raceId == 0) {
			return { false, "This charm is not assigned." };
		}

		const uint64_t removeCost = static_cast<uint64_t>(player.getLevel()) * 100;
		if (!hasGold(player, removeCost)) {
			return { false, "You do not have enough gold." };
		}

		const bool removed = DBTransaction::executeWithinTransactionRollbackOnFailure([&]() {
			return Database::getInstance().executeQuery(fmt::format(
			    "UPDATE `player_bestiary_charms` SET `raceid` = 0 WHERE `player_id` = {:d} AND `charm_id` = {:d}",
			    playerGuid, charmId));
		});
		if (!removed) {
			return { false, "Could not remove this charm." };
		}
		if (!removeGold(player, removeCost)) {
			const bool restored = restoreCharmStates(playerGuid, states);
			invalidatePlayer(playerGuid);
			if (!restored) {
				return { false, "Could not charge gold or restore charm state." };
			}
			return { false, "Could not charge gold for removing this charm." };
		}
		invalidatePlayer(playerGuid);
		return { true, "Charm removed." };
	}

	return { false, "Invalid charm action." };
}
