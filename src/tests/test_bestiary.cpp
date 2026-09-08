#include "../otpch.h"

#include "../bestiary_charm.h"
#include "../item.h"
#include "../player.h"

#include "test_support.h"

namespace {

/// RAII helper to set a boolean config value and restore it on scope exit.
struct ScopedConfigOverride
{
	ConfigManager::Boolean key;
	bool original;

	ScopedConfigOverride(ConfigManager::Boolean k, bool value) : key(k), original(ConfigManager::getBoolean(k))
	{
		ConfigManager::setBoolean(k, value);
	}

	~ScopedConfigOverride() { ConfigManager::setBoolean(key, original); }

	ScopedConfigOverride(const ScopedConfigOverride&) = delete;
	ScopedConfigOverride& operator=(const ScopedConfigOverride&) = delete;
};

void ensureItemTypesLoaded()
{
	if (Item::items.size() != 0) {
		return;
	}

	const auto itemsPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
	                       "data/items/items.otb";
	CHECK(Item::items.loadFromOtb(itemsPath.string()));
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
	ScopedConfigOverride guard(ConfigManager::BESTIARY_SYSTEM_ENABLED, true);
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
	CHECK(registered.has_value());
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

// ---------------------------------------------------------------------------
// OFF-mode regression tests: verify that disabled Bestiary produces safe
// neutral values and never reaches DB access paths.
// ---------------------------------------------------------------------------

TEST_CASE(bestiary_charm_system_disabled_registration_is_noop_and_get_returns_nullopt)
{
	ScopedConfigOverride guard(ConfigManager::BESTIARY_SYSTEM_ENABLED, false);
	CHECK(!BestiaryCharmSystem::isEnabled());

	BestiaryCharmSystem system;
	BestiaryCreatureInfo info;
	info.raceId = 9999;
	info.toKill = 100;
	info.firstUnlock = 10;
	info.secondUnlock = 50;
	system.registerMonster(info);

	const auto registered = system.getMonster(9999);
	CHECK(!registered.has_value());
}

TEST_CASE(bestiary_charm_system_disabled_returns_zero_tier)
{
	ScopedConfigOverride guard(ConfigManager::BESTIARY_SYSTEM_ENABLED, false);
	CHECK(!BestiaryCharmSystem::isEnabled());

	ensureItemTypesLoaded();
	Player player(nullptr);

	// Even if a valid charm definition exists, tier must be 0 when disabled.
	CHECK(g_bestiaryCharmSystem.getAssignedCharmTier(player, 0, 100) == 0);
	CHECK(g_bestiaryCharmSystem.getAssignedCharmTier(player, 15, 100) == 0);
}

TEST_CASE(bestiary_charm_system_disabled_returns_zero_bonus)
{
	ScopedConfigOverride guard(ConfigManager::BESTIARY_SYSTEM_ENABLED, false);
	CHECK(!BestiaryCharmSystem::isEnabled());

	// Bonus must be 0.0 for any charm/tier combination when disabled.
	CHECK(g_bestiaryCharmSystem.getCharmBonus(0, 1) == 0.0);
	CHECK(g_bestiaryCharmSystem.getCharmBonus(15, 3) == 0.0);
}

TEST_CASE(bestiary_charm_system_disabled_handle_action_returns_disabled)
{
	ScopedConfigOverride guard(ConfigManager::BESTIARY_SYSTEM_ENABLED, false);
	CHECK(!BestiaryCharmSystem::isEnabled());

	ensureItemTypesLoaded();
	Player player(nullptr);

	const auto result = g_bestiaryCharmSystem.handleCharmAction(player, 0, 0, 100);
	CHECK(!result.success);
	CHECK(result.message == "Bestiary system is disabled.");
}

TEST_CASE(bestiary_charm_system_disabled_add_minor_echoes_returns_false)
{
	ScopedConfigOverride guard(ConfigManager::BESTIARY_SYSTEM_ENABLED, false);
	CHECK(!BestiaryCharmSystem::isEnabled());

	// addMinorCharmEchoes must return false (no-op) when disabled.
	CHECK(!g_bestiaryCharmSystem.addMinorCharmEchoes(1, 100));
}

TEST_CASE(bestiary_charm_system_enabled_returns_nonzero_bonus)
{
	ScopedConfigOverride guard(ConfigManager::BESTIARY_SYSTEM_ENABLED, true);
	CHECK(BestiaryCharmSystem::isEnabled());

	// Charm 0 tier 1 should return a non-zero bonus when enabled.
	CHECK(g_bestiaryCharmSystem.getCharmBonus(0, 1) > 0.0);
}

TEST_CASE(bestiary_dirty_snapshot_preserved_when_system_disabled_during_save)
{
	ensureItemTypesLoaded();
	Player player(nullptr);
	player.clearBestiaryDirty();

	// 1. Bestiary is enabled, player registers kills
	{
		ScopedConfigOverride guard(ConfigManager::BESTIARY_SYSTEM_ENABLED, true);
		player.addBestiaryKillCount(100, 5);
		CHECK(player.getBestiaryDirtySnapshot().modifiedRaceIds.contains(100));
	}

	// 2. Bestiary is toggled OFF (simulating config reload before save).
	// buildPlayerSave only captures an empty snapshot when BESTIARY_SYSTEM_ENABLED is false.
	{
		ScopedConfigOverride guard(ConfigManager::BESTIARY_SYSTEM_ENABLED, false);
		const Player::BestiaryDirtySnapshot bestiarySnapshot =
		    ConfigManager::getBoolean(ConfigManager::BESTIARY_SYSTEM_ENABLED)
		        ? player.getBestiaryDirtySnapshot()
		        : Player::BestiaryDirtySnapshot{};

		CHECK(bestiarySnapshot.modifiedRaceIds.empty());

		// Flush acknowledgement with empty snapshot must NOT clear the dirty kill.
		player.acknowledgeBestiaryDirty(bestiarySnapshot);
	}

	// 3. Bestiary is toggled back ON
	{
		ScopedConfigOverride guard(ConfigManager::BESTIARY_SYSTEM_ENABLED, true);
		// Dirty state was NOT cleared by the OFF-mode save
		const auto newSnapshot = player.getBestiaryDirtySnapshot();
		CHECK(newSnapshot.modifiedRaceIds.contains(100));
	}
}

TFS_TEST_MAIN()
