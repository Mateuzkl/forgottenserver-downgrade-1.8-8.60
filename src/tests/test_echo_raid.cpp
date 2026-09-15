#include "../otpch.h"

#include "../bestiary_charm.h"
#include "../configmanager.h"
#include "../astraclient.h"
#include "../echo_raid.h"
#include "../item.h"
#include "../monster.h"

#include "test_support.h"

struct EchoRaidManagerTestAccess
{
	static uint64_t addWardenRaid(EchoRaidManager& manager, uint32_t wardenId, std::deque<bool> companions)
	{
		EchoRaidManager::RaidInstance raid;
		raid.id = manager.nextRaidId++;
		raid.wardenId = wardenId;
		raid.wardenPending = false;
		raid.pendingSpawns = std::move(companions);
		raid.creatureIds.insert(wardenId);
		manager.creatureToRaid[wardenId] = raid.id;
		const uint64_t raidId = raid.id;
		manager.raids.emplace(raidId, std::move(raid));
		return raidId;
	}

	static size_t pendingCompanions(const EchoRaidManager& manager, uint64_t raidId)
	{
		return manager.raids.at(raidId).pendingSpawns.size();
	}
};

namespace {

struct ScopedBooleanConfig
{
	ConfigManager::Boolean key;
	bool oldValue;

	ScopedBooleanConfig(ConfigManager::Boolean configKey, bool value) :
		key(configKey), oldValue(ConfigManager::getBoolean(configKey))
	{
		ConfigManager::setBoolean(key, value);
	}

