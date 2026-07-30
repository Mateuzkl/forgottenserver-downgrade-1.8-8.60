#include "../otpch.h"

#include "../monster.h"

#include "test_support.h"

TEST_CASE(reward_boss_zero_aggregate_has_zero_loot_rate)
{
	const double lootRate = reward_boss::calculateLootRate(0.0, 0, 4, 1.0);

	CHECK(std::isfinite(lootRate));
	CHECK(lootRate == 0.0);
}

TEST_CASE(reward_boss_positive_scores_keep_proportional_rate)
{
	CHECK(reward_boss::calculateLootRate(25.0, 100, 4, 1.0) == 1.0);
	CHECK(reward_boss::calculateLootRate(10.0, 100, 4, 1.0) == 0.4);
	CHECK(reward_boss::calculateLootRate(50.0, 100, 4, 1.0) == 1.0);
}

TFS_TEST_MAIN()
