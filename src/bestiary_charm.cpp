// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "bestiary_charm.h"

#include "database.h"
#include "game.h"
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
	uint16_t price = 0;
};

constexpr std::array<BestiaryCharmDefinition, 25> charmDefinitions = {{
	{0, BestiaryCharmCategory::Major, 240},
	{1, BestiaryCharmCategory::Major, 400},
	{2, BestiaryCharmCategory::Major, 240},
	{3, BestiaryCharmCategory::Major, 320},
	{4, BestiaryCharmCategory::Major, 320},
	{5, BestiaryCharmCategory::Major, 360},
	{6, BestiaryCharmCategory::Minor, 100},
	{7, BestiaryCharmCategory::Major, 400},
	{8, BestiaryCharmCategory::Major, 240},
	{9, BestiaryCharmCategory::Minor, 100},
	{10, BestiaryCharmCategory::Minor, 100},
	{11, BestiaryCharmCategory::Minor, 100},
	{12, BestiaryCharmCategory::Minor, 100},
	{13, BestiaryCharmCategory::Minor, 100},
	{14, BestiaryCharmCategory::Minor, 100},
	{15, BestiaryCharmCategory::Major, 800},
	{16, BestiaryCharmCategory::Major, 600},
	{17, BestiaryCharmCategory::Minor, 100},
	{18, BestiaryCharmCategory::Minor, 100},
	{19, BestiaryCharmCategory::Major, 800},
	{20, BestiaryCharmCategory::Minor, 100},
	{21, BestiaryCharmCategory::Minor, 100},
	{22, BestiaryCharmCategory::Major, 600},
	{23, BestiaryCharmCategory::Major, 600},
	{24, BestiaryCharmCategory::Major, 600},
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

} // namespace

BestiaryCharmSystem g_bestiaryCharmSystem;

void BestiaryCharmSystem::registerMonster(BestiaryCreatureInfo info)
{
	if (info.raceId == 0) {
		return;
	}

	if (info.name.empty()) {
		info.name = "?";
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
		CharmState state;
		state.unlocked = result->getNumber<uint16_t>("unlocked") != 0;
		state.raceId = result->getNumber<uint16_t>("raceid");
		states[charmId] = state;
	} while (result->next());

	return states;
}

uint32_t BestiaryCharmSystem::getKillCount(uint32_t playerGuid, uint16_t raceId) const
{
	auto result = Database::getInstance().storeQuery(fmt::format(
	    "SELECT `kills` FROM `player_bestiary_kills` WHERE `player_id` = {:d} AND `raceid` = {:d}",
	    playerGuid, raceId));
	return result ? result->getNumber<uint32_t>("kills") : 0;
}

uint32_t BestiaryCharmSystem::getCharmPoints(uint32_t playerGuid) const
{
	auto result = Database::getInstance().storeQuery(
	    fmt::format("SELECT `charmpoints` FROM `players` WHERE `id` = {:d}", playerGuid));
	return result ? result->getNumber<uint32_t>("charmpoints") : 0;
}

bool BestiaryCharmSystem::setCharmPoints(uint32_t playerGuid, uint32_t points) const
{
	return Database::getInstance().executeQuery(
	    fmt::format("UPDATE `players` SET `charmpoints` = {:d} WHERE `id` = {:d}", points, playerGuid));
}

bool BestiaryCharmSystem::removeGold(Player& player, uint64_t amount) const
{
	if (amount == 0) {
		return true;
	}

	const uint64_t inventoryMoney = player.getMoney();
	const uint64_t bankBalance = player.getBankBalance();
	if (inventoryMoney < amount && bankBalance < amount - inventoryMoney) {
		return false;
	}

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
		if (getCharmDefinition(charmId) && state.unlocked && state.raceId > 0) {
			++count;
		}
	}
	return count;
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
		if (currentState.unlocked) {
			return { false, "This charm is already unlocked." };
		}

		const uint32_t charmPoints = getCharmPoints(playerGuid);
		if (charmPoints < charm->price) {
			return { false, "You do not have enough charm points." };
		}

		const bool updated = DBTransaction::executeWithinTransactionRollbackOnFailure([&]() {
			Database& db = Database::getInstance();
			return db.executeQuery(fmt::format(
			           "INSERT INTO `player_bestiary_charms` (`player_id`, `charm_id`, `unlocked`, `raceid`) "
			           "VALUES ({:d}, {:d}, 1, 0) ON DUPLICATE KEY UPDATE `unlocked` = 1",
			           playerGuid, charmId)) &&
			       setCharmPoints(playerGuid, charmPoints - charm->price);
		});
		if (!updated) {
			return { false, "Could not unlock this charm." };
		}
		return { true, "Charm unlocked." };
	}

	if (action == 3) {
		const uint64_t resetCost = 100000 + (player.getLevel() > 100 ? static_cast<uint64_t>(player.getLevel()) * 11000 : 0);
		if (!removeGold(player, resetCost)) {
			return { false, "You do not have enough gold." };
		}

		if (!Database::getInstance().executeQuery(fmt::format(
		        "UPDATE `player_bestiary_charms` SET `raceid` = 0 WHERE `player_id` = {:d}", playerGuid))) {
			return { false, "Could not reset charms." };
		}
		return { true, "All charm assignments were cleared." };
	}

	if (!currentState.unlocked) {
		return { false, "This charm is not unlocked." };
	}

	if (action == 1) {
		const auto monster = getMonster(raceId);
		if (!monster) {
			return { false, "Creature not found." };
		}

		if (getKillCount(playerGuid, raceId) < monster->get().toKill) {
			return { false, "This creature is not fully unlocked." };
		}

		if (currentState.raceId == 0) {
			const uint8_t usedSlots = getAssignedCharmCount(states);
			const uint8_t maxSlots = player.isPremium() ? 6 : 2;
			if (usedSlots >= maxSlots) {
				return { false, "You do not have any charm slots available." };
			}
		}

		for (const auto& [otherCharmId, state] : states) {
			if (otherCharmId == charmId || !state.unlocked || state.raceId != raceId) {
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
		return { true, "Charm assigned." };
	}

	if (action == 2) {
		if (currentState.raceId == 0) {
			return { false, "This charm is not assigned." };
		}

		constexpr uint64_t removeCost = 10000;
		if (!removeGold(player, removeCost)) {
			return { false, "You do not have enough gold." };
		}

		if (!Database::getInstance().executeQuery(fmt::format(
		        "UPDATE `player_bestiary_charms` SET `raceid` = 0 WHERE `player_id` = {:d} AND `charm_id` = {:d}",
		        playerGuid, charmId))) {
			return { false, "Could not remove this charm." };
		}
		return { true, "Charm removed." };
	}

	return { false, "Invalid charm action." };
}