	~ScopedBooleanConfig() { ConfigManager::setBoolean(key, oldValue); }
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

constexpr std::array<bool, 5> commonAndUncommon = {false, true, true, false, false};

static_assert(AstraClient::SINGLE_CREATURE_MARK_OPCODE == 0x93);
static_assert(AstraClient::ECHO_RAID_VISUAL_MARK_TYPE == 15);

} // namespace

TEST_CASE(echo_raid_uses_occurrence_instead_of_bestiary_stars)
{
	CHECK(EchoRaidManager::isOccurrenceEligible(1, commonAndUncommon));
	CHECK(EchoRaidManager::isOccurrenceEligible(2, commonAndUncommon));
	CHECK(!EchoRaidManager::isOccurrenceEligible(0, commonAndUncommon));
	CHECK(!EchoRaidManager::isOccurrenceEligible(3, commonAndUncommon));
	CHECK(!EchoRaidManager::isOccurrenceEligible(4, commonAndUncommon));
}

TEST_CASE(echo_raid_eligibility_blocks_incompatible_monster_states)
{
	auto eligible = [](bool summon, bool boss, bool rewardBoss, bool echoSpawn, bool influenced, bool fiendish,
	                   bool warden) {
		return EchoRaidManager::passesEligibilityPolicy(1, commonAndUncommon, summon, boss, rewardBoss, echoSpawn,
		                                                influenced, fiendish, warden);
	};
	CHECK(eligible(false, false, false, false, false, false, false));
	CHECK(!eligible(true, false, false, false, false, false, false));
	CHECK(!eligible(false, true, false, false, false, false, false));
	CHECK(!eligible(false, false, true, false, false, false, false));
	CHECK(!eligible(false, false, false, true, false, false, false));
	CHECK(!eligible(false, false, false, false, true, false, false));
	CHECK(!eligible(false, false, false, false, false, true, false));
	CHECK(!eligible(false, false, false, false, false, false, true));
	CHECK(!EchoRaidManager::passesEligibilityPolicy(3, commonAndUncommon, false, false, false, false, false,
	                                               false, false));
}

TEST_CASE(echo_raid_outcomes_cover_normal_influenced_and_warden)
{
	CHECK(EchoRaidManager::selectOutcome(0, 78, 20, 2, false, 2.0) == EchoRaidOutcome::Normal);
	CHECK(EchoRaidManager::selectOutcome(77, 78, 20, 2, false, 2.0) == EchoRaidOutcome::Normal);
	CHECK(EchoRaidManager::selectOutcome(78, 78, 20, 2, false, 2.0) == EchoRaidOutcome::Influenced);
	CHECK(EchoRaidManager::selectOutcome(97, 78, 20, 2, false, 2.0) == EchoRaidOutcome::Influenced);
	CHECK(EchoRaidManager::selectOutcome(98, 78, 20, 2, false, 2.0) == EchoRaidOutcome::Warden);
	CHECK(EchoRaidManager::selectOutcome(99, 78, 20, 2, false, 2.0) == EchoRaidOutcome::Warden);
}

TEST_CASE(completed_bestiary_doubles_only_the_warden_outcome_weight)
{
	uint32_t incompleteWardenSlots = 0;
	for (uint64_t roll = 0; roll < 100; ++roll) {
		incompleteWardenSlots += EchoRaidManager::selectOutcome(roll, 78, 20, 2, false, 2.0) ==
		                          EchoRaidOutcome::Warden;
	}
	uint32_t completedWardenSlots = 0;
	for (uint64_t roll = 0; roll < 102; ++roll) {
		completedWardenSlots += EchoRaidManager::selectOutcome(roll, 78, 20, 2, true, 2.0) ==
		                         EchoRaidOutcome::Warden;
	}
	CHECK(incompleteWardenSlots == 2);
	CHECK(completedWardenSlots == 4);
}

TEST_CASE(echo_warden_spawn_plan_is_one_warden_with_two_normal_and_two_influenced_companions)
{
	const auto plan = EchoRaidManager::buildSpawnPlan(EchoRaidOutcome::Warden, 0, 0, 2, 2);
	CHECK(plan.size() == 4);
	CHECK(!plan[0]);
	CHECK(!plan[1]);
	CHECK(plan[2]);
	CHECK(plan[3]);
}

TEST_CASE(echo_raid_spawn_plans_keep_normal_and_influenced_outcomes_separate)
{
	const auto normal = EchoRaidManager::buildSpawnPlan(EchoRaidOutcome::Normal, 5, 4, 2, 2);
	CHECK(normal.size() == 5);
	CHECK(std::none_of(normal.begin(), normal.end(), [](bool influenced) { return influenced; }));

	const auto influenced = EchoRaidManager::buildSpawnPlan(EchoRaidOutcome::Influenced, 5, 4, 2, 2);
	CHECK(influenced.size() == 4);
	CHECK(std::all_of(influenced.begin(), influenced.end(), [](bool value) { return value; }));
}

TEST_CASE(echo_raid_fixed_spawn_position_never_falls_back_from_the_portal_origin)
{
	const Position origin{321, 654, 7};
	const auto available = EchoRaidManager::selectFixedSpawnPosition(origin, true);
	CHECK(available.has_value());
	if (available) {
		CHECK(available->x == origin.x);
		CHECK(available->y == origin.y);
		CHECK(available->z == origin.z);
	}
	CHECK(!EchoRaidManager::selectFixedSpawnPosition(origin, false).has_value());
}

TEST_CASE(echo_raid_failed_spawn_attempt_preserves_warden_and_companion_queue)
{
	bool wardenPending = true;
	std::deque<bool> companions = {false, false, true, true};

	EchoRaidManager::finishSpawnAttempt(false, true, wardenPending, companions);
	CHECK(wardenPending);
	CHECK(companions.size() == 4);

	EchoRaidManager::finishSpawnAttempt(true, true, wardenPending, companions);
	CHECK(!wardenPending);
	CHECK(companions.size() == 4);

	EchoRaidManager::finishSpawnAttempt(false, false, wardenPending, companions);
	CHECK(companions.size() == 4);
	EchoRaidManager::finishSpawnAttempt(true, false, wardenPending, companions);
	CHECK(companions.size() == 3);
	CHECK(!companions.front());
}

TEST_CASE(echo_raid_warden_removal_preserves_scheduled_companions)
{
	EchoRaidManager manager;
	constexpr uint32_t wardenId = 0x40000123;
	const uint64_t raidId = EchoRaidManagerTestAccess::addWardenRaid(
	    manager, wardenId, std::deque<bool>{false, false, true, true});

	CHECK(manager.hasActiveVisuals());
	manager.onCreatureRemoved(wardenId);
	CHECK(!manager.hasActiveVisuals());
	CHECK(manager.getStatus().activeRaids == 1);
	CHECK(manager.getStatus().pendingSpawns == 4);
	CHECK(EchoRaidManagerTestAccess::pendingCompanions(manager, raidId) == 4);
}

TEST_CASE(echo_raid_damage_multiplier_scales_each_damage_component_once)
{
	CHECK(Monster::scaleEchoRaidCombatValue(-100, 1.0) == -100);
	CHECK(Monster::scaleEchoRaidCombatValue(-100, 1.5) == -150);
	CHECK(Monster::scaleEchoRaidCombatValue(-40, 1.5) == -60);
	CHECK(Monster::scaleEchoRaidCombatValue(100, 1.5) == 150);
	CHECK(Monster::scaleEchoRaidCombatValue(-100, 0.0) == -100);
}

TEST_CASE(echo_warden_reward_authority_accepts_each_death_only_once)
{
	auto monsterType = std::make_shared<MonsterType>();
	Monster monster(monsterType);
	CHECK(monster.markEchoWardenRewardsGranted());
	CHECK(!monster.markEchoWardenRewardsGranted());
}

TEST_CASE(echo_raid_item_ids_are_aligned_and_present_in_otb)
{
	ensureItemTypesLoaded();
	const std::array<uint16_t, 28> requiredItems = {
	    54133, 54266, 51588, 51589, 53751, 53752, 53753, 53754, 53755, 53756, 53757, 53758,
	    53759, 53760, 53761, 53762, 53763, 53764, 53765, 53766, 53767, 53768, 53769, 53770,
	    53771, 53772, 53773, 53774,
	};
	for (uint16_t itemId : requiredItems) {
		CHECK(Item::items[itemId].id == itemId);
		const auto item = Item::CreateItem(itemId, 1);
		CHECK(item != nullptr);
		CHECK(item->getID() == itemId);
	}
}

TEST_CASE(echo_raid_startup_validation_accepts_the_real_portal_and_loot_items)
{
	ensureItemTypesLoaded();
	ScopedBooleanConfig bestiary(ConfigManager::BESTIARY_SYSTEM_ENABLED, true);
	ScopedBooleanConfig echo(ConfigManager::ECHO_RAID_SYSTEM_ENABLED, true);
	EchoRaidConfig config;
	CHECK(config.wardenSelfAttackMultiplier == 1.0);
	CHECK(config.empoweredDamageMultiplier == 1.5);
	CHECK(config.wardenDust == 15);
	for (uint16_t itemId = 53751; itemId <= 53774; ++itemId) {
		config.basicScrollItemIds.push_back(itemId);
	}
	config.catalystItems = {{54266, 80}, {51588, 18}, {51589, 2}};

	EchoRaidManager manager;
	CHECK(manager.configure(config));
	CHECK(manager.isEnabled());
	CHECK(manager.getStatus().activeRaids == 0);
	manager.cleanupAll();
	CHECK(manager.getStatus().trackedCreatures == 0);

	config.portalItemId = 0;
	CHECK(!manager.configure(config));
	CHECK(!manager.isEnabled());
}

TEST_CASE(echo_raid_cleanup_all_releases_pending_runtime_state_after_10000_cycles)
{
	ensureItemTypesLoaded();
	ScopedBooleanConfig bestiary(ConfigManager::BESTIARY_SYSTEM_ENABLED, true);
	ScopedBooleanConfig echo(ConfigManager::ECHO_RAID_SYSTEM_ENABLED, true);

	BestiaryCreatureInfo bestiaryInfo;
	bestiaryInfo.raceId = 65530;
	bestiaryInfo.name = "Echo lifecycle test";
	bestiaryInfo.toKill = 1;
	bestiaryInfo.occurrence = 1;
	g_bestiaryCharmSystem.registerMonster(bestiaryInfo);

	auto monsterType = std::make_shared<MonsterType>();
	monsterType->raceId = bestiaryInfo.raceId;
	monsterType->name = bestiaryInfo.name;
	monsterType->nameDescription = bestiaryInfo.name;
	Monster monster(monsterType);

	EchoRaidConfig config;
	config.spawnChanceNumerator = 1;
	config.spawnChanceDenominator = 1;
	for (uint16_t itemId = 53751; itemId <= 53774; ++itemId) {
		config.basicScrollItemIds.push_back(itemId);
	}
	config.catalystItems = {{54266, 80}, {51588, 18}, {51589, 2}};

	EchoRaidManager manager;
	CHECK(manager.configure(config));
	for (uint32_t cycle = 0; cycle < 10000; ++cycle) {
		manager.onMonsterDeath(monster);
		CHECK(manager.getStatus().pendingEchoes == 1);
		manager.cleanupAll();
		const EchoRaidRuntimeStatus status = manager.getStatus();
		CHECK(status.pendingEchoes == 0);
		CHECK(status.portals == 0);
		CHECK(status.activeRaids == 0);
		CHECK(status.trackedCreatures == 0);
	}
}

TFS_TEST_MAIN()
