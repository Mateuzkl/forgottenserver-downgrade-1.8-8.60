#include "../otpch.h"

#include "../bestiary_charm.h"
#include "../item.h"
#include "../player.h"

#include "test_support.h"

namespace {

void ensureItemTypesLoaded()
{
	if (Item::items.size() != 0) {
		return;
	}

	const auto itemsPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
	                       "data/items/items.otb";
	REQUIRE(Item::items.loadFromOtb(itemsPath.string()));
}

} // namespace

TEST_CASE(bestiary_progress_matches_legacy_lua_thresholds)
{
	BestiaryCreatureInfo info;
	info.toKill = 3;
	info.firstUnlock = 1;
	info.secondUnlock = 2;

	CHECK(BestiaryCharmSystem::getProgress(info, 0) == 0);
	CHECK(BestiaryCharmSystem::getProgress(info, 1) == 2);
	CHECK(BestiaryCharmSystem::getProgress(info, 2) == 3);
	CHECK(BestiaryCharmSystem::getProgress(info, 3) == 4);
	CHECK(BestiaryCharmSystem::getProgress(info, 4) == 4);
}

TEST_CASE(bestiary_progress_keeps_discovery_before_first_unlock)
{
	BestiaryCreatureInfo info;
	info.toKill = 100;
	info.firstUnlock = 10;
	info.secondUnlock = 50;

	CHECK(BestiaryCharmSystem::getProgress(info, 0) == 0);
	CHECK(BestiaryCharmSystem::getProgress(info, 1) == 1);
	CHECK(BestiaryCharmSystem::getProgress(info, 9) == 1);
	CHECK(BestiaryCharmSystem::getProgress(info, 10) == 2);
	CHECK(BestiaryCharmSystem::getProgress(info, 49) == 2);
	CHECK(BestiaryCharmSystem::getProgress(info, 50) == 3);
	CHECK(BestiaryCharmSystem::getProgress(info, 99) == 3);
	CHECK(BestiaryCharmSystem::getProgress(info, 100) == 4);
	CHECK(BestiaryCharmSystem::getProgress(info, 101) == 4);
}

TEST_CASE(bestiary_registration_uses_race_id_as_legacy_identity)
{
	BestiaryCharmSystem system;
	BestiaryCreatureInfo first;
	first.raceId = 2764;
	first.name = "Day Night Harpy";
	first.toKill = 2500;
	first.firstUnlock = 100;
	first.secondUnlock = 1000;
	system.registerMonster(first);

	BestiaryCreatureInfo replacement = first;
	replacement.name = "Night Harpy";
	system.registerMonster(replacement);

	const auto registered = system.getMonster(2764);
	REQUIRE(registered.has_value());
	CHECK(registered->get().name == "Night Harpy");
}

TEST_CASE(bestiary_kills_are_saturated_dirty_and_consumed_once)
{
	ensureItemTypesLoaded();
	Player player(nullptr);
	player.clearBestiaryDirty();

	const auto [oldCount, newCount] = player.addBestiaryKillCount(35, 1);
	CHECK(oldCount == 0);
	CHECK(newCount == 1);
	CHECK(player.getBestiaryKillCount(35) == 1);
	CHECK(player.getBestiaryDirtySnapshot().modifiedRaceIds.contains(35));

	player.setPendingBestiaryKill({1001, 35, oldCount, newCount, false});
	CHECK(!player.takePendingBestiaryKill(1002, 35).has_value());
	CHECK(!player.takePendingBestiaryKill(1001, 36).has_value());
	const auto result = player.takePendingBestiaryKill(1001, 35);
	CHECK(result.has_value());
	CHECK(result->oldCount == 0);
	CHECK(result->newCount == 1);
	CHECK(!result->charmPointsAwarded);
	CHECK(!player.takePendingBestiaryKill(1001, 35).has_value());

	player.setBestiaryKillCount(35, std::numeric_limits<uint32_t>::max() - 1);
	const auto [beforeSaturation, saturated] = player.addBestiaryKillCount(35, 10);
	CHECK(beforeSaturation == std::numeric_limits<uint32_t>::max() - 1);
	CHECK(saturated == std::numeric_limits<uint32_t>::max());
}

TEST_CASE(bestiary_first_kill_is_recorded_when_race_id_is_patched_after_death)
{
	ensureItemTypesLoaded();
	Player player(nullptr);
	player.clearBestiaryDirty();

	constexpr uint32_t victimId = 1002;
	constexpr uint16_t patchedRaceId = 2764;

	// Death processing could not queue a pending kill because the monster had no race ID yet.
	CHECK(!player.takePendingBestiaryKill(victimId, patchedRaceId).has_value());

	// Name-based Lua resolution patches the race ID and must fall back to the normal add path.
	const auto [oldCount, newCount] = player.addBestiaryKillCount(patchedRaceId, 1);
	CHECK(oldCount == 0);
	CHECK(newCount == 1);
	CHECK(player.getBestiaryKillCount(patchedRaceId) == 1);
	CHECK(player.getBestiaryDirtySnapshot().modifiedRaceIds.contains(patchedRaceId));
}

TFS_TEST_MAIN()
